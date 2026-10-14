#include "world_sky.h"
#include "world_offsets.h"
#include "world_settings.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <limits>

namespace {
unsigned failures = 0, checks = 0;
constexpr std::uintptr_t base = 0x10000000, renderReturn = 0xAABBCCDD, setterReturn = 0x12345678;
std::array<unsigned char, 0x110> visible{}, lighting{}, unknown{};
std::array<unsigned char, 0x70> visiblePacket{}, lightingPacket{}, unknownPacket{};
float output[4]{}, nestedOutput[4]{}, baseline[4]{2,3,4,0.37f};
bool nested = false, inNested = false, wrongKey = false, wrongCaller = false;
HANDLE copierEntered = nullptr, copierResume = nullptr, renderEntered = nullptr, renderResume = nullptr;
void Check(bool good, const char* label) { ++checks; if (!good) { ++failures; std::printf("FAIL %s\n", label); } }
bool Near(float a, float b) { return std::abs(a-b) < 0.00001f; }
template<class T> void Put(void* pointer, std::size_t offset, T value) { std::memcpy(static_cast<unsigned char*>(pointer)+offset, &value, sizeof(value)); }
std::uintptr_t Copier(void* destination, std::uint32_t, const float* value) {
    if (copierEntered) { SetEvent(copierEntered); WaitForSingleObject(copierResume, INFINITE); }
    std::memcpy(destination, value, 16); return setterReturn;
}
std::uintptr_t Render(void*, void*, void*, int, void*, void*, void*) {
    if (renderEntered) { SetEvent(renderEntered); WaitForSingleObject(renderResume, INFINITE); }
    if (nested && !inNested) {
        inNested = true;
        WorldSkyTestDraw(lightingPacket.data(), 1);
        inNested = false;
    }
    const auto result = WorldSkyTestSet(inNested ? nestedOutput : output,
        wrongKey ? 0x1234 : world_offsets::SkyTintKey, baseline,
        base + world_offsets::SkyTintReturn + (wrongCaller ? 1 : 0));
    Check(result == setterReturn, "native setter RAX preserved");
    return renderReturn;
}
void Setup() {
    visible.fill(0xAB); lighting.fill(0xCD); unknown.fill(0xEF);
    visiblePacket.fill(0); lightingPacket.fill(0); unknownPacket.fill(0);
    Put(visible.data(), world_offsets::SceneKind, world_offsets::VisibleKind);
    Put(lighting.data(), world_offsets::SceneKind, world_offsets::LightingKind);
    Put(unknown.data(), world_offsets::SceneKind, std::uint32_t{0x1234});
    Put(visiblePacket.data(), world_offsets::DrawSource, reinterpret_cast<std::uintptr_t>(visible.data()));
    Put(lightingPacket.data(), world_offsets::DrawSource, reinterpret_cast<std::uintptr_t>(lighting.data()));
    Put(unknownPacket.data(), world_offsets::DrawSource, reinterpret_cast<std::uintptr_t>(unknown.data()));
    nested = inNested = wrongKey = wrongCaller = false;
    Check(WorldSkyTestConfigure(base, Render, Copier), "dynamic TLS allocation");
    WorldSettings settings{}; settings.skyEnabled = true; settings.skyBrightness = 2;
    settings.skyTint[0] = 0.5f; settings.skyTint[1] = 0.25f; settings.skyTint[2] = 0;
    settings.skyLightingTint[0] = 0.1f; settings.skyLightingTint[1] = 0.2f; settings.skyLightingTint[2] = 0.3f;
    WorldSetSettings(settings);
}
bool NativeOutput() { return std::memcmp(output, baseline, sizeof(output)) == 0; }
}
int main() {
    Setup(); const auto untouchedVisible = visible, untouchedLighting = lighting;
    Check(WorldSkyTestDraw(visiblePacket.data(), 1) == renderReturn, "native render RAX preserved");
    Check(Near(output[0],2) && Near(output[1],1.5f) && Near(output[2],0) && Near(output[3],baseline[3]), "visible tint and brightness preserve alpha");
    WorldSkyTestDraw(lightingPacket.data(),1);
    Check(Near(output[0],0.4f) && Near(output[1],1.2f) && Near(output[2],2.4f), "lighting sky receives separate tint");
    Check(visible == untouchedVisible && lighting == untouchedLighting, "scene bytes never change");
    for (unsigned i=0; i<100; ++i) WorldSkyTestDraw(visiblePacket.data(),1);
    Check(Near(output[1],1.5f) && baseline[1] == 3, "repeated draws use baseline without accumulation");
    nested = true; WorldSkyTestDraw(visiblePacket.data(),1); nested = false;
    Check(Near(nestedOutput[1],1.2f) && Near(output[1],1.5f), "nested draw restores outer TLS scope");
    WorldSkyTestSet(output,world_offsets::SkyTintKey,baseline,base+world_offsets::SkyTintReturn);
    Check(NativeOutput(), "TLS scope removed when render returns");
    wrongKey = true; WorldSkyTestDraw(visiblePacket.data(),1); wrongKey = false;
    Check(NativeOutput(), "other shader key forwards unchanged");
    wrongCaller = true; WorldSkyTestDraw(visiblePacket.data(),1); wrongCaller = false;
    Check(NativeOutput(), "other caller forwards unchanged");
    WorldSkyTestDraw(unknownPacket.data(),1); Check(NativeOutput(), "unknown scene kind forwards unchanged");
    WorldSkyTestDraw(nullptr,0); Check(NativeOutput(), "empty draw forwards without designation");
    auto settings = WorldGetSettings(); settings.skyEnabled = false; WorldSetSettings(settings);
    WorldSkyTestDraw(visiblePacket.data(),1); Check(NativeOutput(), "disabled uses next native draw");
    Check(std::strstr(WorldSkyStatus(),"disabled") != nullptr, "disabled status reflects current controls");
    settings.skyEnabled = true; settings.skyBrightness = 0; WorldSetSettings(settings);
    WorldSkyTestDraw(visiblePacket.data(),1); Check(output[0]==0 && output[1]==0 && output[2]==0 && Near(output[3],baseline[3]), "zero brightness preserves alpha");
    Setup(); std::array<unsigned char,0xE0> batch{};
    std::memcpy(batch.data(),visiblePacket.data(),0x70); std::memcpy(batch.data()+0x70,lightingPacket.data(),0x70);
    WorldSkyTestDraw(batch.data(),2); Check(Near(output[1],1.2f), "batch uses final native source");
    baseline[1] = std::numeric_limits<float>::quiet_NaN(); WorldSkyTestDraw(visiblePacket.data(),1);
    Check(std::isnan(output[1]) && output[0]==baseline[0], "invalid baseline forwards original value"); baseline[1]=3;
    WorldSkyStop("test"); WorldSkyActivate(); WorldSkyTestDraw(visiblePacket.data(),1);
    Check(NativeOutput() && std::strstr(WorldSkyStatus(),"resident"), "stop stays disarmed and resident");

    // Native Render may wait; Stop must return without waiting on full Render.
    Setup(); renderEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr); renderResume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread busyRender([] { WorldSkyTestDraw(visiblePacket.data(),1); });
    WaitForSingleObject(renderEntered,INFINITE);
    WorldSkyTestSet(output,world_offsets::SkyTintKey,baseline,base+world_offsets::SkyTintReturn);
    Check(NativeOutput(), "render TLS designation stays on its thread");
    WorldSkyStop("inside native render");
    SetEvent(renderResume); busyRender.join(); CloseHandle(renderEntered); CloseHandle(renderResume); renderEntered=renderResume=nullptr;
    Check(NativeOutput(), "stop during original render prevents later modifier");

    // Modified copier owns a short stop barrier until the by-value store ends.
    Setup(); copierEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr); copierResume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE stoppedEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr), stoppingEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread busyCopier([] { WorldSkyTestDraw(visiblePacket.data(),1); });
    WaitForSingleObject(copierEntered,INFINITE);
    std::thread stopper([&] { SetEvent(stoppingEvent); WorldSkyStop("inside modified copier"); SetEvent(stoppedEvent); });
    WaitForSingleObject(stoppingEvent,INFINITE);
    Check(WaitForSingleObject(stoppedEvent,30)==WAIT_TIMEOUT, "stop drains current modified copier");
    SetEvent(copierResume); busyCopier.join(); stopper.join();
    CloseHandle(copierEntered); CloseHandle(copierResume); CloseHandle(stoppedEvent); CloseHandle(stoppingEvent); copierEntered=copierResume=nullptr;
    WorldSkyTestDraw(visiblePacket.data(),1); Check(NativeOutput(), "draws after drained stop forward baseline");
    std::printf("world sky checks: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
