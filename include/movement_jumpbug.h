#pragma once
#include <array>
#include <cstdint>

struct MovementJumpbugResult {
    bool valid = false, landing = false;
    float fraction = 0;
    std::array<float, 3> normal{};
    // Copied probe inputs for correlation with native movement, never pointers.
    std::array<float, 3> origin{}, mins{}, maxs{};
    // Binary-exact candidate event inside the native ground capture window.
    // These bounds contain the accepted candidate times, not contact times.
    float captureMinFraction = 0, captureMaxFraction = 0, clearance = 0;
    std::array<float, 3> crouchedMins{}, crouchedMaxs{}, duckOriginDelta{}, eventOrigin{};
};
// Called under the command owner lock; no pointers escape synchronous probing.
bool MovementJumpbugPrepare(std::uintptr_t client) noexcept;
MovementJumpbugResult MovementProbeJumpbug(std::uintptr_t pawn, std::uintptr_t services,
    const std::array<float, 3>& velocity, bool holdingDuck) noexcept;

#ifdef CS2_JUMPBUG_CHECKS
using JumpbugFilterFn = void* (*)(void*, void*, std::uint64_t, int);
using JumpbugResultFn = void (*)(void*);
using JumpbugTraceFn = void (*)(void*, void*, const void*, const void*, const void*, void*);
using JumpbugVectorsFn = const void* (*)();
void MovementJumpbugTestConfigure(std::uintptr_t client, JumpbugFilterFn filter,
    JumpbugResultFn result, JumpbugTraceFn trace, JumpbugVectorsFn vectors) noexcept;
#endif
