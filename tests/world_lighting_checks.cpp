#include "world_lighting.h"
#include "world_lighting_offsets.h"
#include "world_settings.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <limits>
namespace {
using namespace world_lighting_offsets;
constexpr std::uintptr_t base = 0x10000000, nativeResult = 0x8877665544332211ULL;
unsigned checks = 0, failures = 0, nativeCalls = 0;
std::array<unsigned char, GpuStride> output{}, expectedNative{};
std::array<unsigned char, 0x640> source{};
std::array<unsigned char, 128> binner{};
float nativeRgb[3]{2, 3, 4};
bool argsPreserved = false;
HANDLE originalEntered = nullptr, originalResume = nullptr;
void Check(bool good, const char* name) {
    ++checks;
    if (!good) { ++failures; std::printf("FAIL %s\n", name); }
}
float Channel(unsigned channel) {
    float value = 0;
    std::memcpy(&value, output.data() + GpuRadiance + channel * 4, 4);
    return value;
}
bool Near(float a, float b) { return std::abs(a - b) < 0.00001f; }
std::uintptr_t NativePack(void* context, void* input, void* out) {
    ++nativeCalls;
    argsPreserved = context == binner.data() && input == source.data() && (out == output.data() || !out);
    if (originalEntered) { SetEvent(originalEntered); WaitForSingleObject(originalResume, INFINITE); }
    // Model a complete native pack, including geometry/flags. Our modifier must
    // preserve every one of these native output bytes outside the RGB float3.
    for (std::size_t i = 0; i < expectedNative.size(); ++i)
        expectedNative[i] = static_cast<unsigned char>(i * 37 + nativeCalls);
    std::memcpy(expectedNative.data() + GpuRadiance, nativeRgb, sizeof(nativeRgb));
    if (out) std::memcpy(out, expectedNative.data(), expectedNative.size());
    return nativeResult;
}
void Setup() {
    output.fill(0xAB); source.fill(0xCD); binner.fill(0xEF);
    nativeRgb[0] = 2; nativeRgb[1] = 3; nativeRgb[2] = 4;
    argsPreserved = false; nativeCalls = 0;
    WorldLightingTestConfigure(base, NativePack);
    WorldSettings s{};
    s.lightingEnabled = true; s.localLightBrightness = 2;
    s.localLightTint[0] = 0.5f; s.localLightTint[1] = 0.25f; s.localLightTint[2] = 0;
    WorldSetSettings(s);
}
std::uintptr_t Pack(std::uintptr_t caller = base + PackReturn, void* target = output.data()) {
    return WorldLightingTestPackLight(binner.data(), source.data(), target, caller);
}
bool OutsideRgbUnchanged() {
    auto expected = expectedNative;
    std::memcpy(expected.data() + GpuRadiance, output.data() + GpuRadiance, 12);
    return expected == output;
}
}
int main() {
    Setup();
    const auto sourceBefore = source;
    const auto binnerBefore = binner;
    Check(Pack() == nativeResult && argsPreserved && nativeCalls == 1,
        "three native arguments, one original call and complete RAX preserved");
    Check(Near(Channel(0), 2) && Near(Channel(1), 1.5f) && Channel(2) == 0,
        "final GPU radiance tint and brightness applied once");
    Check(OutsideRgbUnchanged(), "every byte outside twelve GPU RGB bytes preserved");
    Check(source == sourceBefore && binner == binnerBefore, "per-view source and binner never modified");
    for (unsigned i = 0; i < 100; ++i) Pack();
    Check(Near(Channel(1), 1.5f) && OutsideRgbUnchanged(), "fresh complete native pack prevents accumulation");
    nativeRgb[0] = 8; nativeRgb[1] = 6; nativeRgb[2] = 10;
    Pack();
    Check(Near(Channel(0), 8) && Near(Channel(1), 3) && Channel(2) == 0,
        "new registered or generated light baseline receives same final modifier");
    Pack(base + PackReturn + 1);
    Check(output == expectedNative && argsPreserved, "other callback caller forwards exact native record");
    auto s = WorldGetSettings(); s.lightingEnabled = false; WorldSetSettings(s); Pack();
    Check(output == expectedNative && std::strstr(WorldLightingStatus(), "disabled"), "disabled preserves complete native pack");
    s.lightingEnabled = true; s.localLightBrightness = 1;
    for (float& channel : s.localLightTint) channel = 1;
    WorldSetSettings(s); Pack();
    Check(output == expectedNative && std::strstr(WorldLightingStatus(), "neutral"), "neutral preserves exact native bytes");
    nativeRgb[1] = std::numeric_limits<float>::quiet_NaN(); Pack();
    Check(output == expectedNative, "neutral forwards nonfinite native payload without rewrite");
    Setup(); s = WorldGetSettings(); s.localLightBrightness = 0; WorldSetSettings(s); Pack();
    Check(Channel(0) == 0 && Channel(1) == 0 && Channel(2) == 0 && OutsideRgbUnchanged(), "zero local brightness changes RGB only");
    Setup(); nativeRgb[2] = std::numeric_limits<float>::quiet_NaN(); Pack();
    Check(output == expectedNative, "entire RGB validates before any write on NaN");
    nativeRgb[2] = std::numeric_limits<float>::infinity(); Pack();
    Check(output == expectedNative, "nonfinite native RGB rejected");
    nativeRgb[2] = -1; Pack();
    Check(output == expectedNative, "unexpected negative native RGB rejected");
    Setup(); nativeRgb[0] = std::numeric_limits<float>::max();
    s = WorldGetSettings(); s.localLightTint[0] = 1; s.localLightBrightness = 8; WorldSetSettings(s); Pack();
    Check(output == expectedNative, "overflow rejects full RGB modification");
    Setup(); Check(Pack(base + PackReturn, nullptr) == nativeResult && argsPreserved, "null output rejects modifier while forwarding full native call");
    Setup(); s = WorldGetSettings();
    s.sunLightBrightness = 8; s.sunLightTint[0] = 0; s.skyEnabled = true; s.skyBrightness = 0;
    WorldSetSettings(s); Pack();
    Check(Near(Channel(0), 2) && Near(Channel(1), 1.5f), "sun and sky controls independent of local RGB modifier");
    WorldLightingStop("test"); WorldLightingActivate(); Pack();
    Check(output == expectedNative && std::strstr(WorldLightingStatus(), "resident"), "stopped forwarding remains native and resident despite activation");
    Setup();
    originalEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    originalResume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE stoppedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(originalEntered && originalResume && stoppedEvent, "create stop race events");
    std::thread inNative([] { Pack(); });
    Check(WaitForSingleObject(originalEntered, 5000) == WAIT_OBJECT_0, "native original entered");
    std::thread stop([stoppedEvent] { WorldLightingStop("native GPU pack busy"); SetEvent(stoppedEvent); });
    const bool stopDidNotWait = WaitForSingleObject(stoppedEvent, 5000) == WAIT_OBJECT_0;
    SetEvent(originalResume);
    inNative.join(); stop.join();
    Check(stopDidNotWait, "stop barrier never encloses native renderer");
    Check(output == expectedNative, "stop during original prevents later RGB write");
    CloseHandle(originalEntered); CloseHandle(originalResume); CloseHandle(stoppedEvent);
    originalEntered = originalResume = nullptr;
    Pack();
    Check(output == expectedNative && argsPreserved, "later native callback remains forwarded after stop");
    std::printf("world lighting checks: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
