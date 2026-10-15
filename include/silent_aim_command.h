#pragma once
#include "silent_aim_math.h"
#include <cstdint>
#include <array>

struct AimHistoryAngle { std::uintptr_t entry = 0, angles = 0; AimAngles view{}; };

struct AimShot {
    std::uintptr_t command = 0;
    int index = -1;
    int count = 0;
    std::uint64_t buttons = 0;
    AimAngles view{};
    std::array<AimHistoryAngle, 256> history{};
};
struct AimCommandDiagnostics {
    const char* reason = "not_read";
    int size = 0, capacity = 0, allocated = 0, index = -1, entry = -1;
    std::uint32_t presence = 0, entryPresence = 0, anglePresence = 0;
    std::uint64_t buttons = 0;
    std::uintptr_t angles = 0, baseAngles = 0;
};
// Borrow native primary-fire history, including held-fire continuation entries.
// No message allocation, attack synthesis, base-angle writes or input writes.
bool SilentAimReadShot(std::uintptr_t command, AimShot& shot,
    AimCommandDiagnostics* diagnostics = nullptr) noexcept;
bool SilentAimWriteShot(const AimShot& shot, const AimAngles& angles) noexcept;
