#include "world_sky.h"
#include "world_offsets.h"
#include "world_settings.h"
#include "world_skybox_offsets.h"
#include "player_chams_material_offsets.h"
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
alignas(8) std::array<unsigned char,0x28> customBinding{};
alignas(8) std::array<unsigned char,0x20> customMaterial{};
std::uintptr_t observedBinding = 0, observedSource = 0, nestedBinding = 0;
int observedCount = 0;
unsigned resourceLoads = 0, resourceAttemptLogs = 0;
char resourceAttemptLog[1400]{};
void ResourceLog(const char* message) {
    if (std::strstr(message,"WORLD_SKYBOX_LOAD path=")) {
        ++resourceAttemptLogs;
        std::snprintf(resourceAttemptLog,sizeof(resourceAttemptLog),"%s",message);
    }
}
bool failResource = false, stopResource = false, changeResource = false;
HANDLE copierEntered = nullptr, copierResume = nullptr, renderEntered = nullptr, renderResume = nullptr;
void Check(bool good, const char* label) { ++checks; if (!good) { ++failures; std::printf("FAIL %s\n", label); } }
bool Near(float a, float b) { return std::abs(a-b) < 0.00001f; }
template<class T> void Put(void* pointer, std::size_t offset, T value) { std::memcpy(static_cast<unsigned char*>(pointer)+offset, &value, sizeof(value)); }
std::uintptr_t Copier(void* destination, std::uint32_t, const float* value) {
    if (copierEntered) { SetEvent(copierEntered); WaitForSingleObject(copierResume, INFINITE); }
    std::memcpy(destination, value, 16); return setterReturn;
}
std::uintptr_t Render(void*, void*, void* packets, int count, void*, void*, void*) {
    if (renderEntered) { SetEvent(renderEntered); WaitForSingleObject(renderResume, INFINITE); }
    if (nested && !inNested) {
        inNested = true;
        WorldSkyTestDraw(lightingPacket.data(), 1);
        inNested = false;
    }
    std::uintptr_t source=0, binding=0;
    if (packets && count>0) {
        std::memcpy(&source,static_cast<unsigned char*>(packets)+(count-1)*0x70+0x18,sizeof(source));
        if (source) std::memcpy(&binding,reinterpret_cast<void*>(source+world_skybox_offsets::SceneMaterial),sizeof(binding));
    }
    if (inNested) nestedBinding=binding;
    else { observedBinding=binding; observedSource=source; observedCount=count; }
    const auto result = WorldSkyTestSet(inNested ? nestedOutput : output,
        wrongKey ? 0x1234 : world_offsets::SkyTintKey, baseline,
        base + world_offsets::SkyTintReturn + (wrongCaller ? 1 : 0));
    Check(result == setterReturn, "native setter RAX preserved");
    return renderReturn;
}
std::uintptr_t LoadSky(const char*) {
    ++resourceLoads;
    if (stopResource) WorldSkyStop("during native resource load");
    if (changeResource) { auto settings=WorldGetSettings(); WorldSetSkyboxMaterial(settings,"materials/skybox/other.vtex"); WorldSetSettings(settings); }
    return failResource ? 0 : reinterpret_cast<std::uintptr_t>(customBinding.data());
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
    customMaterial.fill(0);
    Put(customMaterial.data(),0,base+player_chams_material_offsets::MaterialVtable);
    customBinding.fill(0); Put(customBinding.data(),0,reinterpret_cast<std::uintptr_t>(customMaterial.data()));
    // Match native registration: a newly created material has no strong owner
    // until the caller explicitly retains the returned binding.
    Put(customBinding.data(),world_skybox_offsets::BindingReferences,0);
    failResource=stopResource=changeResource=false; resourceLoads=0;
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

    // Imported cubemaps are loaded by the frame owner; rendering only borrows
    // an immutable retained binding and callback-local scene/packet copies.
    Setup(); WorldSkyTestResourceLoader(LoadSky);
    settings=WorldGetSettings(); settings.customSkyboxEnabled=true;
    Check(WorldSetSkyboxMaterial(settings,"materials/skies/custom.vtex_c"),"compiled cubemap selection normalizes"); WorldSetSettings(settings);
    const auto customVisible=visible;
    const auto customVisiblePacket=visiblePacket;
    std::uintptr_t nativeVisibleBinding=0, nativeLightingBinding=0;
    std::memcpy(&nativeVisibleBinding,visible.data()+world_skybox_offsets::SceneMaterial,sizeof(nativeVisibleBinding));
    std::memcpy(&nativeLightingBinding,lighting.data()+world_skybox_offsets::SceneMaterial,sizeof(nativeLightingBinding));
    WorldSkyTestDraw(visiblePacket.data(),1); Check(observedBinding==nativeVisibleBinding && resourceLoads==0,"draw never performs resource loading");
    WorldSkyTestOwnerFrame(); WorldSkyTestDraw(visiblePacket.data(),1);
    int retainedReferences=0;
    std::memcpy(&retainedReferences,customBinding.data()+world_skybox_offsets::BindingReferences,sizeof(retainedReferences));
    Check(retainedReferences==1,"zero-reference created material gains its first owner before publication");
    Check(resourceLoads==1 && observedBinding==reinterpret_cast<std::uintptr_t>(customBinding.data()),"owner-prepared custom material replaces visible sky");
    Check(observedSource!=reinterpret_cast<std::uintptr_t>(visible.data()),"custom draw uses callback-local scene copy");
    Check(visible==customVisible && visiblePacket==customVisiblePacket,"custom draw preserves shared scene and original packet bytes");
    WorldSkyTestDraw(lightingPacket.data(),1); Check(observedBinding==nativeLightingBinding,"custom sky leaves reflection/lighting sky native");
    nested=true; WorldSkyTestDraw(visiblePacket.data(),1); nested=false;
    Check(observedBinding==reinterpret_cast<std::uintptr_t>(customBinding.data()) && nestedBinding==nativeLightingBinding,"nested lighting draw preserves outer custom sky");
    settings.skyEnabled=false; WorldSetSettings(settings); WorldSkyTestDraw(visiblePacket.data(),1);
    Check(NativeOutput() && observedBinding==reinterpret_cast<std::uintptr_t>(customBinding.data()),"custom sky independent of tint toggle");
    settings.customSkyboxEnabled=false; WorldSetSettings(settings); WorldSkyTestDraw(visiblePacket.data(),1);
    Check(observedBinding==nativeVisibleBinding,"disable restores native material on next draw before owner callback");
    WorldSkyTestOwnerFrame(); settings.customSkyboxEnabled=true; WorldSetSettings(settings); WorldSkyTestOwnerFrame();
    Check(resourceLoads==1,"re-enabling reuses bounded retained resource");
    std::memcpy(&retainedReferences,customBinding.data()+world_skybox_offsets::BindingReferences,sizeof(retainedReferences));
    Check(retainedReferences==1,"cached reuse does not retain a second time");
    WorldSetSkyboxMaterial(settings,"materials/skies/missing.vtex"); WorldSetSettings(settings); WorldSkyTestDraw(visiblePacket.data(),1);
    Check(observedBinding==nativeVisibleBinding,"changed selection invalidates stale published material immediately");
    resourceAttemptLogs=0; WorldSkyTestLogger(ResourceLog);
    failResource=true; WorldSkyTestOwnerFrame(); WorldSkyTestDraw(visiblePacket.data(),1);
    Check(resourceAttemptLogs==1 && std::strstr(resourceAttemptLog,"path=\"materials/skies/missing.vtex\"") &&
        std::strstr(resourceAttemptLog,"result=failed stage=test_resource_boundary"),"failed attempt logs selected path and exact preparation boundary");
    Check(std::strstr(WorldSkyboxRuntimeStatus().data(),"test loading boundary"),"public runtime feedback exposes the failing preparation stage");
    Check(observedBinding==nativeVisibleBinding && resourceLoads==2,"missing resource preserves native sky");
    WorldSkyTestOwnerFrame(); Check(resourceLoads==2,"failed resource is not retried every frame");
    Check(resourceAttemptLogs==1,"failed resource diagnostics are not emitted on every frame");
    WorldSkyTestLogger(nullptr);
    WorldSetSkyboxMaterial(settings,"materials/skies/custom.vtex"); WorldSetSettings(settings); WorldSkyTestOwnerFrame();
    std::memcpy(batch.data(),lightingPacket.data(),0x70); std::memcpy(batch.data()+0x70,visiblePacket.data(),0x70);
    WorldSkyTestDraw(batch.data(),2);
    Check(observedCount==1 && observedBinding==reinterpret_cast<std::uintptr_t>(customBinding.data()),"native final-source-only batch uses isolated local packet");
    Check(!std::memcmp(batch.data()+0x70,visiblePacket.data(),0x70),"batch source packet remains unchanged");
    Put(customBinding.data(),world_skybox_offsets::BindingReferences,0); WorldSkyTestDraw(visiblePacket.data(),1);
    Check(observedBinding==nativeVisibleBinding,"invalidated native binding falls back without stale material use");
    Put(customBinding.data(),world_skybox_offsets::BindingReferences,2);
    WorldSkyStop("custom resource test"); WorldSkyActivate(); WorldSkyTestDraw(visiblePacket.data(),1);
    Check(observedBinding==nativeVisibleBinding,"stop prevents further replacement while resources remain resident");
    Setup(); WorldSkyTestResourceLoader(LoadSky); settings=WorldGetSettings(); settings.customSkyboxEnabled=true;
    WorldSetSkyboxMaterial(settings,"materials/skies/custom.vtex"); WorldSetSettings(settings); stopResource=true;
    WorldSkyTestOwnerFrame(); WorldSkyTestDraw(visiblePacket.data(),1);
    Check(observedBinding==nativeVisibleBinding,"stop inside blocking resource load prevents publication");
    Setup(); WorldSkyTestResourceLoader(LoadSky); settings=WorldGetSettings(); settings.customSkyboxEnabled=true;
    WorldSetSkyboxMaterial(settings,"materials/skies/custom.vtex"); WorldSetSettings(settings); changeResource=true;
    WorldSkyTestOwnerFrame(); WorldSkyTestDraw(visiblePacket.data(),1);
    Check(observedBinding==nativeVisibleBinding,"selection changed during load cannot publish obsolete material");
    Setup(); WorldSkyTestResourceLoader(LoadSky); settings=WorldGetSettings(); settings.customSkyboxEnabled=true;
    for (unsigned i=0;i<65;++i) {
        char path[96]{}; std::snprintf(path,sizeof(path),"materials/skies/retained_%u.vtex",i);
        WorldSetSkyboxMaterial(settings,path); WorldSetSettings(settings); WorldSkyTestOwnerFrame();
    }
    WorldSkyTestDraw(visiblePacket.data(),1);
    Check(resourceLoads==64 && observedBinding==nativeVisibleBinding,"retained sky resource cache has hard64 selection bound");
    WorldSetSkyboxMaterial(settings,"materials/skies/retained_0.vtex"); WorldSetSettings(settings); WorldSkyTestOwnerFrame();
    WorldSkyTestDraw(visiblePacket.data(),1);
    Check(resourceLoads==64 && observedBinding==reinterpret_cast<std::uintptr_t>(customBinding.data()),"full cache can still reuse an existing retained sky");

    // A zero initial count is legal only for a created material of the exact
    // native type. Borrowed resources and already-published draws stay strict.
    for (int invalidCase=0;invalidCase<4;++invalidCase) {
        Setup(); WorldSkyTestResourceLoader(LoadSky); settings=WorldGetSettings(); settings.customSkyboxEnabled=true;
        WorldSetSkyboxMaterial(settings,invalidCase==3 ? "materials/skybox/sky_de_dust2.vmat" : "materials/skies/custom.vtex");
        WorldSetSettings(settings);
        if (invalidCase==0) Put(customMaterial.data(),0,base+player_chams_material_offsets::MaterialVtable+8);
        if (invalidCase==1) Put(customBinding.data(),world_skybox_offsets::BindingReferences,-1);
        if (invalidCase==2) Put(customBinding.data(),world_skybox_offsets::BindingReferences,MAXLONG);
        int before=0,after=0;
        std::memcpy(&before,customBinding.data()+world_skybox_offsets::BindingReferences,sizeof(before));
        WorldSkyTestOwnerFrame(); WorldSkyTestDraw(visiblePacket.data(),1);
        std::memcpy(&after,customBinding.data()+world_skybox_offsets::BindingReferences,sizeof(after));
        Check(observedBinding!=reinterpret_cast<std::uintptr_t>(customBinding.data()) && before==after,
            invalidCase==0 ? "wrong material type rejects without acquiring ownership" :
            invalidCase==1 ? "negative native references reject without mutation" :
            invalidCase==2 ? "reference overflow rejects without mutation" :
            "borrowed builtin with zero references rejects before retention");
    }
    Setup(); WorldSkyTestResourceLoader(LoadSky); settings=WorldGetSettings(); settings.customSkyboxEnabled=true;
    WorldSetSkyboxMaterial(settings,"materials/skybox/sky_de_dust2.vmat"); WorldSetSettings(settings);
    Put(customBinding.data(),world_skybox_offsets::BindingReferences,1);
    WorldSkyTestOwnerFrame(); WorldSkyTestDraw(visiblePacket.data(),1);
    std::memcpy(&retainedReferences,customBinding.data()+world_skybox_offsets::BindingReferences,sizeof(retainedReferences));
    Check(observedBinding==reinterpret_cast<std::uintptr_t>(customBinding.data()) && retainedReferences==2,
        "already-owned builtin material retains existing native ownership and still renders");

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
