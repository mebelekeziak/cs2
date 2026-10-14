#include "world_postprocess.h"
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <thread>

using Microsoft::WRL::ComPtr;
namespace {
unsigned checks = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "world_postprocess_checks failure: %s\n", message);
        std::exit(1);
    }
}
void Ok(HRESULT result, const char* message) { Check(SUCCEEDED(result), message); }
bool Near(float actual, float expected, float tolerance = 0.006f) {
    return std::isfinite(actual) && std::fabs(actual - expected) <= tolerance;
}
float UnsignedFloat(unsigned bits, unsigned fractionBits) {
    const unsigned exponent = bits >> fractionBits;
    const unsigned fraction = bits & ((1u << fractionBits) - 1);
    if (exponent == 0) return std::ldexp(static_cast<float>(fraction), -14 - static_cast<int>(fractionBits));
    if (exponent == 31) return std::numeric_limits<float>::infinity();
    return std::ldexp(1.0f + static_cast<float>(fraction) / static_cast<float>(1u << fractionBits),
        static_cast<int>(exponent) - 15);
}
struct Fixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Fixture() {
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        Ok(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
            D3D11_SDK_VERSION, &device, nullptr, &context), "create WARP device");
    }
};
struct Image {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> view;
    UINT width, height;
    DXGI_FORMAT format;
    Image(Fixture& f, UINT w = 8, UINT h = 8, DXGI_FORMAT fmt = DXGI_FORMAT_R32G32B32A32_FLOAT,
        UINT samples = 1) : width(w), height(h), format(fmt) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w; d.Height = h; d.MipLevels = d.ArraySize = 1;
        d.Format = fmt; d.SampleDesc.Count = samples;
        d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_RENDER_TARGET;
        Ok(f.device->CreateTexture2D(&d, nullptr, &texture), "create target");
        Ok(f.device->CreateRenderTargetView(texture.Get(), nullptr, &view), "create target view");
    }
    void Clear(Fixture& f, const std::array<float, 4>& color) {
        f.context->ClearRenderTargetView(view.Get(), color.data());
    }
    std::array<float, 4> Read(Fixture& f, UINT x = 3, UINT y = 3) {
        D3D11_TEXTURE2D_DESC d{};
        texture->GetDesc(&d);
        ComPtr<ID3D11Texture2D> single;
        ID3D11Texture2D* input = texture.Get();
        if (d.SampleDesc.Count > 1) {
            d.SampleDesc.Count = 1; d.SampleDesc.Quality = 0;
            Ok(f.device->CreateTexture2D(&d, nullptr, &single), "create readback resolve");
            f.context->ResolveSubresource(single.Get(), 0, texture.Get(), 0, format);
            input = single.Get();
        }
        d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        Ok(f.device->CreateTexture2D(&d, nullptr, &staging), "create staging");
        f.context->CopyResource(staging.Get(), input);
        D3D11_MAPPED_SUBRESOURCE map{};
        Ok(f.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), "map staging");
        const auto row = static_cast<const unsigned char*>(map.pData) + y * map.RowPitch;
        std::array<float, 4> result{};
        if (format == DXGI_FORMAT_R32G32B32A32_FLOAT) {
            const auto pixel = reinterpret_cast<const float*>(row) + x * 4;
            for (unsigned c = 0; c < 4; ++c) result[c] = pixel[c];
        } else if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
            const auto pixel = reinterpret_cast<const std::uint16_t*>(row) + x * 4;
            for (unsigned c = 0; c < 4; ++c)
                result[c] = UnsignedFloat(pixel[c] & 0x7fff, 10) * ((pixel[c] & 0x8000) ? -1.0f : 1.0f);
        } else if (format == DXGI_FORMAT_R10G10B10A2_UNORM) {
            const auto pixel = reinterpret_cast<const std::uint32_t*>(row)[x];
            for (unsigned c = 0; c < 3; ++c) result[c] = ((pixel >> (c * 10)) & 1023) / 1023.0f;
            result[3] = (pixel >> 30) / 3.0f;
        } else if (format == DXGI_FORMAT_R11G11B10_FLOAT) {
            const auto pixel = reinterpret_cast<const std::uint32_t*>(row)[x];
            result = {UnsignedFloat(pixel & 2047, 6), UnsignedFloat((pixel >> 11) & 2047, 6),
                UnsignedFloat(pixel >> 22, 5), 1.0f};
        } else {
            const auto pixel = row + x * 4;
            for (unsigned c = 0; c < 4; ++c) result[c] = pixel[c] / 255.0f;
            if (format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
                std::swap(result[0], result[2]);
        }
        f.context->Unmap(staging.Get(), 0);
        return result;
    }
    bool Render(Fixture& f, const WorldSettings& s) {
        return WorldPostprocessRender(f.device.Get(), f.context.Get(), texture.Get(), view.Get(), s);
    }
};

