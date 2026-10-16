#include "third_person_settings.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
std::mutex settingsMutex;
ThirdPersonSettings currentSettings;
float Clamp(float value, float minimum, float maximum, float fallback) noexcept {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}
}

ThirdPersonSettings ThirdPersonSanitizeSettings(const ThirdPersonSettings& settings) noexcept {
    ThirdPersonSettings result = settings;
    result.distance = Clamp(result.distance, 30.0f, 300.0f, 120.0f);
    result.shoulder = Clamp(result.shoulder, -60.0f, 60.0f, 0.0f);
    result.height = Clamp(result.height, -60.0f, 60.0f, 0.0f);
    return result;
}

ThirdPersonSettings ThirdPersonGetSettings() noexcept {
    std::lock_guard<std::mutex> lock(settingsMutex);
    return currentSettings;
}

void ThirdPersonSetSettings(const ThirdPersonSettings& settings) noexcept {
    const auto sanitized = ThirdPersonSanitizeSettings(settings);
    std::lock_guard<std::mutex> lock(settingsMutex);
    currentSettings = sanitized;
}

void ThirdPersonResetSettings() noexcept { ThirdPersonSetSettings(ThirdPersonSettings{}); }
