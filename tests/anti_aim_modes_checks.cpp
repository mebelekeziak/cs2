#include "anti_aim_command.h"
#include "anti_aim_boundary.h"
#include "anti_aim_history.h"
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
}
int main() {
    PatternMath(); CustomCommands(); SparseAndDigital(); PatternCommands(); HistoricalHandoffs(); Rejections();
    return failures ? 1 : 0;
}
