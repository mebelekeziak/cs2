#pragma once
#include "aim_hitchance.h"
#include "aim_spread.h"

// Experimental statistical sampling through the verified private native RNG.
// Supply a context returned by AimSpreadPrepare and the live pattern-cvar value
// captured on the native command owner. The context is borrowed for this call;
// the full client spread function and engine-global RNG are never invoked.
// Samples use independent native stream seeds 1..N, not an eventual shot seed.
// Only the copied single-projectile primary model is represented. Existing
// Halton sampling and indexed compensation remain separate implementations.
// Failure invalidates output and does not silently substitute another source.
bool AimPrepareNativeSpreadPattern(const AimSpreadContext& context,
    const AimHitchanceInput& input, bool shotgunPatterns, AimSpreadPattern& output,
    const char** reason = nullptr) noexcept;
