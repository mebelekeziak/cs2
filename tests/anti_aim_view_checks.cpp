#include "anti_aim_view.h"
#include "silent_aim_math.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
struct Fixture {
    AimAngles view{};
    int calls = 0, slot = -1, lifecycle = 1;
    float timestamp = 0;
    bool invalidate = false;
};
void Native(void* input, int slot) {
    auto& f = *static_cast<Fixture*>(input);
    ++f.calls; f.slot = slot; f.timestamp += 0.125f;
    f.view = {0, 180, 0};
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
    bool exact = true;
    // Every replay receives a fresh physical view; no frozen camera cache.
    for (int i = 0; i < 10000; ++i) {
        const AimAngles physical{std::sin(i * 0.17f) * 89, std::remainder(i * 3.141f, 360.0f), std::cos(i * 0.1f) * 23};
        f.view = physical;
        const auto calls = f.calls;
        exact &= AntiAimReplayPreservingView(address, true, Native, &f, 0, Valid, &f) &&
            !std::memcmp(&f.view, &physical, sizeof(physical)) && f.calls == calls + 1 && f.slot == 0;
    }
    Expect(exact && f.timestamp == 1250, "10000 changing physical views survive native angle resets byte-for-byte; native lifecycle runs once");
    f.view = {45, -33, 0};
    Expect(!AntiAimReplayPreservingView(address, false, Native, &f, 0, Valid, &f) && f.view.yaw == 180,
        "disabled compensation forwards original view behavior");
    f.view = {45, -33, 0};
    Expect(!AntiAimReplayPreservingView(address, true, Native, &f, 2, Valid, &f) && f.slot == 2 && f.view.yaw == 180,
        "nonzero native split-screen slot is passed through");
    f.view = {45, -33, 0}; f.invalidate = true;
    Expect(!AntiAimReplayPreservingView(address, true, Native, &f, 0, Valid, &f) && f.view.yaw == 180,
        "source lifecycle changed inside native replay rejects stale restore");
    f.invalidate = false; f.lifecycle = 1;
    f.view = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
    Expect(!AntiAimReplayPreservingView(address, true, Native, &f, 0, Valid, &f) && f.view.yaw == 180,
        "invalid incoming view still calls original without restoring NaN");
    const auto calls = f.calls;
    Expect(!AntiAimReplayPreservingView(1, true, Native, &f, 0, Valid, &f) && f.calls == calls + 1,
        "unmapped view fails safely while preserving native lifecycle");
    return failures ? 1 : 0;
}
