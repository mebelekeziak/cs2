#include "world_settings.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
std::mutex settingsMutex;
WorldSettings currentSettings;
float Clamp(float value, float minimum, float maximum, float fallback) noexcept {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}
}

WorldSettings WorldSanitizeSettings(const WorldSettings& settings) noexcept {
    WorldSettings result = settings;
    result.exposureEv = Clamp(result.exposureEv, -3.0f, 3.0f, 0.0f);
    result.contrast = Clamp(result.contrast, 0.5f, 1.5f, 1.0f);
    result.saturation = Clamp(result.saturation, 0.0f, 2.0f, 1.0f);
    result.gamma = Clamp(result.gamma, 0.5f, 2.0f, 1.0f);
    for (unsigned channel = 0; channel < 3; ++channel) {
        result.tint[channel] = Clamp(result.tint[channel], 0.0f, 1.0f, 1.0f);
        result.skyTint[channel] = Clamp(result.skyTint[channel], 0.0f, 1.0f, 1.0f);
        result.skyLightingTint[channel] = Clamp(result.skyLightingTint[channel], 0.0f, 1.0f, 1.0f);
        result.worldTint[channel] = Clamp(result.worldTint[channel], 0.0f, 1.0f, 1.0f);
    }
    result.vignette = Clamp(result.vignette, 0.0f, 1.0f, 0.0f);
    result.sharpening = Clamp(result.sharpening, 0.0f, 1.0f, 0.0f);
    result.skyBrightness = Clamp(result.skyBrightness, 0.0f, 4.0f, 1.0f);
    return result;
}

WorldSettings WorldGetSettings() noexcept {
    std::lock_guard<std::mutex> lock(settingsMutex);
    return currentSettings;
}

void WorldSetSettings(const WorldSettings& settings) noexcept {
    const WorldSettings sanitized = WorldSanitizeSettings(settings);
    std::lock_guard<std::mutex> lock(settingsMutex);
    currentSettings = sanitized;
}

void WorldResetSettings() noexcept { WorldSetSettings(WorldSettings{}); }

