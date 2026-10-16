#include "anti_aim_view.h"
#include "silent_aim_math.h"
#include "movement_memory.h"
#include <cmath>
#include <cstring>

bool AntiAimReplayPreservingView(std::uintptr_t angles, bool preserve,
    AntiAimNativeReplay native, void* input, int slot,
    AntiAimReplaySourceValid valid, void* context) noexcept {
    if (!native) return false;
    AimAngles physical{};
    const bool captured = preserve && slot == 0 && valid &&
        MovementCopy(angles, &physical, sizeof(physical)) &&
        std::isfinite(physical.pitch) && std::isfinite(physical.yaw) && std::isfinite(physical.roll) &&
        MovementOwnedWritable(angles, sizeof(physical));
    native(input, slot);
    if (!captured || !valid(context) || !MovementOwnedWritable(angles, sizeof(physical))) return false;
    std::memcpy(reinterpret_cast<void*>(angles), &physical, sizeof(physical));
    return true;
}
