#include "silent_aim_command.h"
#include "silent_aim_offsets.h"
#include "movement_offsets.h"
#include "movement_memory.h"
#include <cmath>
#include <cstring>

namespace {
namespace off = silent_aim_offsets;
static_assert(sizeof(AimAngles) == 3 * sizeof(float));
static_assert(off::angle::Yaw == off::angle::Pitch + sizeof(float));
static_assert(off::angle::Roll == off::angle::Yaw + sizeof(float));
template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
bool Valid(const AimAngles& value) noexcept {
    return std::isfinite(value.pitch) && std::isfinite(value.yaw) && std::isfinite(value.roll) &&
        std::abs(value.pitch) <= 89 && std::abs(value.yaw) <= 360 && value.roll == 0;
}
}

bool SilentAimReadShot(std::uintptr_t command, AimShot& shot) noexcept {
    shot = {};
    std::uint32_t presence = 0;
    int index = -1, size = 0, capacity = 0, allocated = 0;
    std::uint64_t buttons = 0;
    std::uintptr_t rep = 0, base = 0, baseAngles = 0;
    if (!Read(command + off::command::Presence, presence) ||
        !Read(command + off::command::Attack1Index, index) ||
        !Read(command + movement_offsets::command::Buttons, buttons) ||
        !Read(command + off::command::HistorySize, size) || size <= 0 || size > 256 ||
        !Read(command + off::command::HistoryCapacity, capacity) || capacity < size ||
        !Read(command + off::command::HistoryRep, rep) || !rep ||
        !Read(rep + off::repeated::Allocated, allocated) || allocated < size || allocated > capacity ||
        !Read(command + movement_offsets::command::Base, base) || !base ||
        !Read(base + movement_offsets::base::Angles, baseAngles) || !baseAngles) return false;
    const bool initial = (presence & off::command::Attack1Present) && index >= 0 && index < size;
    if (!initial && !(buttons & 1)) return false;
    // For held fire the weapon can use/interpolate retained history, and its
    // final fallback uses the first current entry. Feed every native entry;
    // never synthesize an attack index or edit the weapon's retained ring.
    shot.command = command; shot.count = size; shot.index = initial ? index : -1; shot.buttons = buttons;
    for (int i = 0; i < size; ++i) {
        auto& entry = shot.history[static_cast<std::size_t>(i)];
        std::uint32_t entryPresence = 0, anglePresence = 0;
        if (!Read(rep + off::repeated::Entries + static_cast<std::uintptr_t>(i) * sizeof(std::uintptr_t), entry.entry) || !entry.entry ||
            !Read(entry.entry + off::history::Presence, entryPresence) || !(entryPresence & off::history::AnglesPresent) ||
            !Read(entry.entry + off::history::Angles, entry.angles) || !entry.angles || entry.angles == baseAngles ||
            !Read(entry.angles + off::angle::Presence, anglePresence) ||
            (anglePresence & off::angle::AllPresent) != off::angle::AllPresent ||
            !Read(entry.angles + off::angle::Pitch, entry.view) || !Valid(entry.view)) { shot = {}; return false; }
    }
    shot.view = shot.history[static_cast<std::size_t>(initial ? index : 0)].view;
    return true;
}
bool SilentAimWriteShot(const AimShot& shot, const AimAngles& angles) noexcept {
    AimShot current{};
    if (!Valid(angles) || !SilentAimReadShot(shot.command, current) || current.index != shot.index ||
        current.count != shot.count || current.buttons != shot.buttons) return false;
    for (int i = 0; i < shot.count; ++i) {
        const auto& before = shot.history[static_cast<std::size_t>(i)];
        const auto& now = current.history[static_cast<std::size_t>(i)];
        if (before.entry != now.entry || before.angles != now.angles ||
            std::memcmp(&before.view, &now.view, sizeof(now.view)) ||
            !MovementOwnedWritable(before.angles + off::angle::Pitch, sizeof(AimAngles))) return false;
    }
    // The message already owns all three serialized components. Their presence
    // bits and every other field stay exactly as native code produced them.
    for (int i = 0; i < shot.count; ++i)
        std::memcpy(reinterpret_cast<void*>(shot.history[static_cast<std::size_t>(i)].angles + off::angle::Pitch), &angles, sizeof(angles));
    return true;
}
