#include "anti_aim_settings.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
std::mutex settingsMutex;
AntiAimSettings currentSettings;
float Clamp(float value, float minimum, float maximum, float fallback) noexcept {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}
template<class T> T Mode(T value, T last, T fallback) noexcept {
    return static_cast<int>(value) >= 0 && static_cast<int>(value) <= static_cast<int>(last) ? value : fallback;
}
}

AntiAimSettings AntiAimNormalizeSettings(const AntiAimSettings& settings) noexcept {
    auto result = settings;
    const AntiAimSettings defaults;
    result.yawMode = Mode(result.yawMode, AntiAimYawMode::Random, defaults.yawMode);
    result.yawBase = Mode(result.yawBase, AntiAimYawBase::World, defaults.yawBase);
    result.yawOffset = Clamp(result.yawOffset, -180, 180, defaults.yawOffset);
    result.jitterRange = Clamp(result.jitterRange, 0, 360, defaults.jitterRange);
    result.jitterInterval = std::clamp(result.jitterInterval, 1, 64);
    result.spinSpeed = Clamp(result.spinSpeed, -180, 180, defaults.spinSpeed);
    result.pitchMode = Mode(result.pitchMode, AntiAimPitchMode::Jitter, defaults.pitchMode);
    result.pitch = Clamp(result.pitch, -89, 89, defaults.pitch);
    result.pitchAlternate = Clamp(result.pitchAlternate, -89, 89, defaults.pitchAlternate);
    return result;
}

AntiAimSettings AntiAimGetSettings() noexcept {
    std::lock_guard<std::mutex> lock(settingsMutex);
    return currentSettings;
}

void AntiAimSetSettings(const AntiAimSettings& settings) noexcept {
    const auto normalized = AntiAimNormalizeSettings(settings);
    std::lock_guard<std::mutex> lock(settingsMutex);
    currentSettings = normalized;
}

void AntiAimResetSettings() noexcept { AntiAimSetSettings({}); }
