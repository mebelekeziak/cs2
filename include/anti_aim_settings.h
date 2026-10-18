#pragma once

enum class AntiAimYawMode { Static, Jitter, Spin, Random };
enum class AntiAimYawBase { View, World };
enum class AntiAimPitchMode { Down, Up, Level, Keep, Custom, Jitter };

struct AntiAimSettings {
    bool enabled = false;
    AntiAimYawMode yawMode = AntiAimYawMode::Static;
    AntiAimYawBase yawBase = AntiAimYawBase::View;
    float yawOffset = 180.0f;
    float jitterRange = 60.0f; // Total width: 60 means +/-30 degrees.
    int jitterInterval = 8; // Commands per jitter side or random sample.
    float spinSpeed = 10.0f; // Signed degrees per command.
    AntiAimPitchMode pitchMode = AntiAimPitchMode::Down;
    float pitch = 89.0f;
    float pitchAlternate = -89.0f;
    bool operator==(const AntiAimSettings&) const noexcept = default;
};

// Render owner publishes one normalized snapshot; command owner consumes it.
AntiAimSettings AntiAimNormalizeSettings(const AntiAimSettings& settings) noexcept;
AntiAimSettings AntiAimGetSettings() noexcept;
void AntiAimSetSettings(const AntiAimSettings& settings) noexcept;
void AntiAimResetSettings() noexcept;
