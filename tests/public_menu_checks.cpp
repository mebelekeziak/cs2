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
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <limits>
#include <chrono>
#include <fstream>
#include <cstdint>

namespace {
int failures = 0;
void Expect(bool condition, const char* label) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", label);
    failures += !condition;
}
void Frame(bool locateClippedItem = false) {
    ImGui::NewFrame();
    if (locateClippedItem) ImGui::LogToBuffer();
    DrawPublicMenu();
    if (locateClippedItem) ImGui::LogFinish();
    ImGui::Render();
}
void Move(ImVec2 position) {
    ImGui::GetIO().AddMousePosEvent(position.x, position.y);
    Frame();
}
void Click(ImVec2 position) {
    Move(position);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    Frame();
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    Frame();
}
ImGuiWindow* Menu() { return ImGui::FindWindowByName("##PublicMenu"); }
ImGuiWindow* Body() {
    ImGuiWindow* menu = Menu();
    return menu && !menu->DC.ChildWindows.empty() ? menu->DC.ChildWindows[0] : nullptr;
}
ImVec2 EnablePosition() {
    const ImVec2 start = Body()->DC.CursorStartPos;
    return { start.x + 8, start.y + ImGui::GetFrameHeight() * 0.5f };
}
ImVec2 OptionPosition(int row) {
    // Rows follow the master control and a one-pixel separator. Use the actual
    // child origin and style metrics so movement/viewport changes remain covered.
    ImVec2 position = EnablePosition();
    position.y += ImGui::GetFrameHeightWithSpacing() + 1 + ImGui::GetStyle().ItemSpacing.y;
    position.y += static_cast<float>(row) * ImGui::GetFrameHeightWithSpacing();
    return position;
}
ImVec2 TabPosition(int index);
ImVec2 MovementPosition(int row) {
    ImVec2 position = EnablePosition();
    position.y += static_cast<float>(row) * ImGui::GetFrameHeightWithSpacing();
    return position;
}
ImRect ControlRect(const char* label, ImGuiWindow* window = nullptr, ImGuiID itemId = 0) {
    // Ask the pinned ImGui build to outline the submitted item's real bounds.
    // Reading that diagnostic outline avoids copying the menu's layout math.
    if (!window) window = Body();
    ImGui::DebugLocateItem(itemId ? itemId : window->GetID(label));
    Frame(true); // Logging includes clipped submissions without activating them.
    ImDrawList* outline = ImGui::GetForegroundDrawList();
    const bool found = ImGui::GetCurrentContext()->DebugLocateId == 0 && outline->IdxBuffer.Size >= 12;
    Expect(found, label);
    if (!found) return {};
    ImRect bounds({ FLT_MAX, FLT_MAX }, { -FLT_MAX, -FLT_MAX });
    // The first two rectangle edges contain both opposing corners. The later
    // diagnostic cursor line is deliberately excluded.
    for (int index = 0; index < 12; ++index)
        bounds.Add(outline->VtxBuffer[outline->IdxBuffer[index]].pos);
    return bounds;
}
ImVec2 TabPosition(int index) {
    constexpr const char* labels[]{"Visuals", "Movement", "World", "Lighting", "Aim", "Configs"};
    return ControlRect(labels[index], Menu()).GetCenter();
}
void ClickControl(const char* label, float fraction = 0.5f, ImGuiWindow* window = nullptr, ImGuiID itemId = 0) {
    if (!window) window = Body();
    ImRect rect = ControlRect(label, window, itemId);
    for (int attempt = 0; attempt < 4; ++attempt) {
        const float top = window->InnerRect.Min.y + 4;
        const float bottom = window->InnerRect.Max.y - 4;
        const float center = rect.GetCenter().y;
        if (center >= top && center <= bottom) break;
        ImGui::SetScrollY(window, window->Scroll.y + center - (top + bottom) * 0.5f);
        Frame(); Frame();
        rect = ControlRect(label, window, itemId);
    }
    Click({ rect.Min.x + rect.GetWidth() * fraction, rect.GetCenter().y });
}
void ClickHitgroup(const char* label) {
    const auto table = Body()->GetID("##AimHitgroups");
    ClickControl(label, 0.5f, Body(), ImHashStr(label, 0, table));
}
void SelectDirectionalMode(const char* label) {
    ClickControl("##DirectionalLightmaps");
    ImGuiContext& context = *ImGui::GetCurrentContext();
    const bool opened = !context.OpenPopupStack.empty() && context.OpenPopupStack.back().Window;
    Expect(opened, "directional lightmap menu opens through mouse input");
    if (opened) {
        ImGuiWindow* popup = context.OpenPopupStack.back().Window;
        const int index = std::strcmp(label, "Game value") == 0 ? 0 : std::strcmp(label, "Enabled") == 0 ? 1 : 2;
        // ImGui's Combo helper scopes selectable IDs by their list index.
        ClickControl(label, 0.5f, popup, ImHashStr(label, 0, popup->GetID(index)));
    }
}
void SelectEspCombo(const char* control, const char* label, int index = -1) {
    ClickControl(control);
    Frame(); Frame(); // Settle popup placement and clipping after it first opens.
    ImGuiContext& context = *ImGui::GetCurrentContext();
    const bool opened = !context.OpenPopupStack.empty() && context.OpenPopupStack.back().Window;
    Expect(opened, "ESP style combo opens through mouse input");
    if (!opened) return;
    ImGuiWindow* popup = context.OpenPopupStack.back().Window;
    const ImGuiID item = index < 0 ? popup->GetID(label) : ImHashStr(label, 0, popup->GetID(index));
    ClickControl(label, 0.5f, popup, item);
}
void EditBoxColor() {
    const ImGuiID swatch = ImHashStr("##ColorButton", 0, Body()->GetID("##BoxColor"));
    ClickControl("box color swatch", 0.5f, Body(), swatch);
    ImGuiContext& context = *ImGui::GetCurrentContext();
    const bool opened = !context.OpenPopupStack.empty() && context.OpenPopupStack.back().Window;
    Expect(opened, "RGBA picker opens from the box color swatch");
    if (!opened) return;
    ImGuiWindow* popup = context.OpenPopupStack.back().Window;
    const ImGuiID picker = popup->GetID("##picker");
    const ImRect saturation = ControlRect("box saturation/value", popup, ImHashStr("sv", 0, picker));
    Click({ saturation.Min.x + saturation.GetWidth() * 0.7f,
            saturation.Min.y + saturation.GetHeight() * 0.3f });
    const ImRect alpha = ControlRect("box alpha", popup, ImHashStr("alpha", 0, picker));
    Click({ alpha.GetCenter().x, alpha.Min.y + alpha.GetHeight() * 0.75f });
    Expect(EspGetSettings().boxColor[0] != EspSettings{}.boxColor[0] &&
           EspGetSettings().boxColor[3] > 0.15f && EspGetSettings().boxColor[3] < 0.35f,
           "mouse-driven RGBA picker publishes box RGB and transparency");
    Click({ Menu()->Pos.x + 5, Menu()->Pos.y + 5 });
    Expect(context.OpenPopupStack.empty(), "outside mouse click closes the color picker");
}
void EspAppearanceControls() {
    ClickControl("Boxes and colors");
    ClickControl("Use team colors");
    Expect(!EspGetSettings().teamColors, "single box color can replace team colors");
    EditBoxColor();
    SelectEspCombo("##BoxStyle", "Corners", 1);
    ClickControl("##BoxThickness", 0.7f);
    ClickControl("Box fill");
    Expect(EspGetSettings().boxStyle == 1 && EspGetSettings().boxThickness > 2 && EspGetSettings().boxFill,
           "corner style, line thickness and fill publish independently of feature toggles");
    ClickControl("Boxes and colors");
    ClickControl("Names and text");
    const int font = EspFontAvailable(3) ? 3 : 0;
    SelectEspCombo("##NameFont", font == 3 ? "Consolas" : "Menu font");
    ClickControl("##NameSize", 0.8f);
    ClickControl("Names use box color");
    ClickControl("##DetailSize", 0.6f);
    ClickControl("Text outlines");
    Expect(EspGetSettings().nameFont == font && EspGetSettings().nameSize > 20 &&
           !EspGetSettings().nameUseBoxColor && EspGetSettings().detailSize > 14 && !EspGetSettings().textOutline,
           "font, name size and color source, detail size and text outlines are configurable");
    ClickControl("Names and text");
    ClickControl("Health bar");
    ClickControl("Health gradient");
    ClickControl("##HealthBarWidth", 0.6f);
    ClickControl("Show health value");
    Expect(EspGetSettings().healthGradient && EspGetSettings().healthBarWidth > 4 && !EspGetSettings().healthText,
           "health gradient, width and numeric value have separate controls");
    ClickControl("Enable ESP");
    ClickControl("Health gradient");
    ClickControl("##HealthBarWidth", 0.1f);
    Expect(!EspGetSettings().enabled && EspGetSettings().healthGradient && EspGetSettings().healthBarWidth > 4,
           "disabled ESP master blocks appearance edits");
    ClickControl("Reset ESP appearance");
    const EspSettings reset = EspGetSettings();
    const EspSettings defaults;
    Expect(!reset.enabled && !reset.boxes && reset.armor && reset.names && reset.health && reset.distance && reset.teammates &&
           reset.teamColors && reset.boxStyle == 0 && reset.boxThickness == defaults.boxThickness && !reset.boxFill &&
           reset.boxColor[3] == 1 && reset.nameFont == 0 && reset.nameSize == defaults.nameSize && reset.nameUseBoxColor &&
           reset.detailSize == defaults.detailSize && reset.textOutline && !reset.healthGradient &&
           reset.healthBarWidth == defaults.healthBarWidth && reset.healthText,
           "appearance reset works while disabled and preserves every feature toggle");
    Expect(MovementGetSettings().bunnyhop && MovementGetSettings().autostrafe && !WorldGetSettings().enabled,
           "ESP customization and reset preserve movement and world settings");
    ClickControl("Enable ESP");
    ClickControl("Health bar");
    ImGui::SetScrollY(Body(), 0);
    Frame(); Frame();
}
void NativeSettingsBounds() {
    WorldSettings settings;
    settings.localLightBrightness = -1;
    settings.sunLightBrightness = 100;
    settings.localLightTint[0] = -1;
    settings.localLightTint[1] = 2;
    settings.sunLightTint[0] = std::numeric_limits<float>::quiet_NaN();
    settings.sunLightTint[1] = std::numeric_limits<float>::infinity();
    settings.directionalLightmapsMode = -9;
    const auto bounded = WorldSanitizeSettings(settings);
    Expect(bounded.localLightBrightness == 0 && bounded.sunLightBrightness == 8 &&
           bounded.localLightTint[0] == 0 && bounded.localLightTint[1] == 1 &&
           bounded.sunLightTint[0] == 1 && bounded.sunLightTint[1] == 1 && bounded.directionalLightmapsMode == 0,
           "native lighting settings reject nonfinite values and enforce supported bounds");
    settings.localLightBrightness = std::numeric_limits<float>::infinity();
    settings.sunLightBrightness = std::numeric_limits<float>::quiet_NaN();
    settings.directionalLightmapsMode = 9;
    const auto fallback = WorldSanitizeSettings(settings);
    Expect(fallback.localLightBrightness == 1 && fallback.sunLightBrightness == 1 && fallback.directionalLightmapsMode == 2,
           "invalid brightness uses neutral values and directional mode stays in range");
}
std::string SubmittedMenuText() {
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    DrawPublicMenu();
    const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
    ImGui::LogFinish();
    ImGui::Render();
    return text;
}
void AntiAimControls() {
    const auto savedAim = SilentAimGetSettings();
    const auto savedMovement = MovementGetSettings();
    ClickControl("Enable anti aim");
    ClickControl("##AntiAimYawOffset", 0.2f);
    Expect(AntiAimGetSettings() == AntiAimSettings{}, "disabled anti aim master blocks angle edits");
    ClickControl("Enable anti aim");
    const auto staticText = SubmittedMenuText();
    Expect(staticText.find("Yaw mode") != std::string::npos && staticText.find("Yaw offset") != std::string::npos &&
        staticText.find("Yaw range (total width)") == std::string::npos && staticText.find("Spin speed") == std::string::npos &&
        staticText.find("Custom pitch") == std::string::npos && staticText.find("Change interval") == std::string::npos,
        "static yaw and fixed pitch show only relevant controls");
    SelectEspCombo("##AntiAimYawBase", "World absolute", 1);
    ClickControl("##AntiAimYawOffset", 0.25f);
    SelectEspCombo("##AntiAimYawMode", "Jitter", 1);
    ClickControl("##AntiAimJitterRange", 0.6f);
    ClickControl("##AntiAimInterval", 0.3f);
    const auto jitter = AntiAimGetSettings();
    Expect(jitter.yawMode == AntiAimYawMode::Jitter && jitter.yawBase == AntiAimYawBase::World &&
        jitter.yawOffset < -70 && jitter.jitterRange > 180 && jitter.jitterInterval > 10,
        "yaw base, offset, jitter width and command interval publish through mouse input");
    SelectEspCombo("##AntiAimYawMode", "Random", 3);
    Expect(AntiAimGetSettings().yawMode == AntiAimYawMode::Random &&
        SubmittedMenuText().find("Yaw range (total width)") != std::string::npos,
        "random yaw exposes its range and uses preserved jitter interval");
    SelectEspCombo("##AntiAimYawMode", "Spin", 2);
    ClickControl("##AntiAimSpinSpeed", 0.25f);
    const auto spinText = SubmittedMenuText();
    Expect(AntiAimGetSettings().yawMode == AntiAimYawMode::Spin && AntiAimGetSettings().spinSpeed < -70 &&
        spinText.find("Spin speed") != std::string::npos && spinText.find("Yaw range (total width)") == std::string::npos,
        "spin mode exposes signed degrees per command and hides jitter width");
    for (const auto mode : {AntiAimPitchMode::Down, AntiAimPitchMode::Up, AntiAimPitchMode::Level,
                           AntiAimPitchMode::Keep, AntiAimPitchMode::Custom, AntiAimPitchMode::Jitter}) {
        constexpr const char* labels[]{"Down", "Up", "Level", "Keep view pitch", "Custom", "Jitter"};
        SelectEspCombo("##AntiAimPitchMode", labels[static_cast<int>(mode)], static_cast<int>(mode));
        Expect(AntiAimGetSettings().pitchMode == mode, "pitch selector publishes each supported mode");
        const auto text = SubmittedMenuText();
        Expect((text.find("Custom pitch") != std::string::npos) == (mode == AntiAimPitchMode::Custom) &&
            (text.find("Pitch B") != std::string::npos) == (mode == AntiAimPitchMode::Jitter),
            "custom and jitter pitch sliders follow the selected mode");
        if (mode == AntiAimPitchMode::Custom) {
            ClickControl("##AntiAimPitch", 0.25f);
            Expect(AntiAimGetSettings().pitch < -30, "custom pitch slider publishes upward pitch");
        }
    }
    ClickControl("##AntiAimPitch", 0.75f);
    ClickControl("##AntiAimPitchAlternate", 0.25f);
    ClickControl("##AntiAimInterval", 0.5f);
    const auto edited = AntiAimGetSettings();
    Expect(edited.pitch > 30 && edited.pitchAlternate < -30 && edited.jitterInterval > 25,
        "pitch jitter independently edits both angles and exposes shared interval during spin");
    ClickControl("Enable anti aim");
    ClickControl("Reset anti aim");
    Expect(AntiAimGetSettings() == AntiAimSettings{}, "anti aim reset works while disabled and restores every control");
    Expect(SilentAimGetSettings() == savedAim && MovementGetSettings().bunnyhop == savedMovement.bunnyhop &&
        MovementGetSettings().autostrafe == savedMovement.autostrafe,
        "anti aim edits and reset preserve independent aim and movement settings");
    AntiAimSetSettings(edited); // Keep non-default fields for the Configs mouse flow.
}
void CameraControls() {
    const EspSettings savedEsp = EspGetSettings();
    ClickControl("Enable ESP");
    Expect(!EspGetSettings().enabled, "camera test starts with ESP disabled");
    ClickControl("Camera");
    Expect(ThirdPersonGetSettings() == ThirdPersonSettings{}, "camera opens at safe defaults");
    ClickControl("##CameraDistance", 0.8f);
    ClickControl("##CameraShoulder", 0.8f);
    ClickControl("##CameraHeight", 0.8f);
    ClickControl("Camera collision");
    Expect(ThirdPersonGetSettings() == ThirdPersonSettings{}, "disabled camera master blocks sliders and collision checkbox");
    ClickControl("Enable third person");
    ClickControl("##CameraDistance", 0.7f);
    ClickControl("##CameraShoulder", 0.2f);
    ClickControl("##CameraHeight", 0.8f);
    ClickControl("Camera collision");
    const auto edited = ThirdPersonGetSettings();
    Expect(edited.enabled && edited.distance > 180 && edited.shoulder < -20 && edited.height > 20 && !edited.collision,
           "camera master, distance, shoulder, height and collision publish through mouse input while ESP is disabled");
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    DrawPublicMenu();
    const char* labels = ImGui::GetCurrentContext()->LogBuffer.c_str();
    Expect(std::strstr(labels, "F6  Toggle third person"), "camera presents its runtime toggle key");
    ImGui::LogFinish();
    ImGui::Render();
    ClickControl("Enable third person");
    ClickControl("Reset camera");
    Expect(ThirdPersonGetSettings() == ThirdPersonSettings{}, "camera reset works with its master disabled");
    ClickControl("Camera");
    ClickControl("Enable ESP");
    Expect(EspGetSettings() == savedEsp && MovementGetSettings().bunnyhop && MovementGetSettings().autostrafe &&
           !WorldGetSettings().enabled, "camera edits preserve independent visuals, movement and world controls");
    // Leave non-default camera values so the Configs mouse-flow checks persistence.
    ThirdPersonSetSettings(edited);
    ImGui::SetScrollY(Body(), 0);
    Frame(); Frame();
}
void EditChamsColor(const char* control, bool iridescent) {
    const auto before = PlayerChamsGetSettings();
    const ImGuiID swatch = ImHashStr("##ColorButton", 0, Body()->GetID(control));
    ClickControl("player chams color swatch", 0.5f, Body(), swatch);
    ImGuiContext& context = *ImGui::GetCurrentContext();
    const bool opened = !context.OpenPopupStack.empty() && context.OpenPopupStack.back().Window;
    Expect(opened, "player chams RGBA swatch opens a color picker");
    if (!opened) return;
    ImGuiWindow* popup = context.OpenPopupStack.back().Window;
    const ImGuiID picker = popup->GetID("##picker");
    const auto saturation = ControlRect("chams saturation/value", popup, ImHashStr("sv", 0, picker));
    Click({ saturation.Min.x + saturation.GetWidth() * 0.7f,
            saturation.Min.y + saturation.GetHeight() * 0.3f });
    const auto alpha = ControlRect("chams alpha", popup, ImHashStr("alpha", 0, picker));
    Click({ alpha.GetCenter().x, alpha.Min.y + alpha.GetHeight() * 0.75f });
    const auto edited = PlayerChamsGetSettings();
    const float* value = iridescent ? edited.iridescentColor : edited.color;
    const float* oldValue = iridescent ? before.iridescentColor : before.color;
    bool rgbChanged = false;
    bool otherColorUnchanged = true;
    for (int channel = 0; channel < 4; ++channel) {
        if (channel < 3) rgbChanged |= value[channel] != oldValue[channel];
        otherColorUnchanged &= iridescent ? edited.color[channel] == before.color[channel]
                                         : edited.iridescentColor[channel] == before.iridescentColor[channel];
    }
    Expect(rgbChanged && value[3] > 0.15f && value[3] < 0.35f && otherColorUnchanged,
        "player chams RGBA picker publishes color and opacity without changing the other material color");
    Click({ Menu()->Pos.x + 5, Menu()->Pos.y + 5 });
}
void PlayerChamsControls() {
    const auto savedEsp = EspGetSettings();
    const auto savedCamera = ThirdPersonGetSettings();
    const auto savedModel = ModelSwapGetSettings();
    auto disabledEsp = savedEsp;
    disabledEsp.enabled = false;
    EspSetSettings(disabledEsp);
    ClickControl("Player chams");
    Expect(PlayerChamsGetSettings() == PlayerChamsSettings{} && PlayerChamsGetSettings().throughWalls,
        "player chams open disabled with flat material and through-walls rendering selected");
    ClickControl("##PlayerChamsStyle");
    ClickControl("Chams on teammates");
    ClickControl("Chams on local player");
    ClickControl("Through walls");
    Expect(PlayerChamsGetSettings() == PlayerChamsSettings{} && ImGui::GetCurrentContext()->OpenPopupStack.empty(),
        "disabled player chams master blocks material, target and through-walls controls");
    ClickControl("Enable player chams");
    Expect(PlayerChamsGetSettings().enabled && !EspGetSettings().enabled,
        "player chams enable independently while ESP is disabled");
    const auto flatText = SubmittedMenuText();
    Expect(flatText.find("Flat color") != std::string::npos && flatText.find("Iridescent color") == std::string::npos,
        "flat chams expose only the selected material's flat RGBA color");
    EditChamsColor("##PlayerChamsColor", false);
    SelectEspCombo("##PlayerChamsStyle", "Iridescent", 1);
    const auto iridescentText = SubmittedMenuText();
    Expect(PlayerChamsGetSettings().style == PlayerChamsStyle::Iridescent &&
        iridescentText.find("Iridescent color") != std::string::npos && iridescentText.find("Flat color") == std::string::npos,
        "iridescent material selection exposes only its own RGBA tint");
    EditChamsColor("##PlayerChamsIridescentColor", true);
    ClickControl("Chams on teammates");
    ClickControl("Chams on local player");
    const auto beforeWalls = PlayerChamsGetSettings();
    ClickControl("Through walls");
    auto expectedWithoutWalls = beforeWalls;
    expectedWithoutWalls.throughWalls = false;
    Expect(PlayerChamsGetSettings() == expectedWithoutWalls,
        "through-walls checkbox disables depth override independently of material, colors and targets");
    ClickControl("Through walls");
    Expect(PlayerChamsGetSettings() == beforeWalls, "through-walls checkbox re-enables depth override through mouse input");
    ClickControl("Through walls");
    const auto edited = PlayerChamsGetSettings();
    Expect(edited.teammates && edited.localPlayer && edited.enabled && !edited.throughWalls,
        "player chams teammate and local-player targets publish independently of through-walls selection");
    SelectEspCombo("##PlayerChamsStyle", "Flat", 0);
    const auto flat = PlayerChamsGetSettings();
    Expect(flat.style == PlayerChamsStyle::Flat && flat.color[3] == edited.color[3] &&
        flat.iridescentColor[3] == edited.iridescentColor[3] && flat.teammates && flat.localPlayer && !flat.throughWalls,
        "switching back to flat preserves both custom colors, targets and through-walls switch");
    ClickControl("Enable player chams");
    ClickControl("Reset player chams");
    Expect(PlayerChamsGetSettings() == PlayerChamsSettings{} && PlayerChamsGetSettings().throughWalls,
        "player chams reset works while disabled and restores material, target and enabled through-walls defaults");
    Expect(EspGetSettings() == disabledEsp && ThirdPersonGetSettings() == savedCamera && ModelSwapGetSettings() == savedModel,
        "player chams edits preserve ESP, camera and player-model settings");
    ClickControl("Player chams");
    PlayerChamsSetSettings(edited); // Exercise persistence through the mouse-driven config flow.
    EspSetSettings(savedEsp);
    ImGui::SetScrollY(Body(), 0);
    Frame(); Frame();
}
void GrenadeControls() {
    const auto savedEsp = EspGetSettings();
    auto disabledEsp = savedEsp;
    disabledEsp.enabled = false;
    EspSetSettings(disabledEsp);
    ClickControl("Grenades");
    Expect(GrenadeGetSettings() == GrenadeSettings{}, "grenade section starts with every effect disabled");
    ClickControl("##GrenadeAuraWidth", 0.9f);
    Expect(GrenadeGetSettings() == GrenadeSettings{}, "aura width stays disabled without an active aura");
    ClickControl("Remove smoke");
    Expect(GrenadeGetSettings().removeSmoke && !GrenadeGetSettings().removeFlash && !GrenadeGetSettings().smokeAura &&
        !GrenadeGetSettings().molotovAura && !EspGetSettings().enabled, "smoke removal toggles independently of ESP and auras");
    ClickControl("Remove flash");
    ClickControl("Smoke border aura");
    ClickControl("Molotov border aura");
    ClickControl("##GrenadeAuraWidth", 0.75f);
    const auto edited = GrenadeGetSettings();
    Expect(edited.removeSmoke && edited.removeFlash && edited.smokeAura && edited.molotovAura &&
        edited.auraWidth > 4 && edited.auraWidth < 6, "grenade removal and aura switches publish independently with editable width");
    const auto labels = SubmittedMenuText();
    Expect(labels.find("Smoke aura color") != std::string::npos && labels.find("Molotov aura color") != std::string::npos &&
        labels.find("Borders appear after smoke deploys or a molotov ignites.") != std::string::npos,
        "grenades expose separate aura colors and describe post-landing timing");
    ClickControl("Remove smoke");
    Expect(!GrenadeGetSettings().removeSmoke && GrenadeGetSettings().removeFlash && GrenadeGetSettings().smokeAura &&
        GrenadeGetSettings().molotovAura, "smoke aura remains enabled when smoke removal is disabled");
    ClickControl("Reset grenades");
    Expect(GrenadeGetSettings() == GrenadeSettings{} && EspGetSettings() == disabledEsp,
        "grenade reset restores defaults independently of disabled ESP");
    ClickControl("Grenades");
    GrenadeSetSettings(edited);
    EspSetSettings(savedEsp);
    ImGui::SetScrollY(Body(), 0);
    Frame(); Frame();
}
void ZoomControls() {
    const auto savedEsp = EspGetSettings();
    const auto savedCamera = ThirdPersonGetSettings();
    ClickControl("Enable ESP");
    ThirdPersonSetSettings({});
    ClickControl("Scoped zoom");
    Expect(!EspGetSettings().enabled && !ThirdPersonGetSettings().enabled && ZoomGetSettings() == ZoomSettings{},
           "scoped zoom opens at defaults independently of disabled ESP and camera");
    ClickControl("##Zoom1", 0.1f);
    ClickControl("##Zoom2", 0.9f);
    ClickControl("##ZoomSensitivity", 0.1f);
    Expect(ZoomGetSettings() == ZoomSettings{}, "disabled scoped zoom master blocks all three sliders");
    ClickControl("Remove scope overlay");
    Expect(ZoomGetSettings() == ZoomSettings{false, 100, 100, 100, true},
           "scope overlay removal works with zoom, ESP and third person disabled");
    ClickControl("Remove scope overlay");
    Expect(ZoomGetSettings() == ZoomSettings{}, "scope overlay removal can restore native overlay independently");
    ClickControl("Override scoped zoom");
    ClickControl("##Zoom1", 0.2f);
    const float firstLevel = ZoomGetSettings().zoom1;
    ClickControl("##Zoom2", 0.8f);
    Expect(ZoomGetSettings().enabled && firstLevel < 60 && ZoomGetSettings().zoom1 == firstLevel &&
           ZoomGetSettings().zoom2 > 140, "scoped zoom sliders independently publish first and second zoom levels");
    const auto zoomLevels = ZoomGetSettings();
    ClickControl("##ZoomSensitivity", 0.35f);
    Expect(ZoomGetSettings().sensitivity > 25 && ZoomGetSettings().sensitivity < 45 &&
           ZoomGetSettings().zoom1 == zoomLevels.zoom1 && ZoomGetSettings().zoom2 == zoomLevels.zoom2,
           "sensitivity slider publishes blended values without changing either zoom level");
    const auto dragToEndpoint = [](const char* control, bool maximum) {
        ClickControl(control); // Scroll the control fully into view first.
        const auto rectangle = ControlRect(control);
        Move(rectangle.GetCenter());
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        Frame();
        // The diagnostic outline extends beyond the slider's hit rectangle.
        // Drag from inside the slider to its edge to exercise endpoint input.
        Move({ maximum ? rectangle.Max.x + 8 : rectangle.Min.x - 8, rectangle.GetCenter().y });
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        Frame();
    };
    dragToEndpoint("##Zoom1", false);
    dragToEndpoint("##Zoom2", true);
    dragToEndpoint("##ZoomSensitivity", false);
    Expect(ZoomGetSettings() == ZoomSettings{true, 0, 200, 0},
           "scoped slider endpoints reach no zoom, strongest zoom and unscoped sensitivity");
    dragToEndpoint("##ZoomSensitivity", true);
    Expect(ZoomGetSettings() == ZoomSettings{true, 0, 200, 100},
           "sensitivity slider upper endpoint reaches native scoped sensitivity");
    dragToEndpoint("##ZoomSensitivity", false);
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    DrawPublicMenu();
    const char* labels = ImGui::GetCurrentContext()->LogBuffer.c_str();
    Expect(std::strstr(labels, "Zoom 1") && std::strstr(labels, "Zoom 2") &&
           std::strstr(labels, "0%: no zoom. 100%: weapon default. 200%: stronger zoom.") &&
           std::strstr(labels, "Zoom sensitivity") && std::strstr(labels, "0% uses unscoped sensitivity") &&
           std::strstr(labels, "100% uses CS2 native scoped sensitivity") && std::strstr(labels, "Values between blend the two.") &&
           std::strstr(labels, "Remove scope overlay") && std::strstr(labels, "Replaces the first-person scope overlay with the custom crosshair."),
           "scoped zoom labels explain both zoom levels, sensitivity endpoints and blending");
    ImGui::LogFinish();
    ImGui::Render();
    ClickControl("Reset ESP appearance");
    Expect(ZoomGetSettings() == ZoomSettings{true, 0, 200, 0}, "ESP reset preserves scoped zoom and sensitivity controls");
    ClickControl("Override scoped zoom");
    ClickControl("Remove scope overlay");
    Expect(!ZoomGetSettings().enabled && ZoomGetSettings().removeScope,
           "scope replacement remains available after zoom override is disabled");
    ClickControl("Reset scoped zoom");
    Expect(ZoomGetSettings() == ZoomSettings{}, "scoped zoom reset restores native sensitivity while its master is disabled");
    Expect(!ThirdPersonGetSettings().enabled && MovementGetSettings().bunnyhop && MovementGetSettings().autostrafe &&
           !WorldGetSettings().enabled, "scoped zoom edits preserve independent camera, movement and world settings");
    ClickControl("Scoped zoom");
    EspSetSettings(savedEsp);
    ThirdPersonSetSettings(savedCamera);
    // Keep zoom endpoints and blended sensitivity for the Configs mouse-flow persistence check.
    ZoomSetSettings({true, 0, 200, 42.5f, true});
    ImGui::SetScrollY(Body(), 0);
    Frame(); Frame();
}
void WritePickerModel(const std::filesystem::path& root, const char* relative) {
    const auto path = root / relative;
    std::filesystem::create_directories(path.parent_path());
    const std::string graph = "animation/graphs/worldmodel/worldmodel.vnmgraph";
    std::string bytes(52, '\0');
    bytes += graph;
    bytes += '\0';
    const auto put = [&](std::size_t offset, std::uint32_t value) { std::memcpy(bytes.data() + offset, &value, sizeof(value)); };
    put(0, static_cast<std::uint32_t>(bytes.size())); bytes[4] = 12; bytes[6] = 1;
    put(8, 8); put(12, 1); std::memcpy(bytes.data() + 16, "RERL", 4);
    put(20, 8); put(24, static_cast<std::uint32_t>(bytes.size() - 28));
    put(28, 8); put(32, 1); put(36, 1); put(44, 8);
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
void PlayerModelControls() {
    const auto savedEsp = EspGetSettings();
    auto disabledEsp = savedEsp;
    disabledEsp.enabled = false;
    EspSetSettings(disabledEsp);
    const auto fixture = LocalConfigDirectory().parent_path() / "logs" /
        ("menu-model-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto graph = fixture / "animation/graphs/worldmodel/worldmodel.vnmgraph_c";
    std::filesystem::create_directories(graph.parent_path());
    std::ofstream(graph).put('x');
    WritePickerModel(fixture, "agents/models/hmn_ag2/zenless_zone_zero/ellen/ellen.vmdl_c");
    WritePickerModel(fixture, "agents/models/hmn_ag2/zenless_zone_zero/miyabi/miyabi.vmdl_c");
    std::string error;
    Expect(ModelCatalogDiscover(fixture, error), "picker discovers deterministic supported model fixture");
    ClickControl("Player model");
    ImGui::SetScrollY(Body(), Body()->ScrollMax.y);
    Frame(); Frame();
    ClickControl("Enable local player model");
    Expect(ModelSwapGetSettings().enabled && !EspGetSettings().enabled, "local model master works while ESP is disabled");
    ClickControl("##PlayerModelSearch");
    Expect(ImGui::GetIO().WantTextInput, "player model search accepts keyboard input");
    ImGui::GetIO().AddInputCharactersUTF8("miyabi");
    Frame(); Frame();
    ImGuiWindow* list = Body()->DC.ChildWindows.empty() ? nullptr : Body()->DC.ChildWindows.back();
    Expect(list && list->ParentWindow == Body() && (list->Flags & ImGuiWindowFlags_ChildWindow) &&
        list->Size.y >= 150 && list->Size.y <= 156, "model picker has a bounded scrollable list");
    constexpr const char* path = "agents/models/hmn_ag2/zenless_zone_zero/miyabi/miyabi.vmdl";
    const std::string label = std::string("miyabi  (") + path + ")";
    if (list) ClickControl(label.c_str(), 0.3f, list, ImHashStr(label.c_str(), 0, list->GetID(path)));
    Expect(ModelSwapGetSettings().enabled && std::string_view(ModelSwapGetSettings().model.data()) == path,
        "searchable picker publishes the local player resource selection");
    ModelSwapSetRuntimeStatus("Waiting for a local player.");
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    DrawPublicMenu();
    const char* labels = ImGui::GetCurrentContext()->LogBuffer.c_str();
    Expect(std::strstr(labels, "1 matching / 2 available models") && std::strstr(labels, "Selected:") &&
        std::strstr(labels, "Use third person") && std::strstr(labels, "Waiting for a local player."),
        "model picker shows filtered count, selection, third-person help and runtime status");
    ImGui::LogFinish();
    ImGui::Render();
    const auto chosen = ModelSwapGetSettings();
    ClickControl("Reset ESP appearance");
    Expect(ModelSwapGetSettings() == chosen, "ESP reset preserves the selected model");
    ClickControl("Reset player model");
    Expect(ModelSwapGetSettings() == ModelSwapSettings{}, "model reset clears selection and disables local override");
    ClickControl("Player model");
    ModelSwapSetSettings(chosen);
    ModelSwapSetRuntimeStatus(nullptr);
    EspSetSettings(savedEsp);
    std::filesystem::remove_all(fixture);
    ImGui::SetScrollY(Body(), 0);
    Frame(); Frame();
}
void CheckTabsFit(const char* message) {
    bool fit = true;
    for (const char* label : { "Visuals", "Movement", "World", "Lighting", "Aim", "Configs" }) {
        const ImRect rectangle = ControlRect(label, Menu());
        fit &= rectangle.Min.x >= Menu()->Pos.x && rectangle.Max.x <= Menu()->Pos.x + Menu()->Size.x;
        fit &= rectangle.GetWidth() >= ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
    }
    Expect(fit, message);
}
}

int main() {
    NativeSettingsBounds();
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = io.LogFilename = nullptr;
    io.DisplaySize = { 1000, 800 };
    io.DeltaTime = 1.0f / 60.0f;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.ConfigFlags |= ImGuiConfigFlags_NoKeyboard;
    ConfigurePublicMenuStyle();
    Expect(!(io.ConfigFlags & ImGuiConfigFlags_NoKeyboard),
           "public menu enables keyboard input even after diagnostic initialization");
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    EspSetSettings(EspSettings{});
    MovementSetSettings(MovementSettings{});
    WorldResetSettings();
    AntiAimSetSettings({});
    ThirdPersonResetSettings();
    ZoomResetSettings();
    ShotLogSetSettings({});
    PlayerChamsResetSettings();
    GrenadeResetSettings();
    Frame();
    Frame(); // Establish normal item hover state after initial window creation.
    Expect(Menu() && Body(), "public menu and visuals panel created");
    if (!Menu() || !Body()) {
        ImGui::DestroyContext();
        return 1;
    }
    Expect(Menu()->Pos.x == 24 && Menu()->Pos.y == 24, "initial menu position");
    Expect(Menu()->Size.x == 560 && Menu()->Size.y == 620, "larger menu leaves space for appearance controls");
    Expect(ImGui::GetDrawData()->TotalVtxCount > 0, "public menu submits rendering primitives");
    CheckTabsFit("all tab labels fit the normal menu");

    Click(EnablePosition());
    Expect(!EspGetSettings().enabled, "mouse click disables shared ESP setting");
    Click(OptionPosition(0));
    Expect(EspGetSettings().boxes, "disabled master prevents option interaction");
    Click(EnablePosition());
    Expect(EspGetSettings().enabled, "mouse click re-enables shared ESP setting");
    Click(OptionPosition(0));
    Expect(!EspGetSettings().boxes && EspGetSettings().names && EspGetSettings().distance,
           "boxes click changes only its shared setting");
    Click(OptionPosition(4));
    Expect(EspGetSettings().armor && EspGetSettings().health && EspGetSettings().teammates,
           "armor click preserves other visible feature settings");
    EspAppearanceControls();
    CameraControls();
    ZoomControls();
    PlayerModelControls();
    PlayerChamsControls();
    GrenadeControls();

    const ImVec2 beforeTabs = Menu()->Pos;
    Click(TabPosition(1));
    Frame(); // Settle the new child window before using its geometry.
    Expect(Body() && std::strstr(Body()->Name, "##MovementSettings"),
           "movement tab switches to functional movement controls");
    Click(MovementPosition(0));
    Expect(!MovementGetSettings().bunnyhop && MovementGetSettings().autostrafe,
           "bunnyhop control changes its shared setting independently");
    Click(MovementPosition(1));
    Expect(!MovementGetSettings().bunnyhop && !MovementGetSettings().autostrafe,
           "autostrafe can be disabled independently while bunnyhop is off");
    Click(MovementPosition(0));
    Expect(MovementGetSettings().bunnyhop && !MovementGetSettings().autostrafe,
           "bunnyhop can be enabled independently while autostrafe is off");
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    DrawPublicMenu();
    const char* movementLabels = ImGui::GetCurrentContext()->LogBuffer.c_str();
    Expect(std::strstr(movementLabels, "Auto bunnyhop (hold Space)") &&
           std::strstr(movementLabels, "Aggressive autostrafe (hold Space)") &&
           std::strstr(movementLabels, "WASD  Direction and speed gain") &&
           std::strstr(movementLabels, "Release  No autostop") &&
           std::strstr(movementLabels, "F8") && std::strstr(movementLabels, "F9") &&
           !std::strstr(movementLabels, "Enable ESP") &&
           !std::strstr(movementLabels, "Reader") && !std::strstr(movementLabels, "Frames"),
           "movement controls display activation and direction help without diagnostics");
    ImGui::LogFinish();
    ImGui::Render();
    Click(TabPosition(4));
    Frame();
    Expect(Body() && std::strstr(Body()->Name, "##AimSettings") && !SilentAimGetSettings().enabled,
           "Aim tab opens with silent aim disabled");
    Expect(!AntiAimGetSettings().enabled, "Aim tab opens with anti aim disabled");
    ClickControl("Visualize ragebot");
    Expect(AimVisualizationGetSettings().enabled && !SilentAimGetSettings().enabled,
        "visualization toggle responds independently without enabling aim");
    const auto aimBeforeColor = SilentAimGetSettings();
    ClickControl("point color swatch", .5f, Body(), ImHashStr("##ColorButton", 0, Body()->GetID("##AimPointColor")));
    auto& pointContext = *ImGui::GetCurrentContext();
    const bool pointPickerOpen = !pointContext.OpenPopupStack.empty() && pointContext.OpenPopupStack.back().Window;
    Expect(pointPickerOpen, "visualization color swatch opens RGBA picker");
    if (pointPickerOpen) {
        auto* popup = pointContext.OpenPopupStack.back().Window;
        const auto picker = popup->GetID("##picker");
        const auto sv = ControlRect("point saturation/value", popup, ImHashStr("sv", 0, picker));
        Click({sv.Min.x + sv.GetWidth()*.7f, sv.Min.y + sv.GetHeight()*.3f});
        const auto alpha = ControlRect("point alpha", popup, ImHashStr("alpha", 0, picker));
        Click({alpha.GetCenter().x, alpha.Min.y + alpha.GetHeight()*.75f});
        const auto edited = AimVisualizationGetSettings();
        Expect(edited.color[0] != 1 && edited.color[3] > .15f && edited.color[3] < .35f &&
            SilentAimGetSettings() == aimBeforeColor, "RGBA picker customizes points without changing aim behavior");
        Click({Menu()->Pos.x + 5, Menu()->Pos.y + 5});
    }
    ClickControl("Visualize ragebot");
    Expect(!AimVisualizationGetSettings().enabled, "visualization toggle can be disabled");
    Expect(!ConsoleControlOnlyOwn(), "native console filtering starts disabled");
    ConsoleControlSetAvailable(true);
    ClickControl("Our logs only");
    Expect(ConsoleControlOnlyOwn() && ConsoleControlTakeRequest(), "console filter toggle requests a fresh native clear");
    ConsoleControlCompleteClear();
    ClickControl("Clear console");
    Expect(ConsoleControlTakeRequest(), "clear console button submits a request independently of shot logs");
    ConsoleControlCompleteClear();
    ClickControl("Our logs only");
    Expect(!ConsoleControlOnlyOwn(), "console filter toggle restores native messages");
    ConsoleControlSetAvailable(false);
    Expect(ShotLogGetSettings()==ShotLogSettings{true,false}, "Aim tab opens with human shot logs enabled and verbose diagnostics disabled");
    const auto aimBeforeLogging=SilentAimGetSettings();
    ClickControl("Shot logs");
    ClickControl("Verbose aim diagnostics");
    Expect(ShotLogGetSettings()==ShotLogSettings{false,true} && SilentAimGetSettings()==aimBeforeLogging && !AntiAimGetSettings().enabled,
        "shot logging and verbose controls remain usable with aim disabled and do not enable aim or autofire");
    ClickControl("Shot logs");
    Expect(ShotLogGetSettings()==ShotLogSettings{true,true}, "human logging can be enabled while verbose diagnostics remain enabled");
    ClickControl("Verbose aim diagnostics");
    Expect(ShotLogGetSettings()==ShotLogSettings{true,false}, "verbose diagnostics can be disabled while human results remain enabled");
    ClickControl("Autofire");
    ClickControl("##AimHitchance", 0.9f);
    ClickControl("##AimSamples", 0.9f);
    ClickControl("##AimMinDamage", 0.9f);
    ClickControl("Quickstop");
    ClickHitgroup("Head");
    Expect(!SilentAimGetSettings().autofire && SilentAimGetSettings().hitchance == 50 && SilentAimGetSettings().samples == 256 &&
           !SilentAimGetSettings().quickstop && SilentAimGetSettings().minDamage == 1 &&
           SilentAimGetSettings().hitgroups == AimHitgroupBit(AimHitgroup::Chest),
           "disabled aim master blocks autofire, quickstop, damage, hitchance and hitgroup edits");
    ClickControl("##AimFov", 0.8f);
    Expect(SilentAimGetSettings().fov == 5, "disabled silent aim blocks FOV editing");
    ClickControl("Enable silent aim");
    ClickControl("##AimFov", 0.5f);
    Expect(SilentAimGetSettings().enabled && SilentAimGetSettings().fov > 80,
           "silent aim master and FOV respond to mouse input");
    ClickControl("##AimFov", 0.99f);
    Expect(SilentAimGetSettings().fov == 180, "silent aim FOV slider reaches 180 degrees");
    Expect(SilentAimGetSettings().hitgroups == AimHitgroupBit(AimHitgroup::Chest), "hitgroup selection starts with chest only");
    constexpr const char* hitgroupNames[]{"Head", "Chest", "Stomach", "Pelvis", "Arms", "Hands", "Legs", "Feet"};
    std::uint32_t expectedHitgroups = AimHitgroupBit(AimHitgroup::Chest);
    for (unsigned index = 0; index < std::size(hitgroupNames); ++index) {
        ClickHitgroup(hitgroupNames[index]);
        expectedHitgroups ^= 1u << index;
        Expect(SilentAimGetSettings().hitgroups == expectedHitgroups, "hitgroup checkbox toggles independently and preserves other groups");
    }
    ClickHitgroup("Chest");
    Expect(SilentAimGetSettings().hitgroups == AimAllHitgroups, "every hitgroup can be enabled simultaneously");
    for (const char* name : hitgroupNames) ClickHitgroup(name);
    Expect(SilentAimGetSettings().hitgroups == 0, "all hitgroups can be deselected without re-enabling a fallback");
    ClickHitgroup("Head");
    ClickHitgroup("Chest");
    Expect(SilentAimGetSettings().hitgroups == 3, "head and chest remain selected together after empty selection");
    ClickControl("Quickstop");
    Expect(!SilentAimGetSettings().quickstop, "quickstop checkbox waits for enabled autofire");
    ClickControl("##AimMinDamage", 0.9f);
    Expect(SilentAimGetSettings().minDamage == 1, "minimum damage slider waits for enabled autofire");
    ClickControl("Autofire");
    ClickControl("##AimHitchance", 0.75f);
    ClickControl("Autowall");
    Expect(SilentAimGetSettings().autowall, "autowall checkbox responds to real mouse input");
    ClickControl("##AimSamples", 0.8f);
    Expect(SilentAimGetSettings().autofire && SilentAimGetSettings().hitchance > 60 && SilentAimGetSettings().samples > 700,
           "autofire threshold and sample count respond to real mouse input");
    ClickControl("##AimMinDamage", 0.99f);
    Expect(SilentAimGetSettings().minDamage == 100, "minimum damage slider reaches 100 HP with real mouse input");
    ClickControl("##AimMinDamage", 0.01f);
    Expect(SilentAimGetSettings().minDamage == 1, "minimum damage slider reaches 1 HP with real mouse input");
    ClickControl("##AimMinDamage", 0.65f);
    const int savedMinDamage = SilentAimGetSettings().minDamage;
    Expect(savedMinDamage > 50 && savedMinDamage < 80, "minimum damage slider publishes intermediate integer HP");
    ClickControl("Quickstop");
    Expect(SilentAimGetSettings().quickstop, "quickstop checkbox publishes its enabled preference with autofire enabled");
    ClickControl("Autofire");
    ClickControl("Quickstop");
    ClickControl("##AimMinDamage", 0.1f);
    Expect(!SilentAimGetSettings().autofire && SilentAimGetSettings().quickstop && SilentAimGetSettings().minDamage == savedMinDamage,
        "disabling autofire preserves quickstop and minimum damage preferences and blocks edits");
    ClickControl("Enable silent aim");
    Expect(!SilentAimGetSettings().enabled && !EspGetSettings().boxes && EspGetSettings().armor,
           "silent aim changes preserve independent ESP settings");
    ClickControl("Shot logs");
    ClickControl("Verbose aim diagnostics");
    Expect(ShotLogGetSettings()==ShotLogSettings{false,true} && !SilentAimGetSettings().enabled && !SilentAimGetSettings().autofire,
        "logging remains independently configurable after disabling active aim");
    ClickControl("Enable anti aim");
    Expect(AntiAimGetSettings().enabled && !SilentAimGetSettings().enabled &&
           MovementGetSettings().bunnyhop && !MovementGetSettings().autostrafe,
           "anti aim toggles on independently while silent aim is disabled");
    ClickControl("Enable anti aim");
    Expect(!AntiAimGetSettings().enabled, "anti aim checkbox disables the shared command setting");
    ClickControl("Enable anti aim");
    AntiAimControls();
    Click(TabPosition(0));
    Frame();
    Expect(Body() && std::strstr(Body()->Name, "##VisualSettings") &&
           !EspGetSettings().boxes && EspGetSettings().armor,
           "returning to visuals preserves ESP settings");
    Expect(Menu()->Pos.x == beforeTabs.x && Menu()->Pos.y == beforeTabs.y,
           "tab and setting clicks do not drag the menu");
    Expect(MovementGetSettings().bunnyhop && !MovementGetSettings().autostrafe,
           "switching tabs preserves movement settings");

    Click(TabPosition(2));
    Frame();
    Expect(Body() && std::strstr(Body()->Name, "##WorldSettings"),
           "world tab opens separate world controls");
    Expect(!WorldGetSettings().enabled && !WorldGetSettings().skyEnabled &&
           !WorldGetSettings().worldMaterialEnabled && !WorldGetSettings().flatTextures,
           "world effects start disabled");
    ClickControl("Override sky");
    Expect(WorldGetSettings().skyEnabled && !WorldGetSettings().enabled,
           "sky checkbox independently updates shared settings");
    Expect(Body()->ScrollMax.y > 0, "world controls have a scrollable body");
    const ImVec2 worldBodyCenter = Body()->InnerRect.GetCenter();
    Move(worldBodyCenter);
    io.AddMouseWheelEvent(0, -2);
    Frame();
    Frame();
    Expect(Body()->Scroll.y > 0, "mouse wheel reaches world controls below the fold");
    // Bring the material section into view through the child's real scrolling API.
    ImGui::SetScrollY(Body(), 190);
    Frame();
    Frame();
    ClickControl("Flat world textures");
    Expect(WorldGetSettings().flatTextures && WorldGetSettings().worldMaterialEnabled &&
           WorldGetSettings().skyEnabled && !WorldGetSettings().enabled,
           "flat world textures independently enables its native material override");
    ClickControl("Flat world textures");
    Expect(!WorldGetSettings().flatTextures && !WorldGetSettings().worldMaterialEnabled,
           "disabling flat world textures disables its native material override");
    ClickControl("Flat world textures");
    // Bring the screen grading master into view without changing native effects.
    ImGui::SetScrollY(Body(), 380);
    Frame();
    Frame();
    ClickControl("Enable color grading");
    Expect(WorldGetSettings().enabled && WorldGetSettings().skyEnabled && WorldGetSettings().worldMaterialEnabled,
           "color grading checkbox changes its shared setting independently");
    WorldSettings altered = WorldGetSettings();
    altered.exposureEv = 1.25f;
    altered.saturation = 0.4f;
    altered.tint[0] = 0.6f;
    altered.skyBrightness = 2;
    altered.skyTint[1] = 0.3f;
    WorldSetSettings(altered);
    Click(TabPosition(3));
    Frame();
    Expect(Body() && std::strstr(Body()->Name, "##LightingSettings"),
           "lighting tab opens native lighting controls");
    ClickControl("Disable local lights");
    Expect(!WorldGetSettings().disableLocalLights, "disabled lighting master blocks local-light control");
    ClickControl("Add ambient light");
    ClickControl("##AmbientLightBrightness", 0.7f);
    Expect(!WorldGetSettings().ambientLightEnabled && WorldGetSettings().ambientLightBrightness == 0.25f,
           "disabled lighting master blocks ambient fill checkbox and intensity");
    ClickControl("##SunLightBrightness", 0.8f);
    ClickControl("##DirectionalLightmaps");
    Expect(WorldGetSettings().sunLightBrightness == 1 && WorldGetSettings().directionalLightmapsMode == 0 &&
           ImGui::GetCurrentContext()->OpenPopupStack.empty(),
           "disabled lighting master blocks sunlight and directional mode mouse input");
    ClickControl("Override lighting");
    Expect(WorldGetSettings().lightingEnabled && WorldGetSettings().skyEnabled && WorldGetSettings().enabled,
           "lighting master publishes independently of sky and screen color");
    ClickControl("##AmbientLightBrightness", 0.7f);
    Expect(WorldGetSettings().ambientLightBrightness == 0.25f,
           "disabled ambient checkbox blocks intensity even with lighting enabled");
    ClickControl("Add ambient light");
    ClickControl("##AmbientLightBrightness", 0.3f);
    Expect(WorldGetSettings().ambientLightEnabled && WorldGetSettings().ambientLightBrightness > 0.25f &&
           WorldGetSettings().sunLightBrightness == 1 && WorldGetSettings().localLightBrightness == 1,
           "ambient fill checkbox and intensity publish independently through mouse input");
    ClickControl("Disable local lights");
    Expect(WorldGetSettings().disableLocalLights, "local-light checkbox publishes native suppression setting");
    ClickControl("##LocalLightBrightness", 0.8f);
    Expect(WorldGetSettings().localLightBrightness == 1,
           "local-light suppression disables its brightness control");
    ClickControl("##SunLightBrightness", 0.4f);
    Expect(WorldGetSettings().sunLightBrightness > 1 && WorldGetSettings().disableLocalLights &&
           WorldGetSettings().localLightBrightness == 1,
           "sun brightness mouse input remains independent of local-light suppression");
    SelectDirectionalMode("Disabled");
    Expect(WorldGetSettings().directionalLightmapsMode == 2, "disabled directional mode publishes native selection");
    SelectDirectionalMode("Enabled");
    Expect(WorldGetSettings().directionalLightmapsMode == 1, "enabled directional mode publishes native selection");
    SelectDirectionalMode("Game value");
    Expect(WorldGetSettings().directionalLightmapsMode == 0, "game directional mode restores native selection");
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    DrawPublicMenu();
    const char* lightingLabels = ImGui::GetCurrentContext()->LogBuffer.c_str();
    Expect(std::strstr(lightingLabels, "Point, spot, orthographic and area lights") &&
           std::strstr(lightingLabels, "Sunlight") && std::strstr(lightingLabels, "Directional lightmaps") &&
           std::strstr(lightingLabels, "Ambient fill") && std::strstr(lightingLabels, "Baked map lighting remains."),
           "lighting help covers native ambient fill, local lights, sunlight and directional lightmaps");
    ImGui::LogFinish();
    ImGui::Render();
    altered = WorldGetSettings();
    altered.localLightBrightness = 2.5f;
    altered.localLightTint[0] = 0.3f;
    altered.sunLightTint[2] = 0.2f;
    altered.directionalLightmapsMode = 2;
    WorldSetSettings(altered);
    ImGui::SetScrollY(Body(), Body()->ScrollMax.y);
    Frame(); Frame();
    ClickControl("Reset lighting");
    const auto lightReset = WorldGetSettings();
    Expect(!lightReset.lightingEnabled && !lightReset.ambientLightEnabled && lightReset.ambientLightBrightness == 0.25f &&
           lightReset.ambientLightTint[0] == 1 && lightReset.ambientLightTint[1] == 1 && lightReset.ambientLightTint[2] == 1 &&
           !lightReset.disableLocalLights && lightReset.localLightBrightness == 1 &&
           lightReset.localLightTint[0] == 1 && lightReset.localLightTint[1] == 1 && lightReset.localLightTint[2] == 1 &&
           lightReset.sunLightTint[0] == 1 && lightReset.sunLightTint[1] == 1 && lightReset.sunLightTint[2] == 1 &&
           lightReset.sunLightBrightness == 1 && lightReset.directionalLightmapsMode == 0 &&
           lightReset.skyEnabled && lightReset.skyBrightness == 2 && lightReset.skyTint[1] == 0.3f &&
           lightReset.enabled && lightReset.exposureEv == 1.25f && lightReset.saturation == 0.4f && lightReset.tint[0] == 0.6f &&
           lightReset.worldMaterialEnabled && lightReset.flatTextures,
           "lighting reset restores its controls while preserving other world effects");
    altered = lightReset;
    altered.lightingEnabled = true;
    altered.ambientLightEnabled = true;
    altered.ambientLightBrightness = 0.4f;
    altered.ambientLightTint[1] = 0.5f;
    altered.disableLocalLights = true;
    altered.localLightBrightness = 2.5f;
    altered.localLightTint[0] = 0.3f;
    altered.sunLightBrightness = 3;
    altered.sunLightTint[2] = 0.2f;
    altered.directionalLightmapsMode = 1;
    WorldSetSettings(altered);
    Click(TabPosition(1));
    Frame();
    Expect(MovementGetSettings().bunnyhop && !MovementGetSettings().autostrafe,
           "world interaction preserves movement controls");
    Click(TabPosition(2));
    Frame();
    Expect(WorldGetSettings().enabled && WorldGetSettings().skyEnabled &&
           WorldGetSettings().exposureEv == 1.25f && WorldGetSettings().skyBrightness == 2 &&
           WorldGetSettings().worldMaterialEnabled && WorldGetSettings().flatTextures,
           "tab switching preserves world settings");
    Expect(WorldGetSettings().lightingEnabled && WorldGetSettings().disableLocalLights &&
           WorldGetSettings().ambientLightEnabled && WorldGetSettings().ambientLightBrightness == 0.4f &&
           WorldGetSettings().ambientLightTint[1] == 0.5f &&
           WorldGetSettings().localLightBrightness == 2.5f && WorldGetSettings().localLightTint[0] == 0.3f &&
           WorldGetSettings().sunLightBrightness == 3 && WorldGetSettings().sunLightTint[2] == 0.2f &&
           WorldGetSettings().directionalLightmapsMode == 1,
           "tab switching preserves native lighting settings");
    ImGui::SetScrollY(Body(), Body()->ScrollMax.y);
    Frame();
    Frame();
    ClickControl("Reset world visuals");
    const WorldSettings reset = WorldGetSettings();
    Expect(!reset.enabled && !reset.skyEnabled && !reset.worldMaterialEnabled && !reset.flatTextures &&
           !reset.lightingEnabled && !reset.disableLocalLights && reset.localLightBrightness == 1 &&
           !reset.ambientLightEnabled && reset.ambientLightBrightness == 0.25f &&
           reset.ambientLightTint[0] == 1 && reset.ambientLightTint[1] == 1 && reset.ambientLightTint[2] == 1 &&
           reset.localLightTint[0] == 1 && reset.localLightTint[1] == 1 && reset.localLightTint[2] == 1 &&
           reset.sunLightBrightness == 1 && reset.sunLightTint[0] == 1 && reset.sunLightTint[1] == 1 &&
           reset.sunLightTint[2] == 1 && reset.directionalLightmapsMode == 0 &&
           reset.exposureEv == 0 &&
           reset.contrast == 1 && reset.saturation == 1 && reset.gamma == 1 &&
           reset.tint[0] == 1 && reset.tint[1] == 1 && reset.tint[2] == 1 &&
           reset.vignette == 0 && reset.sharpening == 0 && reset.skyBrightness == 1 &&
           reset.skyTint[0] == 1 && reset.skyTint[1] == 1 && reset.skyTint[2] == 1 &&
           reset.skyLightingTint[0] == 1 && reset.skyLightingTint[1] == 1 && reset.skyLightingTint[2] == 1,
           "world reset restores neutral values and disables all effects");
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    DrawPublicMenu();
    const char* worldLabels = ImGui::GetCurrentContext()->LogBuffer.c_str();
    Expect(std::strstr(worldLabels, "Sky") && std::strstr(worldLabels, "Override sky") &&
           std::strstr(worldLabels, "Sky probe/reflections tint") && !std::strstr(worldLabels, "Sky lighting tint") &&
           std::strstr(worldLabels, "World surfaces") && std::strstr(worldLabels, "Flat world textures") &&
           !std::strstr(worldLabels, "Surface tint") && !std::strstr(worldLabels, "World material override") &&
           std::strstr(worldLabels, "Screen color") && std::strstr(worldLabels, "Also affects the game HUD.") &&
           !std::strstr(worldLabels, "Texture replacement") && !std::strstr(worldLabels, "Reader"),
           "world labels describe implemented effects and HUD impact");
    ImGui::LogFinish();
    ImGui::Render();
    Click(TabPosition(0));
    Frame();
    Expect(!EspGetSettings().boxes && EspGetSettings().armor,
           "world controls preserve existing player visuals");

    Click(TabPosition(5));
    Frame();
    Expect(Body() && std::strstr(Body()->Name, "##ConfigSettings"), "Configs tab opens");
    ClickControl("##ConfigName");
    Expect(io.WantTextInput, "config name field requests keyboard text input after click");
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    Frame();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    const std::string testName = "ui-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    io.AddInputCharactersUTF8(testName.c_str());
    Frame();
    const auto savedEsp = EspGetSettings();
    const auto savedCamera = ThirdPersonGetSettings();
    const auto savedZoom = ZoomGetSettings();
    const auto savedAim = SilentAimGetSettings();
    const auto savedAntiAim = AntiAimGetSettings();
    const auto savedShotLog=ShotLogGetSettings();
    const auto savedModel=ModelSwapGetSettings();
    const auto savedChams=PlayerChamsGetSettings();
    const auto savedGrenades=GrenadeGetSettings();
    const auto configPath = LocalConfigDirectory() / (testName + ".cfg");
    Expect(!std::filesystem::exists(configPath), "UI test preset name is unused");
    ClickControl("Save / overwrite");
    Expect(std::filesystem::exists(configPath), "Save button writes named preset");
    EspSetSettings(EspSettings{});
    AntiAimSetSettings({false});
    ThirdPersonResetSettings();
    ZoomResetSettings();
    SilentAimSetSettings({});
    ShotLogSetSettings({});
    ModelSwapResetSettings();
    PlayerChamsResetSettings();
    GrenadeResetSettings();
    ClickControl("Load config");
    Expect(EspGetSettings() == savedEsp && AntiAimGetSettings() == savedAntiAim && ThirdPersonGetSettings() == savedCamera &&
           ZoomGetSettings() == savedZoom && SilentAimGetSettings() == savedAim && ShotLogGetSettings()==savedShotLog &&
           ModelSwapGetSettings() == savedModel && PlayerChamsGetSettings() == savedChams && GrenadeGetSettings() == savedGrenades,
           "Load button restores appearance, aim, hitchance, anti aim, camera, scoped zoom, logging, player model, chams and grenades settings");
    ClickControl("Refresh list");
    std::error_code cleanupError;
    std::filesystem::remove(configPath, cleanupError);
    Click(TabPosition(0));
    Frame();

    const auto beforeDragging = EspGetSettings();
    // Exercise corners, side padding, center and the lower edge of the bar.
    constexpr ImVec2 dragPoints[]{ {1, 1}, {0.5f, 0.5f}, {-1, 1}, {1, -1}, {-1, -1} };
    for (const auto point : dragPoints) {
        const ImVec2 origin = Menu()->Pos;
        const ImRect bar = ControlRect("##DragMenu", Menu());
        const ImVec2 start {
            // DebugLocateItem outlines expand by four pixels around the item.
            point.x == 0.5f ? bar.GetCenter().x : point.x < 0 ? bar.Max.x - 6 : bar.Min.x + 6,
            point.y == 0.5f ? bar.GetCenter().y : point.y < 0 ? bar.Max.y - 6 : bar.Min.y + 6
        };
        Move(start);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        Frame();
        Move({ start.x + 24, start.y + 16 });
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        Frame();
        Expect(Menu()->Pos.x == origin.x + 24 && Menu()->Pos.y == origin.y + 16,
               "every part of the title bar drags through real mouse events");
    }
    Expect(EspGetSettings() == beforeDragging, "title bar dragging preserves settings");

    io.DisplaySize = { 1000, 300 };
    Frame(); Frame();
    for (const int category : { 0, 2, 3, 4, 5 }) {
        Click(TabPosition(category));
        ImGui::SetScrollY(Body(), 0);
        Frame(); Frame();
        Expect(Body()->ScrollMax.y > 0 && Body()->ScrollbarY,
               "overflowing settings expose a vertical scrollbar");
        Move(Body()->InnerRect.GetCenter());
        io.AddMouseWheelEvent(0, -2);
        Frame(); Frame();
        Expect(Body()->Scroll.y > 0, "mouse wheel scrolls each overflowing settings category");
        io.AddMouseWheelEvent(0, 100);
        Frame(); Frame();
        Expect(Body()->Scroll.y == 0, "mouse wheel returns each settings category to the top");
        const ImRect scrollbar = ImGui::GetWindowScrollbarRect(Body(), ImGuiAxis_Y);
        Move({ scrollbar.GetCenter().x, scrollbar.Min.y + 6 });
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        Frame();
        Move({ scrollbar.GetCenter().x, scrollbar.Max.y - 6 });
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        Frame(); Frame();
        Expect(Body()->Scroll.y > 0, "scrollbar dragging reaches lower settings");
    }
    Click(TabPosition(0));

    io.DisplaySize = { 180, 150 };
    Frame();
    Expect(Menu()->Pos.x >= 0 && Menu()->Pos.y >= 0 &&
           Menu()->Pos.x + Menu()->Size.x <= 180 && Menu()->Pos.y + Menu()->Size.y <= 150,
           "menu clamps position and size after viewport shrink");
    const auto categoryRect = ControlRect("##Category", Menu());
    Expect(categoryRect.Min.x >= Menu()->Pos.x && categoryRect.Max.x <= Menu()->Pos.x + Menu()->Size.x,
           "short viewport uses a compact category selector to leave settings accessible");
    Expect(Body()->ScrollMax.y > 0, "ESP appearance remains reachable by scrolling in a narrow viewport");

    io.DisplaySize = { 1000, 800 };
    Frame(); // Settle the child geometry after restoring a larger viewport.
    Frame();
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    DrawPublicMenu();
    const char* labels = ImGui::GetCurrentContext()->LogBuffer.c_str();
    Expect(std::strstr(labels, "Visuals") && std::strstr(labels, "Distance labels") &&
           !std::strstr(labels, "Aimbot") && !std::strstr(labels, "Misc") &&
           !std::strstr(labels, "Skeleton") && !std::strstr(labels, "Reader") &&
           !std::strstr(labels, "Frames") && !std::strstr(labels, "range"),
           "only supported visuals controls appear without diagnostics or range control");
    ImGui::LogFinish(); // 1.91.9b clears the buffer here; inspect it beforehand.
    ImGui::Render();
    EspSetSettings(EspSettings{});
    MovementSetSettings(MovementSettings{});
    WorldResetSettings();
    AntiAimSetSettings({});
    ThirdPersonResetSettings();
    ZoomResetSettings();
    ShotLogSetSettings({});
    ModelSwapResetSettings();
    PlayerChamsResetSettings();
    ImGui::DestroyContext();
    return failures ? 1 : 0;
}
