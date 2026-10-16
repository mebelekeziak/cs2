#include "zoom_settings.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
std::mutex settingsMutex;
ZoomSettings currentSettings;
float Clamp(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 200.0f) : 100.0f;
}
}

ZoomSettings ZoomSanitizeSettings(const ZoomSettings& settings) noexcept {
    ZoomSettings result = settings;
    result.zoom1 = Clamp(result.zoom1);
    result.zoom2 = Clamp(result.zoom2);
    return result;
}

ZoomSettings ZoomGetSettings() noexcept {
    std::lock_guard<std::mutex> lock(settingsMutex);
    return currentSettings;
}

void ZoomSetSettings(const ZoomSettings& settings) noexcept {
    const auto sanitized = ZoomSanitizeSettings(settings);
    std::lock_guard<std::mutex> lock(settingsMutex);
    currentSettings = sanitized;
}

void ZoomResetSettings() noexcept { ZoomSetSettings(ZoomSettings{}); }
