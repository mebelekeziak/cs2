#include "world_postprocess.h"
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <utility>

using Microsoft::WRL::ComPtr;
namespace {
// Only independent resources are cached. In particular, no swap-chain texture,
// its views, or the game's saved context state survive a Render call.
struct Cache {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11Texture2D> copy;
    ComPtr<ID3D11ShaderResourceView> source;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
    ComPtr<ID3D11DepthStencilState> depth;
    UINT width = 0, height = 0, samples = 0, quality = 0;
    DXGI_FORMAT inputFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN;
};
Cache cache;
const char* status = "Screen grading disabled";
std::atomic<WorldPostprocessLogCallback> logger{nullptr};

void Report(const char* message) noexcept {
    if (std::strcmp(status, message) == 0) return;
    status = message;
    if (const auto callback = logger.load(std::memory_order_acquire)) callback(message);
}

DXGI_FORMAT Family(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_TYPELESS;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_TYPELESS;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_TYPELESS;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_TYPELESS;
    case DXGI_FORMAT_R11G11B10_FLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

// Exposure/contrast/saturation are applied to sampled RGB. sRGB views decode
// on sampling and encode on output; HDR float keeps values above one. PQ/HLG
// presentation is not inferred here: the pass operates in the RTV's domain.
constexpr char shader[] = R"(
Texture2D<float4> inputColor : register(t0);
SamplerState sourceSampler : register(s0);
cbuffer Grade : register(b0) {
    float4 grade; // exposure multiplier, contrast, saturation, inverse gamma
    float4 tintVignette;
    float4 detail; // inverse width, inverse height, sharpening, unused
};
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex VS(uint id : SV_VertexID) {
    Vertex v;
    v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return v;
}
float4 PS(Vertex v) : SV_Target {
    float4 original = inputColor.SampleLevel(sourceSampler, v.uv, 0);
    float3 color = original.rgb;
    if (detail.z > 0) {
        float3 neighbors = inputColor.SampleLevel(sourceSampler, v.uv + float2(detail.x, 0), 0).rgb
                         + inputColor.SampleLevel(sourceSampler, v.uv - float2(detail.x, 0), 0).rgb
                         + inputColor.SampleLevel(sourceSampler, v.uv + float2(0, detail.y), 0).rgb
                         + inputColor.SampleLevel(sourceSampler, v.uv - float2(0, detail.y), 0).rgb;
        color = max(0, color + detail.z * (color - neighbors * 0.25));
    }
    color *= grade.x;
    color = (color - 0.5) * grade.y + 0.5;
    float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(luminance.xxx, color, grade.z);
    color = pow(max(color, 0), grade.w) * tintVignette.rgb;
    float2 edge = (v.uv - 0.5) * 2;
    color *= 1 - tintVignette.w * smoothstep(0.25, 1.5, dot(edge, edge));
    return float4(color, original.a);
}
)";

bool Build(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& description,
    DXGI_FORMAT viewFormat) noexcept {
    Cache next;
    ComPtr<ID3D11Device1> device1;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1)))) {
        Report("Screen grading unavailable: D3D11.1 device required");
        return false;
    }
    const D3D_FEATURE_LEVEL level = device->GetFeatureLevel();
    if (level < D3D_FEATURE_LEVEL_10_0) {
        Report("Screen grading unavailable: shader model 4 required");
        return false;
    }
    const UINT flags = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
        ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
    if (FAILED(device1->CreateDeviceContextState(flags, &level, 1, D3D11_SDK_VERSION,
        __uuidof(ID3D11Device1), nullptr, &next.state))) {
        Report("Screen grading unavailable: context state isolation unsupported");
        return false;
    }
    UINT support = 0;
    constexpr UINT required = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE
        | D3D11_FORMAT_SUPPORT_RENDER_TARGET;
    if (FAILED(device->CheckFormatSupport(viewFormat, &support))
        || (support & required) != required
        || (description.SampleDesc.Count > 1 && !(support & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE))) {
        Report("Screen grading unavailable: texture format capabilities unsupported");
        return false;
    }
    D3D11_TEXTURE2D_DESC copy{};
    copy.Width = description.Width;
    copy.Height = description.Height;
    copy.MipLevels = copy.ArraySize = 1;
    copy.Format = Family(viewFormat);
    copy.SampleDesc.Count = 1;
    copy.Usage = D3D11_USAGE_DEFAULT;
    copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SHADER_RESOURCE_VIEW_DESC source{};
    source.Format = viewFormat;
    source.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    source.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateTexture2D(&copy, nullptr, &next.copy))
        || FAILED(device->CreateShaderResourceView(next.copy.Get(), &source, &next.source))) {
        Report("Screen grading unavailable: source copy allocation failed");
        return false;
    }
    ComPtr<ID3DBlob> vertexCode, pixelCode, errors;
    if (FAILED(D3DCompile(shader, sizeof(shader) - 1, "world_postprocess", nullptr, nullptr,
        "VS", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vertexCode, &errors))
        || FAILED(D3DCompile(shader, sizeof(shader) - 1, "world_postprocess", nullptr, nullptr,
        "PS", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pixelCode, &errors))
        || FAILED(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(),
            nullptr, &next.vertex))
        || FAILED(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(),
            nullptr, &next.pixel))) {
        Report("Screen grading unavailable: shader compilation/creation failed");
        return false;
    }
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = 48;
    buffer.Usage = D3D11_USAGE_DEFAULT;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    rasterizer.MultisampleEnable = description.SampleDesc.Count > 1;
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(device->CreateBuffer(&buffer, nullptr, &next.constants))
        || FAILED(device->CreateSamplerState(&sampler, &next.sampler))
        || FAILED(device->CreateRasterizerState(&rasterizer, &next.rasterizer))
        || FAILED(device->CreateDepthStencilState(&depth, &next.depth))) {
        Report("Screen grading unavailable: pipeline allocation failed");
        return false;
    }
    next.device = device;
    next.width = description.Width;
    next.height = description.Height;
    next.samples = description.SampleDesc.Count;
    next.quality = description.SampleDesc.Quality;
    next.inputFormat = description.Format;
    next.viewFormat = viewFormat;
    cache = std::move(next);
    return true;
}
}

