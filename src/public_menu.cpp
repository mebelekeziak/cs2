#include "public_menu.h"
#include "esp_draw.h"
#include "movement_settings.h"
#include "world_settings.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <cwchar>

namespace {
ImFont* LoadSystemFont(ImFontAtlas& atlas) {
    wchar_t directory[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(directory, MAX_PATH);
    constexpr UINT fontSuffixLength = 18; // "\\Fonts\\segoeui.ttf"
    if (!length || length + fontSuffixLength >= MAX_PATH) return nullptr;
    wchar_t path[MAX_PATH]{};
    if (swprintf_s(path, L"%ls\\Fonts\\segoeui.ttf", directory) <= 0) return nullptr;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return nullptr;
    LARGE_INTEGER size{};
    constexpr LONGLONG maxFontBytes = 16 * 1024 * 1024;
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 100 || size.QuadPart > maxFontBytes) {
        CloseHandle(file);
        return nullptr;
    }
    void* bytes = ImGui::MemAlloc(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const bool complete = bytes && ReadFile(file, bytes, static_cast<DWORD>(size.QuadPart), &read, nullptr) &&
                          read == static_cast<DWORD>(size.QuadPart);
    CloseHandle(file);
    if (!complete) {
        ImGui::MemFree(bytes);
        return nullptr;
    }
    ImFontConfig config{};
    strcpy_s(config.Name, "Segoe UI, 14px");
    // The atlas owns the ImGui-allocated buffer until context destruction.
    // Load the installed font read-only; never copy or distribute its file.
    config.FontDataOwnedByAtlas = true;
    return atlas.AddFontFromMemoryTTF(bytes, static_cast<int>(size.QuadPart), 14, &config);
}
bool FeatureCheckbox(const char* label, bool& value) {
    // Keep the reference's blue square/white check, including disabled states.
    ImGui::PushStyleColor(ImGuiCol_CheckMark, { 0.96f, 0.98f, 1, 1 });
    const int extraColors = value ? 3 : 0;
    if (value) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, { 0.27f, 0.65f, 0.95f, 1 });
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, { 0.35f, 0.72f, 1, 1 });
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, { 0.22f, 0.58f, 0.87f, 1 });
    }
    const bool changed = ImGui::Checkbox(label, &value);
    ImGui::PopStyleColor(1 + extraColors);
    return changed;
}
ImVec2 ClampPosition(ImVec2 position, ImVec2 size, const ImGuiViewport& viewport) {
    return {
        std::clamp(position.x, viewport.WorkPos.x,
                   viewport.WorkPos.x + std::max(0.0f, viewport.WorkSize.x - size.x)),
        std::clamp(position.y, viewport.WorkPos.y,
                   viewport.WorkPos.y + std::max(0.0f, viewport.WorkSize.y - size.y))
    };
}
}

void ConfigurePublicMenuStyle() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.empty()) {
        io.FontDefault = LoadSystemFont(*io.Fonts);
        if (!io.FontDefault) io.FontDefault = io.Fonts->AddFontDefault();
    }
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = { 12, 10 };
    style.FramePadding = { 3, 2 };
    style.ItemSpacing = { 8, 7 };
    style.WindowMinSize = { 1, 1 };
    style.WindowRounding = style.ChildRounding = style.FrameRounding = 0;
    style.TabRounding = style.ScrollbarRounding = style.GrabRounding = 0;
    style.WindowBorderSize = style.ChildBorderSize = style.FrameBorderSize = 1;
    style.ScrollbarSize = 10;
    style.Alpha = 1;
    style.DisabledAlpha = 0.45f;
    style.Colors[ImGuiCol_Text] = { 0.82f, 0.83f, 0.85f, 1 };
    style.Colors[ImGuiCol_TextDisabled] = { 0.44f, 0.46f, 0.49f, 1 };
    style.Colors[ImGuiCol_WindowBg] = { 0.075f, 0.077f, 0.083f, 0.98f };
    style.Colors[ImGuiCol_ChildBg] = { 0.09f, 0.093f, 0.10f, 1 };
    style.Colors[ImGuiCol_Border] = { 0.22f, 0.24f, 0.27f, 1 };
    style.Colors[ImGuiCol_Separator] = style.Colors[ImGuiCol_Border];
    style.Colors[ImGuiCol_FrameBg] = { 0.11f, 0.12f, 0.13f, 1 };
    style.Colors[ImGuiCol_FrameBgHovered] = { 0.16f, 0.20f, 0.24f, 1 };
    style.Colors[ImGuiCol_FrameBgActive] = { 0.18f, 0.27f, 0.34f, 1 };
    style.Colors[ImGuiCol_CheckMark] = { 0.27f, 0.65f, 0.95f, 1 };
    style.Colors[ImGuiCol_ScrollbarBg] = { 0.075f, 0.077f, 0.083f, 1 };
    style.Colors[ImGuiCol_ScrollbarGrab] = { 0.25f, 0.28f, 0.32f, 1 };
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = { 0.34f, 0.40f, 0.46f, 1 };
    style.Colors[ImGuiCol_ScrollbarGrabActive] = { 0.27f, 0.65f, 0.95f, 1 };
}

