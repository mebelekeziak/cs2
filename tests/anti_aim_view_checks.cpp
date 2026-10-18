#include "anti_aim_view.h"
#include "silent_aim_math.h"
#include "movement_command.h"
#include "movement_offsets.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
struct Fixture {
    AimAngles view{};
    std::uint64_t currentDown = 0;
    MovementButtons buttons{};
    int calls = 0, slot = -1, lifecycle = 1;
    float timestamp = 0;
    bool invalidate = false;
};
void Native(void* input, int slot) {
    auto& f = *static_cast<Fixture*>(input);
    ++f.calls; f.slot = slot; f.timestamp += 0.125f;
    f.view = {0, 180, 0};
    f.buttons = {movement_offsets::Back | movement_offsets::Right | 0x20, 0, 0};
    if (f.invalidate) f.lifecycle = 2;
}
bool Valid(void* input) { return static_cast<Fixture*>(input)->lifecycle == 1; }
int failures = 0;
void Expect(bool value, const char* label) {
    std::printf("%s %s\n", value ? "PASS" : "FAIL", label); failures += !value;
}
}
int main() {
    Fixture f{};
    const auto address = reinterpret_cast<std::uintptr_t>(&f.view);
    const auto buttons = reinterpret_cast<std::uintptr_t>(&f.buttons.down);
    const auto current = reinterpret_cast<std::uintptr_t>(&f.currentDown);
    bool exact = true;
    // Every replay receives a fresh physical view; no frozen camera cache.
    for (int i = 0; i < 10000; ++i) {
        const AimAngles physical{std::sin(i * 0.17f) * 89, std::remainder(i * 3.141f, 360.0f), std::cos(i * 0.1f) * 23};
        f.view = physical;
        constexpr std::array<std::uint64_t, 4> directions{movement_offsets::Forward,
            movement_offsets::Forward | movement_offsets::Left, movement_offsets::Back, 0};
        f.currentDown = directions[static_cast<std::size_t>(i) % directions.size()] | 2;
        f.buttons.changed = f.buttons.third = UINT64_MAX;
        const auto calls = f.calls;
        exact &= AntiAimReplayPreservingInput(address, current, buttons, true, Native, &f, 0, Valid, &f) &&
            !std::memcmp(&f.view, &physical, sizeof(physical)) && f.calls == calls + 1 && f.slot == 0 &&
            f.buttons.down == ((f.currentDown & movement_offsets::Directions) | 0x20) &&
            f.buttons.changed == 0 && f.buttons.third == 0;
    }
    Expect(exact && f.timestamp == 1250, "10000 physical views and press/release/reversal transitions survive replay; native lifecycle runs once");
    f.view = {45, -33, 0};
    Expect(!AntiAimReplayPreservingInput(address, current, buttons, false, Native, &f, 0, Valid, &f) && f.view.yaw == 180 &&
        f.buttons.down == (movement_offsets::Back | movement_offsets::Right | 0x20),
        "disabled compensation forwards original view behavior");
    f.view = {45, -33, 0};
    Expect(!AntiAimReplayPreservingInput(address, current, buttons, true, Native, &f, 2, Valid, &f) && f.slot == 2 && f.view.yaw == 180,
        "nonzero native split-screen slot is passed through");
    f.view = {45, -33, 0}; f.invalidate = true;
    Expect(!AntiAimReplayPreservingInput(address, current, buttons, true, Native, &f, 0, Valid, &f) && f.view.yaw == 180 &&
        f.buttons.down == (movement_offsets::Back | movement_offsets::Right | 0x20),
        "source lifecycle changed inside native replay rejects stale restore");
    f.invalidate = false; f.lifecycle = 1;
    f.view = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
    Expect(!AntiAimReplayPreservingInput(address, current, buttons, true, Native, &f, 0, Valid, &f) && f.view.yaw == 180,
        "invalid incoming view still calls original without restoring NaN");
    const auto calls = f.calls;
    Expect(!AntiAimReplayPreservingInput(1, current, buttons, true, Native, &f, 0, Valid, &f) && f.calls == calls + 1,
        "unmapped view fails safely while preserving native lifecycle");
    f.view = {45, -33, 0};
    Expect(!AntiAimReplayPreservingInput(address, 1, buttons, true, Native, &f, 0, Valid, &f) && f.view.yaw == 180,
        "unmapped direction storage forwards native without a partial input restore");
    f.view = {45, -33, 0};
    Expect(!AntiAimReplayPreservingInput(address, current, 1, true, Native, &f, 0, Valid, &f) && f.view.yaw == 180,
        "unmapped previous-direction storage forwards native without a partial input restore");
    auto* readonly = static_cast<std::uint64_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Expect(readonly != nullptr, "guarded direction storage allocated");
    if (readonly) {
        *readonly = movement_offsets::Forward;
        DWORD old = 0;
        const bool protectedPage = VirtualProtect(readonly, 4096, PAGE_READONLY, &old) != 0;
        f.view = {45, -33, 0};
        Expect(protectedPage && !AntiAimReplayPreservingInput(address, current, reinterpret_cast<std::uintptr_t>(readonly), true,
            Native, &f, 0, Valid, &f) && f.view.yaw == 180 && *readonly == movement_offsets::Forward,
            "read-only direction storage forwards native without writing input or protected bytes");
        VirtualProtect(readonly, 4096, PAGE_READWRITE, &old);
        VirtualFree(readonly, 0, MEM_RELEASE);
    }
    return failures ? 1 : 0;
}
