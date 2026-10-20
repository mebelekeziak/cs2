#pragma once
#include "movement_settings.h"

// Called outside DllMain, after MinHook initialization, before ApplyQueued.
// An unsupported build leaves movement inactive and reports the exact reason.
bool MovementPrepare(MovementLogCallback callback) noexcept;
void MovementActivate() noexcept;
// Disarm and wait for command mutations to finish. Hooks/code remain resident.
void MovementStop(const char* reason) noexcept;
const char* MovementStatus() noexcept;
// Drain bounded queued diagnostics on the render owner, outside command callbacks.
void MovementFlushTelemetry() noexcept;
// Native command owner only: regenerate CRC after cancelling an autofire
// request whose original finalizer already ran. Calls the original directly.
bool MovementRefinalizeCommand(void* input, void* command) noexcept;

// Native command owner: ground-only counter-velocity brake, or release handoff.
// Called after normal movement and before anti aim / native CRC generation.
bool MovementQuickstop(void* command, bool brake) noexcept;