void DrawPublicMenu() {
    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    if (viewport.WorkSize.x <= 0 || viewport.WorkSize.y <= 0) return;
    const ImVec2 size { std::min(370.0f, viewport.WorkSize.x),
                        std::min(310.0f, viewport.WorkSize.y) };
    ImGui::SetNextWindowPos({ 24, 24 }, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(size);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##PublicMenu", nullptr, flags)) {
        ImGui::SetWindowPos(ClampPosition(ImGui::GetWindowPos(), size, viewport));

        // A dedicated slim strip keeps settings clicks from moving the panel.
        ImGui::InvisibleButton("##DragMenu", { std::max(1.0f, ImGui::GetContentRegionAvail().x), 8 });
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 position = ImGui::GetWindowPos();
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ClampPosition({ position.x + delta.x, position.y + delta.y }, size, viewport));
        }

        // Store the selected category in this window's context-owned state.
        const ImGuiID tabId = ImGui::GetID("##SelectedCategory");
        int selectedTab = ImGui::GetStateStorage()->GetInt(tabId, 0);
        const float available = ImGui::GetContentRegionAvail().x;
        const float minimumTabWidth = ImGui::CalcTextSize("Movement").x + ImGui::GetStyle().FramePadding.x * 2;
        const int columns = available >= minimumTabWidth * 4 + ImGui::GetStyle().ItemSpacing.x * 3 ? 4 : 2;
        const float tabWidth = std::min(100.0f, std::max(1.0f,
            (available - ImGui::GetStyle().ItemSpacing.x * (columns - 1)) / columns));
        const auto tab = [&](const char* label, int index) {
            const bool selected = selectedTab == index;
            ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4{ 0.12f, 0.18f, 0.23f, 1 }
                                                           : ImVec4{ 0.11f, 0.12f, 0.13f, 1 });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.14f, 0.22f, 0.29f, 1 });
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, { 0.14f, 0.22f, 0.29f, 1 });
            ImGui::PushStyleColor(ImGuiCol_Border, selected ? ImVec4{ 0.27f, 0.65f, 0.95f, 1 }
                                                           : ImGui::GetStyleColorVec4(ImGuiCol_Border));
            if (ImGui::Button(label, { tabWidth, 25 })) selectedTab = index;
            ImGui::PopStyleColor(4);
        };
        tab("Visuals", 0);
        ImGui::SameLine();
        tab("Movement", 1);
        if (columns == 4) ImGui::SameLine();
        tab("World", 2);
        ImGui::SameLine();
        tab("Lighting", 3);
        ImGui::GetStateStorage()->SetInt(tabId, selectedTab);
        ImGui::Separator();

        const float footerHeight = ImGui::GetTextLineHeightWithSpacing() + 5;
        const float bodyHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y - footerHeight);
        if (selectedTab == 0) {
            EspSettings settings = EspGetSettings();
            bool changed = false;
            if (ImGui::BeginChild("##VisualSettings", { 0, bodyHeight }, ImGuiChildFlags_Borders)) {
                changed |= FeatureCheckbox("Enable ESP", settings.enabled);
                ImGui::Separator();
                ImGui::BeginDisabled(!settings.enabled);
                changed |= FeatureCheckbox("Boxes", settings.boxes);
                changed |= FeatureCheckbox("Names", settings.names);
                changed |= FeatureCheckbox("Distance labels", settings.distance);
                changed |= FeatureCheckbox("Health bars and values", settings.health);
                changed |= FeatureCheckbox("Armor", settings.armor);
                changed |= FeatureCheckbox("Show teammates", settings.teammates);
                ImGui::EndDisabled();
            }
            ImGui::EndChild();
            if (changed) EspSetSettings(settings);
        } else if (selectedTab == 1) {
            MovementSettings settings = MovementGetSettings();
            bool changed = false;
            if (ImGui::BeginChild("##MovementSettings", { 0, bodyHeight }, ImGuiChildFlags_Borders)) {
                changed |= FeatureCheckbox("Auto bunnyhop (hold Space)", settings.bunnyhop);
                changed |= FeatureCheckbox("Aggressive autostrafe (hold Space)", settings.autostrafe);
                ImGui::Separator();
                ImGui::TextDisabled("WASD  Direction and speed gain");
                ImGui::TextDisabled("Release  No autostop");
                ImGui::TextDisabled("F8  Bunnyhop    F9  Autostrafe");
            }
            ImGui::EndChild();
            if (changed) MovementSetSettings(settings);
        } else if (selectedTab == 2) {
            WorldSettings settings = WorldGetSettings();
            bool changed = false;
            bool reset = false;
            if (ImGui::BeginChild("##WorldSettings", { 0, bodyHeight }, ImGuiChildFlags_Borders)) {
                ImGui::TextUnformatted("Sky");
                changed |= FeatureCheckbox("Override sky", settings.skyEnabled);
                ImGui::BeginDisabled(!settings.skyEnabled);
                ImGui::TextUnformatted("Sky tint");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::ColorEdit3("##SkyTint", settings.skyTint, ImGuiColorEditFlags_NoInputs);
                ImGui::TextUnformatted("Sky probe/reflections tint");
                ImGui::SetItemTooltip("Updates only when the engine refreshes sky probe/reflection views. Use Lighting for sunlight and local lights.");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::ColorEdit3("##SkyLightingTint", settings.skyLightingTint, ImGuiColorEditFlags_NoInputs);
                ImGui::TextUnformatted("Sky brightness");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::SliderFloat("##SkyBrightness", &settings.skyBrightness, 0, 4, "%.2f");
                ImGui::EndDisabled();
                ImGui::Separator();
                ImGui::TextUnformatted("World surfaces");
                if (FeatureCheckbox("Flat world textures", settings.flatTextures)) {
                    settings.worldMaterialEnabled = settings.flatTextures;
                    changed = true;
                }
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("Uses a built-in flat material for static world surfaces.");
                ImGui::PopStyleColor();
                ImGui::Separator();
                ImGui::TextUnformatted("Screen color");
                changed |= FeatureCheckbox("Enable color grading", settings.enabled);
                ImGui::TextDisabled("Also affects the game HUD.");
                ImGui::BeginDisabled(!settings.enabled);
                const auto slider = [&](const char* label, float& value, float minimum, float maximum, const char* format = "%.2f") {
                    ImGui::TextUnformatted(label);
                    ImGui::PushID(label);
                    ImGui::SetNextItemWidth(-1);
                    const bool edited = ImGui::SliderFloat("##Value", &value, minimum, maximum, format);
                    ImGui::PopID();
                    return edited;
                };
                changed |= slider("Exposure", settings.exposureEv, -3, 3, "%.2f EV");
                changed |= slider("Contrast", settings.contrast, 0.5f, 1.5f);
                changed |= slider("Saturation", settings.saturation, 0, 2);
                changed |= slider("Gamma", settings.gamma, 0.5f, 2);
                ImGui::TextUnformatted("Color tint");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::ColorEdit3("##ColorTint", settings.tint, ImGuiColorEditFlags_NoInputs);
                changed |= slider("Vignette", settings.vignette, 0, 1);
                changed |= slider("Sharpening", settings.sharpening, 0, 1);
                ImGui::EndDisabled();
                ImGui::Separator();
                reset = ImGui::Button("Reset world visuals", { -1, 0 });
            }
            ImGui::EndChild();
            if (reset) WorldResetSettings();
            else if (changed) WorldSetSettings(settings);
        } else {
            WorldSettings settings = WorldGetSettings();
            bool changed = false;
            bool reset = false;
            if (ImGui::BeginChild("##LightingSettings", { 0, bodyHeight }, ImGuiChildFlags_Borders)) {
                changed |= FeatureCheckbox("Override lighting", settings.lightingEnabled);
                ImGui::Separator();
                ImGui::TextUnformatted("Local lights");
                ImGui::TextDisabled("Point, spot, orthographic and area lights");
                ImGui::BeginDisabled(!settings.lightingEnabled);
                changed |= FeatureCheckbox("Disable local lights", settings.disableLocalLights);
                ImGui::BeginDisabled(settings.disableLocalLights);
                ImGui::TextUnformatted("Light color");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::ColorEdit3("##LocalLightTint", settings.localLightTint, ImGuiColorEditFlags_NoInputs);
                ImGui::TextUnformatted("Light brightness");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::SliderFloat("##LocalLightBrightness", &settings.localLightBrightness, 0, 8, "%.2f");
                ImGui::EndDisabled();
                ImGui::Separator();
                ImGui::TextUnformatted("Sunlight");
                ImGui::TextUnformatted("Sun color");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::ColorEdit3("##SunLightTint", settings.sunLightTint, ImGuiColorEditFlags_NoInputs);
                ImGui::TextUnformatted("Sun brightness");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::SliderFloat("##SunLightBrightness", &settings.sunLightBrightness, 0, 8, "%.2f");
                ImGui::Separator();
                ImGui::TextUnformatted("Directional lightmaps");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::Combo("##DirectionalLightmaps", &settings.directionalLightmapsMode,
                                       "Game value\0Enabled\0Disabled\0");
                ImGui::EndDisabled();
                ImGui::Separator();
                reset = ImGui::Button("Reset lighting", { -1, 0 });
            }
            ImGui::EndChild();
            if (reset) WorldResetLightingSettings();
            else if (changed) WorldSetSettings(settings);
        }
        ImGui::TextDisabled("Insert  Menu    End  Stop");
    }
    ImGui::End();
}
