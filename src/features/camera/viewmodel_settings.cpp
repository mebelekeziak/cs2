#include "viewmodel_settings.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
std::mutex settingsMutex;
ViewmodelSettings currentSettings;
float Offset(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, -10.0f, 10.0f) : 0.0f;
}
}

ViewmodelSettings ViewmodelSanitizeSettings(const ViewmodelSettings& settings) noexcept {
    auto result = settings;
    result.x = Offset(result.x);
    result.y = Offset(result.y);
    result.z = Offset(result.z);
    result.fov = std::isfinite(result.fov) ? std::clamp(result.fov, 40.0f, 120.0f) : 68.0f;
    return result;
}

ViewmodelSettings ViewmodelGetSettings() noexcept {
    std::lock_guard<std::mutex> lock(settingsMutex);
    return currentSettings;
}

void ViewmodelSetSettings(const ViewmodelSettings& settings) noexcept {
    const auto sanitized = ViewmodelSanitizeSettings(settings);
    std::lock_guard<std::mutex> lock(settingsMutex);
    currentSettings = sanitized;
}

void ViewmodelResetSettings() noexcept { ViewmodelSetSettings({}); }
