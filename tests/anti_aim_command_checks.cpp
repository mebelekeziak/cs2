#include "anti_aim_command.h"
#include "movement_offsets.h"
#include "silent_aim_offsets.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
namespace mo = movement_offsets;
namespace so = silent_aim_offsets;
int failures = 0;
void Expect(bool ok, const char* label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label); failures += !ok; }
template<class T> std::uintptr_t Address(T& v) { return reinterpret_cast<std::uintptr_t>(&v); }
template<class T> void Put(std::uintptr_t a, const T& v) { std::memcpy(reinterpret_cast<void*>(a), &v, sizeof(v)); }
template<class T> T Get(std::uintptr_t a) { T v{}; std::memcpy(&v, reinterpret_cast<void*>(a), sizeof(v)); return v; }
bool Near(float a, float b) { return std::abs(a - b) < 0.0002f; }
struct Fixture {
    std::array<unsigned char, 0x90> command{}, base{};
    std::array<unsigned char, 0x38> angles{}, history{}, historyAngles{};
    std::array<unsigned char, 0x18> rep{}, historyRep{};
    std::array<MovementSubtick, 2> steps{};
    Fixture() {
        command.fill(0xA5); base.fill(0xA5); angles.fill(0xA5);
        history.fill(0xA5); historyAngles.fill(0xA5);
        Put(Address(command) + mo::command::Sequence, 42);
        Put(Address(command) + mo::command::Presence, std::uint32_t{0x8001});
        Put(Address(command) + mo::command::Base, Address(base));
        Put(Address(command) + mo::command::Buttons, MovementButtons{mo::Forward | mo::Left | 3, mo::Forward | 2, 2});
        Put(Address(command) + so::command::HistorySize, 1);
        Put(Address(command) + so::command::HistoryCapacity, 1);
        Put(Address(command) + so::command::HistoryRep, Address(historyRep));
        Put(Address(historyRep), 1); Put(Address(historyRep) + 8, Address(history));
        Put(Address(history) + so::history::Presence, std::uint32_t{1});
        Put(Address(history) + so::history::Angles, Address(historyAngles));
        Put(Address(base) + mo::base::Presence, std::uint32_t{0x80C4});
        Put(Address(base) + mo::base::Angles, Address(angles));
        Put(Address(base) + mo::base::Forward, 0.8f); Put(Address(base) + mo::base::Left, 0.2f);
        Put(Address(base) + mo::base::SubtickSize, 2); Put(Address(base) + mo::base::SubtickCapacity, 2);
        Put(Address(base) + mo::base::SubtickRep, Address(rep)); Put(Address(rep), 2);
        Put(Address(angles) + so::angle::Presence, std::uint32_t{0x8007});
        Put(Address(angles) + so::angle::Pitch, AimAngles{20, 35, 0});
        for (unsigned i = 0; i < steps.size(); ++i) {
            Put(Address(rep) + 8 + i * 8, Address(steps[i]));
            steps[i].vtable = 0x123450; steps[i].arena = 0x543210;
            steps[i].presence = 0x8000007f; steps[i].cachedSize = 123;
            steps[i].button = (i ? mo::Left : mo::Forward) | 3;
            steps[i].pressed = i == 0; steps[i].padding.fill(std::byte{0xBA});
            steps[i].when = 0.25f + 0.5f * i; steps[i].forwardDelta = 0.1f;
            steps[i].leftDelta = -0.05f; steps[i].pitchDelta = 5; steps[i].yawDelta = 10;
        }
    }
};
bool Same(const Fixture& f, const Fixture& before) { return std::memcmp(&f, &before, sizeof(f)) == 0; }
void Transactions() {
    Fixture f; const Fixture before = f; AntiAimCommandPatch p{};
    Expect(AntiAimApplyCommand(Address(f.command), p), "valid native command transforms");
    Expect(p.sequence == 42 && p.valid && Near(p.editedView.pitch, 89) && Near(p.editedView.yaw, -145), "outgoing base faces backwards with downward pitch");
    Expect(Near(p.editedForward, -0.8f) && Near(p.editedLeft, -0.2f), "base movement is corrected in positive-left basis");
    Expect((p.editedButtons.down & mo::Directions) == (mo::Back | mo::Right) &&
           (p.editedButtons.down & ~mo::Directions) == 3 && (p.editedButtons.changed & ~mo::Directions) == 2,
           "direction buttons follow corrected axes while fire and jump survive");
    bool steps = true;
    for (unsigned i = 0; i < 2; ++i) {
        const auto& s = f.steps[i]; const auto& old = before.steps[i];
        steps &= Near(s.forwardDelta, -old.forwardDelta) && Near(s.leftDelta, -old.leftDelta) && s.pitchDelta == 0 &&
            s.yawDelta == old.yawDelta && s.when == old.when && s.pressed == old.pressed && s.padding == old.padding &&
            s.cachedSize == old.cachedSize && s.vtable == old.vtable && s.arena == old.arena &&
            (s.button & ~mo::Directions) == (old.button & ~mo::Directions) && s.presence == old.presence;
    }
    Expect(steps && (f.steps[0].button & mo::Directions) == mo::Back && (f.steps[1].button & mo::Directions) == mo::Right,
           "subtick analog and direction edges transform; timing action camera-yaw and metadata survive");
    Expect(f.history == before.history && f.historyAngles == before.historyAngles && f.historyRep == before.historyRep,
           "all history bytes remain physical");
    Fixture expected = before;
    Put(Address(expected.command) + mo::command::Buttons, p.editedButtons);
    Put(Address(expected.base) + mo::base::Forward, p.editedForward);
    Put(Address(expected.base) + mo::base::Left, p.editedLeft);
    Put(Address(expected.angles) + so::angle::Pitch, p.editedView);
    Put(Address(expected.angles) + so::angle::Presence, p.editedAnglePresence);
    expected.steps = f.steps;
    Expect(Same(f, expected), "only permitted native payload bytes change");
    const Fixture outgoing = f;
    Expect(AntiAimSetReplayView(p, true) && Get<AimAngles>(Address(f.angles) + so::angle::Pitch).pitch == 20 &&
           f.base == outgoing.base && f.command == outgoing.command &&
           std::memcmp(f.steps.data(), outgoing.steps.data(), sizeof(f.steps)) == 0,
           "replay exposes physical base view without changing movement");
    Expect(AntiAimSetReplayView(p, false) && Same(f, outgoing), "replay view scope restores outgoing angles exactly");
    bool repeat = true;
    for (int i = 0; i < 100; ++i) repeat &= AntiAimRestoreCommand(p) && Same(f, before) && AntiAimReapplyCommand(p) && Same(f, outgoing);
    Expect(repeat, "one hundred finalize restore/reapply cycles have no accumulated rotation");
    Put(Address(f.command) + mo::command::Sequence, 43); const Fixture stale = f;
    Expect(!AntiAimRestoreCommand(p) && Same(f, stale), "changed sequence rejects stale restoration without writes");
}
void Malformed() {
    for (int kind = 0; kind < 9; ++kind) {
        Fixture f; AntiAimCommandPatch p{};
        if (kind == 0) Put(Address(f.angles) + so::angle::Pitch, AimAngles{90, 30, 0});
        if (kind == 1) f.steps[1].pitchDelta = std::numeric_limits<float>::quiet_NaN();
        if (kind == 2) Put(Address(f.history) + so::history::Angles, Address(f.angles));
        if (kind == 3) Put(Address(f.rep) + 16, Address(f.steps[0]));
        if (kind == 4) Put(Address(f.base) + mo::base::SubtickSize, 129);
        if (kind == 5) Put(Address(f.angles) + so::angle::Presence, std::uint32_t{2});
        if (kind == 6) f.steps[1].when = 0.1f;
        if (kind == 7) Put(Address(f.angles) + so::angle::Pitch, AimAngles{0, 30, 1});
        if (kind == 8) Put(Address(f.base) + mo::base::Forward, std::numeric_limits<float>::infinity());
        const Fixture before = f;
        Expect(!AntiAimApplyCommand(Address(f.command), p) && !p.valid && Same(f, before), "malformed/singular/nonfinite/aliased command rejects transaction intact");
    }
    Fixture f; AntiAimCommandPatch p{};
    Expect(AntiAimApplyCommand(Address(f.command), p), "mutation fixture applies");
    f.steps[1].leftDelta += 0.25f; const Fixture changed = f;
    Expect(!AntiAimRestoreCommand(p) && Same(f, changed), "changed last-step payload rejects restore without partially restoring base");
    Fixture upside;
    Put(Address(upside.angles) + so::angle::Pitch, AimAngles{160, 2000, 0});
    Expect(AntiAimApplyCommand(Address(upside.command), p) && Near(p.editedForward, 0.8f) && Near(p.editedLeft, -0.2f),
           "wrapped yaw and upside-down pitch preserve horizontal world intent");
    Fixture crossing;
    Put(Address(crossing.angles) + so::angle::Pitch, AimAngles{80, 0, 0}); crossing.steps[0].pitchDelta = 20;
    crossing.steps[1].pitchDelta = 0;
    const Fixture crossingBefore = crossing;
    Expect(!AntiAimApplyCommand(Address(crossing.command), p) && Same(crossing, crossingBefore),
           "pitch crossing ninety rejects because existing events cannot exchange held direction edges");
    Fixture negativeTime; negativeTime.steps[0].when = -0.5f; const Fixture negativeBefore = negativeTime;
    Expect(!AntiAimApplyCommand(Address(negativeTime.command), p) && Same(negativeTime, negativeBefore),
           "negative first subtick time rejects without writes");
    Fixture tiny;
    Put(Address(tiny.base) + mo::base::Forward, 0.00005f);
    Put(Address(tiny.command) + mo::command::Buttons,
        MovementButtons{mo::Forward | mo::Back | mo::Left | 3, mo::Forward | mo::Right | 2, mo::Back | mo::Left | 1});
    Expect(AntiAimApplyCommand(Address(tiny.command), p) && Near(p.editedForward, -0.00005f) &&
           p.editedButtons.down == (mo::Forward | mo::Back | mo::Right | 3) &&
           p.editedButtons.changed == (mo::Back | mo::Left | 2) && p.editedButtons.third == (mo::Forward | mo::Right | 1),
           "exact signed permutation preserves tiny axes opposite held keys and third-word taps");
    for (int kind = 0; kind < 6; ++kind) {
        Fixture rebuilt;
        Expect(AntiAimApplyCommand(Address(rebuilt.command), p), "pooled-rebuild fixture applies");
        if (kind == 0) rebuilt.steps[1].when += 0.01f;
        if (kind == 1) rebuilt.steps[1].pressed = !rebuilt.steps[1].pressed;
        if (kind == 2) rebuilt.steps[1].yawDelta += 1;
        if (kind == 3) rebuilt.steps[1].arena += 8;
        if (kind == 4) Put(Address(rebuilt.command) + mo::command::Presence, std::uint32_t{0x8003});
        if (kind == 5) rebuilt.steps[1].presence ^= 0x40000000;
        const Fixture changedRebuild = rebuilt;
        Expect(!AntiAimRestoreCommand(p) && Same(rebuilt, changedRebuild),
               "changed semantic metadata detects pooled rebuild before transaction writes");
    }
    Fixture serialized;
    Expect(AntiAimApplyCommand(Address(serialized.command), p), "serialization fixture applies");
    serialized.steps[0].cachedSize = 9; serialized.steps[1].cachedSize = 11;
    Expect(AntiAimRestoreCommand(p) && serialized.steps[0].cachedSize == 9 && serialized.steps[1].cachedSize == 11 &&
           AntiAimReapplyCommand(p), "generated cached-size recomputation preserves transaction and is never overwritten");
    Fixture fine;
    Put(Address(fine.base) + mo::base::Forward, 1.0f);
    fine.steps[0].forwardDelta = 1e-10f; fine.steps[0].leftDelta = -1e-12f;
    Expect(AntiAimApplyCommand(Address(fine.command), p) && fine.steps[0].forwardDelta == -1e-10f &&
           fine.steps[0].leftDelta == 1e-12f, "direct signed delta transform retains transitions far below base analog precision");
    Fixture absent;
    Put(Address(absent.base) + mo::base::Presence, std::uint32_t{0x8004});
    Put(Address(absent.base) + mo::base::Forward, 123.0f); Put(Address(absent.base) + mo::base::Left, -456.0f);
    absent.steps[0].presence &= ~0x38u;
    absent.steps[0].forwardDelta = 31; absent.steps[0].leftDelta = 29; absent.steps[0].pitchDelta = 23;
    const Fixture absentBefore = absent;
    Expect(AntiAimApplyCommand(Address(absent.command), p) && p.editedPresence == p.originalPresence &&
           Get<float>(Address(absent.base) + mo::base::Forward) == 123 && Get<float>(Address(absent.base) + mo::base::Left) == -456 &&
           absent.steps[0].forwardDelta == 31 && absent.steps[0].leftDelta == 29 && absent.steps[0].pitchDelta == 23,
           "absent analog and pitch fields preserve inactive payload bytes");
    Put(Address(absent.base) + mo::base::Presence, p.editedPresence | 1u);
    Expect(AntiAimRestoreCommand(p) && Get<std::uint32_t>(Address(absent.base) + mo::base::Presence) == (p.originalPresence | 1u) &&
           Get<float>(Address(absent.base) + mo::base::Forward) == Get<float>(Address(absentBefore.base) + mo::base::Forward) &&
           AntiAimReapplyCommand(p) && Get<std::uint32_t>(Address(absent.base) + mo::base::Presence) == (p.editedPresence | 1u),
           "native CRC presence survives restoration and reapply while original analog presence is retained");
    Put(Address(absent.base) + mo::base::Presence, p.editedPresence | 3u); const Fixture unrelatedPresence = absent;
    Expect(!AntiAimRestoreCommand(p) && Same(absent, unrelatedPresence), "unrelated base presence drift rejects without partial writes");
}
void SparseAngles() {
    for (unsigned mask = 0; mask < 8; ++mask) {
        Fixture f;
        // A present zero roll is legal too; nonzero roll is separately rejected
        // by the walk basis rather than by protobuf presence validation.
        Put(Address(f.angles) + so::angle::Presence, 0x8000u | mask);
        Put(Address(f.angles) + so::angle::Pitch, AimAngles{(mask & 1) ? 20.0f : 0.0f, (mask & 2) ? 35.0f : 0.0f, 0});
        const Fixture original = f;
        AntiAimCommandPatch p{}; const char* reason = nullptr; AntiAimCommandDiagnostics d{};
        const bool applied = AntiAimApplyCommand(Address(f.command), p, &reason, &d);
        Expect(applied && std::strcmp(reason, "applied") == 0 && std::strcmp(d.reason, "applied") == 0 &&
               d.anglePresence == (0x8000u | mask) && d.commandPresence == 0x8001 && d.basePresence == 0x80C4 &&
               d.count == 2 && d.capacity == 2 && d.allocated == 2 && p.editedAnglePresence == 0x8003 &&
               Get<std::uint32_t>(Address(f.angles) + so::angle::Presence) == p.editedAnglePresence,
               "all eight zero-compatible component masks apply with native outgoing presence and diagnostics");
        if (!applied) continue;
        const Fixture outgoing = f;
        bool repeat = true;
        for (int i = 0; i < 25; ++i) {
            repeat &= AntiAimSetReplayView(p, true) && f.angles == original.angles;
            repeat &= AntiAimSetReplayView(p, false) && Same(f, outgoing);
            repeat &= AntiAimRestoreCommand(p) && Same(f, original);
            repeat &= AntiAimReapplyCommand(p) && Same(f, outgoing);
        }
        Expect(repeat, "sparse original masks restore exactly through replay and repeated transactions");
    }
    for (unsigned component = 0; component < 3; ++component) {
        Fixture f; AntiAimCommandPatch p{}; AntiAimCommandDiagnostics d{}; const char* reason = nullptr;
        AimAngles a{0, 0, 0};
        if (component == 0) a.pitch = 20;
        if (component == 1) a.yaw = 35;
        if (component == 2) a.roll = 1;
        Put(Address(f.angles) + so::angle::Presence, 0x8000u);
        Put(Address(f.angles) + so::angle::Pitch, a); const Fixture before = f;
        Expect(!AntiAimApplyCommand(Address(f.command), p, &reason, &d) && !p.valid && Same(f, before) &&
               std::strcmp(reason, "angle_presence") == 0 && std::strcmp(d.reason, reason) == 0 && d.anglePresence == 0x8000,
               "absent component with nonzero raw angle rejects intact with precise presence diagnostics");
    }
    for (int kind = 0; kind < 2; ++kind) {
        Fixture f; AntiAimCommandPatch p{};
        Put(Address(f.angles) + so::angle::Presence, std::uint32_t{0x8000u | (kind ? 0u : 2u)});
        Put(Address(f.angles) + so::angle::Pitch, AimAngles{0, kind ? 0.0f : 180.0f, 0});
        const Fixture before = f;
        Expect(AntiAimApplyCommand(Address(f.command), p) && p.editedView.yaw == (kind ? -180.0f : 0.0f) &&
               p.editedView.pitch == 89 && p.editedAnglePresence == (kind ? 0x8003u : 0x8001u) &&
               Get<std::uint32_t>(Address(f.angles) + so::angle::Presence) == p.editedAnglePresence &&
               AntiAimRestoreCommand(p) && Same(f, before),
               "yaw 180 to zero clears presence and zero to minus 180 sets it, restoring original bytes");
    }
}
void Diagnostics() {
    const char* expected[]{"command_identity", "command_base", "base_angles", "angle_presence", "angle_values",
        "base_movement", "subtick_container", "subtick_payload", "subtick_alias", "history_layout_or_alias"};
    for (unsigned kind = 0; kind < 10; ++kind) {
        Fixture f; AntiAimCommandPatch p{}; AntiAimCommandDiagnostics d{}; const char* reason = nullptr;
        if (kind == 0) Put(Address(f.command) + mo::command::Sequence, 0);
        if (kind == 1) Put(Address(f.command) + mo::command::Base, std::uintptr_t{0});
        if (kind == 2) Put(Address(f.base) + mo::base::Presence, std::uint32_t{0});
        if (kind == 3) Put(Address(f.angles) + so::angle::Presence, std::uint32_t{0});
        if (kind == 4) Put(Address(f.angles) + so::angle::Pitch, AimAngles{std::numeric_limits<float>::quiet_NaN(), 35, 0});
        if (kind == 5) {
            // A readable base prefix with unreadable movement payload is covered
            // by the guarded-memory suite; keep this branch for scalar math.
            Put(Address(f.base) + mo::base::Forward, std::numeric_limits<float>::infinity());
        }
        if (kind == 6) Put(Address(f.base) + mo::base::SubtickSize, 129);
        if (kind == 7) f.steps[0].when = -1;
        if (kind == 8) Put(Address(f.rep) + 16, Address(f.steps[0]));
        if (kind == 9) Put(Address(f.history) + so::history::Angles, Address(f.angles));
        const Fixture before = f;
        const char* stage = kind == 5 ? "walk_basis" : expected[kind];
        Expect(!AntiAimApplyCommand(Address(f.command), p, &reason, &d) && Same(f, before) &&
               std::strcmp(reason, stage) == 0 && std::strcmp(d.reason, stage) == 0,
               "failed command reports its exact read validation or math stage without mutation");
    }
}
void ReadOnly() {
    Fixture f; AntiAimCommandPatch p{};
    auto* memory = static_cast<MovementSubtick*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Expect(memory != nullptr, "read-only test page allocated"); if (!memory) return;
    *memory = f.steps[1]; Put(Address(f.rep) + 16, Address(*memory));
    DWORD previous = 0; const BOOL protectedPage = VirtualProtect(memory, 4096, PAGE_READONLY, &previous);
    const Fixture before = f; const MovementSubtick last = *memory;
    Expect(protectedPage && !AntiAimApplyCommand(Address(f.command), p) && Same(f, before) &&
           std::memcmp(memory, &last, sizeof(last)) == 0, "read-only final subtick leaves all earlier writable fields unchanged");
    VirtualProtect(memory, 4096, PAGE_READWRITE, &previous);
    Expect(AntiAimApplyCommand(Address(f.command), p), "writable command applies after page permission restored");
    VirtualProtect(memory, 4096, PAGE_READONLY, &previous); const Fixture outgoing = f;
    Expect(!AntiAimRestoreCommand(p) && Same(f, outgoing), "read-only final subtick rejects restoration atomically");
    VirtualFree(memory, 0, MEM_RELEASE);
    // Put has-bits on a read-only page while all three angle floats remain on
    // a separate writable page. Presence must be preflighted before any edit.
    auto* pages = static_cast<unsigned char*>(VirtualAlloc(nullptr, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Expect(pages != nullptr, "split angle-presence test pages allocated"); if (!pages) return;
    Fixture split;
    const auto angleAddress = Address(*pages) + 4096 - so::angle::Pitch;
    std::memcpy(reinterpret_cast<void*>(angleAddress), split.angles.data(), 0x24);
    Put(Address(split.base) + mo::base::Angles, angleAddress);
    const Fixture splitBefore = split;
    std::array<unsigned char, 8192> pagesBefore{}; std::memcpy(pagesBefore.data(), pages, 8192);
    const BOOL protectedPresence = VirtualProtect(pages, 4096, PAGE_READONLY, &previous);
    Expect(protectedPresence && !AntiAimApplyCommand(Address(split.command), p) && Same(split, splitBefore) &&
           std::memcmp(pages, pagesBefore.data(), 8192) == 0,
           "read-only angle has-bits prevents partial writes to separate writable payload and movement");
    VirtualProtect(pages, 4096, PAGE_READWRITE, &previous);
    Expect(AntiAimApplyCommand(Address(split.command), p), "split-page angles apply when has-bits is writable");
    const Fixture splitOutgoing = split;
    std::array<unsigned char, 8192> outgoingPages{}; std::memcpy(outgoingPages.data(), pages, 8192);
    VirtualProtect(pages, 4096, PAGE_READONLY, &previous);
    Expect(!AntiAimRestoreCommand(p) && !AntiAimSetReplayView(p, true) && Same(split, splitOutgoing) &&
           std::memcmp(pages, outgoingPages.data(), 8192) == 0,
           "read-only angle has-bits prevents partial restore and replay edits");
    VirtualProtect(pages, 4096, PAGE_READWRITE, &previous);
    Expect(AntiAimRestoreCommand(p), "split-page original view restores after has-bits permission restored");
    VirtualProtect(pages, 4096, PAGE_READONLY, &previous);
    std::memcpy(pagesBefore.data(), pages, 8192);
    Expect(!AntiAimReapplyCommand(p) && Same(split, splitBefore) && std::memcmp(pages, pagesBefore.data(), 8192) == 0,
           "read-only angle has-bits also prevents partial reapply");
    VirtualFree(pages, 0, MEM_RELEASE);
}
}
int main() { Transactions(); Malformed(); SparseAngles(); Diagnostics(); ReadOnly(); return failures ? 1 : 0; }
