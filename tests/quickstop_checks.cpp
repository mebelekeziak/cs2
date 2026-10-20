#include "quickstop_command.h"
#include "movement_memory.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
namespace off = movement_offsets;
int failures = 0;
void Expect(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label); failures += !ok;
}
template<class T> std::uintptr_t Address(T& v) { return reinterpret_cast<std::uintptr_t>(&v); }
template<class T> void Put(std::uintptr_t a, const T& v) { std::memcpy(reinterpret_cast<void*>(a), &v, sizeof(v)); }
template<class T> T Get(std::uintptr_t a) { T v{}; std::memcpy(&v, reinterpret_cast<void*>(a), sizeof(v)); return v; }
bool Near(float a, float b) { return std::abs(a - b) < 0.00001f; }
template<class T> bool Equal(const T& a, const T& b) { return !std::memcmp(&a, &b, sizeof(T)); }
struct Fixture {
    std::array<std::byte, 0x90> command{}, base{}, angles{};
    std::array<std::array<std::uintptr_t, 12>, 4> representations{};
    std::array<MovementSubtick, 4> steps{}, generated{};
    int allocations = 0, appends = 0, mode = 0;
    Fixture() {
        Put(Address(command) + off::command::Sequence, 17);
        Put(Address(command) + off::command::Presence, std::uint32_t{1});
        Put(Address(command) + off::command::Base, Address(base));
        Put(Address(command) + off::command::Buttons, MovementButtons{off::Forward | off::Jump | 1, off::Jump, 0x8000});
        Put(Address(base) + off::base::Presence, std::uint32_t{4 | 0xC0});
        Put(Address(base) + off::base::Angles, Address(angles));
        Put(Address(base) + off::base::Forward, 0.8f); Put(Address(base) + off::base::Left, -0.1f);
        Put(Address(base) + 0x18, Address(*this));
        Put(Address(base) + off::base::SubtickSize, 4); Put(Address(base) + off::base::SubtickCapacity, 4);
        Put(Address(base) + off::base::SubtickRep, Address(representations[0]));
        Put(Address(representations[0]), 4);
        for (std::size_t i = 0; i < steps.size(); ++i) {
            steps[i].vtable = 0x123400 + i; steps[i].arena = Address(*this);
            steps[i].presence = 7; steps[i].button = i == 0 ? off::Forward : i == 1 ? off::Jump : 1;
            steps[i].pressed = true; steps[i].when = static_cast<float>(i) * 0.2f;
            Put(Address(representations[0]) + 8 + i * 8, Address(steps[i]));
            generated[i].vtable = 0x234500 + i; generated[i].arena = Address(*this);
            generated[i].presence = 4; generated[i].when = 0.9f;
        }
        steps[3].presence = 0x7C; steps[3].button = 0; steps[3].pressed = false;
        steps[3].forwardDelta = -0.2f; steps[3].leftDelta = 0.4f;
        steps[3].pitchDelta = 2; steps[3].yawDelta = 3;
    }
    int Count() { return Get<int>(Address(base) + off::base::SubtickSize); }
    std::uintptr_t Rep() { return Get<std::uintptr_t>(Address(base) + off::base::SubtickRep); }
    MovementButtons Buttons() { return Get<MovementButtons>(Address(command) + off::command::Buttons); }
};
Fixture* fixture = nullptr;
void* Allocate(void* arena) {
    if (!fixture || arena != fixture || fixture->mode == 1 || fixture->allocations >= 4) return nullptr;
    return &fixture->generated[static_cast<std::size_t>(fixture->allocations++)];
}
void* Append(void* repeated, void* value) {
    if (!fixture || repeated != reinterpret_cast<void*>(Address(fixture->base) + 0x18) || fixture->mode == 2) return nullptr;
    const int size = fixture->Count();
    auto& grown = fixture->representations[static_cast<std::size_t>(++fixture->appends) % fixture->representations.size()];
    std::memcpy(grown.data(), reinterpret_cast<void*>(fixture->Rep()), 8 + static_cast<std::size_t>(size) * 8);
    Put(Address(grown), size + 1); Put(Address(grown) + 8 + static_cast<std::size_t>(size) * 8, reinterpret_cast<std::uintptr_t>(value));
    Put(Address(fixture->base) + off::base::SubtickRep, Address(grown));
    Put(Address(fixture->base) + off::base::SubtickCapacity, 10);
    Put(Address(fixture->base) + off::base::SubtickSize, size + 1);
    return fixture->mode == 3 ? nullptr : value;
}
bool Apply(Fixture& f, QuickstopCommandPatch& p, float forward = -1, float left = 0,
    float historyF = 0.8f, float historyL = -0.1f, bool preserve = false) {
    fixture = &f;
    return QuickstopApplyCommand(Address(f.command), forward, left, historyF, historyL,
        off::Forward, reinterpret_cast<std::uintptr_t>(&Allocate), reinterpret_cast<std::uintptr_t>(&Append), p, preserve);
}
}

