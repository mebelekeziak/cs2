#pragma once

enum class AimHitbox { Head, Chest, Stomach, Pelvis };

struct SilentAimSettings {
    bool enabled = false;
    float fov = 5;
    AimHitbox hitbox = AimHitbox::Chest;
    bool autofire = false;
    float hitchance = 50;
    int samples = 256;

    bool operator==(const SilentAimSettings&) const = default;
};

// Render owner publishes one atomic snapshot; the command owner consumes it.
SilentAimSettings SilentAimGetSettings() noexcept;
void SilentAimSetSettings(const SilentAimSettings& settings) noexcept;
void SilentAimPollControls(bool foreground) noexcept;
