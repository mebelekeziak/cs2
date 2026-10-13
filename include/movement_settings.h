#pragma once

struct MovementSettings {
    bool bunnyhop = true;
    bool autostrafe = true;
};
using MovementLogCallback = void (*)(const char* message);

// The menu/render owner updates one atomic snapshot; the command owner reads it.
MovementSettings MovementGetSettings() noexcept;
void MovementSetSettings(const MovementSettings& settings) noexcept;
void MovementSetLogCallback(MovementLogCallback callback) noexcept;
void MovementPollControls(bool foreground) noexcept;