void SettingsChecks() {
    WorldResetSettings();
    Check(!WorldGetSettings().enabled && !WorldGetSettings().skyEnabled
        && !WorldGetSettings().worldMaterialEnabled && !WorldGetSettings().flatTextures, "disabled defaults");
    WorldSettings s;
    s.enabled = s.skyEnabled = true;
    s.exposureEv = 100; s.contrast = -1; s.saturation = 100; s.gamma = 0;
    s.tint[0] = -1; s.tint[1] = 2; s.tint[2] = std::numeric_limits<float>::quiet_NaN();
    s.vignette = 2; s.sharpening = -1; s.skyBrightness = 0;
    s.skyTint[0] = 2; s.skyLightingTint[1] = -2;
    s.worldTint[0] = -2; s.worldTint[1] = 2; s.worldTint[2] = std::numeric_limits<float>::quiet_NaN();
    WorldSetSettings(s);
    s = WorldGetSettings();
    Check(s.exposureEv == 3 && s.contrast == 0.5f && s.saturation == 2 && s.gamma == 0.5f,
        "grading range clamps");
    Check(s.tint[0] == 0 && s.tint[1] == 1 && s.tint[2] == 1, "tint finite clamps");
    Check(s.vignette == 1 && s.sharpening == 0 && s.skyBrightness == 0,
        "detail/sky clamps");
    Check(s.skyTint[0] == 1 && s.skyLightingTint[1] == 0, "sky tint clamps");
    Check(s.worldTint[0] == 0 && s.worldTint[1] == 1 && s.worldTint[2] == 1, "material tint finite clamps");
    s.exposureEv = std::numeric_limits<float>::infinity();
    s.gamma = std::numeric_limits<float>::quiet_NaN();
    Check(WorldSanitizeSettings(s).exposureEv == 0 && WorldSanitizeSettings(s).gamma == 1,
        "nonfinite values use neutral defaults");
    WorldSettings neutral;
    neutral.enabled = true; neutral.skyEnabled = true; neutral.skyBrightness = 4;
    Check(!WorldPostprocessIsActive(neutral), "sky settings cannot activate screen pass");
    neutral.worldMaterialEnabled = neutral.flatTextures = true; neutral.worldTint[0] = 0;
    Check(!WorldPostprocessIsActive(neutral), "material settings cannot activate screen pass");
    Check(WorldPostprocessRender(nullptr, nullptr, nullptr, nullptr, neutral), "neutral uses no device");
    neutral.enabled = false; neutral.exposureEv = 1;
    Check(WorldPostprocessRender(nullptr, nullptr, nullptr, nullptr, neutral), "disabled uses no device");
    neutral.enabled = true;
    Check(!WorldPostprocessRender(nullptr, nullptr, nullptr, nullptr, neutral), "active validates resources");
    WorldResetSettings();
    std::thread writer([] {
        for (unsigned i = 0; i < 10000; ++i) {
            WorldSettings value;
            value.exposureEv = (i & 1) ? 2.0f : -2.0f;
            value.saturation = (i & 1) ? 2.0f : 0.0f;
            WorldSetSettings(value);
        }
    });
    for (unsigned i = 0; i < 10000; ++i) {
        const auto value = WorldGetSettings();
        Check((value.exposureEv == 0 && value.saturation == 1)
            || (value.exposureEv == 2 && value.saturation == 2)
            || (value.exposureEv == -2 && value.saturation == 0), "settings snapshot is coherent");
    }
    writer.join();
    WorldResetSettings();
}

