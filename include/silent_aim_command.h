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
// Borrow native primary-fire history, including held-fire continuation entries.
// No message allocation, attack synthesis, base-angle writes or input writes.
bool SilentAimReadShot(std::uintptr_t command, AimShot& shot) noexcept;
bool SilentAimWriteShot(const AimShot& shot, const AimAngles& angles) noexcept;
