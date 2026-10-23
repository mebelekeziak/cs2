#include "player_chams_settings.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
std::mutex settingsMutex;
PlayerChamsSettings currentSettings;
}

PlayerChamsSettings PlayerChamsSanitizeSettings(const PlayerChamsSettings& settings) noexcept {
    auto result = settings;
    const PlayerChamsSettings defaults;
    if (result.style != PlayerChamsStyle::Flat && result.style != PlayerChamsStyle::Iridescent &&
        result.style != PlayerChamsStyle::Fresnel)
        result.style = defaults.style;
    for (int channel = 0; channel < 4; ++channel) {
        result.color[channel] = std::isfinite(result.color[channel])
            ? std::clamp(result.color[channel], 0.0f, 1.0f) : defaults.color[channel];
        result.iridescentColor[channel] = std::isfinite(result.iridescentColor[channel])
            ? std::clamp(result.iridescentColor[channel], 0.0f, 1.0f) : defaults.iridescentColor[channel];
        result.fresnelColor[channel] = std::isfinite(result.fresnelColor[channel])
            ? std::clamp(result.fresnelColor[channel], 0.0f, 1.0f) : defaults.fresnelColor[channel];
    }
    return result;
}

PlayerChamsSettings PlayerChamsGetSettings() noexcept {
    std::lock_guard<std::mutex> lock(settingsMutex);
    return currentSettings;
}

void PlayerChamsSetSettings(const PlayerChamsSettings& settings) noexcept {
    const auto sanitized = PlayerChamsSanitizeSettings(settings);
    std::lock_guard<std::mutex> lock(settingsMutex);
    currentSettings = sanitized;
}

void PlayerChamsResetSettings() noexcept { PlayerChamsSetSettings({}); }
