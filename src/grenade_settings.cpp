#include "grenade_settings.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
std::mutex settingsMutex;
GrenadeSettings currentSettings;
}

GrenadeSettings GrenadeSanitizeSettings(const GrenadeSettings& settings) noexcept {
    auto result = settings;
    const GrenadeSettings defaults;
    for (int channel = 0; channel < 4; ++channel) {
        result.smokeColor[channel] = std::isfinite(result.smokeColor[channel])
            ? std::clamp(result.smokeColor[channel], 0.0f, 1.0f) : defaults.smokeColor[channel];
        result.molotovColor[channel] = std::isfinite(result.molotovColor[channel])
            ? std::clamp(result.molotovColor[channel], 0.0f, 1.0f) : defaults.molotovColor[channel];
        result.predictionColor[channel] = std::isfinite(result.predictionColor[channel])
            ? std::clamp(result.predictionColor[channel], 0.0f, 1.0f) : defaults.predictionColor[channel];
        result.enemyPredictionColor[channel] = std::isfinite(result.enemyPredictionColor[channel])
            ? std::clamp(result.enemyPredictionColor[channel], 0.0f, 1.0f) : defaults.enemyPredictionColor[channel];
    }
    result.auraWidth = std::isfinite(result.auraWidth)
        ? std::clamp(result.auraWidth, 1.0f, 6.0f) : defaults.auraWidth;
    result.predictionWidth = std::isfinite(result.predictionWidth)
        ? std::clamp(result.predictionWidth, 1.0f, 6.0f) : defaults.predictionWidth;
    return result;
}

GrenadeSettings GrenadeGetSettings() noexcept {
    std::lock_guard<std::mutex> lock(settingsMutex);
    return currentSettings;
}

void GrenadeSetSettings(const GrenadeSettings& settings) noexcept {
    const auto sanitized = GrenadeSanitizeSettings(settings);
    std::lock_guard<std::mutex> lock(settingsMutex);
    currentSettings = sanitized;
}

void GrenadeResetSettings() noexcept { GrenadeSetSettings({}); }
