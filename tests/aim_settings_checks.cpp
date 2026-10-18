#include "silent_aim_settings.h"
#include <atomic>
#include <cstdio>
#include <limits>
#include <thread>

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* name) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        failures += !ok;
    };
    check(SilentAimGetSettings() == SilentAimSettings{}, "default settings keep automatic attack disabled");
    SilentAimSetSettings({true, -999, static_cast<AimHitbox>(99), true, 999, 99999});
    check(SilentAimGetSettings() == SilentAimSettings{true, 0.1f, AimHitbox::Pelvis, true, 100, 1024}, "all fields clamp without bit overlap");
    SilentAimSetSettings({false, 999, static_cast<AimHitbox>(-1), false, -999, -1});
    check(SilentAimGetSettings() == SilentAimSettings{false, 30, AimHitbox::Head, false, 0, 64}, "zero threshold and minimum samples remain valid");
    SilentAimSetSettings({true, std::numeric_limits<float>::quiet_NaN(), AimHitbox::Head, true,
        std::numeric_limits<float>::infinity(), 256});
    check(SilentAimGetSettings() == SilentAimSettings{true, 5, AimHitbox::Head, true, 50, 256}, "nonfinite numbers restore defaults");
    const SilentAimSettings first{true, 0.25f, AimHitbox::Head, false, 99.25f, 65};
    const SilentAimSettings second{false, 29.75f, AimHitbox::Pelvis, true, 0.75f, 1023};
    SilentAimSetSettings(first);
    std::atomic<bool> torn{false};
    std::thread writer([&] { for (int i = 0; i < 100000; ++i) SilentAimSetSettings((i & 1) ? first : second); });
    for (int i = 0; i < 100000; ++i) {
        const auto snapshot = SilentAimGetSettings();
        if (!(snapshot == first) && !(snapshot == second)) torn.store(true);
    }
    writer.join();
    check(!torn.load(), "command reader sees one coherent snapshot during concurrent publication");
    return failures ? 1 : 0;
}
