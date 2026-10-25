#pragma once

struct ShotLogSettings {
    bool enabled = true;
    bool verbose = false;
    bool operator==(const ShotLogSettings&) const = default;
};
ShotLogSettings ShotLogGetSettings() noexcept;
void ShotLogSetSettings(const ShotLogSettings& settings) noexcept;
// Returns the console body, stripping only the internal telemetry prefix.
// Command attempts without native fire confirmation follow verbose diagnostics.
// Null suppresses console output; the caller still writes the session file.
const char* ShotLogConsoleMessage(const char* message, const ShotLogSettings& settings) noexcept;