bool WorldPostprocessIsActive(const WorldSettings& settings) noexcept {
    const auto s = WorldSanitizeSettings(settings);
    return s.enabled && (s.exposureEv != 0 || s.contrast != 1 || s.saturation != 1 || s.gamma != 1
        || s.tint[0] != 1 || s.tint[1] != 1 || s.tint[2] != 1 || s.vignette != 0 || s.sharpening != 0);
}

bool WorldPostprocessRender(ID3D11Device* device, ID3D11DeviceContext* context,
    ID3D11Texture2D* sourceBackbuffer, ID3D11RenderTargetView* target,
    const WorldSettings& settings) noexcept {
    const auto s = WorldSanitizeSettings(settings);
    if (!WorldPostprocessIsActive(s)) {
        Report(s.enabled ? "Screen grading neutral (no GPU work)" : "Screen grading disabled");
        return true;
    }
    if (!device || !context || !sourceBackbuffer || !target
        || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        Report("Screen grading unavailable: invalid resource/immediate context");
        return false;
    }
    D3D11_TEXTURE2D_DESC description{};
    sourceBackbuffer->GetDesc(&description);
    D3D11_RENDER_TARGET_VIEW_DESC targetDescription{};
    target->GetDesc(&targetDescription);
    ComPtr<ID3D11Resource> targetResource;
    target->GetResource(&targetResource);
    // This pass writes back to the exact source texture, never an unrelated view.
    if (targetResource.Get() != static_cast<ID3D11Resource*>(sourceBackbuffer)
        || !description.Width || !description.Height || description.MipLevels != 1 || description.ArraySize != 1
        || !description.SampleDesc.Count || Family(description.Format) == DXGI_FORMAT_UNKNOWN
        || Family(description.Format) != Family(targetDescription.Format)
        || (description.SampleDesc.Count == 1 && (targetDescription.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D
            || targetDescription.Texture2D.MipSlice != 0))
        || (description.SampleDesc.Count > 1 && targetDescription.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2DMS)) {
        Report("Screen grading unavailable: unsupported backbuffer/view layout");
        return false;
    }
    ComPtr<ID3D11Device> contextDevice, sourceDevice;
    context->GetDevice(&contextDevice);
    sourceBackbuffer->GetDevice(&sourceDevice);
    if (contextDevice.Get() != device || sourceDevice.Get() != device) {
        Report("Screen grading unavailable: mismatched device ownership");
        return false;
    }
    ComPtr<ID3D11DeviceContext1> context1;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(&context1)))) {
        Report("Screen grading unavailable: D3D11.1 context required");
        return false;
    }
    if (cache.device.Get() != device || cache.width != description.Width || cache.height != description.Height
        || cache.inputFormat != description.Format || cache.viewFormat != targetDescription.Format
        || cache.samples != description.SampleDesc.Count || cache.quality != description.SampleDesc.Quality) {
        cache = Cache{};
        if (!Build(device, description, targetDescription.Format)) return false;
    }
    struct Constants { float grade[4]; float tint[4]; float detail[4]; };
    const Constants constants{{std::exp2(s.exposureEv), s.contrast, s.saturation, 1.0f / s.gamma},
        {s.tint[0], s.tint[1], s.tint[2], s.vignette},
        {1.0f / static_cast<float>(description.Width), 1.0f / static_cast<float>(description.Height), s.sharpening, 0}};
    // No pipeline mutations occur before isolation has been prepared successfully.
    ComPtr<ID3DDeviceContextState> previous;
    context1->SwapDeviceContextState(cache.state.Get(), &previous);
    context->ClearState();
    context->SetPredication(nullptr, FALSE);
    if (description.SampleDesc.Count > 1)
        context->ResolveSubresource(cache.copy.Get(), 0, sourceBackbuffer, 0, targetDescription.Format);
    else
        context->CopyResource(cache.copy.Get(), sourceBackbuffer);
    context->UpdateSubresource(cache.constants.Get(), 0, nullptr, &constants, 0, 0);
    ID3D11RenderTargetView* renderTarget = target;
    context->OMSetRenderTargets(1, &renderTarget, nullptr);
    context->OMSetDepthStencilState(cache.depth.Get(), 0);
    context->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(description.Width),
        static_cast<float>(description.Height), 0, 1};
    context->RSSetViewports(1, &viewport);
    context->RSSetState(cache.rasterizer.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(cache.vertex.Get(), nullptr, 0);
    context->PSSetShader(cache.pixel.Get(), nullptr, 0);
    ID3D11ShaderResourceView* source = cache.source.Get();
    ID3D11SamplerState* sampler = cache.sampler.Get();
    ID3D11Buffer* buffer = cache.constants.Get();
    context->PSSetShaderResources(0, 1, &source);
    context->PSSetSamplers(0, 1, &sampler);
    context->PSSetConstantBuffers(0, 1, &buffer);
    context->Draw(3, 0);
    // Empty our saved state before switching back, releasing target references.
    context->ClearState();
    context1->SwapDeviceContextState(previous.Get(), nullptr);
    Report("Screen grading active");
    return true;
}

void WorldPostprocessRelease() noexcept {
    cache = Cache{};
    Report("Screen grading resources released");
}

const char* WorldPostprocessStatus() noexcept { return status; }
void WorldPostprocessSetLogCallback(WorldPostprocessLogCallback callback) noexcept {
    logger.store(callback, std::memory_order_release);
}

