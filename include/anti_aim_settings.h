#pragma once

struct AntiAimSettings {
    bool enabled = false;
};

// Render owner publishes the setting; the command owner consumes it.
AntiAimSettings AntiAimGetSettings() noexcept;
void AntiAimSetSettings(const AntiAimSettings& settings) noexcept;
