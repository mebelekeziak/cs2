#pragma once

#include <cstdint>

// All inputs are copied values; this module never reads game objects.
// The caller establishes that the local player is alive, scoped with a scoped
// weapon, with an applied third-person camera or a replaced first-person scope.
struct ScopedCrosshairDrawInput {
    bool visible = false;
    float spread = 0;
    float inaccuracy = 0;
    std::uint64_t sourceKey = 0; // Nonzero identity of the local pawn + weapon.
};

struct ScopedCrosshairState {
    std::uint64_t sourceKey = 0;
    float extraGap = 0; // Reference pixels at 1080p, always in [0, 3].
    bool active = false;
};

struct ScopedCrosshairLayout {
    float centerX = 0;
    float centerY = 0;
    float scale = 0;
    float gap = 0;
    float radius = 0;
};

// Pure copied-data calculation; no ImGui or engine access. Hidden/invalid input
// clears both state and output immediately. Identity changes start at the new
// weapon's current gap, without blending in the previous weapon's spread.
bool ScopedCrosshairBuildLayout(const ScopedCrosshairDrawInput& input,
    float width, float height, float deltaTime, ScopedCrosshairState& state,
    ScopedCrosshairLayout& output) noexcept;

// Render-owner thread only, between ImGui::NewFrame and ImGui::Render.
bool ScopedCrosshairDraw(const ScopedCrosshairDrawInput& input,
    float width, float height, float deltaTime);
void ScopedCrosshairReset() noexcept;
