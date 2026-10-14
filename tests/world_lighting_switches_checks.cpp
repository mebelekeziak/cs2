#include "world_lighting_switches.h"
#include "world_settings.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <thread>

namespace {
unsigned failures = 0, checks = 0;
std::atomic<unsigned> pointCalls{0}, areaCalls{0};
void* observed[3]{};
HANDLE entered = nullptr, resume = nullptr;
void Check(bool condition, const char* label) {
    ++checks; if (!condition) { ++failures; std::printf("FAIL %s\n", label); }
}
void Point(void* binner, void* light, void* context) {
    ++pointCalls; observed[0] = binner; observed[1] = light; observed[2] = context;
    if (entered) { SetEvent(entered); WaitForSingleObject(resume, INFINITE); }
}
void Area(void* binner, void* light, void* context) {
    ++areaCalls; observed[0] = binner; observed[1] = light; observed[2] = context;
}
void Setup(bool master, bool suppress) {
    WorldSettings settings{}; settings.lightingEnabled = master; settings.disableLocalLights = suppress;
    WorldSetSettings(settings);
    WorldLightingSwitchesTestConfigure(Point, Area);
    pointCalls = 0; areaCalls = 0;
}
}
int main() {
    int binner = 1, light = 2, context = 3;
    Setup(false, true);
    WorldLightingSwitchesTestGather(false, &binner, &light, &context);
    WorldLightingSwitchesTestGather(true, &binner, &light, &context);
    Check(pointCalls == 1 && areaCalls == 1, "master disabled forwards both native light kinds");
    Check(observed[0] == &binner && observed[1] == &light && observed[2] == &context,
        "all three native pointer arguments preserved");
    Setup(true, false);
    WorldLightingSwitchesTestGather(false, &binner, &light, &context);
    Check(pointCalls == 1, "neutral lighting forwards original");
    Setup(true, true);
    WorldLightingSwitchesTestGather(false, &binner, &light, &context);
    WorldLightingSwitchesTestGather(true, &binner, &light, &context);
    Check(pointCalls == 0 && areaCalls == 0, "disabled scene lights mirror both native no-output gates");
    Check(binner == 1 && light == 2 && context == 3, "suppression writes no native state");
    WorldResetSettings();
    WorldLightingSwitchesTestGather(false, &binner, &light, &context);
    WorldLightingSwitchesTestGather(true, &binner, &light, &context);
    Check(pointCalls == 1 && areaCalls == 1, "reset restores both next native gathers");
    Setup(true, false);
    entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread engine([&] { WorldLightingSwitchesTestGather(false, &binner, &light, &context); });
    Check(WaitForSingleObject(entered, 2000) == WAIT_OBJECT_0, "native routine can run independently of decision lock");
    WorldLightingSwitchesStop("standalone stop while original runs");
    SetEvent(resume); engine.join();
    CloseHandle(entered); CloseHandle(resume); entered = resume = nullptr;
    auto settings = WorldGetSettings(); settings.disableLocalLights = true; WorldSetSettings(settings);
    WorldLightingSwitchesActivate();
    WorldLightingSwitchesTestGather(false, &binner, &light, &context);
    WorldLightingSwitchesTestGather(true, &binner, &light, &context);
    Check(pointCalls == 2 && areaCalls == 1, "stopped resident hooks forward and cannot reactivate");
    std::printf("%u local lighting switch checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
