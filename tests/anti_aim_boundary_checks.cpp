#include "anti_aim_boundary.h"
#include "anti_aim_math.h"
#include "anti_aim_command.h"
#include "anti_aim_history.h"
#include "silent_aim_offsets.h"
#include "movement_offsets.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
namespace off = movement_offsets;
int failures = 0;
void Expect(bool result, const char* description) {
    std::printf("%s %s\n", result ? "PASS" : "FAIL", description); failures += !result;
}
template<class T> std::uintptr_t Address(T& value) { return reinterpret_cast<std::uintptr_t>(&value); }
template<class T> void Put(std::uintptr_t address, const T& value) { std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value)); }
template<class T> T Get(std::uintptr_t address) { T value{}; std::memcpy(&value, reinterpret_cast<void*>(address), sizeof(value)); return value; }
bool Near(float a, float b) { return std::abs(a - b) < 0.00001f; }
struct Fixture {
    std::array<unsigned char, 0x90> command{}, base{};
    std::array<unsigned char, 0x28> angles{};
    std::array<std::uintptr_t, 10> rep{}, grown{};
    std::array<MovementSubtick, 4> steps{};
    MovementSubtick generated{};
    int allocations = 0, appends = 0, mode = 0;
    Fixture() {
        Put(Address(command) + off::command::Sequence, 123);
        Put(Address(command) + off::command::Presence, std::uint32_t{1});
        Put(Address(command) + off::command::Base, Address(base));
        Put(Address(command) + off::command::Buttons, MovementButtons{off::Forward, 0, 0});
        Put(Address(base) + off::base::Presence, std::uint32_t{4 | 0x40 | 0x80});
        Put(Address(base) + off::base::Angles, Address(angles));
        Put(Address(base) + off::base::Forward, 0.8f);
        Put(Address(base) + off::base::Left, -0.1f);
        // Native base-angle producer omits the zero roll component.
        Put(Address(angles) + silent_aim_offsets::angle::Presence, std::uint32_t{3});
        Put(Address(angles) + silent_aim_offsets::angle::Pitch, AimAngles{25, 42, 0});
        Put(Address(base) + 0x18, Address(*this));
        Put(Address(base) + off::base::SubtickSize, 2);
        Put(Address(base) + off::base::SubtickCapacity, 2);
        Put(Address(base) + off::base::SubtickRep, Address(rep));
        Put(Address(rep), 2);
        for (int i = 0; i < 2; ++i) {
            steps[static_cast<std::size_t>(i)].vtable = 0x123400 + i;
            steps[static_cast<std::size_t>(i)].arena = Address(*this);
            steps[static_cast<std::size_t>(i)].presence = 7;
            steps[static_cast<std::size_t>(i)].button = i ? off::Jump : off::Forward;
            steps[static_cast<std::size_t>(i)].pressed = true;
            steps[static_cast<std::size_t>(i)].when = i ? 0.5f : 0;
            Put(Address(rep) + 8 + static_cast<std::size_t>(i) * 8, Address(steps[static_cast<std::size_t>(i)]));
        }
        generated.vtable = 0x123456; generated.arena = Address(*this); generated.cachedSize = 27;
        generated.presence = 0x44; generated.yawDelta = 7; generated.when = 0.8f;
    }
    int Size() { return Get<int>(Address(base) + off::base::SubtickSize); }
    std::uintptr_t Representation() { return Get<std::uintptr_t>(Address(base) + off::base::SubtickRep); }
    std::uintptr_t Step(int i) { return Get<std::uintptr_t>(Representation() + 8 + static_cast<std::size_t>(i) * 8); }
};
Fixture* fixture = nullptr;
void* Allocate(void* arena) {
    if (!fixture || arena != fixture) return nullptr;
    ++fixture->allocations;
    return fixture->mode == 1 ? nullptr : &fixture->generated;
}
void* Append(void* repeated, void* step) {
    if (!fixture || repeated != reinterpret_cast<void*>(Address(fixture->base) + 0x18)) return nullptr;
    ++fixture->appends;
    if (fixture->mode == 2) return nullptr;
    const auto old = fixture->Representation();
    const int size = fixture->Size();
    std::memcpy(fixture->grown.data(), reinterpret_cast<void*>(old), 8 + static_cast<std::size_t>(size) * 8);
    Put(Address(fixture->grown), size + 1);
    Put(Address(fixture->grown) + 8 + static_cast<std::size_t>(size) * 8, reinterpret_cast<std::uintptr_t>(step));
    Put(Address(fixture->base) + off::base::SubtickRep, Address(fixture->grown));
    Put(Address(fixture->base) + off::base::SubtickCapacity, 8);
    Put(Address(fixture->base) + off::base::SubtickSize, size + 1);
    return fixture->mode == 3 ? nullptr : step;
}
bool Apply(Fixture& target, AntiAimBoundaryPatch& patch, float forward = -2, float left = 0.5f) {
    fixture = &target;
    return AntiAimAppendBoundary(Address(target.command), forward, left,
        reinterpret_cast<std::uintptr_t>(&Allocate), reinterpret_cast<std::uintptr_t>(&Append), patch);
}
bool OriginalOrder(Fixture& target) {
    return target.Size() == 2 && target.Step(0) == Address(target.steps[0]) && target.Step(1) == Address(target.steps[1]);
}
}

