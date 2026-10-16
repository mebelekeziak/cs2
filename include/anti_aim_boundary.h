#pragma once
#include "movement_command.h"
#include <array>
#include <cstdint>

struct AntiAimBoundaryStep {
    std::uintptr_t address = 0;
    MovementSubtick value{};
};
struct AntiAimBoundaryPatch {
    std::uintptr_t command = 0, base = 0, rep = 0, generated = 0;
    int sequence = 0, originalCount = 0, capacity = 0, allocated = 0;
    std::array<AntiAimBoundaryStep, 127> originals{};
    MovementSubtick originalGenerated{}, editedGenerated{};
    bool valid = false, appended = false;
};

// Callback-owned engine arena/container mutation. Inserts a separate analog
// event ahead of all original equal-time events. Original payloads are untouched.
// Native helper addresses must have been verified by the runtime caller.
bool AntiAimAppendBoundary(std::uintptr_t command, float deltaF, float deltaL,
    std::uintptr_t allocator, std::uintptr_t append, AntiAimBoundaryPatch& patch,
    const char** reason = nullptr) noexcept;
// Exact owner/layout/order/semantic check before any restoration. Keeps native
// allocation/container growth and returns the generated object to its pool.
bool AntiAimRestoreBoundary(const AntiAimBoundaryPatch& patch) noexcept;

struct AntiAimCommandPatch;
// Native append can grow the pool even when its logical insertion is rolled
// back. Refresh a pre-boundary command transaction's container identity only
// after confirming its original event stream still matches this boundary.
bool AntiAimRebindCommandContainer(const AntiAimBoundaryPatch& boundary,
    AntiAimCommandPatch& command) noexcept;
