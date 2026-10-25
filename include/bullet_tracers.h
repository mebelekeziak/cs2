#pragma once
#include "esp_draw.h"
#include "shot_events.h"
#include <array>
#include <cstdint>

enum class BulletTracerMaterial { Solid, Laser, Plasma };
struct BulletTracerSettings {
    bool enabled = false;
    BulletTracerMaterial material = BulletTracerMaterial::Laser;
    float color[4] = {0.3f, 0.75f, 1, 1};
    bool glow = true;
    float glowStrength = 0.6f;
    float duration = 1.5f;
    float width = 2;
    bool fade = true;
    bool impactMarker = false;
    // Render-only camera-right/down shift from the saved eye; approximate muzzle.
    float originOffset = 6;
    bool operator==(const BulletTracerSettings&) const = default;
};
BulletTracerSettings BulletTracerSanitizeSettings(const BulletTracerSettings& value) noexcept;
BulletTracerSettings BulletTracerGetSettings() noexcept;
void BulletTracerSetSettings(const BulletTracerSettings& value) noexcept;
void BulletTracerResetSettings() noexcept;

struct BulletTracerSegment {
    EspVec3 start{}, end{};
    std::uint64_t receiptMs = 0;
};
struct BulletTracerFrame {
    std::array<BulletTracerSegment, 128> segments{};
    std::size_t count = 0;
    std::uint32_t pawn = UINT32_MAX;
};
// Engine-owner inputs are copied; render callbacks never read native objects.
// Source changes/invalidations clear all pending fire and impact state.
void BulletTracerSource(std::uint64_t generation, std::uint32_t pawn,
    EspVec3 eye, bool valid, std::uint64_t now) noexcept;
void BulletTracerEvent(const ShotNativeEvent& event) noexcept;
void BulletTracerClear() noexcept;
BulletTracerFrame BulletTracerCapture(std::uint64_t now) noexcept;
std::size_t BulletTracerDraw(const BulletTracerFrame& frame, const EspFrameSnapshot& players,
    float width, float height, std::uint64_t now, const BulletTracerSettings& settings);
void BulletTracerDrawPreview(float width, float height, const BulletTracerSettings& settings);
