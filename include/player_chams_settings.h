#pragma once

enum class PlayerChamsStyle { Flat, Iridescent };

struct PlayerChamsSettings {
    bool enabled = false;
    bool teammates = false;
    bool localPlayer = false;
    PlayerChamsStyle style = PlayerChamsStyle::Flat;
    float color[4]{0.4f, 0.7f, 1.0f, 1.0f}; // Flat style RGBA.
    float iridescentColor[4]{1.0f, 0.3f, 0.8f, 1.0f}; // Iridescent style RGBA tint.

    bool operator==(const PlayerChamsSettings&) const = default;
};

PlayerChamsSettings PlayerChamsSanitizeSettings(const PlayerChamsSettings& settings) noexcept;
PlayerChamsSettings PlayerChamsGetSettings() noexcept;
void PlayerChamsSetSettings(const PlayerChamsSettings& settings) noexcept;
void PlayerChamsResetSettings() noexcept;
