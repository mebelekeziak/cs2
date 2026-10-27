#include "silent_aim_command.h"
#include "silent_aim_offsets.h"
#include "movement_offsets.h"
#include "movement_memory.h"
#include "aim_autofire_offsets.h"
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

bool SilentAimReadView(std::uintptr_t command, AimAngles& view, AimViewDiagnostics* diagnostics) noexcept {
    AimViewDiagnostics observed{};
    const auto report = [&](const char* reason, bool success = false) noexcept {
        observed.reason = reason;
        if (diagnostics) *diagnostics = observed;
        return success;
    };
    if (!Read(command + movement_offsets::command::Base, observed.base)) return report("base_unreadable");
    if (!observed.base) return report("base_missing");
    if (!Read(observed.base + movement_offsets::base::Presence, observed.basePresence)) return report("base_presence_unreadable");
    if (!Read(observed.base + movement_offsets::base::Angles, observed.angles)) return report("base_angles_unreadable");
    if (!observed.angles) {
        if (observed.basePresence & aim_autofire_offsets::BaseAnglesPresent) return report("present_base_angles_missing");
        // Both the native CRC and view replay use the zero default message
        // when this pointer is null. Fresh all-zero views need no allocation.
        observed.nativeDefault = true;
        view = {};
        return report("ready_native_default", true);
    }
    if (!Read(observed.angles + off::angle::Presence, observed.presence)) return report("angle_presence_unreadable");
    if (!Read(observed.angles + off::angle::Pitch, observed.values)) return report("angle_values_unreadable");
    if (!Valid(observed.values)) return report("angle_values_invalid");
    // Native sparse producers and Clear zero omitted scalar payloads. Native
    // CRC/replay read these floats directly, independent of scalar hasbits.
    if ((!(observed.presence & 1) && observed.values.pitch != 0) ||
        (!(observed.presence & 2) && observed.values.yaw != 0) ||
        (!(observed.presence & 4) && observed.values.roll != 0)) return report("absent_angle_payload_nonzero");
    view = observed.values;
    return report((observed.presence & off::angle::AllPresent) == off::angle::AllPresent ? "ready" : "ready_sparse", true);
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
    if (!observed.baseAngles) {
        std::uint32_t basePresence = 0;
        if (!Read(base + movement_offsets::base::Presence, basePresence)) return fail("base_presence_unreadable");
        if (basePresence & aim_autofire_offsets::BaseAnglesPresent) return fail("present_base_angles_missing");
        // A native zero-default base view has no payload to alias. Existing
        // history angles still require their own complete writable message.
    }
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

namespace {
namespace fire = aim_autofire_offsets;
template<class T> void Store(std::uintptr_t address, const T& value) noexcept {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}
bool SameAttack(const MovementButtons& left, const MovementButtons& right) noexcept {
    return !((left.down ^ right.down) & fire::PrimaryAttack) &&
        !((left.changed ^ right.changed) & fire::PrimaryAttack) &&
        !((left.third ^ right.third) & fire::PrimaryAttack);
}
bool HasAttack(const MovementButtons& buttons) noexcept {
    return ((buttons.down | buttons.changed | buttons.third) & fire::PrimaryAttack) != 0;
}
bool PhysicalPrimary(std::uintptr_t input, bool& physical) noexcept {
    MovementButtons buttons{};
    int events = 0;
    std::uint8_t held = 0;
    if (!Read(input + fire::InputNativeButtons, buttons) ||
        !Read(input + fire::InputEventCount, events) || events < 0 || events > fire::InputEventCapacity ||
        !Read(input + fire::InputPrimaryHeld, held)) return false;
    physical = HasAttack(buttons) || held != 0;
    for (int index = 0; index < events && !physical; ++index) {
        std::uint64_t button = 0;
        if (!Read(input + fire::InputEvents + index * fire::InputEventStride + fire::EventButton, button)) return false;
        physical = (button & fire::PrimaryAttack) != 0;
    }
    return true;
}
}

bool SilentAimStartAutofire(std::uintptr_t input, std::uintptr_t command,
    AimRecordHistoryFn record, AimAutofirePatch& patch, AimAutofireDiagnostics* diagnostics) noexcept {
    AimAutofireDiagnostics observed{};
    const auto fail = [&](const char* reason) noexcept {
        observed.reason = reason;
        if (diagnostics) *diagnostics = observed;
        return false;
    };
    if (patch.active) return fail("previous_request_still_owned");
    if (!record) return fail("native_recorder_missing");
    AimAutofirePatch next{};
    next.input = input; next.command = command;
    std::uint8_t held = 0;
    if (!Read(command + movement_offsets::command::Sequence, next.sequence) || next.sequence <= 0 ||
        !Read(command + movement_offsets::command::Buttons, next.before) ||
        !Read(command + off::command::Presence, next.originalCommandPresence) ||
        !Read(command + off::command::Attack1Index, next.originalCommandIndex))
        return fail("command_unreadable");
    observed.sequence = next.sequence;
    if (HasAttack(next.before) || ((next.originalCommandPresence & off::command::Attack1Present) &&
        next.originalCommandIndex >= 0)) return fail("manual_primary_preserved");
    if (!Read(input + fire::InputEventCount, observed.events) || observed.events < 0 ||
        observed.events >= fire::InputEventCapacity ||
        !Read(input + fire::InputEventsProcessed, observed.processed) || observed.processed < 0 ||
        observed.processed > observed.events ||
        !Read(input + fire::InputPrimaryHeld, held) ||
        !Read(input + fire::InputPrimaryIndex, next.originalInputIndex))
        return fail("input_layout_unavailable");
    if (held || next.originalInputIndex != -1) return fail("native_primary_preserved");
    bool physical = false;
    if (!PhysicalPrimary(input, physical)) return fail("physical_input_unreadable");
    if (physical) return fail("physical_primary_preserved");
    // Any physical primary event wins, including a completed intra-frame tap.
    for (int index = 0; index < observed.events; ++index) {
        std::uint64_t button = 0;
        if (!Read(input + fire::InputEvents + index * fire::InputEventStride + fire::EventButton, button))
            return fail("input_event_unreadable");
        if (button & fire::PrimaryAttack) return fail("physical_primary_event_preserved");
    }
    const auto event = input + fire::InputEvents + observed.events * fire::InputEventStride;
    std::array<unsigned char, fire::InputEventStride> beforeEvent{}, press{};
    if (!MovementCopy(event, beforeEvent.data(), beforeEvent.size()) ||
        !MovementOwnedWritable(event, beforeEvent.size()) ||
        !MovementOwnedWritable(input + fire::InputEventCount, sizeof(int)) ||
        !MovementOwnedWritable(input + fire::InputEventsProcessed, sizeof(int)) ||
        !MovementOwnedWritable(input + fire::InputPrimaryHeld, sizeof(held)) ||
        !MovementOwnedWritable(input + fire::InputPrimaryIndex, sizeof(int)) ||
        !MovementOwnedWritable(command + movement_offsets::command::Buttons, sizeof(MovementButtons)) ||
        !MovementOwnedWritable(command + off::command::Presence, sizeof(std::uint32_t)) ||
        !MovementOwnedWritable(command + off::command::Attack1Index, sizeof(int)))
        return fail("request_storage_not_writable");
    const std::uint64_t primary = fire::PrimaryAttack;
    std::memcpy(press.data() + fire::EventButton, &primary, sizeof(primary));
    press[fire::EventPressed] = 1;
    std::memcpy(reinterpret_cast<void*>(event), press.data(), press.size());
    const int temporaryCount = observed.events + 1;
    Store(input + fire::InputEventCount, temporaryCount);
    record(reinterpret_cast<void*>(input), 0);
    const bool nativeReadable = Read(input + fire::InputPrimaryIndex, observed.nativeIndex) &&
        Read(input + fire::InputHistoryCount, observed.nativeHistory);
    // The call is synchronous on the input owner. Remove our entire temporary
    // event and restore pre-call scan/held state before invoking other code.
    std::memcpy(reinterpret_cast<void*>(event), beforeEvent.data(), beforeEvent.size());
    Store(input + fire::InputEventCount, observed.events);
    Store(input + fire::InputEventsProcessed, observed.processed);
    Store(input + fire::InputPrimaryHeld, held);
    observed.inputRestored = true;
    if (!nativeReadable || observed.nativeIndex < 0 || observed.nativeIndex >= observed.nativeHistory) {
        Store(input + fire::InputPrimaryIndex, next.originalInputIndex);
        return fail("native_attack_history_unavailable");
    }
    MovementButtons current{};
    int sequence = 0;
    if (!Read(command + movement_offsets::command::Sequence, sequence) || sequence != next.sequence ||
        !Read(command + movement_offsets::command::Buttons, current) || !SameAttack(current, next.before)) {
        Store(input + fire::InputPrimaryIndex, next.originalInputIndex);
        return fail("command_changed_during_record");
    }
    next.applied = current;
    next.applied.down |= primary; next.applied.changed |= primary;
    next.applied.third &= ~primary;
    next.nativeIndex = observed.nativeIndex; next.eventCount = observed.events;
    next.active = true; next.inputPending = true;
    Store(command + movement_offsets::command::Buttons, next.applied);
    patch = next;
    observed.reason = "requested"; observed.requested = true;
    if (diagnostics) *diagnostics = observed;
    return true;
}

bool SilentAimFinishAutofireHistory(AimAutofirePatch& patch, AimAutofireDiagnostics* diagnostics) noexcept {
    if (diagnostics) {
        *diagnostics = {};
        diagnostics->sequence = patch.sequence;
        diagnostics->nativeIndex = patch.nativeIndex;
        diagnostics->requested = patch.active;
    }
    const auto report = [&](const char* reason, bool restored) noexcept {
        if (diagnostics) {
            diagnostics->reason = reason; diagnostics->inputRestored = restored;
            diagnostics->sequence = patch.sequence;
        }
        return restored;
    };
    if (!patch.active || !patch.inputPending) return report("input_already_restored", true);
    int events = 0, currentIndex = -1;
    std::uint8_t held = 0;
    bool physical = false;
    if (!Read(patch.input + fire::InputEventCount, events) || events != patch.eventCount ||
        !Read(patch.input + fire::InputPrimaryHeld, held) || held ||
        !Read(patch.input + fire::InputPrimaryIndex, currentIndex) ||
        !PhysicalPrimary(patch.input, physical) || physical ||
        !MovementOwnedWritable(patch.input + fire::InputPrimaryIndex, sizeof(int)))
        return report("input_changed_before_restore", false);
    // Native history thinning can remap our input index to zero. The event
    // list and held state above establish that no physical primary took over.
    Store(patch.input + fire::InputPrimaryIndex, patch.originalInputIndex);
    patch.inputPending = false;
    return report("input_restored", true);
}

bool SilentAimCancelAutofire(AimAutofirePatch& patch, AimAutofireDiagnostics* diagnostics) noexcept {
    if (diagnostics) *diagnostics = {};
    if (!patch.active) {
        if (diagnostics) { diagnostics->reason = "no_owned_request"; diagnostics->commandRestored = true; }
        return true;
    }
    const bool inputRestored = SilentAimFinishAutofireHistory(patch, diagnostics);
    bool physical = false;
    if (!PhysicalPrimary(patch.input, physical) || physical) {
        if (diagnostics) diagnostics->reason = physical ? "manual_primary_takeover" : "physical_input_unreadable";
        return false;
    }
    MovementButtons current{};
    int sequence = 0;
    std::uint32_t presence = 0;
    if (!Read(patch.command + movement_offsets::command::Sequence, sequence) || sequence != patch.sequence ||
        !Read(patch.command + movement_offsets::command::Buttons, current) || !SameAttack(current, patch.applied) ||
        !Read(patch.command + off::command::Presence, presence) ||
        !MovementOwnedWritable(patch.command + movement_offsets::command::Buttons, sizeof(current)) ||
        !MovementOwnedWritable(patch.command + off::command::Presence, sizeof(presence)) ||
        !MovementOwnedWritable(patch.command + off::command::Attack1Index, sizeof(int))) {
        if (diagnostics) diagnostics->reason = "command_changed_before_cancel";
        return false;
    }
    const auto restore = [](std::uint64_t currentBits, std::uint64_t beforeBits) noexcept {
        return (currentBits & ~fire::PrimaryAttack) | (beforeBits & fire::PrimaryAttack);
    };
    current.down = restore(current.down, patch.before.down);
    current.changed = restore(current.changed, patch.before.changed);
    current.third = restore(current.third, patch.before.third);
    presence = (presence & ~off::command::Attack1Present) |
        (patch.originalCommandPresence & off::command::Attack1Present);
    Store(patch.command + movement_offsets::command::Buttons, current);
    Store(patch.command + off::command::Presence, presence);
    Store(patch.command + off::command::Attack1Index, patch.originalCommandIndex);
    if (diagnostics) {
        diagnostics->reason = inputRestored ? "request_cancelled" : "command_cancelled_input_changed";
        diagnostics->commandRestored = true;
    }
    if (inputRestored) patch = {};
    return inputRestored;
}

bool SilentAimRestoreAutofireRebuild(std::uintptr_t input, std::uintptr_t command,
    AimAutofirePatch& patch, AimAutofireDiagnostics* diagnostics, bool retainCompleted) noexcept {
    if (diagnostics) *diagnostics = {};
    if (!patch.active) return true;
    int sequence = 0;
    if (input != patch.input || command != patch.command ||
        !Read(command + movement_offsets::command::Sequence, sequence) || sequence != patch.sequence) {
        patch = {};
        if (diagnostics) diagnostics->reason = "stale_ownership_abandoned";
        return true;
    }
    MovementButtons buttons{};
    bool physical = false;
    if (!Read(command + movement_offsets::command::Buttons, buttons) ||
        !PhysicalPrimary(input, physical)) {
        if (diagnostics) diagnostics->reason = "rebuild_source_unreadable";
        return false;
    }
    if (physical) {
        // The current command now belongs to native physical input. There is
        // no safe synthetic-primary bit to remove, even if its mask matches.
        patch = {};
        if (diagnostics) diagnostics->reason = "manual_primary_takeover";
        return true;
    }
    if (!SameAttack(buttons, patch.applied)) {
        patch = {};
        if (diagnostics) diagnostics->reason = "native_primary_rebuilt";
        return true;
    }
    if (retainCompleted && !patch.inputPending) {
        if (diagnostics) diagnostics->reason = "completed_primary_retained";
        return true;
    }
    return SilentAimCancelAutofire(patch, diagnostics);
}
