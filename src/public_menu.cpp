#include "public_menu.h"
#include "aim_visualization.h"
#include "esp_draw.h"
#include "movement_settings.h"
#include "silent_aim_settings.h"
#include "anti_aim_settings.h"
#include "third_person_settings.h"
#include "zoom_settings.h"
#include "shot_log_settings.h"
#include "model_swap_settings.h"
#include "player_chams_settings.h"
#include "grenade_settings.h"
#include "model_catalog.h"
#include "console_control.h"
#include "world_settings.h"
#include "local_config.h"
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
#include <string>
#include <type_traits>
#include <vector>

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

void DrawConfigs(bool entering) {
    // Accessed only by the render owner; no engine pointers or callbacks retained.
    static char name[65] = "default";
    static std::vector<std::string> presets;
    static std::string status;
    const auto refresh = [&]() {
        std::string error;
        if (!LocalConfigList(presets, error)) status = error;
    };
    if (entering) refresh();
    ImGui::TextUnformatted("Saved presets");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##ConfigPresets", presets.empty() ? "No saved configs" : name)) {
        for (const auto& preset : presets) {
            if (ImGui::Selectable(preset.c_str(), preset == name)) {
                strcpy_s(name, preset.c_str());
                status.clear();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextUnformatted("Config name");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##ConfigName", name, sizeof(name));
    ImGui::TextWrapped("Letters, numbers, spaces, - and _. Up to 64 characters.");
    if (ImGui::Button("Save / overwrite", { -1, 0 })) {
        std::string error;
        if (LocalConfigSave(name, error)) {
            status = std::string("Saved: ") + name;
            refresh();
        } else status = error;
    }
    if (ImGui::Button("Load config", { -1, 0 })) {
        std::string error;
        status = LocalConfigLoad(name, error) ? std::string("Loaded: ") + name : error;
    }
    if (ImGui::Button("Refresh list", { -1, 0 })) refresh();
    if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("Saves Visuals, Movement, World, Lighting and Aim options. Loading replaces current options.");
    ImGui::TextWrapped("Presets stay on disk between runs. Choose Load config to apply one.");
}

bool DrawAntiAimSettings(AntiAimSettings& settings) {
    bool changed = FeatureCheckbox("Enable anti aim", settings.enabled);
    ImGui::BeginDisabled(!settings.enabled);
    const auto combo = [&](const char* label, const char* id, auto& mode, const char* items) {
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(-1);
        int selected = static_cast<int>(mode);
        const bool edited = ImGui::Combo(id, &selected, items);
        if (edited) mode = static_cast<std::remove_reference_t<decltype(mode)>>(selected);
        return edited;
    };
    const auto slider = [&](const char* label, const char* id, float& value,
                            float minimum, float maximum, const char* format = "%.1f degrees") {
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(-1);
        return ImGui::SliderFloat(id, &value, minimum, maximum, format);
    };
    changed |= combo("Yaw mode", "##AntiAimYawMode", settings.yawMode, "Static\0Jitter\0Spin\0Random\0");
    changed |= combo("Yaw base", "##AntiAimYawBase", settings.yawBase, "View relative\0World absolute\0");
    changed |= slider("Yaw offset", "##AntiAimYawOffset", settings.yawOffset, -180, 180);
    if (settings.yawMode == AntiAimYawMode::Jitter || settings.yawMode == AntiAimYawMode::Random) {
        changed |= slider("Yaw range (total width)", "##AntiAimJitterRange", settings.jitterRange, 0, 360);
        ImGui::TextWrapped("Range 60 means up to 30 degrees either side of the yaw offset.");
    }
    if (settings.yawMode == AntiAimYawMode::Spin)
        changed |= slider("Spin speed", "##AntiAimSpinSpeed", settings.spinSpeed, -180, 180, "%.1f degrees / command");
    changed |= combo("Pitch mode", "##AntiAimPitchMode", settings.pitchMode,
        "Down\0Up\0Level\0Keep view pitch\0Custom\0Jitter\0");
    if (settings.pitchMode == AntiAimPitchMode::Custom || settings.pitchMode == AntiAimPitchMode::Jitter)
        changed |= slider(settings.pitchMode == AntiAimPitchMode::Jitter ? "Pitch A" : "Custom pitch",
            "##AntiAimPitch", settings.pitch, -89, 89);
    if (settings.pitchMode == AntiAimPitchMode::Jitter)
        changed |= slider("Pitch B", "##AntiAimPitchAlternate", settings.pitchAlternate, -89, 89);
    if (settings.yawMode == AntiAimYawMode::Jitter || settings.yawMode == AntiAimYawMode::Random ||
        settings.pitchMode == AntiAimPitchMode::Jitter) {
        ImGui::TextUnformatted("Change interval");
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::SliderInt("##AntiAimInterval", &settings.jitterInterval, 1, 64, "%d commands");
        ImGui::TextWrapped("Commands per jitter side or random yaw sample. Pitch jitter uses the same interval.");
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Positive pitch looks down; negative looks up. Local view stays normal.");
    if (ImGui::Button("Reset anti aim", { -1, 0 })) {
        settings = {};
        changed = true;
    }
    return changed;
}

void DrawPlayerModel() {
    if (!ImGui::CollapsingHeader("Player model", ImGuiTreeNodeFlags_NoAutoOpenOnLog)) return;
    auto settings = ModelSwapGetSettings();
    bool changed = FeatureCheckbox("Enable local player model", settings.enabled);
    ImGui::TextWrapped("Changes your player only. Use third person to inspect the model.");

    static ImGuiTextFilter filter;
    static std::shared_ptr<const ModelCatalog> cachedCatalog;
    static std::vector<std::size_t> matches;
    const auto catalog = ModelCatalogSnapshot();
    ImGui::TextUnformatted("Search models");
    ImGui::SetNextItemWidth(-1);
    const bool filterChanged = filter.Draw("##PlayerModelSearch", -1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Search model names or paths. Separate terms with commas.");
    if (filterChanged || cachedCatalog != catalog) {
        cachedCatalog = catalog;
        matches.clear();
        if (catalog) {
            for (std::size_t index = 0; index < catalog->size(); ++index) {
                const auto& entry = (*catalog)[index];
                const auto searchable = entry.path + " " + entry.label;
                if (filter.PassFilter(searchable.c_str())) matches.push_back(index);
            }
        }
    }
    ImGui::TextDisabled("%zu matching / %zu available models", matches.size(), catalog ? catalog->size() : 0);
    if (filterChanged) ImGui::SetNextWindowScroll({0, 0});
    if (ImGui::BeginListBox("##PlayerModelList", { -1, 155 })) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(matches.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& entry = (*catalog)[matches[static_cast<std::size_t>(row)]];
                const auto label = entry.label + "  (" + entry.path + ")";
                const bool selected = entry.path == settings.model.data();
                ImGui::PushID(entry.path.c_str());
                if (ImGui::Selectable(label.c_str(), selected)) changed |= ModelSwapSetModel(settings, entry.path);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", entry.path.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndListBox();
    }
    if (!catalog) ImGui::TextWrapped("No model catalog available. Loose custom models are discovered when the overlay starts.");
    else if (catalog->empty()) ImGui::TextWrapped("No supported loose player models found.");
    else if (matches.empty()) ImGui::TextWrapped("No models match this search.");
    if (settings.model[0]) {
        ImGui::TextWrapped("Selected: %s", settings.model.data());
        const auto found = catalog ? std::lower_bound(catalog->begin(), catalog->end(), settings.model.data(),
            [](const ModelCatalogEntry& entry, const char* path) { return entry.path.compare(path) < 0; }) : ModelCatalog::const_iterator{};
        if (catalog && (found == catalog->end() || found->path != settings.model.data()))
            ImGui::TextWrapped("The selected model is unavailable in the current catalog. Choose an available model.");
    } else ImGui::TextWrapped("Select a model to apply.");
    const auto status = ModelSwapRuntimeStatus();
    if (status[0]) ImGui::TextWrapped("%s", status.data());
    if (changed) ModelSwapSetSettings(settings);
    if (ImGui::Button("Reset player model", { -1, 0 })) ModelSwapResetSettings();
}
}

void ConfigurePublicMenuStyle() {
    ImGuiIO& io = ImGui::GetIO();
    // Public text fields consume the polled keyboard/text events. A context
    // initialized with the diagnostic flags must not silently discard them.
    io.ConfigFlags &= ~ImGuiConfigFlags_NoKeyboard;
    if (io.Fonts->Fonts.empty()) {
        io.FontDefault = LoadSystemFont(*io.Fonts);
        if (!io.FontDefault) io.FontDefault = io.Fonts->AddFontDefault();
    }
    EspConfigureFonts();
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
    const ImVec2 size { std::min(560.0f, viewport.WorkSize.x),
                        std::min(620.0f, viewport.WorkSize.y) };
    ImGui::SetNextWindowPos({ 24, 24 }, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(size);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##PublicMenu", nullptr, flags)) {
        ImGui::SetWindowPos(ClampPosition(ImGui::GetWindowPos(), size, viewport));

        // The whole visible title bar, including its side/top padding, is a
        // drag target. Controls below it keep their own mouse interaction.
        constexpr float titleHeight = 28;
        const ImVec2 windowOrigin = ImGui::GetWindowPos();
        ImGui::PushClipRect(windowOrigin, { windowOrigin.x + size.x, windowOrigin.y + size.y }, false);
        ImGui::SetCursorPos({ 0, 0 });
        ImGui::InvisibleButton("##DragMenu", { size.x, titleHeight });
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 position = ImGui::GetWindowPos();
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ClampPosition({ position.x + delta.x, position.y + delta.y }, size, viewport));
        }
        const ImVec2 titleOrigin = ImGui::GetWindowPos();
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(titleOrigin, { titleOrigin.x + size.x, titleOrigin.y + titleHeight },
                            ImGui::GetColorU32(ImGuiCol_FrameBg));
        draw->AddText({ titleOrigin.x + ImGui::GetStyle().WindowPadding.x,
                        titleOrigin.y + (titleHeight - ImGui::GetTextLineHeight()) * 0.5f },
                      ImGui::GetColorU32(ImGuiCol_Text), "CS2");
        draw->AddLine({ titleOrigin.x, titleOrigin.y + titleHeight },
                      { titleOrigin.x + size.x, titleOrigin.y + titleHeight },
                      ImGui::GetColorU32(ImGuiCol_Border));
        ImGui::PopClipRect();
        ImGui::SetCursorPos({ ImGui::GetStyle().WindowPadding.x,
                             titleHeight + ImGui::GetStyle().ItemSpacing.y });

        // Store the selected category in this window's context-owned state.
        const ImGuiID tabId = ImGui::GetID("##SelectedCategory");
        int selectedTab = ImGui::GetStateStorage()->GetInt(tabId, 0);
        const ImGuiID lastTabId = ImGui::GetID("##LastDrawnCategory");
        const int lastTab = ImGui::GetStateStorage()->GetInt(lastTabId, -1);
        const bool compactCategories = viewport.WorkSize.y < 240;
        if (compactCategories) {
            constexpr const char* categories[]{"Visuals", "Movement", "World", "Lighting", "Aim", "Configs"};
            ImGui::SetNextItemWidth(-1);
            ImGui::Combo("##Category", &selectedTab, categories, 6);
        } else {
        const float available = ImGui::GetContentRegionAvail().x;
        const float minimumTabWidth = ImGui::CalcTextSize("Movement").x + ImGui::GetStyle().FramePadding.x * 2;
        const int columns = available >= minimumTabWidth * 6 + ImGui::GetStyle().ItemSpacing.x * 5 ? 6 : 2;
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
        if (columns == 6) ImGui::SameLine();
        tab("World", 2);
        ImGui::SameLine();
        tab("Lighting", 3);
        if (columns == 6) ImGui::SameLine();
        tab("Aim", 4);
        ImGui::SameLine();
        tab("Configs", 5);
        }
        ImGui::GetStateStorage()->SetInt(tabId, selectedTab);
        ImGui::GetStateStorage()->SetInt(lastTabId, selectedTab);
        ImGui::Separator();

        const float footerHeight = ImGui::GetTextLineHeightWithSpacing() + 5;
        const float bodyHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y - footerHeight);
        if (selectedTab == 0) {
            EspSettings settings = EspGetSettings();
            bool changed = false;
            bool reset = false;
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
                ImGui::Separator();
                const auto color = [&](const char* label, const char* id, float (&value)[4]) {
                    ImGui::TextUnformatted(label);
                    ImGui::SetNextItemWidth(-1);
                    return ImGui::ColorEdit4(id, value, ImGuiColorEditFlags_NoInputs |
                        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
                };
                const auto slider = [&](const char* label, const char* id, float& value,
                                        float minimum, float maximum, const char* format) {
                    ImGui::TextUnformatted(label);
                    ImGui::SetNextItemWidth(-1);
                    return ImGui::SliderFloat(id, &value, minimum, maximum, format);
                };
                if (ImGui::CollapsingHeader("Boxes and colors", ImGuiTreeNodeFlags_NoAutoOpenOnLog)) {
                    changed |= FeatureCheckbox("Use team colors", settings.teamColors);
                    if (settings.teamColors) {
                        changed |= color("T color", "##TColor", settings.tColor);
                        changed |= color("CT color", "##CTColor", settings.ctColor);
                    } else {
                        changed |= color("Box color", "##BoxColor", settings.boxColor);
                    }
                    ImGui::TextUnformatted("Box style");
                    ImGui::SetNextItemWidth(-1);
                    changed |= ImGui::Combo("##BoxStyle", &settings.boxStyle, "Full rectangle\0Corners\0");
                    changed |= slider("Box thickness", "##BoxThickness", settings.boxThickness, 0.5f, 5, "%.1f px");
                    changed |= FeatureCheckbox("Box outline", settings.boxOutline);
                    changed |= FeatureCheckbox("Box fill", settings.boxFill);
                    ImGui::BeginDisabled(!settings.boxFill);
                    changed |= color("Fill color", "##FillColor", settings.fillColor);
                    ImGui::EndDisabled();
                }
                if (ImGui::CollapsingHeader("Names and text", ImGuiTreeNodeFlags_NoAutoOpenOnLog)) {
                    ImGui::TextUnformatted("Name font");
                    constexpr const char* fonts[] = { "Menu font", "Segoe UI Bold", "Tahoma", "Consolas" };
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::BeginCombo("##NameFont", fonts[std::clamp(settings.nameFont, 0, 3)])) {
                        for (int index = 0; index < 4; ++index) {
                            const bool fontAvailable = EspFontAvailable(index);
                            ImGui::BeginDisabled(!fontAvailable);
                            if (ImGui::Selectable(fonts[index], settings.nameFont == index)) {
                                settings.nameFont = index;
                                changed = true;
                            }
                            ImGui::EndDisabled();
                            if (!fontAvailable) ImGui::TextDisabled("Unavailable on this computer");
                        }
                        ImGui::EndCombo();
                    }
                    if (!EspFontAvailable(settings.nameFont)) ImGui::TextWrapped("Selected font unavailable. Using menu font.");
                    changed |= slider("Name size", "##NameSize", settings.nameSize, 10, 28, "%.0f px");
                    changed |= FeatureCheckbox("Names use box color", settings.nameUseBoxColor);
                    ImGui::BeginDisabled(settings.nameUseBoxColor);
                    changed |= color("Name color", "##NameColor", settings.nameColor);
                    ImGui::EndDisabled();
                    changed |= slider("Detail size", "##DetailSize", settings.detailSize, 9, 22, "%.0f px");
                    changed |= color("Detail color", "##DetailColor", settings.detailColor);
                    changed |= FeatureCheckbox("Text outlines", settings.textOutline);
                }
                if (ImGui::CollapsingHeader("Health bar", ImGuiTreeNodeFlags_NoAutoOpenOnLog)) {
                    changed |= color("Health color", "##HealthColor", settings.healthColor);
                    changed |= FeatureCheckbox("Health gradient", settings.healthGradient);
                    ImGui::BeginDisabled(!settings.healthGradient);
                    changed |= color("Low health color", "##HealthLowColor", settings.healthLowColor);
                    ImGui::EndDisabled();
                    changed |= slider("Health bar width", "##HealthBarWidth", settings.healthBarWidth, 1, 8, "%.1f px");
                    changed |= FeatureCheckbox("Show health value", settings.healthText);
                }
                if (ImGui::CollapsingHeader("Outlines", ImGuiTreeNodeFlags_NoAutoOpenOnLog)) {
                    ImGui::BeginDisabled(!settings.boxOutline && !settings.textOutline);
                    changed |= color("Outline color", "##OutlineColor", settings.outlineColor);
                    ImGui::EndDisabled();
                    ImGui::TextWrapped("Shared by box and text outlines.");
                }
                ImGui::EndDisabled();
                // Publish edits before the sample so it responds in the same frame.
                if (changed) EspSetSettings(settings);
                if (ImGui::CollapsingHeader("Live preview", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_NoAutoOpenOnLog)) {
                    ImGui::TextWrapped("Sample players, independent of game data.");
                    EspDrawPreview(std::max(1.0f, ImGui::GetContentRegionAvail().x), 170);
                }
                reset = ImGui::Button("Reset ESP appearance", { -1, 0 });
                ImGui::Separator();
                if (ImGui::CollapsingHeader("Player chams", ImGuiTreeNodeFlags_NoAutoOpenOnLog)) {
                    auto chams = PlayerChamsGetSettings();
                    bool chamsChanged = FeatureCheckbox("Enable player chams", chams.enabled);
                    ImGui::BeginDisabled(!chams.enabled);
                    ImGui::TextUnformatted("Material");
                    int style = static_cast<int>(chams.style);
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("##PlayerChamsStyle", &style, "Flat\0Iridescent\0Fresnel\0")) {
                        chams.style = static_cast<PlayerChamsStyle>(style);
                        chamsChanged = true;
                    }
                    if (chams.style == PlayerChamsStyle::Fresnel)
                        chamsChanged |= color("Fresnel color", "##PlayerChamsFresnelColor", chams.fresnelColor);
                    else if (chams.style == PlayerChamsStyle::Iridescent)
                        chamsChanged |= color("Iridescent color", "##PlayerChamsIridescentColor", chams.iridescentColor);
                    else
                        chamsChanged |= color("Flat color", "##PlayerChamsColor", chams.color);
                    chamsChanged |= FeatureCheckbox("Chams on teammates", chams.teammates);
                    chamsChanged |= FeatureCheckbox("Chams on local player", chams.localPlayer);
                    chamsChanged |= FeatureCheckbox("Through walls", chams.throughWalls);
                    ImGui::EndDisabled();
                    if (chamsChanged) PlayerChamsSetSettings(chams);
                    if (ImGui::Button("Reset player chams", { -1, 0 })) PlayerChamsResetSettings();
                }
                if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_NoAutoOpenOnLog)) {
                    ThirdPersonSettings camera = ThirdPersonGetSettings();
                    bool cameraChanged = FeatureCheckbox("Enable third person", camera.enabled);
                    ImGui::TextDisabled("F6  Toggle third person");
                    ImGui::BeginDisabled(!camera.enabled);
                    cameraChanged |= slider("Camera distance", "##CameraDistance", camera.distance, 30, 300, "%.0f units");
                    cameraChanged |= slider("Shoulder offset", "##CameraShoulder", camera.shoulder, -60, 60, "%.0f units");
                    cameraChanged |= slider("Height offset", "##CameraHeight", camera.height, -60, 60, "%.0f units");
                    cameraChanged |= FeatureCheckbox("Camera collision", camera.collision);
                    ImGui::EndDisabled();
                    if (cameraChanged) ThirdPersonSetSettings(camera);
                    if (ImGui::Button("Reset camera", { -1, 0 })) ThirdPersonResetSettings();
                }
                if (ImGui::CollapsingHeader("Scoped zoom", ImGuiTreeNodeFlags_NoAutoOpenOnLog)) {
                    ZoomSettings zoom = ZoomGetSettings();
                    bool zoomChanged = FeatureCheckbox("Override scoped zoom", zoom.enabled);
                    ImGui::BeginDisabled(!zoom.enabled);
                    zoomChanged |= slider("Zoom 1", "##Zoom1", zoom.zoom1, 0, 200, "%.0f%%");
                    zoomChanged |= slider("Zoom 2", "##Zoom2", zoom.zoom2, 0, 200, "%.0f%%");
                    ImGui::TextWrapped("0%%: no zoom. 100%%: weapon default. 200%%: stronger zoom.");
                    zoomChanged |= slider("Zoom sensitivity", "##ZoomSensitivity", zoom.sensitivity, 0, 100, "%.0f%%");
                    ImGui::EndDisabled();
                    ImGui::TextWrapped("Zoom sensitivity: 0%% uses unscoped sensitivity, 100%% uses CS2 native scoped sensitivity. Values between blend the two.");
                    zoomChanged |= FeatureCheckbox("Remove scope overlay", zoom.removeScope);
                    ImGui::TextWrapped("Replaces the first-person scope overlay with the custom crosshair. Independent of zoom override.");
                    if (zoomChanged) ZoomSetSettings(zoom);
                    if (ImGui::Button("Reset scoped zoom", { -1, 0 })) ZoomResetSettings();
                }
                DrawPlayerModel();
                if (ImGui::CollapsingHeader("Grenades", ImGuiTreeNodeFlags_NoAutoOpenOnLog)) {
                    auto grenades = GrenadeGetSettings();
                    bool grenadesChanged = FeatureCheckbox("Remove smoke", grenades.removeSmoke);
                    grenadesChanged |= FeatureCheckbox("Remove flash", grenades.removeFlash);
                    ImGui::Separator();
                    grenadesChanged |= FeatureCheckbox("Smoke border aura", grenades.smokeAura);
                    ImGui::BeginDisabled(!grenades.smokeAura);
                    grenadesChanged |= color("Smoke aura color", "##SmokeAuraColor", grenades.smokeColor);
                    ImGui::EndDisabled();
                    grenadesChanged |= FeatureCheckbox("Molotov border aura", grenades.molotovAura);
                    ImGui::BeginDisabled(!grenades.molotovAura);
                    grenadesChanged |= color("Molotov aura color", "##MolotovAuraColor", grenades.molotovColor);
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(!grenades.smokeAura && !grenades.molotovAura);
                    grenadesChanged |= slider("Aura width", "##GrenadeAuraWidth", grenades.auraWidth, 1, 6, "%.1f px");
                    ImGui::EndDisabled();
                    ImGui::TextWrapped("Borders appear after smoke deploys or a molotov ignites.");
                    if (grenadesChanged) GrenadeSetSettings(grenades);
                    if (ImGui::Button("Reset grenades", { -1, 0 })) GrenadeResetSettings();
                }
            }
            ImGui::EndChild();
            if (reset) EspResetStyle();
        } else if (selectedTab == 1) {
            MovementSettings settings = MovementGetSettings();
            bool changed = false;
            if (ImGui::BeginChild("##MovementSettings", { 0, bodyHeight }, ImGuiChildFlags_Borders)) {
                changed |= FeatureCheckbox("Auto bunnyhop (hold Space)", settings.bunnyhop);
                changed |= FeatureCheckbox("Aggressive autostrafe (hold Space)", settings.autostrafe);
                changed |= FeatureCheckbox("Jumpbug (hold Space)", settings.jumpbug);
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
        } else if (selectedTab == 4) {
            auto settings = SilentAimGetSettings();
            auto visualization = AimVisualizationGetSettings();
            bool visualizationChanged = false;
            auto antiAim = AntiAimGetSettings();
            auto shotLog = ShotLogGetSettings();
            bool changed = false;
            bool antiAimChanged = false;
            bool shotLogChanged = false;
            if (ImGui::BeginChild("##AimSettings", { 0, bodyHeight }, ImGuiChildFlags_Borders)) {
                changed |= FeatureCheckbox("Enable silent aim", settings.enabled);
                visualizationChanged |= FeatureCheckbox("Visualize ragebot", visualization.enabled);
                ImGui::BeginDisabled(!visualization.enabled);
                ImGui::TextUnformatted("Point color");
                ImGui::SetNextItemWidth(-1);
                visualizationChanged |= ImGui::ColorEdit4("##AimPointColor", visualization.color,
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
                ImGui::EndDisabled();
                ImGui::TextWrapped("Shows hitbox centers with a valid damage path, including below minimum damage, while moving or waiting to shoot.");
                ImGui::BeginDisabled(!settings.enabled);
                ImGui::TextUnformatted("Hitgroups");
                constexpr const char* hitgroups[]{"Head", "Chest", "Stomach", "Pelvis", "Arms", "Hands", "Legs", "Feet"};
                if (ImGui::BeginTable("##AimHitgroups", 2, ImGuiTableFlags_SizingStretchSame)) {
                    for (unsigned index = 0; index < std::size(hitgroups); ++index) {
                        ImGui::TableNextColumn();
                        const auto bit = 1u << index;
                        bool enabled = (settings.hitgroups & bit) != 0;
                        if (ImGui::Checkbox(hitgroups[index], &enabled)) {
                            settings.hitgroups = enabled ? settings.hitgroups | bit : settings.hitgroups & ~bit;
                            changed = true;
                        }
                    }
                    ImGui::EndTable();
                }
                if (!settings.hitgroups) ImGui::TextDisabled("Select at least one hitgroup to acquire a target.");
                ImGui::TextUnformatted("Aim FOV");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::SliderFloat("##AimFov", &settings.fov, 0.1f, 180, "%.1f degrees");
                changed |= FeatureCheckbox("Autofire", settings.autofire);
                changed |= FeatureCheckbox("Autowall", settings.autowall);
                ImGui::TextUnformatted("Minimum hitchance");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::SliderFloat("##AimHitchance", &settings.hitchance, 0, 100, "%.0f%%");
                ImGui::TextUnformatted("Spread samples");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::SliderInt("##AimSamples", &settings.samples, 64, 1024);
                ImGui::BeginDisabled(!settings.autofire);
                ImGui::TextUnformatted("Minimum damage");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::SliderInt("##AimMinDamage", &settings.minDamage, 1, 100, "%d HP");
                changed |= FeatureCheckbox("Quickstop", settings.quickstop);
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                ImGui::Separator();
                ImGui::TextWrapped("Quickstop brakes ready shots below minimum hitchance and releases movement during weapon cooldown.");
                ImGui::TextWrapped("Autofire requires a reachable target, minimum hitchance and minimum predicted damage. Manual fire remains available.");
                ImGui::TextWrapped("Autowall predicts damage through penetrable cover using the weapon and surface properties, then applies hitgroup and armor.");
                ImGui::TextWrapped("Selected hitgroups compete using damage and hitchance after the configured minimums pass.");
                ImGui::TextWrapped("With Autowall off, cover blocks autofire. Hitchance estimates spread; cover and server timing can still prevent a hit.");
                ImGui::TextDisabled("F10  Silent aim");
                ImGui::Separator();
                shotLogChanged |= FeatureCheckbox("Shot logs", shotLog.enabled);
                shotLogChanged |= FeatureCheckbox("Verbose aim diagnostics", shotLog.verbose);
                ImGui::TextWrapped("Shot results appear in the console and session log. Detailed diagnostics always stay in the file.");
                bool onlyOwn = ConsoleControlOnlyOwn();
                ImGui::BeginDisabled(!ConsoleControlAvailable());
                if (FeatureCheckbox("Our logs only", onlyOwn)) ConsoleControlSetOnlyOwn(onlyOwn);
                if (ImGui::Button("Clear console", { -1, 0 })) ConsoleControlRequestClear();
                ImGui::EndDisabled();
                ImGui::TextWrapped("%s", ConsoleControlStatus());
                ImGui::Separator();
                antiAimChanged = DrawAntiAimSettings(antiAim);
            }
            ImGui::EndChild();
            if (changed) SilentAimSetSettings(settings);
            if (visualizationChanged) AimVisualizationSetSettings(visualization);
            if (antiAimChanged) AntiAimSetSettings(antiAim);
            if (shotLogChanged) ShotLogSetSettings(shotLog);
        } else if (selectedTab == 5) {
            if (ImGui::BeginChild("##ConfigSettings", { 0, bodyHeight }, ImGuiChildFlags_Borders))
                DrawConfigs(lastTab != 5);
            ImGui::EndChild();
        } else {
            WorldSettings settings = WorldGetSettings();
            bool changed = false;
            bool reset = false;
            if (ImGui::BeginChild("##LightingSettings", { 0, bodyHeight }, ImGuiChildFlags_Borders)) {
                changed |= FeatureCheckbox("Override lighting", settings.lightingEnabled);
                ImGui::Separator();
                ImGui::BeginDisabled(!settings.lightingEnabled);
                ImGui::TextUnformatted("Ambient fill");
                changed |= FeatureCheckbox("Add ambient light", settings.ambientLightEnabled);
                ImGui::BeginDisabled(!settings.ambientLightEnabled);
                ImGui::TextUnformatted("Ambient color");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::ColorEdit3("##AmbientLightTint", settings.ambientLightTint, ImGuiColorEditFlags_NoInputs);
                ImGui::TextUnformatted("Ambient intensity");
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::SliderFloat("##AmbientLightBrightness", &settings.ambientLightBrightness, 0, 4, "%.2f");
                ImGui::EndDisabled();
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("Adds diffuse light to surfaces. Baked map lighting remains.");
                ImGui::PopStyleColor();
                ImGui::Separator();
                ImGui::TextUnformatted("Local lights");
                ImGui::TextDisabled("Point, spot, orthographic and area lights");
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
