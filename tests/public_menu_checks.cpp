#include "public_menu.h"
#include "esp_draw.h"
#include "movement_settings.h"
#include "world_settings.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <limits>

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
ImVec2 TabPosition(int index) {
    const ImVec2 start = Menu()->DC.CursorStartPos;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float contentWidth = Menu()->Size.x - style.WindowPadding.x * 2;
    const float tabWidth = std::fmin(100.0f, std::fmax(1.0f,
        (contentWidth - style.ItemSpacing.x * 3) / 4));
    return { start.x + tabWidth * 0.5f + static_cast<float>(index) * (tabWidth + style.ItemSpacing.x),
             start.y + 8 + style.ItemSpacing.y + 12.5f };
}
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
void CheckTabsFit(const char* message) {
    bool fit = true;
    for (const char* label : { "Visuals", "Movement", "World", "Lighting" }) {
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
    io.DisplaySize = { 800, 600 };
    io.DeltaTime = 1.0f / 60.0f;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    ConfigurePublicMenuStyle();
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    EspSetSettings(EspSettings{});
    MovementSetSettings(MovementSettings{});
    WorldResetSettings();
    Frame();
    Frame(); // Establish normal item hover state after initial window creation.
    Expect(Menu() && Body(), "public menu and visuals panel created");
    if (!Menu() || !Body()) {
        ImGui::DestroyContext();
        return 1;
    }
    Expect(Menu()->Pos.x == 24 && Menu()->Pos.y == 24, "initial menu position");
    Expect(ImGui::GetDrawData()->TotalVtxCount > 0, "public menu submits rendering primitives");
    CheckTabsFit("all four tab labels fit the normal menu");

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

    const ImVec2 origin = Menu()->Pos;
    Move({ origin.x + 35, origin.y + 14 });
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    Frame();
    Move({ origin.x + 115, origin.y + 84 });
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    Frame();
    Expect(Menu()->Pos.x > origin.x && Menu()->Pos.y > origin.y,
           "drag strip moves the menu through real mouse events");

    io.DisplaySize = { 180, 150 };
    Frame();
    Expect(Menu()->Pos.x >= 0 && Menu()->Pos.y >= 0 &&
           Menu()->Pos.x + Menu()->Size.x <= 180 && Menu()->Pos.y + Menu()->Size.y <= 150,
           "menu clamps position and size after viewport shrink");
    CheckTabsFit("four tabs wrap with readable labels after viewport shrink");

    io.DisplaySize = { 800, 600 };
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
    ImGui::DestroyContext();
    return failures ? 1 : 0;
}
