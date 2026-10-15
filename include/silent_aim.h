#pragma once

using SilentAimLogCallback = void (*)(const char*);
// Startup and cooperative stop run outside DllMain. Hooks remain resident.
bool SilentAimPrepare(SilentAimLogCallback callback) noexcept;
void SilentAimActivate() noexcept;
void SilentAimStop(const char* reason) noexcept;
const char* SilentAimStatus() noexcept;