int main() {
    {
        bool braking = true;
        for (float yaw : {-720.0f, -135.0f, -90.0f, 0.0f, 45.0f, 90.0f, 180.0f, 720.0f}) {
            for (const auto velocity : {std::array<float, 2>{250, 0}, {0, 250}, {-170, -110}, {5, -7}}) {
                const auto axes = QuickstopBrakeAxes(velocity[0], velocity[1], yaw);
                const float r = yaw * 0.01745329251994329577f;
                const float worldX = axes.forward * std::cos(r) - axes.left * std::sin(r);
                const float worldY = axes.forward * std::sin(r) + axes.left * std::cos(r);
                braking &= axes.valid && worldX * velocity[0] + worldY * velocity[1] < 0 &&
                    std::hypot(axes.forward, axes.left) <= 1.00001f &&
                    std::abs(worldX * velocity[1] - worldY * velocity[0]) < 0.0001f * (1 + std::hypot(velocity[0], velocity[1]));
            }
        }
        Expect(braking, "counter-velocity remains opposite travel across camera rotations, diagonals and reduced speeds");
        const auto slow = QuickstopBrakeAxes(15, 0, 0), resting = QuickstopBrakeAxes(0.5f, 0, 0);
        Expect(slow.valid && slow.forward == -0.5f && resting.valid && resting.forward == 0 && resting.left == 0,
            "feedback taper reduces near-rest counter-input and neutralizes residual velocity below one unit");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Expect(!QuickstopBrakeAxes(nan, 0, 0).valid && !QuickstopBrakeAxes(0, nan, 0).valid &&
            !QuickstopBrakeAxes(0, 0, nan).valid && !QuickstopBrakeAxes(100001, 0, 0).valid,
            "invalid velocity and camera samples cannot produce brake input");
    }
    {
        Fixture f; QuickstopCommandPatch p{};
        const auto originalSteps = f.steps;
        const auto originalAngles = f.angles;
        const auto originalButtons = f.Buttons();
        const bool applied = Apply(f, p);
        Expect(applied && f.Count() == 5 && p.boundary.appended &&
            Near(f.generated[0].forwardDelta, -1.8f) && Near(f.generated[0].leftDelta, 0.1f) &&
            f.generated[0].when == 0 && Get<std::uintptr_t>(f.Rep() + 8) == Address(f.generated[0]) &&
            f.steps[0].button == 0 && f.steps[0].presence == 4 &&
            f.steps[3].forwardDelta == 0 && f.steps[3].leftDelta == 0 &&
            f.steps[3].pitchDelta == 2 && f.steps[3].yawDelta == 3 &&
            !std::memcmp(&f.steps[1], &originalSteps[1], sizeof(MovementSubtick)) &&
            !std::memcmp(&f.steps[2], &originalSteps[2], sizeof(MovementSubtick)) && f.angles == originalAngles,
            "brake emits first-slice transition, sanitizes movement and preserves jump, attack, camera and action order");
        Expect((f.Buttons().down & off::Directions) == off::Back && (f.Buttons().down & ~off::Directions) == (originalButtons.down & ~off::Directions) &&
            (f.Buttons().changed & off::Directions) == (off::Forward | off::Back),
            "native direction edges match analog signs while unrelated button words survive");
        auto buttons = f.Buttons(); buttons.down ^= 1; buttons.changed |= 1;
        Put(Address(f.command) + off::command::Buttons, buttons);
        Put(Address(f.base) + off::base::Presence, Get<std::uint32_t>(Address(f.base) + off::base::Presence) | 1u);
        f.steps[0].cachedSize = 99; f.generated[0].cachedSize = 101;
        Expect(QuickstopRestoreCommand(p) && f.Count() == 4 &&
            Get<float>(Address(f.base) + off::base::Forward) == 0.8f &&
            (f.Buttons().down & off::Directions) == off::Forward && !(f.Buttons().down & 1) &&
            (f.Buttons().changed & 1) && (Get<std::uint32_t>(Address(f.base) + off::base::Presence) & 1) &&
            f.steps[0].cachedSize == 99 && f.steps[0].button == off::Forward && f.generated[0].cachedSize == 101,
            "restoration preserves later autofire changes, native CRC presence and serializer caches");
        bool rebuilt = true;
        for (int repeat = 0; repeat < 1000; ++repeat)
            rebuilt &= Apply(f, p) && f.Count() == 5 && QuickstopRestoreCommand(p) && f.Count() == 4;
        Expect(rebuilt && f.allocations == 1 && f.appends == 1,
            "same-command rebuilds reuse inactive engine messages without stacked transitions or pool growth");
    }
    {
        Fixture f; QuickstopCommandPatch p{};
        bool safe = Apply(f, p);
        AntiAimBoundaryPatch anti{};
        fixture = &f;
        safe &= AntiAimAppendBoundary(Address(f.command), 0.3f, -0.4f,
            reinterpret_cast<std::uintptr_t>(&Allocate), reinterpret_cast<std::uintptr_t>(&Append), anti);
        const auto grown = f.Rep();
        safe &= grown != p.boundary.rep && AntiAimRestoreBoundary(anti) && f.Count() == 5;
        safe &= QuickstopRestoreCommand(p) && f.Count() == 4 && f.Rep() == grown &&
            Get<int>(f.Rep()) == 6 && f.steps[0].button == off::Forward;
        Expect(safe, "anti-aim boundary layering can grow native pool and representation before quickstop restores its exact logical stream");
    }
    {
        Fixture f; QuickstopCommandPatch p{};
        Put(Address(f.base) + off::base::Forward, 0.4f); Put(Address(f.base) + off::base::Left, 1.0f);
        f.steps[0].presence = 0x1C; f.steps[0].button = 0; f.steps[0].pressed = false;
        f.steps[0].forwardDelta = -0.4f; f.steps[0].leftDelta = 1.1f;
        const auto originalSteps = f.steps;
        const auto originalButtons = f.Buttons();
        bool safe = Apply(f, p, 0.8f, -0.1f, -1, 0, true);
        const float afterUserF = -1 + f.generated[0].forwardDelta + f.steps[0].forwardDelta;
        const float afterUserL = f.generated[0].leftDelta + f.steps[0].leftDelta;
        safe &= Equal(f.steps, originalSteps) && !std::memcmp(&originalButtons, &p.editedButtons, sizeof(MovementButtons)) &&
            Get<float>(Address(f.base) + off::base::Forward) == 0.4f &&
            Get<float>(Address(f.base) + off::base::Left) == 1 && Near(afterUserF, 0.4f) && Near(afterUserL, 1);
        safe &= QuickstopRestoreCommand(p) && f.Count() == 4 && Equal(f.steps, originalSteps);
        Expect(safe, "cooldown handoff restores physical predecessor then preserves new user axes, timed movement and every original action");
    }
    {
        Fixture f; QuickstopCommandPatch p{};
        bool safe = Apply(f, p, -1, 0, -1, 0) && !p.boundary.appended && f.Count() == 4;
        safe &= QuickstopRestoreCommand(p) && f.steps[0].button == off::Forward;
        Expect(safe, "matching historical brake axes still restore sanitized original events without a generated boundary");
        safe = Apply(f, p, -1, 0, -1, 0);
        Put(Address(f.base) + off::base::Forward, 0.5f);
        const auto scrubbed = f.steps;
        safe &= !QuickstopRestoreCommand(p) && QuickstopCommandStillEdited(p) && Equal(f.steps, scrubbed);
        Expect(safe, "partial native rebuild retaining sanitized original events is recognized and left untouched");
        f.steps[0] = p.boundary.originals[0].value; f.steps[3] = p.boundary.originals[3].value;
        Expect(QuickstopCommandStillEdited(p), "retained directional button triplet still proves partial quickstop ownership after native axes/events change");
        Put(Address(f.command) + off::command::Buttons, p.originalButtons);
        Expect(!QuickstopCommandStillEdited(p), "fully replaced native axes and original events invalidate stale quickstop ownership");
    }
    {
        bool rejected = true;
        for (int mode : {1, 2, 3}) {
            Fixture f; f.mode = mode; QuickstopCommandPatch p{};
            const auto originalSteps = f.steps;
            const auto originalCommand = f.command;
            rejected &= !Apply(f, p) && !p.valid && f.Count() == 4 && Equal(f.steps, originalSteps) &&
                Get<float>(Address(f.base) + off::base::Forward) == 0.8f && f.command == originalCommand;
        }
        Expect(rejected, "allocator and append failures roll back logical stream before mutating user movement");
    }
    {
        Fixture f; QuickstopCommandPatch p{};
        f.steps[1].when = -1;
        Expect(!Apply(f, p) && f.allocations == 0 && f.Count() == 4 && f.steps[0].button == off::Forward,
            "malformed native timing rejects before allocation or movement sanitization");
        Expect(!QuickstopApplyCommand(0, 0, 0, 0, 0, 0, 0, 0, p), "null command fails guarded snapshots");
    }
    {
        Fixture f; QuickstopCommandPatch p{};
        bool safe = Apply(f, p);
        Put(Address(f.command) + off::command::Sequence, 18);
        const auto before = f.base;
        safe &= !QuickstopRestoreCommand(p) && !QuickstopCommandStillEdited(p) && f.base == before;
        Expect(safe, "pooled command allocation with a new sequence cannot be restored by an older transaction");
    }
    return failures ? 1 : 0;
}
