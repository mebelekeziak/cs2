#include "world_fullbright.h"
#include "world_fullbright_offsets.h"
#include "world_settings.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
namespace off = world_fullbright_offsets;
constexpr std::uintptr_t base = 0x10000000;
unsigned checks = 0, failures = 0, calls = 0;
void* observedContext = nullptr;
std::uint32_t observedKey = 0;
int observedValue = 0;
HANDLE entered = nullptr, resume = nullptr;
void Check(bool good, const char* label) { ++checks; if (!good) { ++failures; std::printf("FAIL %s\n", label); } }
void Setter(void* context, std::uint32_t key, int value) {
    if (entered) { SetEvent(entered); WaitForSingleObject(resume, INFINITE); }
    observedContext = context; observedKey = key; observedValue = value; ++calls;
}
void Set(int value = 0, std::uint32_t key = off::FullbrightKey, std::uintptr_t caller = base + off::SetterReturn, void* context = nullptr) {
    WorldFullbrightTestSet(context, key, value, caller);
}
void Configure(bool active = true) {
    WorldFullbrightTestConfigure(base, Setter);
    WorldSettings settings{}; settings.fullbrightEnabled = active; WorldSetSettings(settings);
}
}
int main() {
    Configure(false); Set(3); Check(observedValue == 3, "disabled retains exact native mode");
    Configure();
    int context = 42;
    WorldFullbrightTestSet(&context, off::FullbrightKey, 0, base + off::SetterReturn);
    Check(observedContext == &context && observedKey == off::FullbrightKey && observedValue == 1, "fullbright changes only exact parameter value");
    Check(context == 42, "hook does not edit context bytes or cache a scene source");
    Check(!WorldGetSettings().lightingEnabled && observedValue == 1, "fullbright independent of other lighting controls");
    for (int value : {-1, 0, 1, 2, 3, 4, 1000}) {
        Set(value); Check(observedValue == 1, "all native modes map to fullbright one while enabled");
        Set(value, off::FullbrightKey + 1); Check(observedValue == value && observedKey == off::FullbrightKey + 1, "unrelated key forwards exact input");
        Set(value, off::FullbrightKey, base + off::SetterReturn + 1); Check(observedValue == value, "unrelated caller forwards exact input");
        for (const auto caller : {off::ClientSetterReturn, off::ClientAuxReturn}) {
            WorldFullbrightTestSetClient(&context, off::FullbrightKey, value, base + caller);
            Check(observedValue == 1 && observedContext == &context, "client cvar-backed producer receives fullbright mode");
            WorldFullbrightTestSetClient(&context, off::FullbrightKey + 1, value, base + caller);
            Check(observedValue == value, "client unrelated key retains native mode");
            WorldFullbrightTestSetClient(&context, off::FullbrightKey, value, base + caller + 1);
            Check(observedValue == value, "client unrelated callsite retains native mode");
        }
        WorldFullbrightTestSetClient(&context, off::FullbrightKey, value, base + off::SetterReturn);
        Check(observedValue == value, "engine callsite cannot authorize client setter override");
        Set(value, off::FullbrightKey, base + off::ClientSetterReturn);
        Check(observedValue == value, "client callsite cannot authorize engine setter override");
    }
    WorldResetSettings(); Set(2); Check(observedValue == 2, "reset restores newly produced native mode");
    WorldFullbrightTestSetClient(&context, off::FullbrightKey, 3, base + off::ClientSetterReturn);
    Check(observedValue == 3, "client disabled mode forwards native value");
    Configure();
    for (unsigned n = 0; n < 1000; ++n) { Set(); Check(observedValue == 1, "repeated frames do not accumulate state"); }
    auto settings = WorldGetSettings(); settings.fullbrightEnabled = false; WorldSetSettings(settings);
    Set(4); Check(observedValue == 4 && std::strstr(WorldFullbrightStatus(), "disabled"), "disable takes next native context value");
    Configure(); WorldFullbrightStop("test"); WorldFullbrightActivate(); Set(3);
    Check(observedValue == 3 && std::strstr(WorldFullbrightStatus(), "resident"), "stop remains disarmed and resident");
    Configure();
    entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(entered && resume, "barrier test events created");
    std::thread producer([] { Set(); });
    Check(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0, "modified native setter entered");
    std::atomic<bool> stopped{false};
    std::thread stopper([&] { WorldFullbrightStop("concurrent test"); stopped.store(true); });
    Sleep(20); Check(!stopped.load(), "stop waits for modified native setter");
    SetEvent(resume); producer.join(); stopper.join();
    Check(stopped.load() && observedValue == 1, "in-flight setter drained before stop returns");
    CloseHandle(entered); CloseHandle(resume); entered = resume = nullptr;
    Set(2); Check(observedValue == 2, "post-stop callbacks forward native mode");
    WorldFullbrightTestSetClient(&context, off::FullbrightKey, 4, base + off::ClientSetterReturn);
    Check(observedValue == 4, "client post-stop callbacks forward native mode");
    Check(calls > 1000, "original called through every tested path");
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
