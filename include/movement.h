#pragma once
#include "movement_settings.h"

// Called outside DllMain, after MinHook initialization, before ApplyQueued.
// An unsupported build leaves movement inactive and reports the exact reason.
bool MovementPrepare(MovementLogCallback callback) noexcept;
void MovementActivate() noexcept;
// Disarm and wait for command mutations to finish. Hooks/code remain resident.
void MovementStop(const char* reason) noexcept;
const char* MovementStatus() noexcept;
