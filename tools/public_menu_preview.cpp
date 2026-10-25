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
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include "public_menu.h"
#include "bullet_tracers.h"
#include "aim_visualization.h"
#include "third_person_settings.h"
#include "zoom_settings.h"
#include "viewmodel_settings.h"
#include "silent_aim_settings.h"
#include "anti_aim_settings.h"
#include "model_swap_settings.h"
#include "skin_settings.h"
#include "skin_catalog.h"
#include "player_chams_settings.h"
#include "grenade_settings.h"
#include "grenade_draw.h"
#include "model_catalog.h"
#include "scoped_crosshair_draw.h"
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <array>

using Microsoft::WRL::ComPtr;

namespace {
bool Check(HRESULT result, const char* action) {
    if (SUCCEEDED(result)) return true;
    std::fprintf(stderr, "%s failed: 0x%08lx\n", action, result);
    return false;
}
struct ModelPickerFixture {
    std::filesystem::path root;
    ~ModelPickerFixture() {
        if (!root.empty()) { std::error_code error; std::filesystem::remove_all(root, error); }
    }
    bool Create() {
        root = std::filesystem::path(CS2_WORKSPACE_ROOT) / L"logs" /
            (L"model-picker-preview-" + std::to_wstring(GetCurrentProcessId()));
        const auto graphPath = root / "animation/graphs/worldmodel/worldmodel.vnmgraph_c";
        std::filesystem::create_directories(graphPath.parent_path());
        std::ofstream(graphPath).put('x');
        const std::string graph = "animation/graphs/worldmodel/worldmodel.vnmgraph";
        for (const auto* name : {"ellen", "miyabi", "soukaku", "zhao"}) {
            const auto model = root / "agents/models/hmn_ag2/zenless_zone_zero" / name / (std::string(name) + ".vmdl_c");
            std::filesystem::create_directories(model.parent_path());
            std::string bytes(52, '\0'); bytes += graph; bytes += '\0';
            const auto put = [&](std::size_t offset, std::uint32_t value) { std::memcpy(bytes.data() + offset, &value, sizeof(value)); };
            put(0, static_cast<std::uint32_t>(bytes.size())); bytes[4] = 12; bytes[6] = 1;
            put(8, 8); put(12, 1); std::memcpy(bytes.data() + 16, "RERL", 4);
            put(20, 8); put(24, static_cast<std::uint32_t>(bytes.size() - 28));
            put(28, 8); put(32, 1); put(36, 1); put(44, 8);
            std::ofstream stream(model, std::ios::binary);
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        std::string error;
        if (!ModelCatalogDiscover(root, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return false; }
        ModelSwapSettings settings;
        settings.enabled = true;
        ModelSwapSetModel(settings, "agents/models/hmn_ag2/zenless_zone_zero/ellen/ellen.vmdl");
        ModelSwapSetSettings(settings);
        ModelSwapSetRuntimeStatus("Waiting for a local player.");
        return true;
    }
};
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 6 || argc == 5) {
        std::fprintf(stderr, "Preview 17 draws synthetic eligible aim centers; 18 shows Fresnel chams; 19-23 show skins and item details; 24 shows viewmodel controls; 25 shows bullet tracers.\n");
        std::fprintf(stderr, "Usage: public_menu_preview <output.bmp> [tab: 0=Visuals, 1=Movement, 2=World, 3=Lighting, 4=Aim, 5=Configs, 6=Camera preview, 7=Scoped zoom preview, 8=Player model preview, 9=Anti aim jitter preview, 10=Anti aim spin preview, 11=Scoped crosshair still, 12=Scoped crosshair spread, 13=Flat player chams menu, 14=Iridescent player chams menu, 15=Grenades menu, 16=Grenade aura drawing, 17=Eligible aim points, 18=Fresnel player chams menu, 19=T knife, 20=CT knife, 21=T weapons, 22=CT gloves, 23=CT knife details, 24=Viewmodel controls, 25=Bullet tracers] [scroll pixels] [width height]\n");
        return 1;
    }
    wchar_t* end = nullptr;
    const long selectedTab = argc >= 3 ? std::wcstol(argv[2], &end, 10) : 0;
    if (selectedTab < 0 || selectedTab > 25 || (argc >= 3 && (end == argv[2] || *end))) {
        std::fprintf(stderr, "Tab must be between 0 and 25 (24=Viewmodel controls, 25=Bullet tracers).\n");
        return 1;
    }
    const long scroll = argc >= 4 ? std::wcstol(argv[3], &end, 10) : 0;
    if (scroll < 0 || scroll > 100000 || (argc >= 4 && (end == argv[3] || *end))) {
        std::fprintf(stderr, "Scroll must be between 0 and 100000 pixels.\n");
        return 1;
    }
    const long requestedWidth = argc == 6 ? std::wcstol(argv[4], &end, 10) : 608;
    if (requestedWidth < 180 || requestedWidth > 4096 || (argc == 6 && (end == argv[4] || *end))) {
        std::fprintf(stderr, "Width must be between 180 and 4096 pixels.\n");
        return 1;
    }
    const long requestedHeight = argc == 6 ? std::wcstol(argv[5], &end, 10) : 668;
    if (requestedHeight < 150 || requestedHeight > 4096 || (argc == 6 && (end == argv[5] || *end))) {
        std::fprintf(stderr, "Height must be between 150 and 4096 pixels.\n");
        return 1;
    }
    const UINT width = static_cast<UINT>(requestedWidth), height = static_cast<UINT>(requestedHeight);
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
    if (selectedTab == 4) {
        SilentAimSetSettings({true, 5, AimHitbox::Chest, true, 70, 256});
        AimVisualizationSetSettings({true, {.3f, .85f, 1, 1}});
    }
    if (selectedTab == 9 || selectedTab == 10) {
        AntiAimSettings antiAim;
        antiAim.enabled = true;
        antiAim.yawMode = selectedTab == 9 ? AntiAimYawMode::Jitter : AntiAimYawMode::Spin;
        antiAim.pitchMode = selectedTab == 9 ? AntiAimPitchMode::Jitter : AntiAimPitchMode::Custom;
        AntiAimSetSettings(antiAim);
    }
    if (selectedTab == 6) ThirdPersonSetSettings({true});
    if (selectedTab == 7) ZoomSetSettings({true, 0, 150, 50, true});
    if (selectedTab == 24) ViewmodelSetSettings({true, 2, -1, 1, 90});
    if (selectedTab == 25) BulletTracerSetSettings({true, BulletTracerMaterial::Plasma, {.3f, .75f, 1, 1}, true, .75f, 2.5f, 3, true, true});
    if (selectedTab == 15) GrenadeSetSettings({true, true, true, true});
    if (selectedTab >= 19 && selectedTab <= 23) {
        SkinSettings skins;
        skins.enabled = true;
        skins.teams[0].weapons[0] = {true, 7, 180, 27, .125f, 42};
        skins.teams[1].weapons[0] = {true, 7, 302, 88, .25f, -1};
        const auto addWeapon = [&](int team, int definition) {
            const auto finishes = skin_catalog::compatible_paints(definition);
            if (auto* weapon = SkinEnsureWeaponSettings(skins.teams[static_cast<std::size_t>(team)], definition))
                *weapon = {true, definition, finishes.size() > 1 ? finishes[1] : 0, 42, .15f, -1};
        };
        addWeapon(0, 4);
        addWeapon(0, 9);
        addWeapon(1, 16);
        addWeapon(1, 60);
        addWeapon(1, 61);
        addWeapon(1, 9);
        skins.teams[0].knife = {true, 500, 38, 17, .125f};
        skins.teams[1].knife = {true, 507, 12, 77, .25f};
        SkinSetCustomName(skins.teams[1].knife, "Crimson Web");
        skins.teams[0].gloves = {true, 5030, 10018, 12, .125f};
        skins.teams[1].gloves = {true, 5031, 10016, 21, .25f};
        SkinSetSettings(skins);
        SkinSetRuntimeStatus("Standalone menu preview; no game client is running.");
    }
    if (selectedTab == 13 || selectedTab == 14 || selectedTab == 18) {
        PlayerChamsSettings chams;
        chams.enabled = true;
        chams.style = selectedTab == 18 ? PlayerChamsStyle::Fresnel :
            selectedTab == 13 ? PlayerChamsStyle::Flat : PlayerChamsStyle::Iridescent;
        PlayerChamsSetSettings(chams);
    }
    ModelPickerFixture modelFixture;
    if (selectedTab == 8 && !modelFixture.Create()) { ImGui::DestroyContext(); return 1; }
    if (!ImGui_ImplDX11_Init(device.Get(), context.Get())) {
        ImGui::DestroyContext();
        return 1;
    }
    const float clear[4] { 0.045f, 0.048f, 0.055f, 1 };
    ID3D11RenderTargetView* view = target.Get();
    context->OMSetRenderTargets(1, &view, nullptr);
    context->ClearRenderTargetView(target.Get(), clear);
    // Establish the real window first, then select its context-owned category.
    // Two further frames settle the selected child geometry.
    for (int frame = 0; frame < 4; ++frame) {
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        if (selectedTab == 11 || selectedTab == 12)
            ScopedCrosshairDraw({true, .0005f, selectedTab == 12 ? .04f : .001f, 1},
                io.DisplaySize.x, io.DisplaySize.y, io.DeltaTime);
        else if (selectedTab == 17) {
            auto* draw = ImGui::GetBackgroundDrawList();
            draw->AddText({24,24}, IM_COL32(225,230,240,255), "RAGEBOT VISUALIZATION / SYNTHETIC DAMAGE-PATH CENTERS");
            draw->AddText({24,48}, IM_COL32(150,165,180,255), "Damage-path candidates before hitchance and firing timing");
            EspPlayerSnapshot actors[2]{};
            actors[0].handle = 1; actors[0].health = 100; actors[0].team = 2; actors[0].localPlayer = true;
            actors[1].handle = 2; actors[1].health = 100; actors[1].team = 3;
            EspFrameSnapshot players{}; players.players = actors; players.playerCount = 2; players.localTeam = 2;
            players.valid = players.matrixValid = true;
            players.worldToClip.m[0][0] = players.worldToClip.m[1][1] = players.worldToClip.m[3][3] = 1;
            AimVisualizationFrame points{}; points.localPawn = 1;
            const EspVec3 centers[]{{0,.60f,0},{0,.35f,0},{0,.15f,0},{0,-.05f,0},
                {-.18f,.3f,0},{.18f,.3f,0},{-.25f,.10f,0},{.25f,.10f,0},
                {-.3f,-.1f,0},{.3f,-.1f,0},{-.1f,-.3f,0},{.1f,-.3f,0},{-.12f,-.55f,0},{.12f,-.55f,0}};
            const auto screen = [&](float x, float y) { return ImVec2{(1+x)*io.DisplaySize.x*.5f,(1-y)*io.DisplaySize.y*.5f}; };
            const auto body = IM_COL32(45,70,100,255);
            draw->AddCircleFilled(screen(0,.60f), io.DisplaySize.y*.055f, body, 32);
            draw->AddLine(screen(0,.4f),screen(0,-.1f),body,io.DisplaySize.x*.14f);
            for (float side : {-1.0f,1.0f}) {
                draw->AddLine(screen(side*.14f,.35f),screen(side*.3f,-.1f),body,io.DisplaySize.x*.045f);
                draw->AddLine(screen(side*.08f,-.05f),screen(side*.12f,-.62f),body,io.DisplaySize.x*.055f);
            }
            for (const auto& center : centers) points.points[points.count++] = {center,2};
            AimVisualizationDraw(points, players, io.DisplaySize.x, io.DisplaySize.y, {true,{.3f,.85f,1,1}});
        }
        else if (selectedTab == 16) {
            // Synthetic copied footprints, rendered by the production aura path.
            // This checks drawing only, without claiming native gameplay output.
            std::array<bool,1024> columns{};
            for (int y=10;y<23;++y) for (int x=10;x<23;++x)
                columns[x+32*y]=(x-16)*(x-16)+(y-16)*(y-16)<42 && !(x>18 && y<15);
            std::array<GrenadeArea,2> areas;
            areas[0].edges=GrenadeSmokeBoundary(columns,{-240,0,0},20);
            areas[1].kind=GrenadeAreaKind::Molotov;
            GrenadeDisk disks[]{{{170,0,0},65},{{230,35,0},65},{{280,-15,0},65},{{170,-70,0},50}};
            areas[1].edges=GrenadeDiskBoundary(disks);
            EspMatrix4x4 matrix{};
            matrix.m[0][0]=1.0f/500; matrix.m[1][1]=1.0f/300; matrix.m[3][3]=1;
            auto* draw=ImGui::GetBackgroundDrawList();
            draw->AddText({24,24},IM_COL32(225,230,240,255),"GRENADE BORDER AURAS / SYNTHETIC FOOTPRINT PREVIEW");
            draw->AddText({24,48},IM_COL32(150,165,180,255),"Smoke voxel columns (blue) and active flame patches (orange)");
            GrenadeSettings settings; settings.smokeAura=settings.molotovAura=true;
            GrenadeDraw(areas,matrix,io.DisplaySize.x,io.DisplaySize.y,settings);
        } else DrawPublicMenu();
        ImGui::Render();
        if (frame == 0) {
            ImGuiWindow* menu = ImGui::FindWindowByName("##PublicMenu");
            if (menu) {
                menu->StateStorage.SetInt(menu->GetID("##SelectedCategory"), selectedTab >= 24 ? 0 : selectedTab >= 19 ? 6 : selectedTab >= 13 ? 0 : selectedTab >= 9 ? 4 : selectedTab >= 6 ? 0 : static_cast<int>(selectedTab));
                if (((selectedTab >= 6 && selectedTab <= 8) || (selectedTab >= 13 && selectedTab <= 18) || selectedTab >= 24) && !menu->DC.ChildWindows.empty()) {
                    auto* child = menu->DC.ChildWindows[0];
                    child->StateStorage.SetInt(child->GetID(selectedTab == 25 ? "Bullet tracers" : selectedTab == 24 ? "Viewmodel" : selectedTab == 15 ? "Grenades" : selectedTab >= 13 ? "Player chams" : selectedTab == 6 ? "Camera" : selectedTab == 7 ? "Scoped zoom" : "Player model"), 1);
                    child->StateStorage.SetInt(child->GetID("Live preview"), 0);
                }
            }
        }
        if ((frame == 1 || (frame == 2 && selectedTab >= 19)) &&
            (scroll || selectedTab == 8 || selectedTab >= 24 || (selectedTab >= 9 && selectedTab <= 18))) {
            ImGuiWindow* menu = ImGui::FindWindowByName("##PublicMenu");
            if (menu && !menu->DC.ChildWindows.empty())
                ImGui::SetScrollY(menu->DC.ChildWindows[0], static_cast<float>(scroll ? scroll : selectedTab >= 13 ? 100000 : selectedTab >= 9 ? 550 : 100000));
        }
    if (frame == 1 && selectedTab >= 19 && selectedTab <= 23) {
            ImGuiWindow* menu = ImGui::FindWindowByName("##PublicMenu");
            if (menu && !menu->DC.ChildWindows.empty()) {
                auto* child = menu->DC.ChildWindows[0];
                child->StateStorage.SetInt(child->GetID("##SkinSelectedTeam"), selectedTab == 20 || selectedTab == 22 || selectedTab == 23 ? 1 : 0);
                child->StateStorage.SetInt(child->GetID("##SkinSelectedKind"), selectedTab == 21 ? 0 : selectedTab == 22 ? 2 : 1);
                if (selectedTab == 23) child->StateStorage.SetInt(child->GetID("Wear & details"), 1);
            }
        }
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
