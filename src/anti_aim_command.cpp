#include "anti_aim_command.h"
#include "movement_memory.h"
#include "silent_aim_offsets.h"
#include "anti_aim_offsets.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace {
namespace mo = movement_offsets;
namespace so = silent_aim_offsets;
template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
template<class T> bool Equal(const T& a, const T& b) noexcept {
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}
bool Overlap(std::uintptr_t a, std::size_t an, std::uintptr_t b, std::size_t bn) noexcept {
    return a < b + bn && b < a + an;
}
bool Pointer(std::uintptr_t value, std::size_t bytes) noexcept {
    return value >= 0x10000 && value <= 0x00007fffffffffffULL - bytes;
}
bool Finite(const AimAngles& v) noexcept {
    return std::isfinite(v.pitch) && std::isfinite(v.yaw) && std::isfinite(v.roll);
}
bool HistorySafe(std::uintptr_t command, std::uintptr_t angles, const AntiAimCommandPatch* patch = nullptr) noexcept {
    int count = 0, capacity = 0, allocated = 0;
    std::uintptr_t rep = 0;
    if (!Read(command + so::command::HistorySize, count) || count < 0 || count > 256 ||
        !Read(command + so::command::HistoryCapacity, capacity) || capacity < count || capacity > 4096 ||
        !Read(command + so::command::HistoryRep, rep)) return false;
    if (!rep) return count == 0 && capacity == 0;
    if (!Pointer(rep, 8 + static_cast<std::size_t>(count) * 8) ||
        !Read(rep, allocated) || allocated < count || allocated > capacity) return false;
    const std::size_t repBytes = 8 + static_cast<std::size_t>(count) * 8;
    if (Overlap(angles, 0x24, rep, repBytes)) return false;
    if (patch) {
        if (Overlap(patch->base, 0x60, rep, repBytes) || Overlap(patch->command, 0x78, rep, repBytes)) return false;
        for (int j = 0; j < patch->count; ++j)
            if (Overlap(patch->steps[static_cast<std::size_t>(j)].address, sizeof(MovementSubtick), rep, repBytes)) return false;
    }
    for (int i = 0; i < count; ++i) {
        std::uintptr_t entry = 0, other = 0;
        std::uint32_t presence = 0;
        if (!Read(rep + 8 + static_cast<std::uintptr_t>(i) * 8, entry) || !Pointer(entry, 0x20) ||
            !Read(entry + so::history::Presence, presence)) return false;
        if (Overlap(angles, 0x24, entry, 0x78)) return false;
        if (patch) {
            if (Overlap(patch->base, 0x60, entry, 0x78) || Overlap(patch->command, 0x78, entry, 0x78)) return false;
            for (int j = 0; j < patch->count; ++j)
                if (Overlap(patch->steps[static_cast<std::size_t>(j)].address, sizeof(MovementSubtick), entry, 0x78)) return false;
        }
        if (presence & so::history::AnglesPresent) {
            if (!Read(entry + so::history::Angles, other) || !Pointer(other, 0x24) ||
                Overlap(angles, 0x24, other, 0x24)) return false;
            if (patch) {
                if (Overlap(patch->base, 0x60, other, 0x24) || Overlap(patch->command, 0x78, other, 0x24)) return false;
                for (int j = 0; j < patch->count; ++j)
                    if (Overlap(patch->steps[static_cast<std::size_t>(j)].address, sizeof(MovementSubtick), other, 0x24)) return false;
            }
        }
    }
    return true;
}
bool Snapshot(std::uintptr_t command, AntiAimCommandPatch& p, const char** reason = nullptr) noexcept {
    const auto fail = [&](const char* stage) noexcept { if (reason) *reason = stage; return false; };
    p.command = command;
    if (!Pointer(command, 0x78) || !Read(command + mo::command::Sequence, p.sequence) || p.sequence <= 0 ||
        !Read(command + mo::command::Presence, p.commandPresence) || !(p.commandPresence & 1)) return fail("command_identity");
    if (!Read(command + mo::command::Base, p.base) || !Pointer(p.base, 0x60) ||
        !Read(command + mo::command::Buttons, p.originalButtons) ||
        !Read(p.base + mo::base::Presence, p.originalPresence)) return fail("command_base");
    if (!(p.originalPresence & 4) || !Read(p.base + mo::base::Angles, p.angles) || !Pointer(p.angles, 0x24)) return fail("base_angles");
    if (!Read(p.angles + so::angle::Presence, p.anglePresence)) return fail("angle_presence");
    if (!Read(p.angles + so::angle::Pitch, p.originalView) || !Finite(p.originalView)) return fail("angle_values");
    // The native producer clears component presence for zero angles. Native
    // readers also consume the raw payload, so absent nonzero data is unsafe.
    if ((!(p.anglePresence & 1) && p.originalView.pitch != 0) ||
        (!(p.anglePresence & 2) && p.originalView.yaw != 0) ||
        (!(p.anglePresence & 4) && p.originalView.roll != 0)) return fail("angle_presence");
    if (!Read(p.base + mo::base::Forward, p.originalForward) ||
        !Read(p.base + mo::base::Left, p.originalLeft)) return fail("base_movement");
    if (!Read(p.base + mo::base::SubtickSize, p.count) || p.count < 0 || p.count > 128 ||
        !Read(p.base + mo::base::SubtickCapacity, p.capacity) || p.capacity < p.count || p.capacity > 4096 ||
        !Read(p.base + mo::base::SubtickRep, p.rep)) return fail("subtick_container");
    if (!p.rep) { if (p.count || p.capacity) return fail("subtick_container"); }
    else {
        if (!Pointer(p.rep, 8 + static_cast<std::size_t>(p.count) * 8) ||
            !Read(p.rep, p.allocated) || p.allocated < p.count || p.allocated > p.capacity) return fail("subtick_container");
        if (Overlap(p.rep, 8 + static_cast<std::size_t>(p.count) * 8, p.angles, 0x24) ||
            Overlap(p.rep, 8 + static_cast<std::size_t>(p.count) * 8, p.base, 0x60) ||
            Overlap(p.rep, 8 + static_cast<std::size_t>(p.count) * 8, p.command, 0x78)) return fail("subtick_alias");
    }
    float previous = -1;
    for (int i = 0; i < p.count; ++i) {
        auto& s = p.steps[static_cast<std::size_t>(i)];
        if (!Read(p.rep + 8 + static_cast<std::uintptr_t>(i) * 8, s.address) || !Pointer(s.address, sizeof(MovementSubtick)) ||
            !Read(s.address, s.original) || !(s.original.presence & 4) ||
            !std::isfinite(s.original.when) || s.original.when < 0 || s.original.when < previous || s.original.when > 1) return fail("subtick_payload");
        previous = s.original.when;
        if (Overlap(s.address, sizeof(MovementSubtick), p.angles, 0x24) ||
            Overlap(s.address, sizeof(MovementSubtick), p.command, 0x78) ||
            Overlap(s.address, sizeof(MovementSubtick), p.base, 0x60) ||
            Overlap(s.address, sizeof(MovementSubtick), p.rep, 8 + static_cast<std::size_t>(p.count) * 8)) return fail("subtick_alias");
        for (int j = 0; j < i; ++j)
            if (Overlap(s.address, sizeof(MovementSubtick), p.steps[static_cast<std::size_t>(j)].address, sizeof(MovementSubtick))) return fail("subtick_alias");
    }
    if (!HistorySafe(command, p.angles, &p)) return fail("history_layout_or_alias");
    if (Overlap(p.command, 0x78, p.base, 0x60) || Overlap(p.command, 0x78, p.angles, 0x24) ||
        Overlap(p.base, 0x60, p.angles, 0x24)) return fail("base_angle_alias");
    return true;
}
bool Sum(float& a, float b) noexcept {
    const double sum = static_cast<double>(a) + b;
    if (!std::isfinite(sum) || std::abs(sum) > std::numeric_limits<float>::max()) return false;
    a = static_cast<float>(sum); return true;
}
bool CrossesVertical(float pitch, float delta) noexcept {
    if (!std::isfinite(delta) || std::abs(static_cast<double>(delta)) >= 180) return true;
    const double start = std::fmod(static_cast<double>(pitch), 360.0);
    return std::floor((start + 90) / 180) != std::floor((start + delta + 90) / 180);
}
bool Directions(std::uint64_t input, const AimAngles& from, const AimAngles& to, std::uint64_t& output) noexcept {
    const auto f = AntiAimCorrectWalkMovement(from, to, 1, 0);
    const auto l = AntiAimCorrectWalkMovement(from, to, 0, 1);
    if (!f.valid || !l.valid || f.left != 0 || l.forward != 0 ||
        std::abs(f.forward) != 1 || std::abs(l.left) != 1) return false;
    output = input & ~mo::Directions;
    if (input & mo::Forward) output |= f.forward > 0 ? mo::Forward : mo::Back;
    if (input & mo::Back) output |= f.forward > 0 ? mo::Back : mo::Forward;
    if (input & mo::Left) output |= l.left > 0 ? mo::Left : mo::Right;
    if (input & mo::Right) output |= l.left > 0 ? mo::Right : mo::Left;
    return true;
}
template<class T> bool CheckField(std::uintptr_t address, const T& actual, const T& original, const T& edited, bool outgoing) noexcept {
    if (Equal(original, edited)) return true;
    return Equal(actual, outgoing ? edited : original) && MovementOwnedWritable(address, sizeof(T));
}
template<class T> void Put(std::uintptr_t address, const T& original, const T& edited, bool outgoing) noexcept {
    if (!Equal(original, edited)) std::memcpy(reinterpret_cast<void*>(address), outgoing ? &edited : &original, sizeof(T));
}
bool Validate(const AntiAimCommandPatch& p, bool currentlyOutgoing, bool viewOnly) noexcept {
    AntiAimCommandPatch now{};
    if (!p.valid || !Snapshot(p.command, now) || now.sequence != p.sequence || now.base != p.base ||
        now.angles != p.angles || now.rep != p.rep || now.count != p.count || now.capacity != p.capacity ||
        now.allocated != p.allocated || now.anglePresence != (currentlyOutgoing ? p.editedAnglePresence : p.anglePresence) ||
        now.commandPresence != p.commandPresence ||
        !Equal(now.originalView, currentlyOutgoing ? p.editedView : p.originalView) ||
        !CheckField(p.angles + so::angle::Pitch, now.originalView, p.originalView, p.editedView, currentlyOutgoing) ||
        !CheckField(p.angles + so::angle::Presence, now.anglePresence, p.anglePresence, p.editedAnglePresence, currentlyOutgoing)) return false;
    if (viewOnly) return true;
    // Finalize serializes movement CRC and legitimately sets base bit 0.
    // Only analog presence belongs to this patch; all other bits remain native.
    if ((now.originalPresence & ~1u) != ((currentlyOutgoing ? p.editedPresence : p.originalPresence) & ~1u) ||
        !Equal(now.originalForward, currentlyOutgoing ? p.editedForward : p.originalForward) ||
        !Equal(now.originalLeft, currentlyOutgoing ? p.editedLeft : p.originalLeft) ||
        !Equal(now.originalButtons, currentlyOutgoing ? p.editedButtons : p.originalButtons) ||
        (((p.originalPresence ^ p.editedPresence) & 0xC0u) &&
         !MovementOwnedWritable(p.base + mo::base::Presence, sizeof(std::uint32_t))) ||
        !CheckField(p.base + mo::base::Forward, now.originalForward, p.originalForward, p.editedForward, currentlyOutgoing) ||
        !CheckField(p.base + mo::base::Left, now.originalLeft, p.originalLeft, p.editedLeft, currentlyOutgoing) ||
        !CheckField(p.command + mo::command::Buttons, now.originalButtons, p.originalButtons, p.editedButtons, currentlyOutgoing)) return false;
    for (int i = 0; i < p.count; ++i) {
        const auto& s = p.steps[static_cast<std::size_t>(i)];
        const auto& n = now.steps[static_cast<std::size_t>(i)];
        if (n.address != s.address) return false;
        auto expected = currentlyOutgoing ? s.edited : s.original;
        // Generated protobuf cached-size is recomputed by serialization. Every
        // semantic byte and allocation identity must retain this transaction.
        expected.cachedSize = n.original.cachedSize;
        if (!Equal(n.original, expected)) return false;
#define CHECK_STEP(field) if (!CheckField(s.address + offsetof(MovementSubtick, field), n.original.field, s.original.field, s.edited.field, currentlyOutgoing)) return false
        CHECK_STEP(presence); CHECK_STEP(button); CHECK_STEP(pressed); CHECK_STEP(forwardDelta); CHECK_STEP(leftDelta); CHECK_STEP(pitchDelta); CHECK_STEP(yawDelta);
#undef CHECK_STEP
    }
    return true;
}
void Write(const AntiAimCommandPatch& p, bool outgoing) noexcept {
    Put(p.angles + so::angle::Pitch, p.originalView, p.editedView, outgoing);
    Put(p.angles + so::angle::Presence, p.anglePresence, p.editedAnglePresence, outgoing);
    if ((p.originalPresence ^ p.editedPresence) & 0xC0u) {
        auto* presence = reinterpret_cast<std::uint32_t*>(p.base + mo::base::Presence);
        *presence = (*presence & ~0xC0u) | ((outgoing ? p.editedPresence : p.originalPresence) & 0xC0u);
    }
    Put(p.base + mo::base::Forward, p.originalForward, p.editedForward, outgoing);
    Put(p.base + mo::base::Left, p.originalLeft, p.editedLeft, outgoing);
    Put(p.command + mo::command::Buttons, p.originalButtons, p.editedButtons, outgoing);
    for (int i = 0; i < p.count; ++i) {
        const auto& s = p.steps[static_cast<std::size_t>(i)];
#define PUT_STEP(field) Put(s.address + offsetof(MovementSubtick, field), s.original.field, s.edited.field, outgoing)
        PUT_STEP(presence); PUT_STEP(button); PUT_STEP(pressed); PUT_STEP(forwardDelta); PUT_STEP(leftDelta); PUT_STEP(pitchDelta); PUT_STEP(yawDelta);
#undef PUT_STEP
    }
}
struct MovementBasis {
    float yaw = 0;
    std::uintptr_t transition = 0;
    const AntiAimMovementHistory* history = nullptr;
};
bool MovementStream(const AntiAimCommandPatch& patch, std::uintptr_t transition, const char** reason) noexcept {
    const auto fail = [&](const char* why) noexcept { if (reason) *reason = why; return false; };
    if (!transition) return fail("movement_transition_missing");
    if ((patch.originalForward != -1 && patch.originalForward != 0 && patch.originalForward != 1) ||
        (patch.originalLeft != -1 && patch.originalLeft != 0 && patch.originalLeft != 1) ||
        (patch.originalForward == 0 && patch.originalLeft == 0) ||
        (!(patch.originalPresence & mo::base::ForwardPresent) && patch.originalForward != 0) ||
        (!(patch.originalPresence & mo::base::LeftPresent) && patch.originalLeft != 0)) return fail("movement_fallback_axes");
    bool found = false;
    for (int i = 0; i < patch.count; ++i) {
        const auto& step = patch.steps[static_cast<std::size_t>(i)];
        if (step.address == transition) {
            if (found || step.original.when != 0 || step.original.button != 0 ||
                step.original.pitchDelta != 0 || step.original.yawDelta != 0) return fail("movement_transition_identity");
            found = true;
        } else if ((step.original.button & mo::Directions) || (step.original.presence & 0x18) ||
                   step.original.forwardDelta != 0 || step.original.leftDelta != 0)
            return fail("movement_multiple_intervals");
    }
    return found || fail("movement_transition_missing");
}
bool Retarget(MovementSubtick& step, float forward, float left, const AntiAimMovementHistory& history) noexcept {
    if (!std::isfinite(history.forward) || !std::isfinite(history.left)) return false;
    step.forwardDelta = forward - history.forward; step.leftDelta = left - history.left;
    if (!std::isfinite(step.forwardDelta) || !std::isfinite(step.leftDelta)) return false;
    step.presence = (step.presence & ~0x18u) |
        (step.forwardDelta != 0 ? 8u : 0u) | (step.leftDelta != 0 ? 0x10u : 0u);
    return true;
}
bool ApplyCommand(std::uintptr_t command, AntiAimCommandPatch& patch, const AntiAimSettings& settings,
                  const MovementBasis* movement, const char** reason, AntiAimCommandDiagnostics* diagnostics) noexcept {
    patch = {};
    if (diagnostics) *diagnostics = {};
    const auto diagnose = [&](const char* why) noexcept {
        if (reason) *reason = why;
        if (diagnostics) {
            diagnostics->reason = why;
            diagnostics->commandPresence = patch.commandPresence;
            diagnostics->basePresence = patch.originalPresence;
            diagnostics->anglePresence = patch.anglePresence;
            diagnostics->count = patch.count;
            diagnostics->capacity = patch.capacity;
            diagnostics->allocated = patch.allocated;
        }
    };
    const auto fail = [&](const char* why) noexcept { diagnose(why); patch = {}; return false; };
    const char* snapshotReason = "not_read";
    if (!Snapshot(command, patch, &snapshotReason)) return fail(snapshotReason);
    if (movement) {
        constexpr auto actions = anti_aim_offsets::Attack | anti_aim_offsets::Attack2 | anti_aim_offsets::Use;
        int attack = -1;
        if (((patch.originalButtons.down | patch.originalButtons.changed | patch.originalButtons.third) & actions) ||
            !Read(command + so::command::Attack1Index, attack) || ((patch.commandPresence & 0x20) && attack >= 0))
            return fail("movement_action_passthrough");
        if (!std::isfinite(movement->yaw) || patch.originalView.pitch < -89 || patch.originalView.pitch > 89 ||
            patch.originalView.roll != 0) return fail("movement_view_basis");
        // The caller has already applied a digital fallback. Do not convert an
        // arbitrary analog/multi-interval command into a different trajectory.
        const char* streamReason = nullptr;
        if (!MovementStream(patch, movement->transition, &streamReason)) return fail(streamReason);
        for (int i = 0; i < patch.count; ++i) {
            const auto& step = patch.steps[static_cast<std::size_t>(i)];
            if ((step.original.presence & 1) && (step.original.button & actions)) return fail("movement_action_passthrough");
        }
    }
    const auto options = AntiAimNormalizeSettings(settings);
    const bool legacyPayload = options.yawMode == AntiAimYawMode::Static && options.yawBase == AntiAimYawBase::View &&
        std::abs(options.yawOffset) == 180 && options.pitchMode == AntiAimPitchMode::Down;
    if (!AntiAimBuildAngles(patch.originalView, options, static_cast<std::uint32_t>(patch.sequence), patch.editedView)) return fail("view_nonfinite");
    // Match the native zero-component omission, preserving unrelated has-bits.
    patch.editedAnglePresence = (patch.anglePresence & ~so::angle::AllPresent) |
        (patch.editedView.pitch != 0 ? 1u : 0u) | (patch.editedView.yaw != 0 ? 2u : 0u) |
        (patch.editedView.roll != 0 ? 4u : 0u);
    float forward = (patch.originalPresence & mo::base::ForwardPresent) ? patch.originalForward : 0;
    float left = (patch.originalPresence & mo::base::LeftPresent) ? patch.originalLeft : 0;
    auto corrected = AntiAimCorrectWalkMovement(patch.originalView, patch.editedView, forward, left);
    if (!corrected.valid) return fail("walk_basis");
    patch.editedPresence = patch.originalPresence;
    patch.editedButtons = patch.originalButtons;
    const bool signedAxes = Directions(patch.originalButtons.down, patch.originalView, patch.editedView, patch.editedButtons.down) &&
        Directions(patch.originalButtons.changed, patch.originalView, patch.editedView, patch.editedButtons.changed) &&
        Directions(patch.originalButtons.third, patch.originalView, patch.editedView, patch.editedButtons.third);
    if (!legacyPayload && ((!(patch.originalPresence & mo::base::ForwardPresent) && patch.originalForward != 0) ||
        (!(patch.originalPresence & mo::base::LeftPresent) && patch.originalLeft != 0))) return fail("base_raw_presence");
    if (signedAxes) {
        // Preserve the original exact sign-permutation behavior and inactive
        // payload bytes for the existing backwards preset.
        patch.editedForward = (patch.originalPresence & mo::base::ForwardPresent) ? corrected.forward : patch.originalForward;
        patch.editedLeft = (patch.originalPresence & mo::base::LeftPresent) ? corrected.left : patch.originalLeft;
    } else {
        // Native ingestion copies raw final axes. Ordinary producers zero
        // omitted fields; reject inconsistent payloads before mixing axes.
        patch.editedForward = corrected.forward; patch.editedLeft = corrected.left;
        patch.editedPresence = (patch.originalPresence & ~0xC0u) |
            (corrected.forward != 0 ? mo::base::ForwardPresent : 0u) |
            (corrected.left != 0 ? mo::base::LeftPresent : 0u);
        std::uint64_t directions = 0;
        if (corrected.forward > 0) directions |= mo::Forward;
        if (corrected.forward < 0) directions |= mo::Back;
        if (corrected.left > 0) directions |= mo::Left;
        if (corrected.left < 0) directions |= mo::Right;
        patch.editedButtons = patch.originalButtons;
        patch.editedButtons.down = (patch.originalButtons.down & ~mo::Directions) | directions;
        // Direction events now live in the analog stream. There are no
        // corresponding digital press/release or intra-command tap bits.
        patch.editedButtons.changed &= ~mo::Directions;
        patch.editedButtons.third &= ~mo::Directions;
    }
    AimAngles from = patch.originalView, to = patch.editedView;
    const auto forwardBasis = AntiAimCorrectWalkMovement(from, to, 1, 0);
    const auto leftBasis = AntiAimCorrectWalkMovement(from, to, 0, 1);
    const float forwardSign = forwardBasis.forward < 0 ? -1.0f : 1.0f;
    const float leftSign = leftBasis.left < 0 ? -1.0f : 1.0f;
    for (int i = 0; i < patch.count; ++i) {
        auto& s = patch.steps[static_cast<std::size_t>(i)]; s.edited = s.original;
        if (!legacyPayload && ((!(s.original.presence & 0x20) && s.original.pitchDelta != 0) ||
            (!(s.original.presence & 0x40) && s.original.yawDelta != 0))) return fail("subtick_raw_presence");
        if (!legacyPayload && !s.original.button && ((!(s.original.presence & 8) && s.original.forwardDelta != 0) ||
            (!(s.original.presence & 0x10) && s.original.leftDelta != 0))) return fail("subtick_raw_presence");
        if (!legacyPayload && (s.original.button & mo::Directions)) {
            if (!(s.original.presence & 1) || !(s.original.presence & 2)) return fail("subtick_digital_presence");
            if (s.original.button != mo::Forward && s.original.button != mo::Back &&
                s.original.button != mo::Left && s.original.button != mo::Right) return fail("subtick_mixed_directions");
        }
        // A basis-sign crossing would require opposite button releases and
        // presses at one time. Existing steps cannot encode both with their
        // single pressed bool; do not create events or leave held edges stale.
        if ((s.original.presence & 0x20) && CrossesVertical(from.pitch, s.original.pitchDelta)) return fail("subtick_pitch_basis_crossing");
        if (((s.original.presence & 8) && !std::isfinite(s.original.forwardDelta)) ||
            ((s.original.presence & 0x10) && !std::isfinite(s.original.leftDelta)) ||
            ((s.original.presence & 0x20) && !Sum(from.pitch, s.original.pitchDelta)) ||
            ((s.original.presence & 0x40) && (!Sum(from.yaw, s.original.yawDelta) || !Sum(to.yaw, s.original.yawDelta)))) return fail("subtick_nonfinite");
        if (options.pitchMode == AntiAimPitchMode::Keep && (s.original.presence & 0x20)) {
            if (CrossesVertical(to.pitch, s.original.pitchDelta) || !Sum(to.pitch, s.original.pitchDelta))
                return fail("subtick_pitch_basis_crossing");
        }
        corrected = AntiAimCorrectWalkMovement(from, to, 1, 1);
        if (!corrected.valid) return fail("subtick_walk_basis");
        // Native initial analog is historical, not the final base axes. Apply
        // the constant linear transform directly, preserving tiny deltas.
        if (signedAxes) {
            if (s.original.presence & 8) s.edited.forwardDelta = forwardSign * s.original.forwardDelta;
            if (s.original.presence & 0x10) s.edited.leftDelta = leftSign * s.original.leftDelta;
            if ((s.original.presence & 1) && !Directions(s.original.button, from, to, s.edited.button)) return fail("subtick_direction_basis");
        } else {
            float deltaF = 0, deltaL = 0;
            const auto button = s.original.button;
            if (button & mo::Directions) {
                const float sign = s.original.pressed ? 1.0f : -1.0f;
                if (button == mo::Forward) deltaF = sign;
                else if (button == mo::Back) deltaF = -sign;
                else if (button == mo::Left) deltaL = sign;
                else if (button == mo::Right) deltaL = -sign;
                else return fail("subtick_mixed_directions");
                s.edited.button = 0; s.edited.pressed = false;
                s.edited.presence &= ~3u;
            } else if (!button) {
                deltaF = s.original.forwardDelta; deltaL = s.original.leftDelta;
            }
            if (!s.edited.button) {
                corrected = AntiAimCorrectWalkMovement(from, to, deltaF, deltaL);
                if (!corrected.valid) return fail("subtick_walk_basis");
                s.edited.forwardDelta = corrected.forward; s.edited.leftDelta = corrected.left;
                s.edited.presence = (s.edited.presence & ~0x18u) |
                    (corrected.forward != 0 ? 8u : 0u) | (corrected.left != 0 ? 0x10u : 0u);
            }
        }
        if (options.pitchMode != AntiAimPitchMode::Keep && (s.original.presence & 0x20)) s.edited.pitchDelta = 0;
    }
    if (movement) {
        for (int i = 0; i < patch.count; ++i) {
            auto& step = patch.steps[static_cast<std::size_t>(i)];
            // Native decoding adds raw deltas to the final base view, then
            // applies that cumulative view before segment integration. The
            // assisted fixed-basis solver requires a fixed outgoing basis.
            // Physical input/camera is retained by the shared replay owner;
            // rebuild restoration restores these original mouse bytes too.
            step.edited.pitchDelta = 0; step.edited.yawDelta = 0;
            step.edited.presence &= ~0x60u;
            if (step.address != movement->transition) continue;
            // Generic rotation produced R(D - physicalPrevious). Replacing D
            // with the ideal full-forward vector gives 1 - R(physicalPrevious).
            // When history is supplied, coalesce the native predecessor's
            // basis change directly below instead of adding a boundary event.
            if (movement->history) {
                // The owned event can coalesce the history handoff directly.
                // This requires no additional native allocation or event and
                // cannot leave a partial change of basis on append failure.
                if (!Retarget(step.edited, 1, 0, *movement->history)) return fail("movement_transition_nonfinite");
            } else {
                if (!Sum(step.edited.forwardDelta, 1.0f - patch.editedForward) ||
                    !Sum(step.edited.leftDelta, -patch.editedLeft)) return fail("movement_transition_nonfinite");
                step.edited.presence = (step.edited.presence & ~0x18u) |
                    (step.edited.forwardDelta != 0 ? 8u : 0u) | (step.edited.leftDelta != 0 ? 0x10u : 0u);
            }
        }
        patch.editedForward = 1; patch.editedLeft = 0;
        patch.editedPresence = (patch.editedPresence & ~0xC0u) | mo::base::ForwardPresent;
        patch.editedButtons.down = (patch.originalButtons.down & ~mo::Directions) | mo::Forward;
        patch.editedButtons.changed &= ~mo::Directions;
        patch.editedButtons.third &= ~mo::Directions;
    }
    patch.valid = true;
    if (!Validate(patch, false, false)) return fail("command_changed_or_readonly");
    Write(patch, true);
    diagnose("applied");
    return true;
}
}

