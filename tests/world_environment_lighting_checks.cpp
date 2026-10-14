#include "world_environment_lighting.h"
#include "world_environment_lighting_offsets.h"
#include "world_settings.h"
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>

namespace {
using namespace world_environment_lighting_offsets;
constexpr std::uintptr_t Base = 0x180000000;
unsigned checks = 0, failures = 0;
std::atomic<unsigned> copied{0};
HANDLE entered = nullptr, resume = nullptr;
void Check(bool condition, const char* label) {
    ++checks; if (!condition) { ++failures; std::printf("FAIL %s\n", label); }
}
void* NativeCopy(void* to, const void* from, std::size_t size) {
    ++copied;
    if (entered) { SetEvent(entered); WaitForSingleObject(resume, INFINITE); }
    return std::memmove(to, from, size);
}
void Seed(unsigned char* bytes, float scalar = 0) {
    std::memset(bytes, 0x29, BufferSize * 2);
    const float sun[4]{8, 4, 2, 0.7f};
    std::memcpy(bytes + SunRgb, sun, sizeof(sun));
    std::memcpy(bytes + DirectionalLightmap, &scalar, sizeof(scalar));
}
float Value(const unsigned char* bytes, std::size_t offset) {
    float value = 0; std::memcpy(&value, bytes + offset, sizeof(value)); return value;
}
WorldSettings Settings() {
    WorldSettings settings{};
    settings.lightingEnabled = true;
    settings.sunLightBrightness = 2;
    settings.sunLightTint[0] = 0.25f;
    settings.sunLightTint[1] = 0.5f;
    settings.sunLightTint[2] = 1;
    settings.directionalLightmapsMode = 2;
    return settings;
}
void Configure(const WorldSettings& settings) {
    WorldSetSettings(settings);
    WorldEnvironmentLightingTestConfigure(Base, NativeCopy);
}
void* Run(unsigned char* buffer, std::uintptr_t caller = Base + CopyReturn) {
    return WorldEnvironmentLightingTestCopy(buffer + BufferSize, buffer, BufferSize, caller);
}
}

int main() {
    // Native buffers belong to the hook caller's current stack, just like the
    // engine's freshly generated view output. Heap buffers must never qualify.
    unsigned char buffer[BufferSize * 2]{};
    unsigned char baseline[BufferSize]{};
    auto settings = Settings();
    Configure(settings);
    Seed(buffer); std::memcpy(baseline, buffer, BufferSize);
    Check(Run(buffer) == buffer + BufferSize, "native memcpy return preserved");
    Check(Value(buffer, SunRgb) == 4 && Value(buffer, SunRgb + 4) == 4 && Value(buffer, SunRgb + 8) == 4,
        "only sun RGB scales in the fresh native base packet");
    Check(Value(buffer, DirectionalLightmap) == 1, "disabled directional lightmaps follow native false constant");
    Check(std::memcmp(buffer, buffer + BufferSize, BufferSize) == 0, "all native copied variant bytes inherit the modified base");
    Check(Value(buffer, SunRgb + 12) == 0.7f, "sun vector fourth float preserved");
    std::memcpy(baseline + SunRgb, buffer + SunRgb, 12);
    std::memcpy(baseline + DirectionalLightmap, buffer + DirectionalLightmap, 4);
    Check(std::memcmp(baseline, buffer, BufferSize) == 0, "positions shadow matrices counts and other packet bytes preserved");
    Seed(buffer); Run(buffer);
    Check(Value(buffer, SunRgb) == 4, "freshly regenerated frames do not compound old sun multipliers");
    settings.directionalLightmapsMode = 1; WorldSetSettings(settings);
    Seed(buffer, 1); Run(buffer);
    Check(Value(buffer, DirectionalLightmap) == 0, "enabled directional lightmaps follow native true constant");
    settings.directionalLightmapsMode = 0; WorldSetSettings(settings);
    Seed(buffer, 1); Run(buffer);
    Check(Value(buffer, DirectionalLightmap) == 1, "game directionality preserves original constant");
    Seed(buffer); Run(buffer, Base + CopyReturn + 1);
    Check(Value(buffer, SunRgb) == 8, "unrelated native memcpy callers forwarded untouched");
    auto heap = std::make_unique<unsigned char[]>(BufferSize * 2);
    Seed(heap.get()); Run(heap.get());
    Check(Value(heap.get(), SunRgb) == 8, "matching size and caller cannot authorize heap or scene state writes");
    Seed(buffer);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(buffer + SunRgb, &nan, sizeof(nan)); Run(buffer);
    Check(std::isnan(Value(buffer + BufferSize, SunRgb)) && Value(buffer, SunRgb + 4) == 4,
        "invalid sun rejects the complete override while native copy forwards");
    settings.directionalLightmapsMode = 2; WorldSetSettings(settings);
    Seed(buffer, 0.5f); Run(buffer);
    Check(Value(buffer, DirectionalLightmap) == 0.5f && Value(buffer, SunRgb) == 8,
        "unexpected native mode rejects rather than interpreting a different layout");
    settings = Settings(); settings.lightingEnabled = false; WorldSetSettings(settings);
    Seed(buffer); Run(buffer);
    Check(Value(buffer, SunRgb) == 8 && Value(buffer, DirectionalLightmap) == 0, "lighting master off forwards native packet");
    Configure(Settings());
    WorldResetSettings(); Seed(buffer); Run(buffer);
    Check(Value(buffer, SunRgb) == 8, "reset uses native baseline on the next view");

    Configure(Settings());
    entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<bool> stopDone{false};
    HANDLE stopStarted = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE stopReturned = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread engine([&] {
        unsigned char owned[BufferSize * 2]{}; Seed(owned); Run(owned);
    });
    Check(WaitForSingleObject(entered, 2000) == WAIT_OBJECT_0, "modified native pure copy enters the cooperative barrier");
    std::thread stop([&] {
        SetEvent(stopStarted);
        WorldEnvironmentLightingStop("standalone concurrent stop");
        stopDone = true;
        SetEvent(stopReturned);
    });
    Check(WaitForSingleObject(stopStarted, 2000) == WAIT_OBJECT_0, "concurrent stop worker begins");
    Check(WaitForSingleObject(stopReturned, 50) == WAIT_TIMEOUT,
        "stop cannot complete while the modified pure copy is in flight");
    SetEvent(resume); engine.join(); stop.join();
    CloseHandle(entered); CloseHandle(resume); entered = resume = nullptr;
    CloseHandle(stopStarted); CloseHandle(stopReturned);
    Check(stopDone.load(), "stop completes after native packet copy drains");
    WorldEnvironmentLightingActivate(); Seed(buffer); Run(buffer);
    Check(Value(buffer, SunRgb) == 8 && Value(buffer, DirectionalLightmap) == 0,
        "stopped resident callback forwards and cannot reactivate");
    std::printf("%u environment lighting checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
