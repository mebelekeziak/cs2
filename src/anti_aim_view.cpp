#include "anti_aim_view.h"
#include "silent_aim_math.h"
#include "movement_memory.h"
#include <cmath>
#include <cstring>

bool AntiAimReplayPreservingInput(std::uintptr_t angles, std::uintptr_t currentButtons,
    std::uintptr_t previousButtons, bool preserve,
    AntiAimNativeReplay native, void* input, int slot,
    AntiAimReplaySourceValid valid, void* context) noexcept {
    if (!native) return false;
    AimAngles physical{};
    std::uint64_t physicalDown = 0;
    const bool captured = preserve && slot == 0 && valid &&
        MovementCopy(angles, &physical, sizeof(physical)) &&
        std::isfinite(physical.pitch) && std::isfinite(physical.yaw) && std::isfinite(physical.roll) &&
        MovementCopy(currentButtons, &physicalDown, sizeof(physicalDown)) &&
        MovementOwnedWritable(angles, sizeof(physical)) && MovementOwnedWritable(previousButtons, sizeof(physicalDown));
    native(input, slot);
    std::uint64_t nativeDown = 0;
    if (!captured || !valid(context) || !MovementCopy(previousButtons, &nativeDown, sizeof(nativeDown)) ||
        !MovementOwnedWritable(angles, sizeof(physical)) || !MovementOwnedWritable(previousButtons, sizeof(nativeDown))) return false;
    nativeDown = (nativeDown & ~movement_offsets::Directions) | (physicalDown & movement_offsets::Directions);
    std::memcpy(reinterpret_cast<void*>(angles), &physical, sizeof(physical));
    std::memcpy(reinterpret_cast<void*>(previousButtons), &nativeDown, sizeof(nativeDown));
    return true;
}
