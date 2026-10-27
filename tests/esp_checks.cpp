#include "esp_draw.h"
#include <imgui.h>
#include <Windows.h>

#include <cmath>
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <limits>
#include <cstring>
#include <vector>

namespace {
int failures = 0;

void Expect(bool result, const char* label) {
    std::printf("%s %s\n", result ? "PASS" : "FAIL", label);
    failures += !result;
}

void HotkeyChecks() {
    const EspSettings original = EspGetSettings();
    for (bool teammates : {false, true}) {
        EspSettings configured = original;
        configured.teammates = teammates;
        EspSetSettings(configured);
        const auto before = EspGetSettings();
        Expect(!EspHandleHotkey(VK_F6) && EspGetSettings() == before,
               "third-person F6 never changes teammate visibility or any ESP setting");
    }
    const struct { int key; bool EspSettings::*field; } assignments[]{
        {VK_F1, &EspSettings::enabled}, {VK_F2, &EspSettings::names},
        {VK_F3, &EspSettings::boxes}, {VK_F4, &EspSettings::distance},
        {VK_F5, &EspSettings::health}, {VK_F7, &EspSettings::armor}
    };
    for (const auto& assignment : assignments) {
        const auto before = EspGetSettings();
        auto expected = before;
        expected.*assignment.field = !(expected.*assignment.field);
        Expect(EspHandleHotkey(assignment.key) && EspGetSettings() == expected,
               "remaining ESP hotkey changes only its assigned option");
        Expect(EspHandleHotkey(assignment.key) && EspGetSettings() == before,
               "second ESP hotkey press restores its assigned option");
    }
    EspSetSettings(original);
}

void SetBounds(EspVec3 (&corners)[8], float offset = 0) {
    for (int i = 0; i < 8; ++i)
        corners[i] = { ((i & 1) ? 0.5f : -0.5f) + offset,
                       (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f };
}

struct Drawing {
    std::vector<ImDrawVert> vertices;
    std::vector<ImDrawIdx> indices;
    EspDrawStats stats;
    bool HasColor(ImU32 color) const {
        for (const auto& vertex : vertices) if (vertex.col == color) return true;
        return false;
    }
    EspScreenRect Bounds(ImU32 color) const {
        EspScreenRect bounds { FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (const auto& vertex : vertices) if (vertex.col == color) {
            bounds.left = std::min(bounds.left, vertex.pos.x);
            bounds.top = std::min(bounds.top, vertex.pos.y);
            bounds.right = std::max(bounds.right, vertex.pos.x);
            bounds.bottom = std::max(bounds.bottom, vertex.pos.y);
        }
        return bounds;
    }
    bool Covers(float x, float y, ImU32 requiredColor = 0) const {
        const auto cross = [](ImVec2 a, ImVec2 b, ImVec2 c) {
            return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        };
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            if (requiredColor && (vertices[indices[i]].col != requiredColor ||
                vertices[indices[i + 1]].col != requiredColor || vertices[indices[i + 2]].col != requiredColor)) continue;
            const ImVec2 a = vertices[indices[i]].pos, b = vertices[indices[i + 1]].pos,
                         c = vertices[indices[i + 2]].pos, p { x, y };
            const float area = cross(a, b, c);
            if (std::abs(area) < 0.0001f) continue;
            const float u = cross(a, b, p), v = cross(b, c, p), w = cross(c, a, p);
            if ((u >= 0 && v >= 0 && w >= 0) || (u <= 0 && v <= 0 && w <= 0)) return true;
        }
        return false;
    }
};

Drawing DrawSample(const EspSettings& appearance, int team = 3, int health = 50,
                   std::uint16_t weapon = 0, bool carrier = false) {
    EspSetSettings(appearance);
    EspPlayerSnapshot player{};
    strcpy_s(player.name, "WWWWiiii");
    player.team = team;
    player.health = health;
    player.weaponDefinition = weapon;
    player.carriesC4 = carrier;
    player.distanceMeters = 24;
    SetBounds(player.worldCorners);
    EspMatrix4x4 matrix{};
    for (int i = 0; i < 4; ++i) matrix.m[i][i] = 1;
    ImGui::GetIO().DisplaySize = { 1000, 800 };
    ImGui::NewFrame();
    const EspDrawStats stats = EspDraw({ matrix, &player, 1, 3, true, true, true }, 1000, 800);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    Drawing result { { draw->VtxBuffer.begin(), draw->VtxBuffer.end() },
                     { draw->IdxBuffer.begin(), draw->IdxBuffer.end() }, stats };
    ImGui::Render();
    return result;
}

void AppearanceChecks() {
    EspSettings style;
    style.names = style.distance = style.health = false;
    style.teamColors = style.boxOutline = false;
    style.boxColor[0] = 1; style.boxColor[1] = style.boxColor[2] = 0;
    const ImU32 red = IM_COL32(255, 0, 0, 255);
    const auto full = DrawSample(style);
    Expect(full.HasColor(red) && full.Covers(500, 200) && full.Covers(250, 250),
           "custom box RGB reaches full-rectangle border geometry");
    style.boxThickness = 5;
    const auto thick = DrawSample(style);
    Expect(thick.Covers(500, 202.5f) && !full.Covers(500, 202.5f),
           "thickness widens the rendered border at a known screen location");
    style.boxStyle = 1;
    const auto corners = DrawSample(style);
    Expect(corners.HasColor(red) && !corners.Covers(500, 200) && corners.Covers(250, 250),
           "corner boxes retain corner segments and leave side middles empty");
    style.boxColor[3] = 0;
    style.boxOutline = true;
    Expect(DrawSample(style).stats.drawn == 0, "transparent box also hides its outline");
    style.boxFill = true;
    style.fillColor[0] = 0; style.fillColor[1] = 1; style.fillColor[2] = 0; style.fillColor[3] = 0.25f;
    const auto filled = DrawSample(style);
    Expect(filled.HasColor(IM_COL32(0, 255, 0, 64)) && filled.Covers(500, 400),
           "independent fill RGB and alpha reach the rectangle interior");
    style.boxFill = false;
    style.boxOutline = false;
    style.teamColors = true;
    style.tColor[0] = 1; style.tColor[1] = 0; style.tColor[2] = 0; style.tColor[3] = 0.5f;
    style.ctColor[0] = 0; style.ctColor[1] = 0; style.ctColor[2] = 1; style.ctColor[3] = 1;
    Expect(DrawSample(style, 2).HasColor(IM_COL32(255, 0, 0, 128)) &&
           DrawSample(style, 3).HasColor(IM_COL32(0, 0, 255, 255)),
           "T and CT colors are independently selected with their own opacity");

    style.boxes = false;
    style.names = true;
    style.nameUseBoxColor = false;
    style.textOutline = false;
    style.nameColor[0] = style.nameColor[2] = 1; style.nameColor[1] = 0;
    style.nameSize = 10;
    const ImU32 magenta = IM_COL32(255, 0, 255, 255);
    const auto small = DrawSample(style).Bounds(magenta);
    style.nameSize = 28;
    const auto large = DrawSample(style).Bounds(magenta);
    Expect(large.right - large.left > 2 * (small.right - small.left) &&
           large.bottom - large.top > 2 * (small.bottom - small.top),
           "independent name RGB and size change actual glyph dimensions");
    style.nameSize = 18;
    float faceWidth = 0;
    bool differentFace = false;
    for (int face = 1; face <= 3; ++face) if (EspFontAvailable(face)) {
        style.nameFont = face;
        const auto drawing = DrawSample(style);
        const auto bounds = drawing.Bounds(magenta);
        Expect(drawing.HasColor(magenta) && bounds.right > bounds.left, "installed font face draws glyphs");
        const float currentWidth = bounds.right - bounds.left;
        differentFace |= faceWidth > 0 && std::abs(faceWidth - currentWidth) > 0.5f;
        faceWidth = currentWidth;
    }
    Expect(differentFace, "font selection produces distinct glyph metrics on this computer");
    style.nameColor[3] = 0;
    style.textOutline = true;
    Expect(DrawSample(style).stats.drawn == 0, "transparent name also hides its text outline");
    style.names = false;
    style.distance = true;
    style.detailColor[0] = 0; style.detailColor[1] = style.detailColor[2] = 1;
    Expect(DrawSample(style).HasColor(IM_COL32(0, 255, 255, 255)), "detail label uses its own color");
    style.distance = false;
    style.health = true;
    style.healthText = false;
    style.healthGradient = true;
    for (int i = 0; i < 4; ++i) {
        style.healthLowColor[i] = i == 0 || i == 3 ? 1.0f : 0.0f;
        style.healthColor[i] = i == 1 || i == 3 ? 1.0f : 0.0f;
    }
    style.healthBarWidth = 8;
    const auto health = DrawSample(style);
    Expect(health.HasColor(IM_COL32(128, 128, 0, 255)) &&
           health.Covers(241, 500, IM_COL32(128, 128, 0, 255)) && !health.Covers(241, 300, IM_COL32(128, 128, 0, 255)),
           "health bar uses interpolated color, selected width and current health fraction");
    style.healthText = true;
    Expect(DrawSample(style).vertices.size() > health.vertices.size(),
           "numeric health can be toggled independently of its bar");
    EspSetSettings(EspSettings{});
}

void ItemDrawingChecks() {
    EspSettings style;
    style.boxes = style.names = style.distance = style.health = false;
    style.weaponIcons = true;
    Expect(DrawSample(style, 2, 100, 7).stats.weaponIcons == 1 && DrawSample(style, 2, 100, 7).stats.drawn == 1,
        "held AK icon and label render without boxes or names");
    Expect(DrawSample(style, 2, 100, 65535).stats.drawn == 0,
        "unknown active definition produces no misleading weapon icon");
    style.weaponColor[3] = 0;
    Expect(DrawSample(style, 2, 100, 7).vertices.empty(), "transparent weapon also hides label and outline");
    style.weaponIcons = false; style.c4Carrier = true;
    Expect(DrawSample(style, 2, 100, 7, true).stats.carriers == 1 &&
        DrawSample(style, 2, 100, 7, false).stats.drawn == 0,
        "C4 badge follows inventory ownership independently of active weapon");
    style.teammates = false;
    Expect(DrawSample(style, 3, 100, 7, true).stats.drawn == 0 &&
        DrawSample(style, 2, 0, 7, true).stats.drawn == 0,
        "carrier badge obeys teammate and death filters");

    EspBombSnapshot bombs[]{{1, {-.4f, 0, 0}, EspBombKind::Dropped},
        {2, {.4f, 0, 0}, EspBombKind::Planted}, {3, {4, 0, 0}, EspBombKind::Dropped},
        {4, {0, std::numeric_limits<float>::quiet_NaN(), 0}, EspBombKind::Planted},
        {5, {}, static_cast<EspBombKind>(99)}};
    EspFrameSnapshot frame{};
    frame.valid = frame.matrixValid = true;
    frame.bombs = bombs; frame.bombCount = std::size(bombs);
    for (int i = 0; i < 4; ++i) frame.worldToClip.m[i][i] = 1;
    const auto world = [&](float width = 1000.0f) {
        EspSetSettings(style);
        ImGui::NewFrame();
        const auto stats = EspDraw(frame, width, 800);
        ImGui::Render();
        return stats.bombs;
    };
    style.enabled = false; style.droppedC4 = true;
    Expect(world() == 1, "dropped C4 projects while player ESP is disabled; offscreen/invalid points reject");
    style.droppedC4 = false; style.plantedC4 = true;
    Expect(world() == 1, "planted C4 has an independent switch");
    style.droppedC4 = true;
    Expect(world() == 2, "both C4 states render through independent world controls");
    Expect(world(0) == 0 && world(std::numeric_limits<float>::infinity()) == 0,
        "invalid world viewport rejects");
    frame.worldToClip.m[3][3] = -1;
    Expect(world() == 0, "C4 behind the camera rejects");
    frame.worldToClip.m[3][3] = 1;
    style.c4Color[3] = 0;
    Expect(world() == 0, "zero C4 opacity hides world icon and label");
    style.c4Color[3] = 1; frame.valid = false;
    Expect(world() == 0, "invalidated source never displays world C4");
    frame.valid = true; frame.bombs = nullptr;
    Expect(world() == 0, "missing world buffer rejects safely");
    EspSetSettings(EspSettings{});
}

void SettingsBounds() {
    EspSettings style;
    style.nameSize = std::numeric_limits<float>::quiet_NaN();
    style.detailSize = 1000;
    style.boxThickness = -10;
    style.healthBarWidth = std::numeric_limits<float>::infinity();
    style.nameFont = 90; style.boxStyle = -1;
    style.boxColor[0] = -1; style.boxColor[1] = 5;
    style.fillColor[3] = std::numeric_limits<float>::quiet_NaN();
    style.weaponIconSize = std::numeric_limits<float>::quiet_NaN(); style.c4IconSize = 100;
    style.weaponColor[0] = -1; style.c4Color[3] = std::numeric_limits<float>::infinity();
    EspSetSettings(style);
    const auto sanitized = EspGetSettings();
    Expect(sanitized.nameSize == 14 && sanitized.detailSize == 22 && sanitized.boxThickness == 0.5f &&
           sanitized.healthBarWidth == 3 && sanitized.nameFont == 3 && sanitized.boxStyle == 0 &&
           sanitized.boxColor[0] == 0 && sanitized.boxColor[1] == 1 && sanitized.fillColor[3] == 0.12f,
           "invalid appearance inputs are finite and bounded before rendering");
    Expect(sanitized.weaponIconSize == 18 && sanitized.c4IconSize == 36 &&
        sanitized.weaponColor[0] == 0 && sanitized.c4Color[3] == 1, "item icon style is finite and bounded");
    style.enabled = false; style.names = false; style.armor = true;
    style.weaponIcons = style.c4Carrier = style.droppedC4 = style.plantedC4 = true;
    EspSetSettings(style);
    EspResetStyle();
    auto expected = EspSettings{};
    expected.enabled = false; expected.names = false; expected.armor = true;
    expected.weaponIcons = expected.c4Carrier = expected.droppedC4 = expected.plantedC4 = true;
    Expect(EspGetSettings() == expected, "appearance reset restores all style fields while preserving toggles");
    EspSetSettings(EspSettings{});
}
} // namespace

int main() {
    HotkeyChecks();
    SettingsBounds();
    EspMatrix4x4 matrix{};
    for (int i = 0; i < 4; ++i) matrix.m[i][i] = 1;
    EspVec3 corners[8]{};
    SetBounds(corners);
    EspScreenRect rect{};

    // Known geometric oracle: a centered [-.5,.5] cube in a 100px orthographic viewport.
    Expect(EspProjectBox(corners, matrix, 100, 100, rect) == EspProjectionResult::Visible &&
           rect.left == 25 && rect.top == 25 && rect.right == 75 && rect.bottom == 75,
           "orthographic viewport oracle");
    matrix.m[0][3] = 0.5f;
    Expect(EspProjectBox(corners, matrix, 100, 100, rect) == EspProjectionResult::Visible &&
           rect.left == 50 && rect.right == 100, "row-major translated bounds");

    matrix.m[0][3] = 0;
    SetBounds(corners, 4);
    Expect(EspProjectBox(corners, matrix, 100, 100, rect) == EspProjectionResult::Offscreen,
           "offscreen rejected");
    SetBounds(corners);
    matrix.m[3][3] = -1;
    Expect(EspProjectBox(corners, matrix, 100, 100, rect) == EspProjectionResult::BehindCamera,
           "behind-camera rejected");
    matrix.m[3][3] = 1;
    corners[0].x = std::numeric_limits<float>::quiet_NaN();
    Expect(EspProjectBox(corners, matrix, 100, 100, rect) == EspProjectionResult::InvalidInput,
           "NaN bounds rejected");

    SetBounds(corners);
    matrix.m[3][2] = 1;
    matrix.m[3][3] = 0;
    Expect(EspProjectBox(corners, matrix, 100, 100, rect) == EspProjectionResult::Visible &&
           std::isfinite(rect.left) && rect.left >= 0 && rect.right <= 100,
           "camera-plane crossing clips to viewport");
    matrix.m[3][2] = 0;
    matrix.m[3][3] = 1;
    Expect(EspProjectBox(corners, matrix, 0, 100, rect) == EspProjectionResult::InvalidInput,
           "zero viewport rejected");
    for (auto& corner : corners) corner = { 0, 0, 0 };
    Expect(EspProjectBox(corners, matrix, 100, 100, rect) == EspProjectionResult::Degenerate,
           "zero-volume projection rejected");

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = { 100, 100 };
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    EspConfigureFonts();
    const int fonts = io.Fonts->Fonts.Size;
    EspConfigureFonts();
    Expect(fonts == io.Fonts->Fonts.Size, "font setup is idempotent before atlas build");
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();

    EspPlayerSnapshot players[6]{};
    for (auto& player : players) {
        player.health = 100;
        player.distanceMeters = 10;
        player.team = 3;
        SetBounds(player.worldCorners);
    }
    players[0].localPlayer = true;
    players[1].health = 0;
    players[2].distanceMeters = 10000;
    players[3].distanceMeters = std::numeric_limits<float>::infinity();
    SetBounds(players[4].worldCorners, 4);
    EspFrameSnapshot frame { matrix, players, 6, 3, true, true, true };
    const EspDrawStats stats = EspDraw(frame, 100, 100);
    Expect(stats.considered == 6 && stats.local == 1 && stats.dead == 1 &&
           stats.invalidDistance == 1 && stats.offscreen == 1 &&
           stats.projected == 3 && stats.drawn == 3,
           "unlimited range; invalid distance does not hide valid player geometry");

    ImGui::Render();
    Expect(ImGui::GetDrawData()->TotalVtxCount > 0, "ImGui primitives submitted");
    AppearanceChecks();
    ItemDrawingChecks();
    ImGui::DestroyContext();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = ImGui::GetIO().LogFilename = nullptr;
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    // A fresh atlas with no optional fonts must fall back without stale pointers.
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    EspSettings missing;
    missing.nameFont = 3;
    Expect(!EspFontAvailable(3) && DrawSample(missing).stats.drawn == 1,
           "context recreation with missing font safely falls back to the current menu font");
    ImGui::DestroyContext();
    return failures ? 1 : 0;
}
