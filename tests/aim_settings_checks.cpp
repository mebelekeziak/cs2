#include "silent_aim_settings.h"
#include "anti_aim_settings.h"
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
    SilentAimSetSettings({true, -999, static_cast<AimHitbox>(99), true, 999, 99999, true});
    check(SilentAimGetSettings() == SilentAimSettings{true, 0.1f, AimHitbox::Pelvis, true, 100, 1024, true}, "all fields clamp without quickstop bit overlap");
    SilentAimSetSettings({false, 999, static_cast<AimHitbox>(-1), false, -999, -1});
    check(SilentAimGetSettings() == SilentAimSettings{false, 180, AimHitbox::Head, false, 0, 64}, "FOV clamps to 180 with zero threshold and minimum samples valid");
    const SilentAimSettings maximumFov{true, 180, AimHitbox::Pelvis, true, 100, 1024};
    SilentAimSetSettings(maximumFov);
    check(SilentAimGetSettings() == maximumFov, "180 degree FOV preserves enable, hitbox and autofire fields");
    SilentAimSetSettings({true, std::numeric_limits<float>::quiet_NaN(), AimHitbox::Head, true,
        std::numeric_limits<float>::infinity(), 256});
    check(SilentAimGetSettings() == SilentAimSettings{true, 5, AimHitbox::Head, true, 50, 256}, "nonfinite numbers restore defaults");
    const SilentAimSettings first{true, 0.25f, AimHitbox::Head, false, 99.25f, 65};
    const SilentAimSettings second{false, 179.75f, AimHitbox::Pelvis, true, 0.75f, 1023, true};
    auto quickstopOnly = first;
    quickstopOnly.quickstop = true;
    SilentAimSetSettings(quickstopOnly);
    check(SilentAimGetSettings() == quickstopOnly, "quickstop preference survives disabled autofire without changing other fields");
    SilentAimSetSettings(first);
    std::atomic<bool> torn{false};
    std::thread writer([&] { for (int i = 0; i < 100000; ++i) SilentAimSetSettings((i & 1) ? first : second); });
    for (int i = 0; i < 100000; ++i) {
        const auto snapshot = SilentAimGetSettings();
        if (!(snapshot == first) && !(snapshot == second)) torn.store(true);
    }
    writer.join();
    check(!torn.load(), "command reader sees one coherent snapshot during concurrent publication");
    const AntiAimSettings antiDefaults;
    check(AntiAimGetSettings() == antiDefaults, "anti aim defaults preserve static backwards yaw and downward pitch, disabled");
    auto antiInvalid = antiDefaults;
    antiInvalid.enabled = true;
    antiInvalid.yawMode = static_cast<AntiAimYawMode>(99);
    antiInvalid.yawBase = static_cast<AntiAimYawBase>(-1);
    antiInvalid.pitchMode = static_cast<AntiAimPitchMode>(99);
    antiInvalid.yawOffset = std::numeric_limits<float>::infinity();
    antiInvalid.jitterRange = std::numeric_limits<float>::quiet_NaN();
    antiInvalid.spinSpeed = -std::numeric_limits<float>::infinity();
    antiInvalid.pitch = std::numeric_limits<float>::quiet_NaN();
    antiInvalid.pitchAlternate = std::numeric_limits<float>::infinity();
    AntiAimSetSettings(antiInvalid);
    auto antiEnabledDefault = antiDefaults;
    antiEnabledDefault.enabled = true;
    check(AntiAimGetSettings() == antiEnabledDefault, "invalid anti aim modes and nonfinite numbers restore independent defaults");
    const AntiAimSettings high{true, AntiAimYawMode::Spin, AntiAimYawBase::World,
        999, 999, 999, 999, AntiAimPitchMode::Jitter, 999, -999};
    check(AntiAimNormalizeSettings(high) == AntiAimSettings{true, AntiAimYawMode::Spin, AntiAimYawBase::World,
        180, 360, 64, 180, AntiAimPitchMode::Jitter, 89, -89}, "anti aim clamps upper limits and both pitch limits");
    const AntiAimSettings low{false, AntiAimYawMode::Random, AntiAimYawBase::View,
        -999, -999, -999, -999, AntiAimPitchMode::Custom, -999, 999};
    check(AntiAimNormalizeSettings(low) == AntiAimSettings{false, AntiAimYawMode::Random, AntiAimYawBase::View,
        -180, 0, 1, -180, AntiAimPitchMode::Custom, -89, 89}, "anti aim clamps lower limits with independent mode fields");
    const AntiAimSettings antiFirst{true, AntiAimYawMode::Jitter, AntiAimYawBase::World,
        -73.25f, 222.5f, 3, -11.75f, AntiAimPitchMode::Custom, 27.25f, -42.5f};
    const AntiAimSettings antiSecond{false, AntiAimYawMode::Random, AntiAimYawBase::View,
        13.5f, 64.25f, 61, 70.5f, AntiAimPitchMode::Jitter, -17.75f, 54.25f};
    AntiAimSetSettings(antiFirst);
    check(AntiAimGetSettings() == antiFirst, "anti aim snapshots retain fractional degree values exactly");
    torn.store(false);
    std::thread antiWriter([&] { for (int i = 0; i < 100000; ++i) AntiAimSetSettings((i & 1) ? antiFirst : antiSecond); });
    for (int i = 0; i < 100000; ++i) {
        const auto snapshot = AntiAimGetSettings();
        if (!(snapshot == antiFirst) && !(snapshot == antiSecond)) torn.store(true);
    }
    antiWriter.join();
    check(!torn.load(), "anti aim reader sees coherent mode, interval and angle snapshots during concurrent publication");
    AntiAimResetSettings();
    check(AntiAimGetSettings() == antiDefaults, "anti aim reset restores all defaults and disables feature");
    return failures ? 1 : 0;
}
