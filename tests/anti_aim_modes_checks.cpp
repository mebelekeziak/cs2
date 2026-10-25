#include "anti_aim_command.h"
#include "anti_aim_boundary.h"
#include "anti_aim_history.h"
#include "anti_aim_view.h"
#include "anti_aim_round.h"
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
void Expect(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    failures += !ok;
}
bool Near(float a, float b) { return std::abs(a - b) < 0.0002f; }
template<class T> std::uintptr_t Address(T& value) { return reinterpret_cast<std::uintptr_t>(&value); }
template<class T> void Put(std::uintptr_t address, const T& value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}
template<class T> T Get(std::uintptr_t address) {
    T value{}; std::memcpy(&value, reinterpret_cast<void*>(address), sizeof(value)); return value;
}
float Wrap(double degrees) {
    degrees = std::fmod(degrees, 360.0);
    if (degrees >= 180) degrees -= 360;
    if (degrees < -180) degrees += 360;
    return static_cast<float>(degrees);
}
AntiAimSettings Settings(float yaw = 37) {
    AntiAimSettings settings;
    settings.enabled = true; settings.yawOffset = yaw;
    return settings;
}
void PatternMath() {
    const AimAngles local{21, 170, 0}; AimAngles output{};
    auto settings = Settings(30);
    Expect(AntiAimBuildAngles(local, settings, 1, output) && output.pitch == 89 && output.yaw == -160,
           "static view-relative yaw wraps at the native boundary");
    settings.yawBase = AntiAimYawBase::World;
    Expect(AntiAimBuildAngles(local, settings, 400, output) && output.yaw == 30,
           "world yaw base is independent of local camera heading");
    settings.yawBase = AntiAimYawBase::View;
    settings.yawMode = AntiAimYawMode::Jitter;
    settings.jitterRange = 60; settings.jitterInterval = 3;
    bool jitter = true;
    for (std::uint32_t sequence = 1; sequence <= 12; ++sequence) {
        jitter &= AntiAimBuildAngles(local, settings, sequence, output) &&
                  output.yaw == Wrap(200 + (((sequence - 1) / 3) & 1 ? 30 : -30));
        const AimAngles first = output;
        AimAngles unrelated{};
        jitter &= AntiAimBuildAngles(local, settings, sequence + 113, unrelated) &&
                  AntiAimBuildAngles(local, settings, sequence, output) &&
                  std::memcmp(&first, &output, sizeof(output)) == 0;
    }
    Expect(jitter, "jitter switches exactly at command-period boundaries and rebuilds deterministically");
    settings.yawBase = AntiAimYawBase::World;
    settings.yawMode = AntiAimYawMode::Spin; settings.yawOffset = 170; settings.spinSpeed = 20;
    Expect(AntiAimBuildAngles(local, settings, 1, output) && output.yaw == 170 &&
           AntiAimBuildAngles(local, settings, 2, output) && output.yaw == -170 &&
           AntiAimBuildAngles(local, settings, 19, output) && output.yaw == 170,
           "spin starts at its offset and wraps one complete revolution");
    settings.spinSpeed = -20;
    Expect(AntiAimBuildAngles(local, settings, 2, output) && output.yaw == 150 &&
           AntiAimBuildAngles(local, settings, UINT32_MAX, output) &&
           output.yaw == Wrap(170 + static_cast<double>(UINT32_MAX - 1u) * -20),
           "negative spin reverses direction and large sequences retain phase precision");
    settings.yawMode = AntiAimYawMode::Random; settings.yawOffset = 0;
    settings.jitterRange = 120; settings.jitterInterval = 4;
    bool random = true, varied = false; float previous = 1000;
    for (std::uint32_t phase = 0; phase < 40; ++phase) {
        AimAngles first{};
        random &= AntiAimBuildAngles(local, settings, phase * 4 + 1, first);
        for (std::uint32_t inPhase = 2; inPhase <= 4; ++inPhase)
            random &= AntiAimBuildAngles(local, settings, phase * 4 + inPhase, output) && output.yaw == first.yaw;
        random &= first.yaw >= -60 && first.yaw <= 60;
        varied |= phase && first.yaw != previous; previous = first.yaw;
    }
    Expect(random && varied, "random yaw is bounded stable within its period and varies across periods");
    settings.jitterRange = 0;
    Expect(AntiAimBuildAngles(local, settings, 90, output) && output.yaw == 0,
           "zero random range produces the exact configured heading");
    settings = Settings(0);
    const std::array<AntiAimPitchMode, 6> modes{AntiAimPitchMode::Down, AntiAimPitchMode::Up,
        AntiAimPitchMode::Level, AntiAimPitchMode::Keep, AntiAimPitchMode::Custom, AntiAimPitchMode::Jitter};
    const std::array<float, 6> expected{89, -89, 0, 21, 12, 12};
    bool pitch = true;
    for (std::size_t i = 0; i < modes.size(); ++i) {
        settings.pitchMode = modes[i]; settings.pitch = 12; settings.pitchAlternate = -34;
        pitch &= AntiAimBuildAngles(local, settings, 1, output) && output.pitch == expected[i] && output.roll == 0;
    }
    settings.jitterInterval = 2;
    pitch &= AntiAimBuildAngles(local, settings, 2, output) && output.pitch == 12 &&
             AntiAimBuildAngles(local, settings, 3, output) && output.pitch == -34;
    settings.pitchMode = AntiAimPitchMode::Keep;
    pitch &= AntiAimBuildAngles({460, 0, 0}, settings, 1, output) && output.pitch == 89 &&
             AntiAimBuildAngles({-460, 0, 0}, settings, 1, output) && output.pitch == -89;
    settings.pitchMode = AntiAimPitchMode::Custom; settings.pitch = 500;
    pitch &= AntiAimBuildAngles(local, settings, 1, output) && output.pitch == 89;
    Expect(pitch, "all pitch modes honor endpoints periods wrapping and ordinary pitch bounds");
    output = {1, 2, 3}; const AimAngles unchanged = output;
    Expect(!AntiAimBuildAngles(local, settings, 0, output) &&
           !AntiAimBuildAngles({0, std::numeric_limits<float>::quiet_NaN(), 0}, settings, 1, output) &&
           std::memcmp(&output, &unchanged, sizeof(output)) == 0,
           "invalid sequence and nonfinite local angles leave the caller output untouched");
}

