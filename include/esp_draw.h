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
    float maxDistanceMeters = 150;
};

enum class EspProjectionResult {
    Visible, InvalidInput, BehindCamera, Offscreen, Degenerate
};

struct EspDrawStats {
    std::size_t considered = 0;
    std::size_t local = 0;
    std::size_t dead = 0;
    std::size_t teammates = 0;
    std::size_t distanceFiltered = 0;
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
void EspPollControls(bool foreground) noexcept;
// Call between ImGui::NewFrame and ImGui::Render, regardless of menu visibility.
EspDrawStats EspDraw(const EspFrameSnapshot& frame, float width, float height);
