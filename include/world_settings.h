#pragma once
#include <array>
#include <string_view>

struct WorldSettings {
    // Screen-wide grading, including the game's HUD. Disabled by default.
    bool enabled = false;
    float exposureEv = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float gamma = 1.0f;
    float tint[3]{1.0f, 1.0f, 1.0f};
    float vignette = 0.0f;
    float sharpening = 0.0f;
    // Separate engine sky controls; the screen pass ignores these fields.
    bool skyEnabled = false;
    float skyTint[3]{1.0f, 1.0f, 1.0f};
    float skyLightingTint[3]{1.0f, 1.0f, 1.0f};
    float skyBrightness = 1.0f;
    bool customSkyboxEnabled = false;
    std::array<char, 256> skyboxMaterial{}; // Logical .vmat or cubemap .vtex resource.
    bool worldMaterialEnabled = false;
    bool flatTextures = false;
    // Native lighting controls, independent of sky and screen grading.
    bool lightingEnabled = false;
    bool fullbrightEnabled = false;
    bool disableLocalLights = false;
    float localLightTint[3]{1.0f, 1.0f, 1.0f};
    float localLightBrightness = 1.0f;
    float sunLightTint[3]{1.0f, 1.0f, 1.0f};
    float sunLightBrightness = 1.0f;
    bool ambientLightEnabled = false;
    float ambientLightTint[3]{1.0f, 1.0f, 1.0f};
    float ambientLightBrightness = 0.25f; // Added scene-linear diffuse fill.
    int directionalLightmapsMode = 0; // Game value / enabled / disabled.
};

WorldSettings WorldSanitizeSettings(const WorldSettings& settings) noexcept;
bool WorldSetSkyboxMaterial(WorldSettings& settings, std::string_view path) noexcept;
WorldSettings WorldGetSettings() noexcept;
void WorldSetSettings(const WorldSettings& settings) noexcept;
void WorldResetSettings() noexcept;
void WorldResetLightingSettings() noexcept;
using WorldSkyboxStatus = std::array<char, 192>;
WorldSkyboxStatus WorldSkyboxRuntimeStatus() noexcept;
void WorldSkyboxSetRuntimeStatus(const char* message) noexcept;

