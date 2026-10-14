#pragma once

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
    bool worldMaterialEnabled = false;
    float worldTint[3]{1.0f, 1.0f, 1.0f};
    bool flatTextures = false;
    // Native lighting controls, independent of sky and screen grading.
    bool lightingEnabled = false;
    bool disableLocalLights = false;
    float localLightTint[3]{1.0f, 1.0f, 1.0f};
    float localLightBrightness = 1.0f;
    float sunLightTint[3]{1.0f, 1.0f, 1.0f};
    float sunLightBrightness = 1.0f;
    int directionalLightmapsMode = 0; // Game value / enabled / disabled.
};

WorldSettings WorldSanitizeSettings(const WorldSettings& settings) noexcept;
WorldSettings WorldGetSettings() noexcept;
void WorldSetSettings(const WorldSettings& settings) noexcept;
void WorldResetSettings() noexcept;
void WorldResetLightingSettings() noexcept;

