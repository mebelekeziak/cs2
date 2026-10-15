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

bool SilentAimReadShot(std::uintptr_t command, AimShot& shot, AimCommandDiagnostics* diagnostics) noexcept {
    shot = {};
    AimCommandDiagnostics observed{};
    const auto fail = [&](const char* reason) noexcept {
        observed.reason = reason;
        if (diagnostics) *diagnostics = observed;
        shot = {};
        return false;
    };
    std::uintptr_t rep = 0, base = 0;
    if (!Read(command + off::command::Presence, observed.presence)) return fail("command_presence_unreadable");
    if (!Read(command + off::command::Attack1Index, observed.index)) return fail("attack_index_unreadable");
    if (!Read(command + movement_offsets::command::Buttons, observed.buttons)) return fail("buttons_unreadable");
    if (!Read(command + off::command::HistorySize, observed.size)) return fail("history_size_unreadable");
    if (observed.size == 0) return fail("history_empty");
    if (observed.size < 0 || observed.size > 256) return fail("history_size_out_of_range");
    if (!Read(command + off::command::HistoryCapacity, observed.capacity)) return fail("history_capacity_unreadable");
    if (observed.capacity < observed.size) return fail("history_capacity_below_size");
    if (!Read(command + off::command::HistoryRep, rep)) return fail("history_rep_unreadable");
    if (!rep) return fail("history_rep_missing");
    if (!Read(rep + off::repeated::Allocated, observed.allocated)) return fail("history_allocated_unreadable");
    if (observed.allocated < observed.size) return fail("history_allocated_below_size");
    if (observed.allocated > observed.capacity) return fail("history_allocated_above_capacity");
    if (!Read(command + movement_offsets::command::Base, base)) return fail("base_unreadable");
    if (!base) return fail("base_missing");
    if (!Read(base + movement_offsets::base::Angles, observed.baseAngles)) return fail("base_angles_unreadable");
    if (!observed.baseAngles) return fail("base_angles_missing");
    const bool initial = (observed.presence & off::command::Attack1Present) &&
        observed.index >= 0 && observed.index < observed.size;
    if (!initial && !(observed.buttons & 1)) return fail("no_primary_attack");
    // For held fire the weapon can use/interpolate retained history, and its
    // final fallback uses the first current entry. Feed every native entry;
    // never synthesize an attack index or edit the weapon's retained ring.
    shot.command = command; shot.count = observed.size;
    shot.index = initial ? observed.index : -1; shot.buttons = observed.buttons;
    for (int i = 0; i < observed.size; ++i) {
        auto& entry = shot.history[static_cast<std::size_t>(i)];
        observed.entry = i;
        observed.entryPresence = observed.anglePresence = 0;
        observed.angles = 0;
        if (!Read(rep + off::repeated::Entries + static_cast<std::uintptr_t>(i) * sizeof(std::uintptr_t), entry.entry))
            return fail("history_entry_unreadable");
        if (!entry.entry) return fail("history_entry_missing");
        if (!Read(entry.entry + off::history::Presence, observed.entryPresence)) return fail("entry_presence_unreadable");
        if (!(observed.entryPresence & off::history::AnglesPresent)) return fail("entry_angles_not_present");
        if (!Read(entry.entry + off::history::Angles, observed.angles)) return fail("entry_angles_unreadable");
        if (!observed.angles) return fail("entry_angles_missing");
        if (observed.angles == observed.baseAngles) return fail("entry_angles_alias_base");
        entry.angles = observed.angles;
        if (!Read(entry.angles + off::angle::Presence, observed.anglePresence)) return fail("angle_presence_unreadable");
        if ((observed.anglePresence & off::angle::AllPresent) != off::angle::AllPresent)
            return fail("angle_components_not_present");
        if (!Read(entry.angles + off::angle::Pitch, entry.view)) return fail("angle_values_unreadable");
        if (!Valid(entry.view)) return fail("angle_values_invalid");
    }
    shot.view = shot.history[static_cast<std::size_t>(initial ? observed.index : 0)].view;
    observed.reason = "ready";
    if (diagnostics) *diagnostics = observed;
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
