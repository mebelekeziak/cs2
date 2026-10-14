#include "public_menu.h"
#include "esp_draw.h"
#include "movement_settings.h"
#include "world_settings.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
void Expect(bool condition, const char* label) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", label);
    failures += !condition;
}
void Frame() {
    ImGui::NewFrame();
    DrawPublicMenu();
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
        (contentWidth - style.ItemSpacing.x) * 0.5f));
    return { start.x + tabWidth * 0.5f + static_cast<float>(index) * (tabWidth + style.ItemSpacing.x),
             start.y + 8 + style.ItemSpacing.y + 12.5f };
}
ImVec2 MovementPosition(int row) {
    ImVec2 position = EnablePosition();
    position.y += static_cast<float>(row) * ImGui::GetFrameHeightWithSpacing();
    return position;
}
void ActivateWorldControl(const char* label) {
    ImGui::ActivateItemByID(Body()->GetID(label));
    Frame();
    Frame();
}
}

int main() {
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
    ActivateWorldControl("Override sky");
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
    ActivateWorldControl("World material override");
    Expect(WorldGetSettings().worldMaterialEnabled && WorldGetSettings().skyEnabled && !WorldGetSettings().enabled,
           "world material master is independent of sky and screen grading");
    ActivateWorldControl("Flat world textures");
    Expect(WorldGetSettings().flatTextures && WorldGetSettings().worldMaterialEnabled,
           "flat world texture checkbox updates shared settings");
    // Bring the screen grading master into view without changing native effects.
    ImGui::SetScrollY(Body(), 380);
    Frame();
    Frame();
    ActivateWorldControl("Enable color grading");
    Expect(WorldGetSettings().enabled && WorldGetSettings().skyEnabled && WorldGetSettings().worldMaterialEnabled,
           "color grading checkbox changes its shared setting independently");
    WorldSettings altered = WorldGetSettings();
    altered.exposureEv = 1.25f;
    altered.saturation = 0.4f;
    altered.tint[0] = 0.6f;
    altered.skyBrightness = 2;
    altered.skyTint[1] = 0.3f;
    altered.worldTint[2] = 0.25f;
    WorldSetSettings(altered);
    Click(TabPosition(1));
    Frame();
    Expect(MovementGetSettings().bunnyhop && !MovementGetSettings().autostrafe,
           "world interaction preserves movement controls");
    Click(TabPosition(2));
    Frame();
    Expect(WorldGetSettings().enabled && WorldGetSettings().skyEnabled &&
           WorldGetSettings().exposureEv == 1.25f && WorldGetSettings().skyBrightness == 2 &&
           WorldGetSettings().worldMaterialEnabled && WorldGetSettings().flatTextures &&
           WorldGetSettings().worldTint[2] == 0.25f,
           "tab switching preserves world settings");
    ImGui::SetScrollY(Body(), Body()->ScrollMax.y);
    Frame();
    Frame();
    ActivateWorldControl("Reset world visuals");
    const WorldSettings reset = WorldGetSettings();
    Expect(!reset.enabled && !reset.skyEnabled && !reset.worldMaterialEnabled && !reset.flatTextures &&
           reset.worldTint[0] == 1 && reset.worldTint[1] == 1 && reset.worldTint[2] == 1 && reset.exposureEv == 0 &&
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
           std::strstr(worldLabels, "World surfaces") && std::strstr(worldLabels, "Flat world textures") &&
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