void PixelChecks(Fixture& f) {
    Image image(f);
    const std::array<float, 4> original{0.2f, 0.4f, 0.6f, 0.37f};
    WorldSettings s;
    s.exposureEv = 1;
    image.Clear(f, original);
    Check(image.Render(f, s), "disabled render succeeds");
    Check(image.Read(f) == original, "disabled preserves exact pixels");
    s = {}; s.enabled = true;
    Check(image.Render(f, s), "neutral render succeeds");
    Check(image.Read(f) == original, "neutral preserves exact pixels");
    s.exposureEv = 1;
    Check(image.Render(f, s), "exposure render");
    auto pixel = image.Read(f);
    Check(Near(pixel[0], 0.4f) && Near(pixel[1], 0.8f) && Near(pixel[2], 1.2f),
        "one EV doubles RGB and preserves HDR range");
    Check(Near(pixel[3], original[3]), "exposure preserves alpha");
    image.Clear(f, original); s.exposureEv = 0; s.saturation = 0;
    Check(image.Render(f, s), "saturation render");
    pixel = image.Read(f);
    const float luma = 0.2f * 0.2126f + 0.4f * 0.7152f + 0.6f * 0.0722f;
    Check(Near(pixel[0], luma) && Near(pixel[1], luma) && Near(pixel[2], luma), "zero saturation uses luminance");
    Check(Near(pixel[3], original[3]), "saturation preserves alpha");
    image.Clear(f, original); s.saturation = 1; s.tint[0] = 0.5f; s.tint[2] = 0;
    Check(image.Render(f, s), "tint render");
    pixel = image.Read(f);
    Check(Near(pixel[0], 0.1f) && Near(pixel[1], 0.4f) && Near(pixel[2], 0), "per channel tint");
    image.Clear(f, original); s = {}; s.enabled = true; s.contrast = 1.5f;
    Check(image.Render(f, s), "contrast render");
    pixel = image.Read(f);
    Check(Near(pixel[0], 0.05f) && Near(pixel[1], 0.35f) && Near(pixel[2], 0.65f), "contrast pivot");
    image.Clear(f, original); s.contrast = 1; s.gamma = 2;
    Check(image.Render(f, s), "gamma render");
    Check(Near(image.Read(f)[0], std::sqrt(original[0])), "gamma curve");
    image.Clear(f, original); s.gamma = 1; s.vignette = 1;
    Check(image.Render(f, s), "vignette render");
    Check(image.Read(f, 0, 0)[0] < image.Read(f, 3, 3)[0] && Near(image.Read(f, 0, 0)[3], original[3]),
        "vignette darkens edge and preserves alpha");
    std::array<std::array<float, 4>, 64> pixels;
    for (auto& value : pixels) value = {0.2f, 0.2f, 0.2f, 0.37f};
    pixels[3 + 3 * 8] = {0.5f, 0.5f, 0.5f, 0.37f};
    f.context->UpdateSubresource(image.texture.Get(), 0, nullptr, pixels.data(), 8 * 16, 0);
    s.vignette = 0; s.sharpening = 1;
    Check(image.Render(f, s), "sharpening render");
    Check(Near(image.Read(f)[0], 0.8f), "sharpening center responds to neighbors");
    for (const auto format : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}) {
        Image unorm(f, 8, 8, format);
        unorm.Clear(f, {0.25f, 0.25f, 0.25f, 0.37f});
        s = {}; s.enabled = true; s.exposureEv = 1;
        Check(unorm.Render(f, s), "UNORM/sRGB render");
        pixel = unorm.Read(f);
        const bool srgb = format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        const float expected = srgb ? 1.055f * std::pow(0.5f, 1.0f / 2.4f) - 0.055f : 0.5f;
        Check(Near(pixel[0], expected) && Near(pixel[1], expected) && Near(pixel[2], expected),
            "UNORM/sRGB output matches view transfer function");
        Check(Near(pixel[3], 0.37f), "UNORM alpha preserved");
    }
    Image msaa(f, 8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, 4);
    msaa.Clear(f, {0.25f, 0.25f, 0.25f, 0.37f});
    s = {}; s.enabled = true; s.exposureEv = 1;
    Check(msaa.Render(f, s), "MSAA resolve render");
    Check(Near(msaa.Read(f)[0], 0.5f) && Near(msaa.Read(f)[3], 0.37f), "MSAA pixels and alpha preserved");
    for (const auto format : {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R10G10B10A2_UNORM,
        DXGI_FORMAT_R11G11B10_FLOAT}) {
        Image formatted(f, 8, 8, format);
        formatted.Clear(f, {0.25f, 0.25f, 0.25f, 0.3333f});
        Check(formatted.Render(f, s), "HDR/10-bit render");
        pixel = formatted.Read(f);
        Check(Near(pixel[0], 0.5f) && Near(pixel[1], 0.5f) && Near(pixel[2], 0.5f), "HDR/10-bit RGB output");
        Check(Near(pixel[3], format == DXGI_FORMAT_R11G11B10_FLOAT ? 1.0f : 0.3333f), "HDR/10-bit alpha output");
    }
    Image resized(f, 17, 5);
    resized.Clear(f, original);
    Check(resized.Render(f, s), "size change rebuild");
    Check(Near(resized.Read(f)[0], 0.4f), "size change pixels");
    WorldPostprocessRelease();
    resized.Clear(f, original);
    Check(resized.Render(f, s), "release rebuild");
    Check(Near(resized.Read(f)[0], 0.4f), "release rebuild pixels");
    Image other(f);
    Check(!WorldPostprocessRender(f.device.Get(), f.context.Get(), resized.texture.Get(), other.view.Get(), s),
        "unrelated output view rejected");
    Fixture second;
    Image secondImage(second);
    secondImage.Clear(second, original);
    Check(secondImage.Render(second, s), "device change rebuild");
    Check(Near(secondImage.Read(second)[0], 0.4f), "device change pixels");
    Check(!WorldPostprocessRender(second.device.Get(), second.context.Get(), image.texture.Get(), image.view.Get(), s),
        "cross-device resource rejected");
    image.Clear(f, original);
    Check(image.Render(f, s), "original device rebuild");
    Check(Near(image.Read(f)[0], 0.4f), "original device rebuilt pixels");
}

