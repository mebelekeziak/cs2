#pragma once

enum class AimHitbox { Head, Chest, Stomach, Pelvis };

struct SilentAimSettings {
    bool enabled = false;
    float fov = 5;
    AimHitbox hitbox = AimHitbox::Chest;
};

// Render owner publishes one atomic snapshot; the command owner consumes it.
SilentAimSettings SilentAimGetSettings() noexcept;
void SilentAimSetSettings(const SilentAimSettings& settings) noexcept;
void SilentAimPollControls(bool foreground) noexcept;