int main() {
    {
        Fixture f; AntiAimBoundaryPatch p{}; const auto originals = f.steps; const auto generated = f.generated;
        const bool applied = Apply(f, p);
        Expect(applied && p.valid && p.appended && f.allocations == 1 && f.appends == 1 &&
            f.Representation() == Address(f.grown) && f.Size() == 3 && f.Step(0) == Address(f.generated) &&
            f.Step(1) == Address(f.steps[0]) && f.Step(2) == Address(f.steps[1]) &&
            f.generated.presence == 0x1c && f.generated.when == 0 && f.generated.button == 0 && !f.generated.pressed &&
            f.generated.forwardDelta == -2 && f.generated.leftDelta == 0.5f && f.generated.pitchDelta == 0 && f.generated.yawDelta == 0 &&
            !std::memcmp(f.steps.data(), originals.data(), sizeof(originals)),
            "native arena append may grow representation; boundary precedes equal-time originals without payload changes");
        f.generated.cachedSize = 100; f.steps[0].cachedSize = 100;
        const bool restored = AntiAimRestoreBoundary(p);
        Expect(restored && OriginalOrder(f) && f.Step(2) == Address(f.generated) &&
            f.generated.presence == generated.presence && f.generated.yawDelta == generated.yawDelta &&
            f.generated.when == generated.when && f.generated.vtable == generated.vtable && f.generated.arena == generated.arena &&
            f.generated.cachedSize == 100 && Get<int>(f.Representation()) == 3 &&
            Get<int>(Address(f.base) + off::base::SubtickCapacity) == 8 && !AntiAimRestoreBoundary(p),
            "restore tolerates serializer cache changes and retains native container growth/inactive object ownership");
        AntiAimBoundaryPatch again{};
        Expect(Apply(f, again) && f.allocations == 1 && f.appends == 1 && AntiAimRestoreBoundary(again) && OriginalOrder(f),
            "second boundary reuses inactive native message without allocation or duplicate append");
    }
    {
        Fixture f; AntiAimBoundaryPatch p{};
        Expect(Apply(f, p, 0, -0.0f) && p.valid && !p.appended && OriginalOrder(f) &&
            f.allocations == 0 && f.appends == 0 && AntiAimRestoreBoundary(p), "zero correction is a validated allocation-free no-op");
    }
    bool failedAppend = true;
    for (int mode : {1, 2, 3}) {
        Fixture f; f.mode = mode; AntiAimBoundaryPatch p{}; const auto old = f.generated;
        failedAppend &= !Apply(f, p) && !p.valid && OriginalOrder(f) &&
            !std::memcmp(&old, &f.generated, sizeof(old));
        if (mode == 3) failedAppend &= Get<int>(f.Representation()) == 3 && f.Step(2) == Address(f.generated);
    }
    Expect(failedAppend, "allocator failure, append refusal and grown append failure preserve original logical stream and generated payload");
    bool malformed = true;
    for (int mode = 0; mode < 7; ++mode) {
        Fixture f; AntiAimBoundaryPatch p{};
        if (mode == 0) f.steps[0].when = -0.1f;
        if (mode == 1) f.steps[0].when = 0.75f;
        if (mode == 2) f.steps[1].when = std::numeric_limits<float>::quiet_NaN();
        if (mode == 3) Put(Address(f.rep) + 16, Address(f.steps[0]));
        if (mode == 4) Put(Address(f.rep), 1);
        if (mode == 5) Put(Address(f.base) + off::base::SubtickSize, 128);
        if (mode == 6) Put(Address(f.rep) + 8, Address(f.base));
        malformed &= !Apply(f, p) && !p.valid && f.allocations == 0 && f.appends == 0;
    }
    Expect(malformed, "negative/unsorted/nonfinite times, aliased steps, bad counts and container overlap reject before allocation");
    {
        Fixture f; AntiAimBoundaryPatch p{};
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Expect(!Apply(f, p, nan, 0) && !Apply(f, p, 0, std::numeric_limits<float>::infinity()) &&
            f.allocations == 0 && f.appends == 0, "nonfinite boundary deltas reject before native helpers");
    }
    {
        Fixture f; AntiAimBoundaryPatch p{};
        void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        bool safe = page != nullptr;
        if (page) {
            std::memcpy(page, f.base.data(), f.base.size());
            Put(Address(f.command) + off::command::Base, reinterpret_cast<std::uintptr_t>(page));
            DWORD old = 0;
            safe &= VirtualProtect(page, 4096, PAGE_READONLY, &old) != 0;
            safe &= !Apply(f, p) && f.allocations == 0 && f.appends == 0;
            DWORD ignored = 0; VirtualProtect(page, 4096, old, &ignored); VirtualFree(page, 0, MEM_RELEASE);
        }
        Expect(safe, "read-only native container rejects before allocation without access violation");
    }
    bool stale = true;
    for (int mode = 0; mode < 8; ++mode) {
        Fixture f; AntiAimBoundaryPatch p{};
        stale &= Apply(f, p);
        if (mode == 0) f.steps[0].when += 0.1f;
        if (mode == 1) f.steps[0].pressed = false;
        if (mode == 2) f.steps[0].yawDelta = 12;
        if (mode == 3) f.generated.forwardDelta += 0.1f;
        if (mode == 4) Put(Address(f.command) + off::command::Sequence, 124);
        if (mode == 5) Put(f.Representation() + 16, Address(f.steps[1]));
        if (mode == 6) f.generated.vtable += 1;
        if (mode == 7) Put(Address(f.base) + off::base::SubtickCapacity, 7);
        const auto base = f.base; const auto rep = f.grown; const auto generated = f.generated;
        stale &= !AntiAimRestoreBoundary(p) && !std::memcmp(base.data(), f.base.data(), base.size()) &&
            !std::memcmp(rep.data(), f.grown.data(), sizeof(rep)) && !std::memcmp(&generated, &f.generated, sizeof(generated));
    }
    Expect(stale, "semantic, sequence, order, ownership and layout changes refuse restoration without writes");
    {
        Fixture f; AntiAimCommandPatch command{}; AntiAimBoundaryPatch boundary{};
        f.steps[0].presence = 0x1c; f.steps[0].button = 0; f.steps[0].pressed = false;
        f.steps[0].forwardDelta = 0.5f; f.steps[0].leftDelta = -0.25f;
        const auto originals = f.steps;
        bool integrated = AntiAimApplyCommand(Address(f.command), command) &&
            Near(command.editedView.pitch, 89) && command.editedAnglePresence == 3 && Apply(f, boundary) &&
            AntiAimRebindCommandContainer(boundary, command) && command.rep == Address(f.grown) &&
            command.count == 2 && command.capacity == 8 && command.allocated == 3 &&
            AntiAimRestoreBoundary(boundary) && AntiAimRestoreCommand(command) && OriginalOrder(f) &&
            !std::memcmp(originals.data(), f.steps.data(), sizeof(originals)) &&
            Near(Get<float>(Address(f.base) + off::base::Forward), 0.8f) &&
            Near(Get<AimAngles>(Address(f.angles) + silent_aim_offsets::angle::Pitch).yaw, 42);
        for (int repeat = 0; repeat < 30 && integrated; ++repeat) {
            integrated &= AntiAimApplyCommand(Address(f.command), command) && Apply(f, boundary) &&
                AntiAimRebindCommandContainer(boundary, command) && AntiAimRestoreBoundary(boundary) &&
                AntiAimRestoreCommand(command) && OriginalOrder(f) &&
                !std::memcmp(originals.data(), f.steps.data(), sizeof(originals));
        }
        Expect(integrated && f.allocations == 1 && f.appends == 1,
            "actual command transform and boundary grow/rebind/restore compose through 30 cached rebuilds using one native allocation");
    }
    {
        Fixture f; AntiAimCommandPatch command{}; AntiAimBoundaryPatch boundary{};
        f.mode = 3; const auto originals = f.steps;
        Expect(AntiAimApplyCommand(Address(f.command), command) && !Apply(f, boundary) &&
            AntiAimRebindCommandContainer(boundary, command) && AntiAimRestoreCommand(command) && OriginalOrder(f) &&
            !std::memcmp(originals.data(), f.steps.data(), sizeof(originals)) && command.rep == Address(f.grown),
            "failed native append with pool growth rebinds command metadata so complete transform rollback still succeeds");
    }
    {
        bool chain = true, previousEdited = false;
        float actualF = 0.3f, actualL = -0.7f;
        constexpr bool edited[] = {true, true, false, true, false, false, true, true, false};
        for (int iteration = 0; iteration < 1000; ++iteration) for (bool currentEdited : edited) {
            const AimAngles physical{static_cast<float>(iteration % 179 - 89), static_cast<float>(iteration * 13), 0};
            AimAngles outgoing{}; AntiAimBackwards(physical, outgoing);
            const auto physicalHistory = previousEdited ? AntiAimCorrectWalkMovement(outgoing, physical, actualF, actualL) : AntiAimMovement{actualF, actualL, 0, true};
            const auto nextHistory = currentEdited ? AntiAimCorrectWalkMovement(physical, outgoing, physicalHistory.forward, physicalHistory.left) : physicalHistory;
            const float boundaryF = nextHistory.forward - actualF, boundaryL = nextHistory.left - actualL;
            const float desiredF = iteration % 3 == 0 ? 0.6f : -0.4f, desiredL = iteration % 2 ? -0.8f : 0.2f;
            const auto event = MovementAnalogTransition(desiredF, desiredL, physicalHistory.forward, physicalHistory.left);
            const auto converted = currentEdited ? AntiAimCorrectWalkMovement(physical, outgoing, event.forwardDelta, event.leftDelta) : AntiAimMovement{event.forwardDelta, event.leftDelta, 0, true};
            const auto expected = currentEdited ? AntiAimCorrectWalkMovement(physical, outgoing, desiredF, desiredL) : AntiAimMovement{desiredF, desiredL, 0, true};
            actualF += boundaryF + converted.forward; actualL += boundaryL + converted.left;
            chain &= Near(actualF, expected.forward) && Near(actualL, expected.left);
            previousEdited = currentEdited;
        }
        Expect(chain, "9000 chained enable/on/disable/action basis changes plus owned desired-minus-history events preserve physical wish");
    }
    {
        // Exercise the actual serialized transaction and pointer-independent
        // publication reader, then apply the native historical+delta model.
        AntiAimHistoryState history{};
        AntiAimHistoryCommand previous{};
        float previousPhysicalF = 0, previousPhysicalL = 0;
        float historicalF = 0, historicalL = 0;
        bool pipeline = true, noRepeatedHandoff = true;
        unsigned recognizedEdits = 0;
        constexpr std::array<std::array<float, 2>, 9> inputs{{
            {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {0, 0}}};
        for (int tick = 0; tick < 1000 && pipeline; ++tick) {
            Fixture current;
            const auto& desired = inputs[tick < 128 ? 0 : static_cast<unsigned>(tick / 8) % inputs.size()];
            const float forward = desired[0], left = desired[1];
            const AimAngles physical{static_cast<float>(tick % 159 - 79), std::remainder(tick * 13.25f, 360.0f), 0};
            Put(Address(current.command) + off::command::Sequence, tick + 1);
            Put(Address(current.base) + off::base::Forward, forward);
            Put(Address(current.base) + off::base::Left, left);
            Put(Address(current.angles) + silent_aim_offsets::angle::Pitch, physical);
            MovementButtons buttons{};
            MovementEncodeStrafe(buttons, forward, left, 0);
            Put(Address(current.command) + off::command::Buttons, buttons);
            Put(Address(current.base) + off::base::SubtickSize, 1);
            current.steps[0].presence = 0x1c; current.steps[0].button = 0; current.steps[0].pressed = false;
            current.steps[0].when = 0.25f;
            current.steps[0].forwardDelta = forward - previousPhysicalF;
            current.steps[0].leftDelta = left - previousPhysicalL;
            const bool edit = tick % 101 >= 9 && tick % 17 != 0; // enable/disable and action passthrough
            AntiAimCommandPatch command{};
            if (edit) pipeline &= AntiAimApplyCommand(Address(current.command), command);
            auto physicalHistory = AntiAimMovement{historicalF, historicalL, 0, true};
            if (previous.edited) physicalHistory = AntiAimCorrectBackwardsMovement(previous.physical, historicalF, historicalL);
            const auto targetHistory = edit ? AntiAimCorrectBackwardsMovement(physical, physicalHistory.forward, physicalHistory.left) : physicalHistory;
            const float deltaF = targetHistory.forward - historicalF, deltaL = targetHistory.left - historicalL;
            const bool boundaryNeeded = deltaF != 0 || deltaL != 0;
            if (previous.edited && edit) noRepeatedHandoff &= !boundaryNeeded;
            AntiAimBoundaryPatch boundary{};
            if (boundaryNeeded) pipeline &= Apply(current, boundary, deltaF, deltaL);
            const auto outgoing = Get<AimAngles>(Address(current.angles) + silent_aim_offsets::angle::Pitch);
            const auto worldMatches = [&](float f, float l, float expectedF, float expectedL) {
                constexpr double radians = 0.01745329251994329577;
                const double yaw = outgoing.yaw * radians, realYaw = physical.yaw * radians;
                const double x = std::cos(yaw) * f - std::sin(yaw) * l;
                const double y = std::sin(yaw) * f + std::cos(yaw) * l;
                return std::abs(x - (std::cos(realYaw) * expectedF - std::sin(realYaw) * expectedL)) < 0.0001 &&
                    std::abs(y - (std::sin(realYaw) * expectedF + std::cos(realYaw) * expectedL)) < 0.0001;
            };
            float simulatedF = historicalF, simulatedL = historicalL;
            bool sawPhysicalEvent = false;
            for (int i = 0; i < current.Size(); ++i) {
                const auto step = Get<MovementSubtick>(current.Step(i));
                if (step.when > 0 && !sawPhysicalEvent) {
                    pipeline &= worldMatches(simulatedF, simulatedL, previousPhysicalF, previousPhysicalL);
                    sawPhysicalEvent = true;
                }
                if (step.presence & 8) simulatedF += step.forwardDelta;
                if (step.presence & 0x10) simulatedL += step.leftDelta;
            }
            pipeline &= worldMatches(simulatedF, simulatedL, forward, left);
            AntiAimHistoryCommand published{};
            pipeline &= AntiAimHistoryReadCommand(Address(current.command), 7, published);
            published.physical = physical; published.edited = edit;
            pipeline &= AntiAimHistoryPublish(history, published);
            Fixture cloned = current;
            Put(Address(cloned.command) + off::command::Base, Address(cloned.base));
            Put(Address(cloned.base) + off::base::Angles, Address(cloned.angles));
            Put(Address(cloned.base) + off::base::Presence, Get<std::uint32_t>(Address(cloned.base) + off::base::Presence) | 1u);
            command = {}; // restoration ownership is gone before cloned ingestion
            AntiAimHistoryCommand observed{}, matched{};
            pipeline &= AntiAimHistoryReadCommand(Address(cloned.command), 7, observed) &&
                AntiAimHistoryMatch(history, observed, matched) && matched.edited == edit;
            if (matched.edited) ++recognizedEdits;
            previous = matched;
            historicalF = (observed.directions & (off::Forward | off::Back)) ? observed.forward : 0;
            historicalL = (observed.directions & (off::Left | off::Right)) ? observed.left : 0;
            previousPhysicalF = forward; previousPhysicalL = left;
        }
        Expect(pipeline && recognizedEdits > 500,
            "1000 actual corrected/cloned commands retain full WASD/diagonal world wish through prediction and action handoffs");
        Expect(noRepeatedHandoff,
            "held edited commands do not add a repeated plus-two movement reversal after copied ingestion");
    }
    return failures ? 1 : 0;
}
