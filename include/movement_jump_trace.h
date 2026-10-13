#pragma once
#include "movement_settings.h"

// Prepare outside DllMain, after MinHook initialization. Queues activation;
// the loader owns ApplyQueued. The callback must enqueue rather than write files.
bool MovementJumpTracePrepare(MovementLogCallback callback) noexcept;
// Disable observation cooperatively; forwarding hook/trampoline remain resident.
void MovementJumpTraceEnable(bool enabled) noexcept;
