#pragma once

struct GrenadeSettings {
    bool removeSmoke = false;
    bool removeFlash = false;
    bool smokeAura = false;
    bool molotovAura = false;
    float smokeColor[4]{0.35f, 0.75f, 1.0f, 1.0f};
    float molotovColor[4]{1.0f, 0.35f, 0.08f, 1.0f};
    float auraWidth = 2.0f;

    bool operator==(const GrenadeSettings&) const = default;
};

GrenadeSettings GrenadeSanitizeSettings(const GrenadeSettings& settings) noexcept;
GrenadeSettings GrenadeGetSettings() noexcept;
void GrenadeSetSettings(const GrenadeSettings& settings) noexcept;
void GrenadeResetSettings() noexcept;
