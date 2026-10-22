#include "aim_visualization.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>

namespace {
SRWLOCK lock = SRWLOCK_INIT;
AimVisualizationSettings settings{};
AimVisualizationFrame published{};
std::uint64_t generation = 1;
}
AimVisualizationSettings AimVisualizationGetSettings() noexcept {
    AcquireSRWLockShared(&lock);
    const auto result = settings;
    ReleaseSRWLockShared(&lock);
    return result;
}
void AimVisualizationSetSettings(const AimVisualizationSettings& value) noexcept {
    auto bounded = value;
    for (auto& channel : bounded.color)
        channel = std::isfinite(channel) ? std::clamp(channel, 0.0f, 1.0f) : 1.0f;
    AcquireSRWLockExclusive(&lock);
    if (!(settings == bounded)) {
        settings = bounded;
        ++generation;
        published.count = 0;
    }
    ReleaseSRWLockExclusive(&lock);
}
bool AimVisualizationBegin(AimVisualizationFrame& frame) noexcept {
    frame.count = 0;
    frame.localPawn = UINT32_MAX;
    AcquireSRWLockShared(&lock);
    frame.generation = generation;
    const bool enabled = settings.enabled;
    ReleaseSRWLockShared(&lock);
    return enabled;
}
void AimVisualizationPublish(const AimVisualizationFrame& frame, std::uint64_t now) noexcept {
    AcquireSRWLockExclusive(&lock);
    if (settings.enabled && frame.generation == generation && frame.count <= frame.points.size()) {
        published = frame;
        published.capturedMs = now;
    }
    ReleaseSRWLockExclusive(&lock);
}
void AimVisualizationClear() noexcept {
    AcquireSRWLockExclusive(&lock);
    published.count = 0;
    ++generation; // Also invalidate any in-flight scan at cooperative stop.
    ReleaseSRWLockExclusive(&lock);
}
AimVisualizationFrame AimVisualizationCapture(std::uint64_t now, const SilentAimSettings* criteria) noexcept {
    AimVisualizationFrame result{};
    AcquireSRWLockShared(&lock);
    if (settings.enabled && published.generation == generation && now >= published.capturedMs &&
        now - published.capturedMs <= AimVisualizationLifetimeMs &&
        (!criteria || published.criteria == *criteria)) result = published;
    ReleaseSRWLockShared(&lock);
    return result;
}