bool AntiAimApplyCommand(std::uintptr_t command, AntiAimCommandPatch& patch, const char** reason,
                         AntiAimCommandDiagnostics* diagnostics) noexcept {
    return AntiAimApplyCommand(command, patch, AntiAimSettings{}, reason, diagnostics);
}
bool AntiAimApplyCommand(std::uintptr_t command, AntiAimCommandPatch& patch, const AntiAimSettings& settings,
                         const char** reason, AntiAimCommandDiagnostics* diagnostics) noexcept {
    return ApplyCommand(command, patch, settings, nullptr, reason, diagnostics);
}
bool AntiAimApplyMovementCommand(std::uintptr_t command, float worldYaw,
                                 std::uintptr_t analogTransition, AntiAimCommandPatch& patch,
                                 const char** reason, AntiAimCommandDiagnostics* diagnostics,
                                 const AntiAimMovementHistory* history) noexcept {
    AntiAimSettings settings{};
    settings.yawBase = AntiAimYawBase::World;
    settings.pitchMode = AntiAimPitchMode::Keep;
    double yaw = std::fmod(static_cast<double>(worldYaw), 360.0);
    if (yaw >= 180) yaw -= 360;
    if (yaw < -180) yaw += 360;
    settings.yawOffset = static_cast<float>(yaw);
    const MovementBasis movement{worldYaw, analogTransition, history};
    return ApplyCommand(command, patch, settings, &movement, reason, diagnostics);
}
bool AntiAimApplyMovementHandoff(std::uintptr_t command, std::uintptr_t analogTransition,
                                 const AntiAimMovementHistory& history, AntiAimCommandPatch& patch,
                                 const char** reason) noexcept {
    patch = {};
    const auto fail = [&](const char* why) noexcept { if (reason) *reason = why; patch = {}; return false; };
    const char* why = nullptr;
    if (!Snapshot(command, patch, &why) || !MovementStream(patch, analogTransition, &why)) return fail(why);
    patch.editedView = patch.originalView; patch.editedAnglePresence = patch.anglePresence;
    patch.editedForward = patch.originalForward; patch.editedLeft = patch.originalLeft;
    patch.editedPresence = patch.originalPresence; patch.editedButtons = patch.originalButtons;
    for (int i = 0; i < patch.count; ++i) {
        auto& step = patch.steps[static_cast<std::size_t>(i)];
        step.edited = step.original;
        if (step.address == analogTransition && !Retarget(step.edited, patch.originalForward, patch.originalLeft, history))
            return fail("movement_transition_nonfinite");
    }
    patch.valid = true;
    if (!Validate(patch, false, false)) return fail("command_changed_or_readonly");
    Write(patch, true);
    if (reason) *reason = "movement_handoff_applied";
    return true;
}
bool AntiAimRestoreCommand(const AntiAimCommandPatch& patch) noexcept {
    if (!Validate(patch, true, false)) return false;
    Write(patch, false); return true;
}
bool AntiAimReapplyCommand(const AntiAimCommandPatch& patch) noexcept {
    if (!Validate(patch, false, false)) return false;
    Write(patch, true); return true;
}
bool AntiAimSetReplayView(const AntiAimCommandPatch& patch, bool physical) noexcept {
    if (!Validate(patch, physical, true)) return false;
    Put(patch.angles + so::angle::Pitch, patch.originalView, patch.editedView, !physical);
    Put(patch.angles + so::angle::Presence, patch.anglePresence, patch.editedAnglePresence, !physical);
    return true;
}