// The synthetic layout uses the same locally verified native offsets as the
// command tests. Payloads absent from protobuf presence are zero, as produced
// by the installed client's normal command builder.
struct Fixture {
    std::array<unsigned char, 0x90> command{}, base{};
    std::array<unsigned char, 0x38> angles{};
    std::array<unsigned char, 0x20> rep{};
    std::array<MovementSubtick, 3> steps{};
    Fixture() {
        Put(Address(command) + mo::command::Sequence, 17);
        Put(Address(command) + mo::command::Presence, std::uint32_t{0x8001});
        Put(Address(command) + mo::command::Base, Address(base));
        Put(Address(command) + mo::command::Buttons,
            MovementButtons{mo::Forward | mo::Left | 3, mo::Forward | 2, mo::Left | 1});
        Put(Address(base) + mo::base::Presence, std::uint32_t{0x80C4});
        Put(Address(base) + mo::base::Angles, Address(angles));
        Put(Address(base) + mo::base::Forward, 0.8f); Put(Address(base) + mo::base::Left, 0.2f);
        Put(Address(base) + mo::base::SubtickSize, 3); Put(Address(base) + mo::base::SubtickCapacity, 3);
        Put(Address(base) + mo::base::SubtickRep, Address(rep)); Put(Address(rep), 3);
        Put(Address(angles) + so::angle::Presence, std::uint32_t{0x8003});
        Put(Address(angles) + so::angle::Pitch, AimAngles{20, 35, 0});
        for (std::size_t i = 0; i < steps.size(); ++i) {
            Put(Address(rep) + 8 + i * 8, Address(steps[i]));
            steps[i].vtable = 0x123450; steps[i].arena = 0x543210;
            steps[i].presence = 0x8000007c; steps[i].cachedSize = 19;
            steps[i].padding.fill(std::byte{0xBA}); steps[i].when = 0.2f + 0.3f * static_cast<float>(i);
            steps[i].forwardDelta = 0.1f; steps[i].leftDelta = -0.05f;
            steps[i].pitchDelta = 2; steps[i].yawDelta = 10;
        }
    }
};
bool Same(const Fixture& a, const Fixture& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }
bool WorldWish(const AimAngles& from, const AimAngles& to, float f, float l, float correctedF, float correctedL) {
    constexpr double rad = 0.01745329251994329577;
    const double a = from.yaw * rad, b = to.yaw * rad;
    return std::abs(f * std::cos(a) - l * std::sin(a) -
                    (correctedF * std::cos(b) - correctedL * std::sin(b))) < 0.0002 &&
           std::abs(f * std::sin(a) + l * std::cos(a) -
                    (correctedF * std::sin(b) + correctedL * std::cos(b))) < 0.0002;
}
void CustomCommands() {
    Fixture f; const Fixture before = f; auto settings = Settings(); AntiAimCommandPatch patch{};
    const char* reason = nullptr; AntiAimCommandDiagnostics diagnostics{};
    const bool applied = AntiAimApplyCommand(Address(f.command), patch, settings, &reason, &diagnostics);
    Expect(applied && patch.valid && std::strcmp(reason, "applied") == 0 &&
           diagnostics.count == 3 && Near(patch.editedView.yaw, 72),
           "arbitrary configured yaw applies through the native transaction with diagnostics");
    if (!applied) return;
    Expect(WorldWish(patch.originalView, patch.editedView, 0.8f, 0.2f, patch.editedForward, patch.editedLeft),
           "arbitrary base-axis rotation preserves the horizontal world wish vector");
    bool subticks = true; AimAngles from = patch.originalView, to = patch.editedView;
    for (std::size_t i = 0; i < f.steps.size(); ++i) {
        from.yaw += before.steps[i].yawDelta; to.yaw += f.steps[i].yawDelta;
        subticks &= WorldWish(from, to, before.steps[i].forwardDelta, before.steps[i].leftDelta,
                             f.steps[i].forwardDelta, f.steps[i].leftDelta) &&
                    f.steps[i].pitchDelta == 0 && f.steps[i].yawDelta == before.steps[i].yawDelta &&
                    f.steps[i].when == before.steps[i].when && f.steps[i].vtable == before.steps[i].vtable &&
                    f.steps[i].arena == before.steps[i].arena && f.steps[i].padding == before.steps[i].padding;
    }
    Expect(subticks, "arbitrary analog subticks retain world movement mouse yaw timing and allocation metadata");
    Expect((patch.editedButtons.down & mo::Directions) == (mo::Forward | mo::Right) &&
           (patch.editedButtons.down & ~mo::Directions) == 3 && patch.editedButtons.changed == 2 &&
           patch.editedButtons.third == 1,
           "base digital directions follow final rotated axes and unrelated button words survive");
    const Fixture outgoing = f;
    bool cycles = true;
    for (int i = 0; i < 40; ++i)
        cycles &= AntiAimRestoreCommand(patch) && Same(f, before) &&
                  AntiAimReapplyCommand(patch) && Same(f, outgoing);
    Expect(cycles, "arbitrary rotations restore and reapply without accumulated movement drift");
    Put(Address(f.command) + mo::command::Sequence, 18); const Fixture stale = f;
    Expect(!AntiAimRestoreCommand(patch) && Same(f, stale), "custom command sequence changes reject stale restoration intact");

    Fixture keep; settings.pitchMode = AntiAimPitchMode::Keep;
    Expect(AntiAimApplyCommand(Address(keep.command), patch, settings) && patch.editedView.pitch == 20 &&
           keep.steps[0].pitchDelta == 2 && keep.steps[1].pitchDelta == 2 && keep.steps[2].pitchDelta == 2,
           "keep pitch retains within-command physical mouse pitch deltas");

    Fixture world; settings.pitchMode = AntiAimPitchMode::Level;
    settings.yawBase = AntiAimYawBase::World; settings.yawOffset = -48;
    Expect(AntiAimApplyCommand(Address(world.command), patch, settings) && patch.editedView.yaw == -48 &&
           WorldWish(patch.originalView, patch.editedView, 0.8f, 0.2f, patch.editedForward, patch.editedLeft),
           "world-based custom yaw preserves movement relative to the physical camera");
}
void SparseAndDigital() {
    auto settings = Settings(45); AntiAimCommandPatch patch{};
    Fixture sparse;
    Put(Address(sparse.base) + mo::base::Presence, std::uint32_t{0x8044});
    Put(Address(sparse.base) + mo::base::Left, 0.0f);
    sparse.steps[0].presence &= ~0x10u; sparse.steps[0].leftDelta = 0;
    const Fixture sparseBefore = sparse;
    const bool applied = AntiAimApplyCommand(Address(sparse.command), patch, settings);
    Expect(applied && (patch.editedPresence & 0xC0u) == 0xC0u &&
           Near(patch.editedForward, 0.8f / std::sqrt(2.0f)) && Near(patch.editedLeft, -0.8f / std::sqrt(2.0f)) &&
           (sparse.steps[0].presence & 0x18u) == 0x18u && Near(sparse.steps[0].leftDelta, -0.1f / std::sqrt(2.0f)),
           "axis mixing materializes originally absent base and subtick analog fields");
    Expect(applied && AntiAimRestoreCommand(patch) && Same(sparse, sparseBefore),
           "new analog presence and payloads restore to the exact sparse original");

    for (const auto direction : {mo::Forward, mo::Back, mo::Left, mo::Right}) {
        Fixture digital;
        for (unsigned i = 0; i < 2; ++i) {
            auto& step = digital.steps[i];
            step.button = direction; step.pressed = i == 0;
            step.presence = 0x80000067; // time, button, pressed, pitch, yaw
            // Native digital readers ignore even present analog payloads.
            step.forwardDelta = 0.37f; step.leftDelta = -0.23f;
        }
        auto& action = digital.steps[2];
        action.button = mo::Jump; action.pressed = true; action.presence = 0x80000067;
        action.forwardDelta = 0.37f; action.leftDelta = -0.23f;
        const Fixture before = digital;
        bool converted = AntiAimApplyCommand(Address(digital.command), patch, settings);
        for (unsigned i = 0; i < 2; ++i) {
            const float sign = i == 0 ? 1.0f : -1.0f;
            const float forward = direction == mo::Forward ? sign : direction == mo::Back ? -sign : 0;
            const float left = direction == mo::Left ? sign : direction == mo::Right ? -sign : 0;
            converted &= digital.steps[i].button == 0 && !digital.steps[i].pressed &&
                         !(digital.steps[i].presence & 3) && (digital.steps[i].presence & 0x18u) == 0x18u &&
                         WorldWish(patch.originalView, patch.editedView, forward, left,
                                   digital.steps[i].forwardDelta, digital.steps[i].leftDelta);
        }
        Expect(converted, "single digital press and release become rotated signed unit analog events");
        Expect(digital.steps[2].button == mo::Jump && digital.steps[2].pressed &&
               digital.steps[2].forwardDelta == before.steps[2].forwardDelta &&
               digital.steps[2].leftDelta == before.steps[2].leftDelta &&
               digital.steps[2].when == before.steps[2].when,
               "non-direction digital action preserves raw analog bytes ignored by native decoding");
        const Fixture outgoing = digital;
        Expect(converted && AntiAimRestoreCommand(patch) && Same(digital, before) &&
               AntiAimReapplyCommand(patch) && Same(digital, outgoing),
               "digital conversion restores button pressed presence and ignored raw payloads exactly");
    }
}
void PatternCommands() {
    bool patterns = true;
    for (const auto mode : {AntiAimYawMode::Jitter, AntiAimYawMode::Spin, AntiAimYawMode::Random}) {
        Fixture f; auto settings = Settings(20);
        settings.yawMode = mode; settings.jitterInterval = 3;
        settings.jitterRange = 50; settings.spinSpeed = -7;
        settings.pitchMode = AntiAimPitchMode::Jitter; settings.pitch = 18; settings.pitchAlternate = -42;
        for (const int sequence : {1, 3, 4, 7, 1001, 4, 1}) {
            Put(Address(f.command) + mo::command::Sequence, sequence);
            const Fixture before = f; AntiAimCommandPatch patch{}; AimAngles expected{};
            const bool applied = AntiAimApplyCommand(Address(f.command), patch, settings);
            patterns &= AntiAimBuildAngles({20, 35, 0}, settings, static_cast<std::uint32_t>(sequence), expected) &&
                        applied && std::memcmp(&expected, &patch.editedView, sizeof(expected)) == 0;
            if (applied) patterns &= AntiAimRestoreCommand(patch) && Same(f, before);
        }
    }
    Expect(patterns, "command rebuilds select configured jitter spin random and pitch phases from their sequence");
}
struct ReplayInput {
    AimAngles view{};
    std::uint64_t currentDown = 0;
    MovementButtons buttons{};
    std::uintptr_t command = 0;
    int calls = 0;
};
void ReplayCommand(void* context, int slot) {
    auto& input = *static_cast<ReplayInput*>(context);
    ++input.calls;
    if (slot) return;
    const auto base = Get<std::uintptr_t>(input.command + mo::command::Base);
    const auto angles = Get<std::uintptr_t>(base + mo::base::Angles);
    input.view = Get<AimAngles>(angles + so::angle::Pitch);
    // Installed B6C9F7..B6CA09 clears changed/tap and copies command down
    // into the physical input state, independently of the camera reset.
    input.buttons = {Get<MovementButtons>(input.command + mo::command::Buttons).down, 0, 0};
}
bool ReplayValid(void*) { return true; }
void PhysicalInputReplay() {
    constexpr std::array<std::array<float, 2>, 9> axes{{
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {0, 0}}};
    bool held = true, edges = true, view = true, native = true, restored = true;
    unsigned incompatibleMasks = 0;
    for (const auto mode : {AntiAimYawMode::Static, AntiAimYawMode::Jitter, AntiAimYawMode::Spin, AntiAimYawMode::Random}) {
        ReplayInput input{};
        std::uint64_t previousPhysical = 0;
        for (int sequence = 1; sequence <= 2304; ++sequence) {
            Fixture f;
            const auto& axis = axes[static_cast<std::size_t>((sequence - 1) / 256)];
            MovementButtons physical{};
            MovementEncodeStrafe(physical, axis[0], axis[1], previousPhysical);
            // Changed directions in physical input must reflect only the real
            // WASD transition, never the previous command's rotating axes.
            if ((sequence - 1) % 256)
                edges &= ((input.buttons.down ^ physical.down) & mo::Directions) == 0;
            previousPhysical = physical.down;
            physical.down |= mo::Jump;
            input.view = {12, Wrap(sequence * 0.25), 0};
            input.currentDown = physical.down; input.command = Address(f.command);
            Put(Address(f.command) + mo::command::Sequence, sequence);
            Put(Address(f.command) + mo::command::Buttons, physical);
            Put(Address(f.base) + mo::base::Forward, axis[0]); Put(Address(f.base) + mo::base::Left, axis[1]);
            Put(Address(f.angles) + so::angle::Pitch, input.view);
            auto settings = Settings(180);
            settings.yawMode = mode; settings.spinSpeed = sequence % 2 ? 137.0f : -179.0f;
            settings.jitterRange = 270; settings.jitterInterval = 1;
            AntiAimCommandPatch patch{};
            if (!AntiAimApplyCommand(Address(f.command), patch, settings)) { held = false; break; }
            incompatibleMasks += ((patch.editedButtons.down ^ physical.down) & mo::Directions) != 0;
            // Native action changes and its edge reset must survive the wrapper.
            auto outgoingButtons = patch.editedButtons;
            outgoingButtons.down &= ~mo::Jump; outgoingButtons.down |= 0x20;
            Put(Address(f.command) + mo::command::Buttons, outgoingButtons);
            const auto outgoingCommand = f.command; const auto outgoingBase = f.base;
            const auto physicalView = input.view;
            const int before = input.calls;
            const bool preserved = AntiAimReplayPreservingInput(Address(input.view), Address(input.currentDown), Address(input.buttons.down), true,
                &ReplayCommand, &input, 0, &ReplayValid, &input);
            held &= preserved && (input.buttons.down & mo::Directions) == (physical.down & mo::Directions);
            view &= std::memcmp(&input.view, &physicalView, sizeof(physicalView)) == 0;
            native &= input.calls == before + 1 && input.buttons.changed == 0 && input.buttons.third == 0 &&
                (input.buttons.down & ~mo::Directions) == (outgoingButtons.down & ~mo::Directions);
            restored &= f.command == outgoingCommand && f.base == outgoingBase;
            Put(Address(f.command) + mo::command::Buttons, patch.editedButtons);
            restored &= AntiAimRestoreCommand(patch);
        }
    }
    Expect(held && incompatibleMasks > 4000,
        "9216 static jitter spin and random command replays retain physical WASD despite corrected command masks");
    Expect(edges, "held physical directions do not generate artificial input edges after fast angle changes");
    Expect(view && native, "replay preserves the fresh physical view while retaining native action changes and edge reset");
    Expect(restored, "physical-input replay leaves corrected wire commands and command restoration intact");
}
struct HandoffFixture : Fixture {
    std::array<std::uintptr_t, 9> grown{};
    MovementSubtick generated{};
    int allocations = 0, appends = 0;
    HandoffFixture() {
        Put(Address(base) + 0x18, Address(*this));
        generated.vtable = 0x123450; generated.arena = Address(*this);
    }
    int Size() { return Get<int>(Address(base) + mo::base::SubtickSize); }
    std::uintptr_t Rep() { return Get<std::uintptr_t>(Address(base) + mo::base::SubtickRep); }
    std::uintptr_t Step(int i) { return Get<std::uintptr_t>(Rep() + 8 + static_cast<std::size_t>(i) * 8); }
};
HandoffFixture* allocatingHandoff = nullptr;
void* HandoffAllocate(void* arena) {
    if (!allocatingHandoff || arena != allocatingHandoff) return nullptr;
    ++allocatingHandoff->allocations;
    return &allocatingHandoff->generated;
}
void* HandoffAppend(void* repeated, void* step) {
    auto* f = allocatingHandoff;
    if (!f || repeated != reinterpret_cast<void*>(Address(f->base) + 0x18)) return nullptr;
    ++f->appends;
    const int count = f->Size();
    if (count != 3 || step != &f->generated) return nullptr;
    std::memcpy(f->grown.data(), reinterpret_cast<const void*>(f->Rep()), 8 + static_cast<std::size_t>(count) * 8);
    Put(Address(f->grown), count + 1);
    Put(Address(f->grown) + 8 + static_cast<std::size_t>(count) * 8, Address(f->generated));
    Put(Address(f->base) + mo::base::SubtickRep, Address(f->grown));
    Put(Address(f->base) + mo::base::SubtickCapacity, 8);
    Put(Address(f->base) + mo::base::SubtickSize, count + 1);
    return step;
}
// Own every pointer used by the copied wire command, including its subticks.
// Restoring the producer after this copy must not affect prediction ingestion.
struct CopiedHandoff {
    std::array<unsigned char, 0x90> command{}, base{};
    std::array<unsigned char, 0x38> angles{};
    std::array<std::uintptr_t, 9> rep{};
    std::array<MovementSubtick, 4> steps{};
    int count = 0;
    void Copy(HandoffFixture& f) {
        command = f.command; base = f.base; angles = f.angles; count = f.Size();
        Put(Address(command) + mo::command::Base, Address(base));
        Put(Address(base) + mo::base::Angles, Address(angles));
        Put(Address(base) + mo::base::SubtickRep, Address(rep));
        Put(Address(base) + mo::base::SubtickCapacity, 8);
        Put(Address(base) + mo::base::Presence, Get<std::uint32_t>(Address(base) + mo::base::Presence) | 1u);
        Put(Address(rep), count);
        for (int i = 0; i < count; ++i) {
            steps[static_cast<std::size_t>(i)] = Get<MovementSubtick>(f.Step(i));
            Put(Address(rep) + 8 + static_cast<std::size_t>(i) * 8, Address(steps[static_cast<std::size_t>(i)]));
        }
    }
};
struct PreviousCommand {
    std::uintptr_t command = 0;
    int requested = 0;
};
void* LookupPrevious(void* controller, int sequence) {
    auto& previous = *static_cast<PreviousCommand*>(controller);
    previous.requested = sequence;
    return reinterpret_cast<void*>(previous.command);
}
void PredictionRebuildHandoffs() {
    static AntiAimHistoryState history{};
    AntiAimHistoryReset(history);
    bool selected = true, intervals = true, deterministic = true;
    unsigned oldFailures = 0, rebuilds = 0;
    for (const auto mode : {AntiAimYawMode::Static, AntiAimYawMode::Jitter, AntiAimYawMode::Spin, AntiAimYawMode::Random}) {
        AntiAimHistoryReset(history);
        auto settings = Settings(180); settings.yawMode = mode;
        settings.jitterRange = 270; settings.jitterInterval = 1; settings.spinSpeed = 137;
        AntiAimHistoryCommand previous{};
        previous.sequence = 1; previous.source = 77;
        previous.physical = previous.outgoing = {12, 23, 0}; previous.anglePresence = 3;
        previous.forward = 1; previous.directions = mo::Forward;
        for (int sequence = 2; sequence <= 385; ++sequence) {
            Fixture predecessor;
            Put(Address(predecessor.command) + mo::command::Sequence, previous.sequence);
            Put(Address(predecessor.command) + mo::command::Buttons, MovementButtons{previous.directions, 0, 0});
            Put(Address(predecessor.base) + mo::base::Forward, previous.forward);
            Put(Address(predecessor.base) + mo::base::Left, previous.left);
            Put(Address(predecessor.angles) + so::angle::Presence, previous.anglePresence);
            Put(Address(predecessor.angles) + so::angle::Pitch, previous.outgoing);
            selected &= AntiAimHistoryPublish(history, previous);
            PreviousCommand controller{Address(predecessor.command)};
            Fixture current;
            const AimAngles physical{12, Wrap(sequence * 0.125), 0};
            Put(Address(current.command) + mo::command::Sequence, sequence);
            Put(Address(current.command) + mo::command::Buttons, MovementButtons{mo::Forward, 0, 0});
            Put(Address(current.base) + mo::base::Forward, 1.0f); Put(Address(current.base) + mo::base::Left, 0.0f);
            Put(Address(current.base) + mo::base::SubtickSize, 0);
            Put(Address(current.angles) + so::angle::Pitch, physical);
            AntiAimCommandPatch patch{};
            selected &= AntiAimApplyCommand(Address(current.command), patch, settings);
            AntiAimHistoryCommand latestIngest{};
            selected &= AntiAimHistoryReadCommand(Address(current.command), 77, latestIngest);
            latestIngest.physical = physical; latestIngest.edited = true;
            // Simulate ingest of N before rebuilding N again. The original
            // runtime selected these axes and produced a zero boundary even
            // though simulation of N restarts from N-1's different axes.
            selected &= AntiAimHistoryPublish(history, latestIngest);
            if (!WorldWish(physical, patch.editedView, 1, 0, previous.forward, previous.left)) ++oldFailures;
            float firstF = 0, firstL = 0;
            for (int replay = 0; replay < 3; ++replay) {
                AntiAimHistoryCommand observed{}, matched{};
                selected &= AntiAimHistoryReadPrevious(&LookupPrevious, &controller, sequence, 77, observed) &&
                    controller.requested == sequence - 1 && AntiAimHistoryMatch(history, observed, matched) &&
                    matched.sequence == previous.sequence;
                const auto physicalHistory = matched.edited ? AntiAimCorrectWalkMovement(matched.outgoing, matched.physical,
                    matched.forward, matched.left) : AntiAimMovement{matched.forward, matched.left, 0, true};
                const auto target = AntiAimCorrectWalkMovement(physical, patch.editedView, physicalHistory.forward, physicalHistory.left);
                const float deltaF = target.forward - observed.forward, deltaL = target.left - observed.left;
                intervals &= physicalHistory.valid && target.valid && WorldWish(physical, patch.editedView, 1, 0,
                    previous.forward + deltaF, previous.left + deltaL);
                if (replay) deterministic &= deltaF == firstF && deltaL == firstL;
                else { firstF = deltaF; firstL = deltaL; }
                ++rebuilds;
            }
            previous = latestIngest;
        }
    }
    Expect(selected && rebuilds == 4608, "4608 prediction rebuilds select sequence-1 while the latest ingested command is sequence N");
    Expect(intervals && deterministic, "rebuilt fast spin and jitter commands retain identical correct time-zero movement boundaries");
    Expect(oldFailures > 1000, "latest-ingestion zero boundaries reproduce sideways world wish in over 1000 changing-angle commands");
}
void HistoricalHandoffs() {
    AntiAimHistoryState history{}; AntiAimHistoryCommand previous{};
    float historicalF = 0, historicalL = 0, previousPhysicalF = 0, previousPhysicalL = 0;
    bool pipeline = true, epochs = true, ordered = true, restored = true, intervals = true;
    unsigned editedCount = 0, passthroughCount = 0, boundaryCount = 0, copies = 0;
    constexpr std::array<std::array<float, 2>, 9> inputs{{
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {0, 0}}};
    for (int tick = 0; tick < 6144 && pipeline; ++tick) {
        const std::uint64_t source = static_cast<std::uint64_t>(tick / 1024 + 1);
        if (tick && tick % 1024 == 0) {
            auto stale = previous; stale.source = source; AntiAimHistoryCommand matched{};
            epochs &= !AntiAimHistoryMatch(history, stale, matched);
            AntiAimHistoryReset(history);
            epochs &= !AntiAimHistoryMatch(history, previous, matched);
            // A new movement-service lifecycle has new native history too.
            previous = {}; historicalF = historicalL = previousPhysicalF = previousPhysicalL = 0;
        }
        HandoffFixture current;
        const int sequence = tick % 1024 + 1;
        auto settings = Settings(static_cast<float>(tick % 241 - 120));
        settings.yawMode = static_cast<AntiAimYawMode>((tick / 256) % 4);
        settings.yawBase = tick / 128 % 2 ? AntiAimYawBase::World : AntiAimYawBase::View;
        settings.jitterInterval = tick / 64 % 9 + 1;
        settings.jitterRange = static_cast<float>(20 + tick / 32 % 121);
        settings.spinSpeed = tick / 48 % 2 ? -13.0f : 17.0f;
        settings.pitchMode = static_cast<AntiAimPitchMode>((tick / 96) % 6);
        settings.pitch = static_cast<float>(tick / 16 % 151 - 75);
        settings.pitchAlternate = -settings.pitch;
        const AimAngles physical{static_cast<float>(tick % 157 - 78), Wrap(tick * 13.25), 0};
        const auto& desired = inputs[tick < 128 ? 0 : static_cast<std::size_t>(tick / 16) % inputs.size()];
        const float endF = desired[0], endL = desired[1];
        const float middleF = tick % 5 ? endF : 0.5f * (previousPhysicalF + endF);
        const float middleL = tick % 5 ? endL : 0.5f * (previousPhysicalL + endL);
        Put(Address(current.command) + mo::command::Sequence, sequence);
        Put(Address(current.base) + mo::base::Forward, endF); Put(Address(current.base) + mo::base::Left, endL);
        Put(Address(current.angles) + so::angle::Pitch, physical);
        MovementButtons buttons{}; MovementEncodeStrafe(buttons, endF, endL, 0);
        const bool action = tick % 17 == 0;
        if (action) { buttons.down |= 1; buttons.changed |= 1; }
        Put(Address(current.command) + mo::command::Buttons, buttons);
        current.steps[0] = MovementAnalogTransition(middleF, middleL, previousPhysicalF, previousPhysicalL, 0.25f);
        current.steps[1] = MovementJumpTransition(true); current.steps[1].when = 0.4f;
        current.steps[2] = MovementAnalogTransition(endF, endL, middleF, middleL, 0.65f);
        const auto originals = current.steps;
        const bool edit = tick % 101 >= 9 && !action;
        AntiAimCommandPatch command{}; AntiAimBoundaryPatch boundary{};
        if (edit) { pipeline &= AntiAimApplyCommand(Address(current.command), command, settings); ++editedCount; }
        else ++passthroughCount;
        // Recover the ingested basis, then hand that historical analog to the
        // current command. The assertions below use independent world math.
        auto physicalHistory = AntiAimMovement{historicalF, historicalL, 0, true};
        if (previous.edited)
            physicalHistory = AntiAimCorrectWalkMovement(previous.outgoing, previous.physical, historicalF, historicalL);
        const auto outgoing = Get<AimAngles>(Address(current.angles) + so::angle::Pitch);
        const auto target = edit ? AntiAimCorrectWalkMovement(physical, outgoing, physicalHistory.forward, physicalHistory.left) : physicalHistory;
        pipeline &= physicalHistory.valid && target.valid;
        const float deltaF = target.forward - historicalF, deltaL = target.left - historicalL;
        const bool needed = deltaF != 0 || deltaL != 0;
        if (needed) {
            allocatingHandoff = &current;
            pipeline &= AntiAimAppendBoundary(Address(current.command), deltaF, deltaL,
                reinterpret_cast<std::uintptr_t>(&HandoffAllocate), reinterpret_cast<std::uintptr_t>(&HandoffAppend), boundary);
            ++boundaryCount;
            ordered &= current.Size() == 4 && current.Step(0) == Address(current.generated) &&
                current.generated.when == 0 && current.generated.button == 0 && current.generated.presence == 0x1c;
            if (edit) pipeline &= AntiAimRebindCommandContainer(boundary, command);
        }
        AntiAimHistoryCommand published{};
        pipeline &= AntiAimHistoryReadCommand(Address(current.command), source, published);
        published.physical = physical; published.edited = edit;
        pipeline &= AntiAimHistoryPublish(history, published);
        CopiedHandoff copied; copied.Copy(current); ++copies;
        if (needed) restored &= AntiAimRestoreBoundary(boundary);
        if (edit) restored &= AntiAimRestoreCommand(command);
        restored &= std::memcmp(current.steps.data(), originals.data(), sizeof(originals)) == 0 &&
            Get<AimAngles>(Address(current.angles) + so::angle::Pitch).yaw == physical.yaw;
        command = {}; boundary = {}; // Native restoration ownership has ended.
        AntiAimHistoryCommand observed{}, matched{};
        pipeline &= AntiAimHistoryReadCommand(Address(copied.command), source, observed) &&
            AntiAimHistoryMatch(history, observed, matched) && matched.edited == edit &&
            matched.physical.pitch == physical.pitch && matched.physical.yaw == physical.yaw;
        float simulatedF = historicalF, simulatedL = historicalL;
        float expectedF = previousPhysicalF, expectedL = previousPhysicalL;
        for (int i = 0; i < copied.count; ++i) {
            const auto& step = copied.steps[static_cast<std::size_t>(i)];
            // Check every interval, including time zero before physical input.
            if (step.when > 0)
                intervals &= WorldWish(physical, outgoing, expectedF, expectedL, simulatedF, simulatedL);
            // Installed native decoding uses the raw button discriminator.
            if (!step.button) { simulatedF += step.forwardDelta; simulatedL += step.leftDelta; }
            if (step.when == 0.25f) { expectedF = middleF; expectedL = middleL; }
            if (step.when == 0.65f) { expectedF = endF; expectedL = endL; }
            intervals &= WorldWish(physical, outgoing, expectedF, expectedL, simulatedF, simulatedL);
        }
        intervals &= WorldWish(physical, outgoing, endF, endL, simulatedF, simulatedL) &&
            Near(simulatedF, observed.forward) && Near(simulatedL, observed.left);
        previous = matched;
        historicalF = observed.directions & (mo::Forward | mo::Back) ? observed.forward : 0;
        historicalL = observed.directions & (mo::Left | mo::Right) ? observed.left : 0;
        previousPhysicalF = endF; previousPhysicalL = endL;
    }
    allocatingHandoff = nullptr;
    Expect(pipeline && copies == 6144 && editedCount > 5000 && passthroughCount > 500,
           "6144 corrected commands publish and match after deep copying through all patterns bases pitches and action handoffs");
    Expect(pipeline && intervals,
           "every historical boundary and physical input interval preserves held WASD diagonal and zero-input world wish");
    Expect(pipeline && ordered && boundaryCount > 4000,
           "changing bases produce actual serialized time-zero events ahead of physical analog and jump actions");
    Expect(pipeline && restored,
           "boundary growth rebind and command restoration preserve producer bytes while copied prediction commands remain valid");
    Expect(pipeline && epochs,
           "source epoch resets reject stale publications before sequence reuse without inheriting old movement history");
}
void RoundReadiness() {
    namespace off = anti_aim_offsets;
    using Round = AntiAimRoundState;
    auto* client = static_cast<unsigned char*>(VirtualAlloc(nullptr,
        off::GameRules + sizeof(std::uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Expect(client != nullptr, "round-state fixture allocated");
    if (!client) return;
    std::array<unsigned char, 0x100> rules{}, replacement{};
    const auto base = reinterpret_cast<std::uintptr_t>(client);
    const auto rulesSlot = base + off::GameRules;
    Expect(AntiAimReadRoundState(0) == Round::Unavailable &&
           AntiAimReadRoundState(base) == Round::Unavailable, "missing client/rules cannot enable angle edits");
    Put(rulesSlot, Address(rules));
    // Warmup and match-start fields are deliberately irrelevant: only freeze
    // blocks the round, so enabling mid-warmup does not await a later event.
    rules[0x41] = 1;
    Expect(AntiAimReadRoundState(base) == Round::Playable, "warmup permits anti aim before the match starts");
    rules[0x41] = 0;
    rules[off::FreezePeriod] = 1;
    Expect(AntiAimReadRoundState(base) == Round::FreezePeriod, "round-start countdown blocks angle edits");
    rules[off::FreezePeriod] = 0;
    Expect(AntiAimReadRoundState(base) == Round::Playable, "first command after freeze permits angle edits");
    rules[off::FreezePeriod] = 1;
    Expect(AntiAimReadRoundState(base) == Round::FreezePeriod, "later rounds pause again without latched readiness");
    Put(rulesSlot, Address(replacement));
    Expect(AntiAimReadRoundState(base) == Round::Playable, "replacement game rules are read on the next command");
    replacement[off::FreezePeriod] = 255;
    Expect(AntiAimReadRoundState(base) == Round::Unavailable, "malformed freeze state cannot enable angle edits");
    Put(rulesSlot, std::uintptr_t{1});
    Expect(AntiAimReadRoundState(base) == Round::Unavailable, "unreadable game rules cannot enable angle edits");
    DWORD old = 0;
    const bool protectedSlot = VirtualProtect(reinterpret_cast<void*>(rulesSlot), sizeof(std::uintptr_t), PAGE_NOACCESS, &old) != 0;
    Expect(protectedSlot && AntiAimReadRoundState(base) == Round::Unavailable, "inaccessible rules slot rejects without crashing");
    if (protectedSlot) VirtualProtect(reinterpret_cast<void*>(rulesSlot), sizeof(std::uintptr_t), old, &old);
    Put(rulesSlot, Address(rules));
    rules[off::FreezePeriod] = 0;
    Expect(AntiAimReadRoundState(base) == Round::Playable, "readiness recovers after unavailable state");
    VirtualFree(client, 0, MEM_RELEASE);
}
void Rejections() {
    const auto settings = Settings();
    for (int configuration = 0; configuration < 4; ++configuration) {
        auto options = settings;
        if (configuration) {
            options.yawOffset = configuration == 1 ? 0.0f : configuration == 2 ? 37.0f : 180.0f;
            options.pitchMode = AntiAimPitchMode::Custom;
        }
        for (int kind = 0; kind < 8; ++kind) {
            Fixture f; AntiAimCommandPatch patch{};
            if (kind == 0) { f.steps[1].button = mo::Forward | mo::Jump; f.steps[1].presence |= 3; }
            if (kind == 1) { f.steps[1].button = mo::Forward | mo::Left; f.steps[1].presence |= 3; }
            if (kind == 2) f.steps[1].presence &= ~0x20u; // nonzero absent pitch
            if (kind == 3) f.steps[1].presence &= ~0x40u; // nonzero absent yaw
            if (kind == 4) f.steps[1].presence &= ~0x10u; // nonzero absent analog on button zero
            if (kind == 5) Put(Address(f.base) + mo::base::Presence, std::uint32_t{0x8044});
            if (kind == 6 || kind == 7) {
                f.steps[1].button = mo::Forward; f.steps[1].pressed = true;
                f.steps[1].presence = (f.steps[1].presence | 3u) & ~(kind == 6 ? 2u : 1u);
            }
            const Fixture before = f;
            Expect(!AntiAimApplyCommand(Address(f.command), patch, options) && !patch.valid && Same(f, before),
                   "nonlegacy arbitrary identity and backwards settings reject mixed masks absent raw payloads and incomplete digital presence intact");
        }
    }
    Fixture f; AntiAimCommandPatch patch{};
    auto* memory = static_cast<MovementSubtick*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Expect(memory != nullptr, "custom-mode guarded subtick page allocated");
    if (!memory) return;
    *memory = f.steps[2]; Put(Address(f.rep) + 24, Address(*memory));
    DWORD previous = 0;
    const bool protectedPage = VirtualProtect(memory, 4096, PAGE_READONLY, &previous) != 0;
    const Fixture before = f; const MovementSubtick stepBefore = *memory;
    Expect(protectedPage && !AntiAimApplyCommand(Address(f.command), patch, settings) &&
           Same(f, before) && std::memcmp(memory, &stepBefore, sizeof(stepBefore)) == 0,
           "read-only last subtick prevents every earlier custom-mode transaction write");
    VirtualProtect(memory, 4096, PAGE_READWRITE, &previous);
    const bool applied = AntiAimApplyCommand(Address(f.command), patch, settings);
    Expect(applied, "custom command applies when final subtick is writable");
    if (applied) {
        VirtualProtect(memory, 4096, PAGE_READONLY, &previous); const Fixture outgoing = f;
        Expect(!AntiAimRestoreCommand(patch) && Same(f, outgoing), "custom restore preflights every subtick before writes");
        VirtualProtect(memory, 4096, PAGE_READWRITE, &previous);
    }
    VirtualFree(memory, 0, MEM_RELEASE);
}
void DigitalBasisFacing() {
    bool equivalent = true, nearCamera = true, fixedPoints = true, sameAcceleration = true;
    constexpr double radians = 0.01745329251994329577;
    for (float camera : {-540.0f, -179.9f, -73.0f, 0.0f, 47.0f, 179.9f, 720.0f}) {
        for (int angle = -1440; angle <= 1440; ++angle) {
            const float wish = angle * 0.5f;
            const auto basis = AntiAimChooseDigitalBasis(camera, wish);
            fixedPoints &= basis.valid && std::abs(basis.forward) <= 1 && std::abs(basis.left) <= 1 &&
                std::round(basis.forward) == basis.forward && std::round(basis.left) == basis.left &&
                (basis.forward != 0 || basis.left != 0);
            nearCamera &= std::abs(Wrap(basis.yaw - Wrap(camera))) <= 22.50002f;
            // Independently combine native horizontal basis, normalize and cap
            // wish speed. This catches diagonal magnitude or direction changes.
            const double y = basis.yaw * radians;
            double x = basis.forward * std::cos(y) - basis.left * std::sin(y);
            double z = basis.forward * std::sin(y) + basis.left * std::cos(y);
            const double length = std::hypot(x, z);
            x /= length; z /= length;
            const double idealX = std::cos(Wrap(wish) * radians), idealY = std::sin(Wrap(wish) * radians);
            equivalent &= std::abs(x - idealX) < 0.000002 && std::abs(z - idealY) < 0.000002;
            const double speed = std::min(250.0, length * 250);
            const double impulse = std::min(12 * speed / 64, std::max(0.0, 30 - (250 * x + 80 * z)));
            const double idealImpulse = std::min(12 * 250.0 / 64, std::max(0.0, 30 - (250 * idealX + 80 * idealY)));
            sameAcceleration &= std::abs(impulse * x - idealImpulse * idealX) < 0.0001 &&
                std::abs(impulse * z - idealImpulse * idealY) < 0.0001;
        }
    }
    Expect(fixedPoints && equivalent, "all world headings retain their exact normalized wish through native digital rounding");
    Expect(nearCamera, "nearest equivalent digital basis bounds command facing within 22.5 degrees across camera seams");
    Expect(sameAcceleration, "native normalization and wish-speed clamp give cardinal and diagonal encodings the same air impulse");
    const auto left = AntiAimChooseDigitalBasis(0, 90), right = AntiAimChooseDigitalBasis(0, -90);
    Expect(left.valid && right.valid && left.yaw == 0 && right.yaw == 0 &&
        left.forward == 0 && right.forward == 0 && left.left == 1 && right.left == -1,
        "alternating perpendicular strafe wishes switch side input without the previous 180-degree body flip");
    bool oppositeStable = true;
    for (float wish : {-157.5f, -112.5f, -67.5f, -22.5f, 22.5f, 67.5f, 112.5f, 157.5f})
        oppositeStable &= Near(AntiAimChooseDigitalBasis(0, wish).yaw, AntiAimChooseDigitalBasis(0, wish + 180).yaw);
    Expect(oppositeStable, "opposite wishes retain the same facing at exact digital-sector ties");
    Expect(!AntiAimChooseDigitalBasis(std::numeric_limits<float>::quiet_NaN(), 0).valid &&
        !AntiAimChooseDigitalBasis(0, std::numeric_limits<float>::infinity()).valid,
        "nonfinite facing inputs do not publish a command basis");
}
void PrepareDigitalMovement(Fixture& f, float forward = 1, float left = 0) {
        MovementButtons buttons{};
        MovementEncodeStrafe(buttons, forward, left, 0);
        buttons.down |= mo::Jump;
        Put(Address(f.command) + mo::command::Buttons, buttons);
        Put(Address(f.base) + mo::base::Presence, std::uint32_t{0x8004} |
            (forward != 0 ? mo::base::ForwardPresent : 0u) | (left != 0 ? mo::base::LeftPresent : 0u));
        Put(Address(f.base) + mo::base::Forward, forward); Put(Address(f.base) + mo::base::Left, left);
        for (std::size_t i = 0; i < f.steps.size(); ++i) {
            auto& step = f.steps[i];
            step.button = 0; step.pressed = false;
            step.presence = 0x80000064; // time and native mouse camera payload
            step.forwardDelta = 0; step.leftDelta = 0;
            step.pitchDelta = 1; step.yawDelta = i ? 10.0f : 0.0f;
        }
        f.steps[0].when = 0; f.steps[0].pitchDelta = 0;
        f.steps[0].forwardDelta = forward - 0.6f; f.steps[0].leftDelta = left - 0.2f;
        f.steps[0].presence = 0x8000001C;
        f.steps[2].button = mo::Jump; f.steps[2].pressed = true; f.steps[2].presence |= 3;
}
void QuantizedMovementBasis() {
    const auto sameWish = [](const AntiAimCommandPatch& patch, float wish) {
        const double axisYaw = std::atan2(static_cast<double>(patch.editedLeft), patch.editedForward) * 57.29577951308232;
        return std::abs(Wrap(static_cast<double>(patch.editedView.yaw) + axisYaw - wish)) < 0.0002f;
    };
    const auto prepare = [](Fixture& f, float forward = 1, float left = 0) { PrepareDigitalMovement(f, forward, left); };
    for (const float yaw : {37.0f, 179.9f, -180.0f, 721.25f, -1080.5f}) {
        Fixture f; prepare(f); const Fixture before = f;
        AntiAimCommandPatch patch{}; const char* reason = nullptr;
        const bool applied = AntiAimApplyMovementCommand(Address(f.command), yaw, Address(f.steps[0]), patch, &reason);
        Expect(applied && patch.valid && patch.editedView.pitch == 20 && sameWish(patch, yaw) &&
               std::abs(Wrap(patch.editedView.yaw - patch.originalView.yaw)) <= 22.50002f &&
               std::round(patch.editedForward) == patch.editedForward && std::round(patch.editedLeft) == patch.editedLeft &&
               Get<float>(Address(f.base) + mo::base::Forward) == patch.editedForward &&
               Get<float>(Address(f.base) + mo::base::Left) == patch.editedLeft,
               "equivalent digital wish stays near physical facing with exact native axes and physical pitch");
        if (!applied) continue;
        const auto rotatedPrevious = AntiAimCorrectWalkMovement(patch.originalView, patch.editedView, 0.6f, 0.2f);
        MovementButtons expectedDirections{};
        MovementEncodeStrafe(expectedDirections, patch.editedForward, patch.editedLeft, 0);
        Expect(rotatedPrevious.valid && Near(f.steps[0].forwardDelta + rotatedPrevious.forward, patch.editedForward) &&
               Near(f.steps[0].leftDelta + rotatedPrevious.left, patch.editedLeft) &&
               (patch.editedButtons.down & mo::Directions) == expectedDirections.down &&
               !(patch.editedButtons.changed & mo::Directions) && !(patch.editedButtons.third & mo::Directions),
               "owned time-zero event replaces digital fallback with ideal axes after predecessor basis handoff");
        bool fixed = true;
        for (const auto& step : f.steps) fixed &= step.pitchDelta == 0 && step.yawDelta == 0 && !(step.presence & 0x60);
        Expect(fixed && f.steps[2].button == mo::Jump && f.steps[2].pressed && f.steps[2].when == before.steps[2].when,
               "assisted command fixes every subtick view basis while jump edges metadata and timing survive");
        const Fixture outgoing = f;
        bool cycles = true;
        for (int i = 0; i < 50; ++i) {
            cycles &= AntiAimSetReplayView(patch, true) && f.angles == before.angles;
            cycles &= AntiAimSetReplayView(patch, false) && Same(f, outgoing);
            cycles &= AntiAimRestoreCommand(patch) && Same(f, before);
            cycles &= AntiAimReapplyCommand(patch) && Same(f, outgoing);
        }
        Expect(cycles, "quantized basis replay and rebuild restores retain every physical mouse delta without accumulated rotation");
    }
    for (const auto axes : {std::array<float, 2>{1, 1}, {-1, 0}, {0, -1}, {-1, -1}}) {
        Fixture f; prepare(f, axes[0], axes[1]); const Fixture before = f; AntiAimCommandPatch patch{};
        const bool applied = AntiAimApplyMovementCommand(Address(f.command), -127.5f, Address(f.steps[0]), patch);
        const auto prior = AntiAimCorrectWalkMovement(patch.originalView, patch.editedView, 0.6f, 0.2f);
        Expect(applied && sameWish(patch, -127.5f) && prior.valid &&
               Near(prior.forward + f.steps[0].forwardDelta, patch.editedForward) &&
               Near(prior.left + f.steps[0].leftDelta, patch.editedLeft) &&
               AntiAimRestoreCommand(patch) && Same(f, before),
               "all cardinal and diagonal digital fallbacks upgrade to the same ideal trajectory and restore exactly");
    }
    for (int kind : {2, 5, 6, 7, 8, 9, 10, 11}) {
        Fixture f; prepare(f); AntiAimCommandPatch patch{};
        auto buttons = Get<MovementButtons>(Address(f.command) + mo::command::Buttons);
        if (kind == 2) buttons.third |= 0x20; // use tap
        Put(Address(f.command) + mo::command::Buttons, buttons);
        if (kind == 5) Put(Address(f.base) + mo::base::Forward, 0.6f);
        if (kind == 6) f.steps[0].when = 0.1f;
        if (kind == 7) { f.steps[1].forwardDelta = 0.25f; f.steps[1].presence |= 8; }
        if (kind == 8) Put(Address(f.angles) + so::angle::Pitch, AimAngles{90, 35, 0});
        if (kind == 9) Put(Address(f.command) + mo::command::Sequence, 0);
        if (kind == 11) { f.steps[1].button = 0x20; f.steps[1].presence |= 3; }
        const Fixture before = f;
        const auto transition = kind == 10 ? Address(f.steps[0]) + 8 : Address(f.steps[0]);
        Expect(!AntiAimApplyMovementCommand(Address(f.command), 62, transition, patch) && !patch.valid && Same(f, before),
               "Use non-digital fallback stale transition identity and unsupported scopes leave safe fallback untouched");
        auto antiAim = Settings();
        Expect(!AntiAimApplyMovementCommand(Address(f.command), 62, transition, patch, nullptr, nullptr, nullptr, &antiAim) &&
               !patch.valid && Same(f, before),
               "anti aim composition retains Use and ownership rejection without invoking legacy payload relaxations");
    }
    Fixture f; prepare(f); AntiAimCommandPatch patch{};
    const Fixture before = f;
    Expect(!AntiAimApplyMovementCommand(Address(f.command), std::numeric_limits<float>::quiet_NaN(), Address(f.steps[0]), patch) && Same(f, before),
           "invalid requested heading cancels upgrade without touching digital input");
    Expect(AntiAimApplyMovementCommand(Address(f.command), 62, Address(f.steps[0]), patch), "stale quantized ownership fixture applies");
    Put(Address(f.command) + mo::command::Sequence, 18); const Fixture replaced = f;
    Expect(!AntiAimRestoreCommand(patch) && Same(f, replaced), "recycled sequence refuses stale movement basis restoration");
    for (const auto historical : {AntiAimMovementHistory{-1, 0.75f}, {1, 0}, {0, -1}, {0.5f, -0.5f}}) {
        HandoffFixture current; prepare(current, -1, 1);
        const Fixture physicalBefore = current; const auto repBefore = current.Rep();
        allocatingHandoff = nullptr; // Native allocation would fail; this path must not call it.
        AntiAimCommandPatch owned{};
        const bool applied = AntiAimApplyMovementCommand(Address(current.command), 126.5f, Address(current.steps[0]), owned,
                                                         nullptr, nullptr, &historical);
        Expect(applied && sameWish(owned, 126.5f) &&
               Near(historical.forward + current.steps[0].forwardDelta, owned.editedForward) &&
               Near(historical.left + current.steps[0].leftDelta, owned.editedLeft) && current.Size() == 3 && current.Rep() == repBefore &&
               current.allocations == 0 && current.appends == 0,
               "native predecessor handoff coalesces into owned event with exact ideal axes under allocation exhaustion");
        AntiAimHistoryCommand wire{}; AntiAimHistoryState history{}; AntiAimHistoryCommand matched{};
        const bool published = AntiAimHistoryReadCommand(Address(current.command), 51, wire);
        wire.edited = true; wire.physical = owned.originalView;
        const auto idealPhysical = AntiAimCorrectWalkMovement(wire.outgoing, wire.physical, wire.forward, wire.left);
        const float rawMagnitude = std::hypot(owned.editedForward, owned.editedLeft);
        const auto expectedIdeal = AntiAimCorrectWalkMovement({20, 126.5f, 0}, owned.originalView, rawMagnitude, 0);
        Expect(published && AntiAimHistoryPublish(history, wire) && AntiAimHistoryMatch(history, wire, matched) &&
               matched.edited && idealPhysical.valid && expectedIdeal.valid && Near(idealPhysical.forward, expectedIdeal.forward) &&
               Near(idealPhysical.left, expectedIdeal.left) &&
               !(Near(idealPhysical.forward, -1) && Near(idealPhysical.left, 1)),
               "published cardinal or diagonal wish inverts to the raw ideal predecessor without losing diagonal magnitude");
        const Fixture outgoing = current;
        bool repeat = applied;
        for (int rebuild = 0; rebuild < 20; ++rebuild) {
            repeat &= AntiAimRestoreCommand(owned) && Same(current, physicalBefore);
            repeat &= AntiAimReapplyCommand(owned) && Same(current, outgoing);
        }
        Expect(repeat && AntiAimRestoreCommand(owned), "coalesced ideal handoff restores and reapplies without a second boundary or accumulated correction");
        auto buttons = Get<MovementButtons>(Address(current.command) + mo::command::Buttons);
        buttons.down |= 0x800; Put(Address(current.command) + mo::command::Buttons, buttons);
        current.steps[1].button = 0x20; current.steps[1].presence |= 3; current.steps[1].pressed = true;
        const Fixture fallbackBefore = current;
        const bool rejectedUpgrade = !AntiAimApplyMovementCommand(Address(current.command), 126.5f, Address(current.steps[0]), owned,
                                                                   nullptr, nullptr, &historical);
        const bool handoff = AntiAimApplyMovementHandoff(Address(current.command), Address(current.steps[0]), historical, owned);
        Expect(rejectedUpgrade && handoff && Near(historical.forward + current.steps[0].forwardDelta, -1) &&
               Near(historical.left + current.steps[0].leftDelta, 1) && current.angles == fallbackBefore.angles &&
               current.command == fallbackBefore.command && current.base == fallbackBefore.base &&
               std::memcmp(&current.steps[1], &fallbackBefore.steps[1], 2 * sizeof(MovementSubtick)) == 0 &&
               current.Size() == 3 && current.Rep() == repBefore && !current.allocations && !current.appends,
               "action-blocked upgrade retains digital fallback with native predecessor handoff and exact camera/action bytes");
        const Fixture outgoingFallback = current;
        repeat = handoff;
        for (int rebuild = 0; rebuild < 20; ++rebuild) {
            repeat &= AntiAimRestoreCommand(owned) && Same(current, fallbackBefore);
            repeat &= AntiAimReapplyCommand(owned) && Same(current, outgoingFallback);
        }
        Expect(repeat, "digital fallback handoff ownership restores original planner event on every rebuild");
    }
}
void FiringMovementCoexistence() {
    for (bool combined : {false, true}) for (int kind = 0; kind < 8; ++kind) {
        Fixture f; PrepareDigitalMovement(f);
        std::array<unsigned char, 0x78> history{};
        std::array<unsigned char, 0x38> historyAngles{};
        std::array<unsigned char, 0x10> historyRep{};
        Put(Address(f.command) + so::command::HistorySize, 1);
        Put(Address(f.command) + so::command::HistoryCapacity, 1);
        Put(Address(f.command) + so::command::HistoryRep, Address(historyRep));
        Put(Address(historyRep), 1); Put(Address(historyRep) + 8, Address(history));
        Put(Address(history) + so::history::Presence, std::uint32_t{1});
        Put(Address(history) + so::history::Angles, Address(historyAngles));
        Put(Address(historyAngles) + so::angle::Presence, std::uint32_t{7});
        Put(Address(historyAngles) + so::angle::Pitch, AimAngles{-7, 112, 0});
        auto buttons = Get<MovementButtons>(Address(f.command) + mo::command::Buttons);
        if (kind == 0) buttons.down |= 1;
        if (kind == 1) buttons.changed |= 1;
        if (kind == 2) buttons.third |= 1;
        if (kind == 3) {
            Put(Address(f.command) + mo::command::Presence, std::uint32_t{0x8021});
            Put(Address(f.command) + so::command::Attack1Index, 0);
        }
        if (kind == 4 || kind == 7) {
            f.steps[1].button = kind == 4 ? 1 : 0x800;
            f.steps[1].pressed = true; f.steps[1].presence |= 3;
        }
        if (kind == 5) buttons.down |= 0x800;
        if (kind == 6) buttons.changed |= 0x800;
        Put(Address(f.command) + mo::command::Buttons, buttons);
        const Fixture before = f;
        const auto historyBefore = history;
        const auto anglesBefore = historyAngles;
        auto settings = Settings(147); settings.pitchMode = AntiAimPitchMode::Up;
        AimAngles requested{};
        AntiAimBuildAngles({20, 35, 0}, settings, 17, requested);
        const bool primary = kind < 5;
        const auto expected = AntiAimChooseDigitalBasis(combined && !primary ? requested.yaw : 35, 62);
        AntiAimCommandPatch patch{};
        const bool applied = AntiAimApplyMovementCommand(Address(f.command), 62, Address(f.steps[0]), patch,
            nullptr, nullptr, nullptr, combined ? &settings : nullptr);
        const double axisYaw = std::atan2(static_cast<double>(patch.editedLeft), patch.editedForward) * 57.29577951308232;
        Expect(applied && expected.valid && Near(patch.editedView.yaw, expected.yaw) &&
            patch.editedView.pitch == (combined && !primary ? -89 : 20) &&
            std::abs(Wrap(patch.editedView.yaw + axisYaw - 62)) < .0002f,
            "held indexed and timed firing retain ideal quantized steering while primary fire bypasses the anti-aim pattern");
        if (!applied) continue;
        const auto outgoing = Get<MovementButtons>(Address(f.command) + mo::command::Buttons);
        Expect((outgoing.down & ~mo::Directions) == (buttons.down & ~mo::Directions) &&
            (outgoing.changed & ~mo::Directions) == (buttons.changed & ~mo::Directions) &&
            (outgoing.third & ~mo::Directions) == (buttons.third & ~mo::Directions) &&
            Get<std::uint32_t>(Address(f.command) + mo::command::Presence) == patch.commandPresence &&
            Get<int>(Address(f.command) + so::command::Attack1Index) == Get<int>(Address(before.command) + so::command::Attack1Index) &&
            f.steps[1].button == before.steps[1].button && f.steps[1].pressed == before.steps[1].pressed &&
            f.steps[1].when == before.steps[1].when && f.steps[2].button == mo::Jump && f.steps[2].pressed &&
            f.steps[2].when == before.steps[2].when && history == historyBefore && historyAngles == anglesBefore,
            "firing steering preserves all attack masks native index primary history and timed jump edges");
        Put(Address(f.base) + mo::base::Presence, patch.editedPresence | 1u);
        const bool restored = AntiAimRestoreCommand(patch);
        Put(Address(f.base) + mo::base::Presence, Get<std::uint32_t>(Address(f.base) + mo::base::Presence) & ~1u);
        Expect(restored && Same(f, before), "firing movement restores exactly after native CRC presence is added");
    }
    // A real jumpbug command has Duck down, a timed Duck release and Jump
    // press. Attack events must coexist with those independently owned edges.
    for (std::uint64_t attack : {std::uint64_t{1}, std::uint64_t{0x800}}) {
        Fixture f; PrepareDigitalMovement(f);
        std::array<MovementSubtick, 6> steps{};
        std::array<unsigned char, 8 + 6 * sizeof(std::uintptr_t)> rep{};
        steps[0] = f.steps[0];
        for (unsigned i = 1; i < steps.size(); ++i) {
            steps[i].vtable = 0x123450; steps[i].arena = 0x543210; steps[i].presence = 0x80000007;
            steps[i].cachedSize = 19;
        }
        steps[1].button = mo::Duck; steps[1].pressed = true; steps[1].when = 0;
        steps[2].button = attack; steps[2].pressed = true; steps[2].when = .25f;
        steps[3].button = mo::Duck; steps[3].pressed = false; steps[3].when = .5f;
        steps[4].button = mo::Jump; steps[4].pressed = true; steps[4].when = .5f;
        steps[5].button = attack; steps[5].pressed = false; steps[5].when = .75f;
        Put(Address(rep), 6);
        for (unsigned i = 0; i < steps.size(); ++i) Put(Address(rep) + 8 + i * 8, Address(steps[i]));
        Put(Address(f.base) + mo::base::SubtickSize, 6); Put(Address(f.base) + mo::base::SubtickCapacity, 6);
        Put(Address(f.base) + mo::base::SubtickRep, Address(rep));
        const auto before = steps;
        AntiAimCommandPatch patch{};
        const bool applied = AntiAimApplyMovementCommand(Address(f.command), 62, Address(steps[0]), patch);
        Expect(applied && std::memcmp(&steps[1], &before[1], 5 * sizeof(MovementSubtick)) == 0 &&
            steps[3].when == steps[4].when && steps[3].when == .5f && !steps[3].pressed && steps[4].pressed,
            "quantized steering remains active during firing without suspending or retiming jumpbug Duck release and Jump");
        Expect(applied && AntiAimRestoreCommand(patch) && std::memcmp(steps.data(), before.data(), sizeof(steps)) == 0,
            "firing jumpbug and steering payload restores exactly on a prediction rebuild");
    }
}
void CombinedMovementAntiAim() {
    bool patterns = true, wishPreserved = true, nativeHistory = true, fixedBasis = true, restoration = true;
    unsigned combinations = 0;
    constexpr AntiAimYawMode yawModes[]{AntiAimYawMode::Static, AntiAimYawMode::Jitter, AntiAimYawMode::Spin, AntiAimYawMode::Random};
    constexpr AntiAimPitchMode pitchModes[]{AntiAimPitchMode::Down, AntiAimPitchMode::Up, AntiAimPitchMode::Level,
        AntiAimPitchMode::Keep, AntiAimPitchMode::Custom, AntiAimPitchMode::Jitter};
    for (const auto yawMode : yawModes) for (const auto yawBase : {AntiAimYawBase::View, AntiAimYawBase::World})
    for (const auto pitchMode : pitchModes) for (int sequence : {1, 2, 8, 9, 17, 65})
    for (float camera : {-179.9f, 35.0f}) for (float wish : {90.0f, -90.0f, 126.5f}) {
        Fixture f; PrepareDigitalMovement(f, -1, 1);
        Put(Address(f.command) + mo::command::Sequence, sequence);
        const AimAngles physical{20, camera, 0};
        Put(Address(f.angles) + so::angle::Pitch, physical);
        const Fixture before = f;
        auto settings = Settings(127.25f);
        settings.yawMode = yawMode; settings.yawBase = yawBase; settings.pitchMode = pitchMode;
        settings.jitterRange = 83; settings.jitterInterval = 8; settings.spinSpeed = 17.5f;
        settings.pitch = 66; settings.pitchAlternate = -42;
        AimAngles requested{};
        const bool target = AntiAimBuildAngles(physical, settings, static_cast<std::uint32_t>(sequence), requested);
        const AntiAimMovementHistory history{-0.75f, 1};
        AntiAimCommandPatch patch{}; const char* reason = nullptr;
        const bool applied = AntiAimApplyMovementCommand(Address(f.command), wish, Address(f.steps[0]), patch,
            &reason, nullptr, &history, &settings);
        ++combinations;
        if (!applied) {
            if (patterns) std::printf("composition rejected mode=%d base=%d pitch=%d sequence=%d reason=%s\n",
                static_cast<int>(yawMode), static_cast<int>(yawBase), static_cast<int>(pitchMode), sequence, reason ? reason : "unknown");
            patterns = false; continue;
        }
        patterns &= target && patch.editedView.pitch == requested.pitch &&
            std::abs(Wrap(patch.editedView.yaw - requested.yaw)) <= 22.50002f;
        const double decoded = patch.editedView.yaw +
            std::atan2(static_cast<double>(patch.editedLeft), patch.editedForward) * 57.29577951308232;
        wishPreserved &= std::abs(Wrap(decoded - wish)) < 0.0002f &&
            std::round(patch.editedForward) == patch.editedForward && std::round(patch.editedLeft) == patch.editedLeft;
        nativeHistory &= Near(history.forward + f.steps[0].forwardDelta, patch.editedForward) &&
            Near(history.left + f.steps[0].leftDelta, patch.editedLeft);
        for (const auto& step : f.steps) fixedBasis &= step.pitchDelta == 0 && step.yawDelta == 0;
        const Fixture outgoing = f;
        restoration &= AntiAimSetReplayView(patch, true) && f.angles == before.angles &&
            AntiAimSetReplayView(patch, false) && Same(f, outgoing) &&
            AntiAimRestoreCommand(patch) && Same(f, before) && AntiAimReapplyCommand(patch) && Same(f, outgoing);
    }
    std::printf("composition combinations=%u\n", combinations);
    Expect(patterns, "every configured yaw/pitch pattern composes with movement using exact pitch and yaw within 22.5 degrees of its sequence target");
    Expect(wishPreserved && nativeHistory, "composed anti aim preserves the ideal world wish and raw native predecessor handoff");
    Expect(fixedBasis && restoration, "combined patterns retain a fixed native movement basis and exact physical-camera restore/replay");

    Fixture off; PrepareDigitalMovement(off); AntiAimCommandPatch patch{};
    auto disabled = Settings(180); disabled.enabled = false; disabled.yawMode = AntiAimYawMode::Spin;
    const AntiAimMovementHistory initialHistory{1, -1};
    const bool withDisabled = AntiAimApplyMovementCommand(Address(off.command), 126.5f, Address(off.steps[0]), patch,
        nullptr, nullptr, &initialHistory, &disabled);
    const Fixture disabledOutgoing = off;
    const bool restored = AntiAimRestoreCommand(patch);
    const bool withoutSettings = AntiAimApplyMovementCommand(Address(off.command), 126.5f, Address(off.steps[0]), patch,
        nullptr, nullptr, &initialHistory);
    Expect(withDisabled && restored && withoutSettings && Same(off, disabledOutgoing),
        "disabled anti aim retains the exact revision 15 camera-relative representation");

    bool toggles = true;
    AntiAimMovementHistory previous{-1, 1};
    for (int sequence = 1; sequence <= 64; ++sequence) {
        Fixture f; PrepareDigitalMovement(f);
        Put(Address(f.command) + mo::command::Sequence, sequence);
        const Fixture before = f;
        auto settings = Settings(); settings.enabled = sequence % 3 != 0;
        settings.pitchMode = AntiAimPitchMode::Jitter; settings.pitch = 75; settings.pitchAlternate = -75;
        settings.yawMode = AntiAimYawMode::Jitter; settings.jitterRange = 110;
        AntiAimCommandPatch current{};
        const bool applied = AntiAimApplyMovementCommand(Address(f.command), sequence & 1 ? 90.0f : 126.5f,
            Address(f.steps[0]), current, nullptr, nullptr, &previous, &settings);
        toggles &= applied && Near(previous.forward + f.steps[0].forwardDelta, current.editedForward) &&
            Near(previous.left + f.steps[0].leftDelta, current.editedLeft);
        previous = {current.editedForward, current.editedLeft};
        toggles &= AntiAimRestoreCommand(current) && Same(f, before);
    }
    Expect(toggles, "AA on/off transitions retain raw cardinal/diagonal history and restore every command's physical input");
}
}
int main() {
    PatternMath(); CustomCommands(); SparseAndDigital(); PatternCommands(); PhysicalInputReplay(); PredictionRebuildHandoffs(); HistoricalHandoffs(); RoundReadiness(); Rejections(); DigitalBasisFacing(); QuantizedMovementBasis(); CombinedMovementAntiAim(); FiringMovementCoexistence();
    return failures ? 1 : 0;
}
