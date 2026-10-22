#pragma once
#include "esp_draw.h"
#include "silent_aim_settings.h"
#include <array>
#include <cstdint>

struct AimVisualizationSettings {
    bool enabled = false;
    float color[4] = {1, 1, 1, 1};
    bool operator==(const AimVisualizationSettings&) const = default;
};
struct AimVisualizationPoint {
    EspVec3 center{};
    std::uint32_t pawn = UINT32_MAX;
};
// Damage-path centers before hitchance/fire timing. No engine pointers reach rendering.
struct AimVisualizationFrame {
    std::array<AimVisualizationPoint, 128 * 18> points{};
    std::size_t count = 0;
    std::uint32_t localPawn = UINT32_MAX;
    std::uint64_t generation = 0, capturedMs = 0;
    SilentAimSettings criteria{};
};
inline constexpr std::uint64_t AimVisualizationLifetimeMs = 100;
AimVisualizationSettings AimVisualizationGetSettings() noexcept;
void AimVisualizationSetSettings(const AimVisualizationSettings& value) noexcept;
// Begin/end on the command owner; centers come from the existing target scan.
bool AimVisualizationBegin(AimVisualizationFrame& frame) noexcept;
void AimVisualizationPublish(const AimVisualizationFrame& frame, std::uint64_t now) noexcept;
void AimVisualizationClear() noexcept;
AimVisualizationFrame AimVisualizationCapture(std::uint64_t now, const SilentAimSettings* criteria = nullptr) noexcept;
// Pure projection/drawing from copied data, between ImGui NewFrame and Render.
std::size_t AimVisualizationDraw(const AimVisualizationFrame& points, const EspFrameSnapshot& players,
    float width, float height, const AimVisualizationSettings& settings);
