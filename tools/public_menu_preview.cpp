// Standalone visual verification: render the real menu to a software D3D11
// target and write a BMP. No window, game process, hook, or live snapshot needed.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include "public_menu.h"
#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace {
bool Check(HRESULT result, const char* action) {
    if (SUCCEEDED(result)) return true;
    std::fprintf(stderr, "%s failed: 0x%08lx\n", action, result);
    return false;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: public_menu_preview <output.bmp>\n");
        return 1;
    }
    constexpr UINT width = 418, height = 388;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (!Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context), "Create WARP device")) return 1;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> target;
    if (!Check(device->CreateTexture2D(&desc, nullptr, &texture), "Create target texture") ||
        !Check(device->CreateRenderTargetView(texture.Get(), nullptr, &target), "Create target view")) return 1;
    desc.BindFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (!Check(device->CreateTexture2D(&desc, nullptr, &staging), "Create readback texture")) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = io.LogFilename = nullptr;
    io.DisplaySize = { static_cast<float>(width), static_cast<float>(height) };
    io.DeltaTime = 1.0f / 60.0f;
    ConfigurePublicMenuStyle();
    if (!ImGui_ImplDX11_Init(device.Get(), context.Get())) {
        ImGui::DestroyContext();
        return 1;
    }
    const float clear[4] { 0.045f, 0.048f, 0.055f, 1 };
    ID3D11RenderTargetView* view = target.Get();
    context->OMSetRenderTargets(1, &view, nullptr);
    context->ClearRenderTargetView(target.Get(), clear);
    // Two frames settle the initial child/window geometry, as in the live menu.
    for (int frame = 0; frame < 2; ++frame) {
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        DrawPublicMenu();
        ImGui::Render();
    }
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    context->CopyResource(staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const bool readable = Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Readback image");
    bool saved = false;
    if (readable) {
        FILE* file = nullptr;
        if (_wfopen_s(&file, argv[1], L"wb") == 0 && file) {
            BITMAPFILEHEADER header{};
            header.bfType = 0x4D42;
            header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
            header.bfSize = header.bfOffBits + width * height * 4;
            BITMAPINFOHEADER info{};
            info.biSize = sizeof(info);
            info.biWidth = static_cast<LONG>(width);
            info.biHeight = -static_cast<LONG>(height);
            info.biPlanes = 1;
            info.biBitCount = 32;
            info.biCompression = BI_RGB;
            info.biSizeImage = width * height * 4;
            saved = std::fwrite(&header, sizeof(header), 1, file) == 1 &&
                    std::fwrite(&info, sizeof(info), 1, file) == 1;
            for (UINT row = 0; row < height && saved; ++row) {
                const auto* data = static_cast<const unsigned char*>(mapped.pData) + row * mapped.RowPitch;
                saved = std::fwrite(data, width * 4, 1, file) == 1;
            }
            if (std::fclose(file) != 0) saved = false;
        }
        context->Unmap(staging.Get(), 0);
    }
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    context->ClearState();
    std::printf("Menu preview %s\n", saved ? "saved" : "failed");
    return saved ? 0 : 1;
}
