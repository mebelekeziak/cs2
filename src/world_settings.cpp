#include "world_settings.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
std::mutex settingsMutex;
WorldSettings currentSettings;
WorldSkyboxStatus skyboxRuntimeStatus{};
float Clamp(float value, float minimum, float maximum, float fallback) noexcept {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}
}

bool WorldSetSkyboxMaterial(WorldSettings& settings, std::string_view path) noexcept {
    std::array<char, 256> normalized{};
    if (path.size() >= normalized.size()) return false;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const unsigned char value = static_cast<unsigned char>(path[i]);
        // Resource paths must also roundtrip through the line-based config.
        if (value < 32 || value >= 127 || path[i] == ':' || path[i] == '*' || path[i] == '?' ||
            path[i] == '"' || path[i] == '<' || path[i] == '>' || path[i] == '|' ||
            path[i] == '=' || path[i] == ';' || path[i] == '#') return false;
        normalized[i] = path[i] == '\\' ? '/' :
            path[i] >= 'A' && path[i] <= 'Z' ? static_cast<char>(path[i] - 'A' + 'a') : path[i];
    }
    std::string_view resource(normalized.data(), path.size());
    if (resource.ends_with(".vmat_c") || resource.ends_with(".vtex_c")) {
        normalized[resource.size() - 2] = '\0';
        normalized[resource.size() - 1] = '\0';
        resource.remove_suffix(2);
    }
    if (!resource.empty()) {
        if (!resource.starts_with("materials/") ||
            (!resource.ends_with(".vmat") && !resource.ends_with(".vtex"))) return false;
        std::size_t start = 0;
        while (start < resource.size()) {
            const auto slash = resource.find('/', start);
            const auto part = resource.substr(start, slash == resource.npos ? resource.size() - start : slash - start);
            if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
            if (slash == resource.npos) {
                if (part.size() <= 5) return false;
                break;
            }
            start = slash + 1;
        }
    }
    settings.skyboxMaterial = normalized;
    return true;
}

WorldSettings WorldSanitizeSettings(const WorldSettings& settings) noexcept {
    WorldSettings result = settings;
    const auto end = std::find(settings.skyboxMaterial.begin(), settings.skyboxMaterial.end(), '\0');
    if (end == settings.skyboxMaterial.end() || !WorldSetSkyboxMaterial(result,
            {settings.skyboxMaterial.data(), static_cast<std::size_t>(end - settings.skyboxMaterial.begin())})) {
        result.customSkyboxEnabled = false;
        result.skyboxMaterial = {};
    }
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
        result.ambientLightTint[channel] = Clamp(result.ambientLightTint[channel], 0.0f, 1.0f, 1.0f);
    }
    result.vignette = Clamp(result.vignette, 0.0f, 1.0f, 0.0f);
    result.sharpening = Clamp(result.sharpening, 0.0f, 1.0f, 0.0f);
    result.skyBrightness = Clamp(result.skyBrightness, 0.0f, 4.0f, 1.0f);
    result.localLightBrightness = Clamp(result.localLightBrightness, 0.0f, 8.0f, 1.0f);
    result.sunLightBrightness = Clamp(result.sunLightBrightness, 0.0f, 8.0f, 1.0f);
    result.ambientLightBrightness = Clamp(result.ambientLightBrightness, 0.0f, 4.0f, 0.25f);
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
WorldSkyboxStatus WorldSkyboxRuntimeStatus() noexcept {
    std::lock_guard<std::mutex> lock(settingsMutex);
    if (skyboxRuntimeStatus[0]) return skyboxRuntimeStatus;
    WorldSkyboxStatus result{};
    constexpr char prompt[] = "Choose a skybox to replace the map sky.";
    std::copy_n(prompt, sizeof(prompt), result.begin());
    return result;
}
void WorldSkyboxSetRuntimeStatus(const char* message) noexcept {
    WorldSkyboxStatus copied{};
    if (message) {
        for (std::size_t i = 0; i + 1 < copied.size() && message[i]; ++i) copied[i] = message[i];
    }
    std::lock_guard<std::mutex> lock(settingsMutex);
    skyboxRuntimeStatus = copied;
}
void WorldResetLightingSettings() noexcept {
    std::lock_guard<std::mutex> lock(settingsMutex);
    currentSettings.lightingEnabled = false;
    currentSettings.fullbrightEnabled = false;
    currentSettings.disableLocalLights = false;
    for (float& channel : currentSettings.localLightTint) channel = 1.0f;
    currentSettings.localLightBrightness = 1.0f;
    for (float& channel : currentSettings.sunLightTint) channel = 1.0f;
    currentSettings.sunLightBrightness = 1.0f;
    currentSettings.ambientLightEnabled = false;
    for (float& channel : currentSettings.ambientLightTint) channel = 1.0f;
    currentSettings.ambientLightBrightness = 0.25f;
    currentSettings.directionalLightmapsMode = 0;
}