void StateChecks(Fixture& f) {
    Image image(f);
    image.Clear(f, {0.2f, 0.3f, 0.4f, 0.5f});
    Image sentinel(f);
    const char code[] = "float4 VS(uint id : SV_VertexID) : SV_Position { return float4(id,0,0,1); }"
        "float4 PS() : SV_Target { return float4(1,0,0,1); }";
    ComPtr<ID3DBlob> vsCode, psCode;
    Ok(D3DCompile(code, sizeof(code) - 1, nullptr, nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vsCode, nullptr), "sentinel VS compile");
    Ok(D3DCompile(code, sizeof(code) - 1, nullptr, nullptr, nullptr, "PS", "ps_4_0", 0, 0, &psCode, nullptr), "sentinel PS compile");
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    Ok(f.device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs), "sentinel VS");
    Ok(f.device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps), "sentinel PS");
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 64; bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> cb;
    Ok(f.device->CreateBuffer(&bd, nullptr, &cb), "sentinel CB");
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER;
    ComPtr<ID3D11Buffer> geometry;
    Ok(f.device->CreateBuffer(&bd, nullptr, &geometry), "sentinel geometry");
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> source;
    ComPtr<ID3D11ShaderResourceView> srv;
    Ok(f.device->CreateTexture2D(&td, nullptr, &source), "sentinel source");
    Ok(f.device->CreateShaderResourceView(source.Get(), nullptr, &srv), "sentinel SRV");
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;
    Ok(f.device->CreateSamplerState(&sd, &sampler), "sentinel sampler");
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_WIREFRAME; rd.CullMode = D3D11_CULL_FRONT;
    rd.ScissorEnable = TRUE; rd.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> rasterizer;
    Ok(f.device->CreateRasterizerState(&rd, &rasterizer), "sentinel rasterizer");
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_GREATER;
    ComPtr<ID3D11DepthStencilState> depth;
    Ok(f.device->CreateDepthStencilState(&dd, &depth), "sentinel depth");
    D3D11_BLEND_DESC blendDesc{};
    auto& rtBlend = blendDesc.RenderTarget[0];
    rtBlend.BlendEnable = TRUE; rtBlend.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    rtBlend.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; rtBlend.BlendOp = D3D11_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D11_BLEND_ONE; rtBlend.DestBlendAlpha = D3D11_BLEND_ZERO;
    rtBlend.BlendOpAlpha = D3D11_BLEND_OP_ADD; rtBlend.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ComPtr<ID3D11BlendState> blend;
    Ok(f.device->CreateBlendState(&blendDesc, &blend), "sentinel blend");
    const float blendFactor[4]{0.1f, 0.2f, 0.3f, 0.4f};
    D3D11_QUERY_DESC queryDesc{D3D11_QUERY_OCCLUSION_PREDICATE, 0};
    ComPtr<ID3D11Predicate> predicate;
    Ok(f.device->CreatePredicate(&queryDesc, &predicate), "sentinel predicate");
    const D3D11_VIEWPORT vp{1, 2, 4, 3, 0.2f, 0.8f};
    const D3D11_RECT scissor{1, 2, 5, 5};
    ID3D11RenderTargetView* rtv = sentinel.view.Get();
    f.context->OMSetRenderTargets(1, &rtv, nullptr);
    f.context->VSSetShader(vs.Get(), nullptr, 0);
    f.context->PSSetShader(ps.Get(), nullptr, 0);
    ID3D11Buffer* b = cb.Get(); f.context->PSSetConstantBuffers(0, 1, &b);
    ID3D11ShaderResourceView* r = srv.Get(); f.context->PSSetShaderResources(0, 1, &r);
    ID3D11SamplerState* sm = sampler.Get(); f.context->PSSetSamplers(0, 1, &sm);
    const UINT stride = 8, offset = 4;
    b = geometry.Get(); f.context->IASetVertexBuffers(0, 1, &b, &stride, &offset);
    f.context->IASetIndexBuffer(geometry.Get(), DXGI_FORMAT_R16_UINT, 2);
    f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    f.context->RSSetViewports(1, &vp);
    f.context->RSSetScissorRects(1, &scissor);
    f.context->RSSetState(rasterizer.Get());
    f.context->OMSetDepthStencilState(depth.Get(), 19);
    f.context->OMSetBlendState(blend.Get(), blendFactor, 0x3f3f3f3f);
    f.context->SetPredication(predicate.Get(), TRUE);
    // Include bindings in a shader stage unused by the grading pass.
    r = srv.Get(); f.context->CSSetShaderResources(7, 1, &r);
    b = cb.Get(); f.context->CSSetConstantBuffers(3, 1, &b);
    WorldSettings s; s.enabled = true; s.exposureEv = 1;
    Check(image.Render(f, s), "state restoration render");
    ComPtr<ID3D11RenderTargetView> actualRtv;
    f.context->OMGetRenderTargets(1, &actualRtv, nullptr);
    Check(actualRtv.Get() == sentinel.view.Get(), "RTV restored");
    ComPtr<ID3D11VertexShader> actualVs;
    ComPtr<ID3D11PixelShader> actualPs;
    f.context->VSGetShader(&actualVs, nullptr, nullptr); f.context->PSGetShader(&actualPs, nullptr, nullptr);
    Check(actualVs.Get() == vs.Get() && actualPs.Get() == ps.Get(), "shaders restored");
    ComPtr<ID3D11Buffer> actualCb;
    f.context->PSGetConstantBuffers(0, 1, &actualCb);
    Check(actualCb.Get() == cb.Get(), "constant buffer restored");
    ComPtr<ID3D11ShaderResourceView> actualSrv;
    f.context->PSGetShaderResources(0, 1, &actualSrv);
    Check(actualSrv.Get() == srv.Get(), "SRV restored");
    ComPtr<ID3D11SamplerState> actualSampler;
    f.context->PSGetSamplers(0, 1, &actualSampler);
    Check(actualSampler.Get() == sampler.Get(), "sampler restored");
    ComPtr<ID3D11Buffer> actualVertex, actualIndex;
    UINT actualStride = 0, actualOffset = 0;
    f.context->IAGetVertexBuffers(0, 1, &actualVertex, &actualStride, &actualOffset);
    Check(actualVertex.Get() == geometry.Get() && actualStride == stride && actualOffset == offset, "vertex bindings restored");
    DXGI_FORMAT actualIndexFormat{};
    f.context->IAGetIndexBuffer(&actualIndex, &actualIndexFormat, &actualOffset);
    Check(actualIndex.Get() == geometry.Get() && actualIndexFormat == DXGI_FORMAT_R16_UINT && actualOffset == 2,
        "index binding restored");
    D3D11_PRIMITIVE_TOPOLOGY topology{}; f.context->IAGetPrimitiveTopology(&topology);
    Check(topology == D3D11_PRIMITIVE_TOPOLOGY_LINELIST, "topology restored");
    D3D11_VIEWPORT actualVp{}; UINT count = 1;
    f.context->RSGetViewports(&count, &actualVp);
    Check(count == 1 && actualVp.TopLeftX == vp.TopLeftX && actualVp.TopLeftY == vp.TopLeftY
        && actualVp.Width == vp.Width && actualVp.Height == vp.Height && actualVp.MinDepth == vp.MinDepth
        && actualVp.MaxDepth == vp.MaxDepth, "viewport restored");
    D3D11_RECT actualScissor{}; count = 1;
    f.context->RSGetScissorRects(&count, &actualScissor);
    Check(count == 1 && actualScissor.left == scissor.left && actualScissor.top == scissor.top
        && actualScissor.right == scissor.right && actualScissor.bottom == scissor.bottom, "scissor restored");
    ComPtr<ID3D11RasterizerState> actualRasterizer;
    f.context->RSGetState(&actualRasterizer);
    Check(actualRasterizer.Get() == rasterizer.Get(), "rasterizer restored");
    ComPtr<ID3D11DepthStencilState> actualDepth; UINT stencilReference = 0;
    f.context->OMGetDepthStencilState(&actualDepth, &stencilReference);
    Check(actualDepth.Get() == depth.Get() && stencilReference == 19, "depth/stencil restored");
    ComPtr<ID3D11BlendState> actualBlend;
    float actualBlendFactor[4]{}; UINT sampleMask = 0;
    f.context->OMGetBlendState(&actualBlend, actualBlendFactor, &sampleMask);
    Check(actualBlend.Get() == blend.Get() && sampleMask == 0x3f3f3f3f
        && actualBlendFactor[0] == blendFactor[0] && actualBlendFactor[1] == blendFactor[1]
        && actualBlendFactor[2] == blendFactor[2] && actualBlendFactor[3] == blendFactor[3], "blend restored");
    ComPtr<ID3D11Predicate> actualPredicate; BOOL predicateValue = FALSE;
    f.context->GetPredication(&actualPredicate, &predicateValue);
    Check(actualPredicate.Get() == predicate.Get() && predicateValue == TRUE, "predication restored");
    actualSrv.Reset(); f.context->CSGetShaderResources(7, 1, &actualSrv);
    actualCb.Reset(); f.context->CSGetConstantBuffers(3, 1, &actualCb);
    Check(actualSrv.Get() == srv.Get() && actualCb.Get() == cb.Get(), "compute resource bindings restored");
    f.context->ClearState();
    // The independent cache's saved state must not hold the game's target.
    const ULONG references = image.texture->AddRef(); image.texture->Release();
    WorldPostprocessRelease();
    const ULONG releasedReferences = image.texture->AddRef(); image.texture->Release();
    Check(references == releasedReferences, "cache retains no source backbuffer references");
    ComPtr<ID3D11DeviceContext> deferred;
    Ok(f.device->CreateDeferredContext(0, &deferred), "create deferred context");
    Check(!WorldPostprocessRender(f.device.Get(), deferred.Get(), image.texture.Get(), image.view.Get(), s),
        "deferred context rejected before mutation");
}
}

int main() {
    WorldResetSettings();
    SettingsChecks();
    Fixture fixture;
    PixelChecks(fixture);
    StateChecks(fixture);
    WorldPostprocessRelease();
    WorldResetSettings();
    std::printf("world_postprocess_checks: %u checks passed on D3D11 WARP\n", checks);
    return 0;
}

