#pragma once

struct ThirdPersonSettings {
    bool enabled = false;
    float distance = 120.0f;
    float shoulder = 0.0f;
    float height = 0.0f;
    bool collision = true;

    bool operator==(const ThirdPersonSettings&) const = default;
};

ThirdPersonSettings ThirdPersonSanitizeSettings(const ThirdPersonSettings& settings) noexcept;
ThirdPersonSettings ThirdPersonGetSettings() noexcept;
void ThirdPersonSetSettings(const ThirdPersonSettings& settings) noexcept;
void ThirdPersonResetSettings() noexcept;
