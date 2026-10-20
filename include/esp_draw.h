#pragma once

#include <cstddef>
#include <cstdint>

// All inputs are copied values. This module never dereferences game objects.
struct EspVec3 { float x = 0, y = 0, z = 0; };
struct EspMatrix4x4 { float m[4][4]{}; };
struct EspScreenRect { float left = 0, top = 0, right = 0, bottom = 0; };

struct EspPlayerSnapshot {
    std::uint32_t handle = 0;
    char name[128]{};
    int team = 0;
    int health = 0;
    int armor = 0;
    bool localPlayer = false;
    float distanceMeters = 0;
    // Corner index: bit 0 selects X max, bit 1 Y max, bit 2 Z max.
    // Transform collision bounds into world space before passing this snapshot.
    EspVec3 worldCorners[8]{};
};

struct EspFrameSnapshot {
    EspMatrix4x4 worldToClip{}; // Row-major: clip = matrix * [world, 1].
    const EspPlayerSnapshot* players = nullptr;
    std::size_t playerCount = 0;
    int localTeam = 0;
    bool valid = false;
    bool matrixValid = false;
    bool distanceAvailable = false;
};

struct EspSettings {
    bool enabled = true;
    bool names = true;
    bool boxes = true;
    bool distance = true;
    bool health = true;
    bool teammates = true;
    bool armor = false;
    bool teamColors = true;
    float boxColor[4] = { 86 / 255.0f, 190 / 255.0f, 1, 1 };
    float tColor[4] = { 1, 192 / 255.0f, 72 / 255.0f, 1 };
    float ctColor[4] = { 86 / 255.0f, 190 / 255.0f, 1, 1 };
    int boxStyle = 0; // 0 = full rectangle, 1 = corners.
    float boxThickness = 1;
    bool boxOutline = true;
    bool boxFill = false;
    float fillColor[4] = { 86 / 255.0f, 190 / 255.0f, 1, 0.12f };
    bool nameUseBoxColor = true;
    float nameColor[4] = { 1, 1, 1, 1 };
    int nameFont = 0; // 0 = menu font, 1 = Segoe UI Bold, 2 = Tahoma, 3 = Consolas.
    float nameSize = 14;
    float detailSize = 13;
    float detailColor[4] = { 1, 1, 1, 1 };
    bool textOutline = true;
    float outlineColor[4] = { 0, 0, 0, 0.94f };
    float healthColor[4] = { 80 / 255.0f, 225 / 255.0f, 100 / 255.0f, 1 };
    bool healthGradient = false;
    float healthLowColor[4] = { 1, 0.2f, 0.2f, 1 };
    float healthBarWidth = 3;
    bool healthText = true;
    bool operator==(const EspSettings&) const = default;
};

enum class EspProjectionResult {
    Visible, InvalidInput, BehindCamera, Offscreen, Degenerate
};

struct EspDrawStats {
    std::size_t considered = 0;
    std::size_t local = 0;
    std::size_t dead = 0;
    std::size_t teammates = 0;
    std::size_t invalidDistance = 0;
    std::size_t invalidProjection = 0;
    std::size_t behindCamera = 0;
    std::size_t offscreen = 0;
    std::size_t degenerate = 0;
    std::size_t projected = 0;
    std::size_t drawn = 0;
    bool sourceValid = false;
    bool matrixValid = false;
    bool distanceAvailable = false;
    bool enabled = false;
};

using EspLogCallback = void (*)(const char* message);

// Pure math; validates finite input, clips edges crossing the camera plane,
// and intersects the projected rectangle with the viewport. Does not use ImGui.
EspProjectionResult EspProjectBox(const EspVec3 (&corners)[8], const EspMatrix4x4& matrix,
                                 float width, float height, EspScreenRect& output) noexcept;

// Call on the render owner thread only, outside DllMain/loader lock.
void EspSetLogCallback(EspLogCallback callback) noexcept;
const EspSettings& EspGetSettings() noexcept;
EspSettings EspSanitizeSettings(const EspSettings& value) noexcept;
void EspSetSettings(const EspSettings& value) noexcept;
void EspResetStyle() noexcept; // Preserve feature toggles, reset appearance only.
// Once per ImGui atlas, before building its GPU texture; never during a frame.
// Fonts are read from Windows, owned by the atlas, with safe menu-font fallback.
void EspConfigureFonts();
bool EspFontAvailable(int font) noexcept;
// Reserve and draw a synthetic player using the same renderer as live ESP.
void EspDrawPreview(float width, float height);
// Returns false for keys not assigned to ESP (including the camera's F6).
// Call on the render owner thread for a newly pressed Windows virtual key.
bool EspHandleHotkey(int virtualKey) noexcept;
void EspPollControls(bool foreground) noexcept;
// Call between ImGui::NewFrame and ImGui::Render, regardless of menu visibility.
EspDrawStats EspDraw(const EspFrameSnapshot& frame, float width, float height);
