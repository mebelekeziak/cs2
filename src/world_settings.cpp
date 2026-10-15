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
        result.localLightTint[channel] = Clamp(result.localLightTint[channel], 0.0f, 1.0f, 1.0f);
        result.sunLightTint[channel] = Clamp(result.sunLightTint[channel], 0.0f, 1.0f, 1.0f);
    }
    result.vignette = Clamp(result.vignette, 0.0f, 1.0f, 0.0f);
    result.sharpening = Clamp(result.sharpening, 0.0f, 1.0f, 0.0f);
    result.skyBrightness = Clamp(result.skyBrightness, 0.0f, 4.0f, 1.0f);
    result.localLightBrightness = Clamp(result.localLightBrightness, 0.0f, 8.0f, 1.0f);
    result.sunLightBrightness = Clamp(result.sunLightBrightness, 0.0f, 8.0f, 1.0f);
    result.directionalLightmapsMode = std::clamp(result.directionalLightmapsMode, 0, 2);
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
void WorldResetLightingSettings() noexcept {
    std::lock_guard<std::mutex> lock(settingsMutex);
    currentSettings.lightingEnabled = false;
    currentSettings.disableLocalLights = false;
    for (float& channel : currentSettings.localLightTint) channel = 1.0f;
    currentSettings.localLightBrightness = 1.0f;
    for (float& channel : currentSettings.sunLightTint) channel = 1.0f;
    currentSettings.sunLightBrightness = 1.0f;
    currentSettings.directionalLightmapsMode = 0;
}

