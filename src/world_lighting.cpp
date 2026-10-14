#include "world_lighting.h"
#include "world_lighting_offsets.h"
#include "world_settings.h"
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using GeneratorFn = int (*)(void*,void*,void*,bool,float);
GeneratorFn original = nullptr;
using AreaFn = std::uintptr_t (*)(void*,void*,int);
AreaFn originalArea = nullptr;
std::uintptr_t sceneBase = 0;
WorldLightingLogCallback logger = nullptr;
std::atomic<bool> enabled{false};
std::atomic<const char*> status{"local light controls not initialized"};
std::atomic<unsigned long long> callbacks{0}, modified{0}, rejected{0};
std::atomic<unsigned long long> areaCallbacks{0},areaModified{0};
SRWLOCK writeBarrier = SRWLOCK_INIT;
bool stopped = false;
unsigned long long lastCallbacks = 0,lastModified = 0,lastRejected = 0;
unsigned long long lastAreaCallbacks = 0,lastAreaModified = 0;
ULONGLONG reportTime = 0;

bool Copy(std::uintptr_t address,void* output,std::size_t size) noexcept {
    if (!output || address < 0x10000 || address > 0x00007fffffffffffULL-size) return false;
    __try { std::memcpy(output,reinterpret_cast<void*>(address),size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool Read(std::uintptr_t address,T& value) noexcept { return Copy(address,&value,sizeof(value)); }
bool Store(std::uintptr_t destination,const void* value,std::size_t size) noexcept {
    __try { std::memcpy(reinterpret_cast<void*>(destination),value,size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void ApplyRadiance(void* outputs,int count,std::size_t stride,std::size_t radiance,bool area) noexcept {
    AcquireSRWLockExclusive(&writeBarrier);
    const auto settings = WorldGetSettings();
    if (!enabled.load(std::memory_order_acquire) || stopped || !settings.lightingEnabled) {
        if (!stopped && enabled.load(std::memory_order_acquire)) status.store("local light controls disabled",std::memory_order_release);
        ReleaseSRWLockExclusive(&writeBarrier); return;
    }
    const auto outputAddress = reinterpret_cast<std::uintptr_t>(outputs);
    float colors[world_lighting_offsets::MaxSubLights][3]{};
    bool valid = outputAddress >= 0x10000 && outputAddress <= 0x00007fffffffffffULL - static_cast<std::size_t>(count)*stride;
    for (int i=0; valid && i<count; ++i) {
        valid = Copy(outputAddress + static_cast<std::size_t>(i)*stride + radiance,colors[i],sizeof(colors[i]));
        for (unsigned channel=0; valid && channel<3; ++channel) {
            valid = std::isfinite(colors[i][channel]) && colors[i][channel]>=0;
            if (valid) {
                colors[i][channel] *= settings.localLightTint[channel]*settings.localLightBrightness;
                valid = std::isfinite(colors[i][channel]);
            }
        }
    }
    // All colors validate before any write; these are caller-owned per-view
    // stack records, never persistent scene data or a cached object pointer.
    if (valid) for (int i=0; i<count; ++i) {
        if (!Store(outputAddress+static_cast<std::size_t>(i)*stride+radiance,colors[i],sizeof(colors[i]))) { valid=false; break; }
    }
    if (valid) {
        (area?areaModified:modified).fetch_add(static_cast<unsigned>(count),std::memory_order_relaxed);
        status.store("local light radiance active",std::memory_order_release);
    } else rejected.fetch_add(1,std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&writeBarrier);
}
int Generate(void* outputs,void* matrices,void* source,bool shadows,float scale,std::uintptr_t caller) noexcept {
    if (!enabled.load(std::memory_order_acquire) || caller != sceneBase+world_lighting_offsets::PerViewReturn)
        return original(outputs,matrices,source,shadows,scale);
    std::uint32_t type = 0;
    const bool known = Read(reinterpret_cast<std::uintptr_t>(source),type) &&
        (type == world_lighting_offsets::Omni || type == world_lighting_offsets::Spot || type == world_lighting_offsets::Ortho);
    // Preserve native shape updates on the original source. Every native call
    // remains outside our short writer barrier.
    const int count = original(outputs,matrices,source,shadows,scale);
    callbacks.fetch_add(1,std::memory_order_relaxed);
    if (known && count > 0 && count <= static_cast<int>(world_lighting_offsets::MaxSubLights))
        ApplyRadiance(outputs,count,world_lighting_offsets::SubLightStride,world_lighting_offsets::Radiance,false);
    return count;
}
__declspec(noinline) int HookGenerator(void* outputs,void* matrices,void* source,bool shadows,float scale) noexcept {
    return Generate(outputs,matrices,source,shadows,scale,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
std::uintptr_t PackArea(void* context,void* output,int index,std::uintptr_t caller) noexcept {
    const bool selected=enabled.load(std::memory_order_acquire) &&
        (caller==sceneBase+world_lighting_offsets::AreaReturnA || caller==sceneBase+world_lighting_offsets::AreaReturnB);
    const auto result=originalArea(context,output,index);
    if (selected) {
        areaCallbacks.fetch_add(1,std::memory_order_relaxed);
        ApplyRadiance(output,1,world_lighting_offsets::AreaStride,world_lighting_offsets::AreaRadiance,true);
    }
    return result;
}
__declspec(noinline) std::uintptr_t HookArea(void* context,void* output,int index) noexcept {
    return PackArea(context,output,index,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
}
bool WorldLightingPrepare(WorldLightingLogCallback callback) noexcept {
    logger = callback;
    sceneBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"scenesystem.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    unsigned char prolog[sizeof(world_lighting_offsets::GeneratorBytes)]{},call[sizeof(world_lighting_offsets::CallBytes)]{};
    unsigned char area[sizeof(world_lighting_offsets::AreaBytes)]{},areaCallA[sizeof(world_lighting_offsets::AreaCallABytes)]{},areaCallB[sizeof(world_lighting_offsets::AreaCallBBytes)]{};
    if (!sceneBase || !Read(sceneBase,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || dos.e_lfanew>0x100000 ||
        !Read(sceneBase+dos.e_lfanew,nt) || nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.TimeDateStamp!=world_lighting_offsets::Timestamp ||
        nt.OptionalHeader.SizeOfImage!=world_lighting_offsets::ImageSize ||
        !Copy(sceneBase+world_lighting_offsets::GenerateSubLights,prolog,sizeof(prolog)) || std::memcmp(prolog,world_lighting_offsets::GeneratorBytes,sizeof(prolog)) ||
        !Copy(sceneBase+world_lighting_offsets::PerViewReturn-5,call,sizeof(call)) || std::memcmp(call,world_lighting_offsets::CallBytes,sizeof(call)) ||
        !Copy(sceneBase+world_lighting_offsets::PackAreaLight,area,sizeof(area)) || std::memcmp(area,world_lighting_offsets::AreaBytes,sizeof(area)) ||
        !Copy(sceneBase+world_lighting_offsets::AreaReturnA-5,areaCallA,sizeof(areaCallA)) || std::memcmp(areaCallA,world_lighting_offsets::AreaCallABytes,sizeof(areaCallA)) ||
        !Copy(sceneBase+world_lighting_offsets::AreaReturnB-5,areaCallB,sizeof(areaCallB)) || std::memcmp(areaCallB,world_lighting_offsets::AreaCallBBytes,sizeof(areaCallB))) {
        status.store("installed local light build verification failed",std::memory_order_release); return false;
    }
    const auto target = reinterpret_cast<void*>(sceneBase+world_lighting_offsets::GenerateSubLights);
    const auto areaTarget=reinterpret_cast<void*>(sceneBase+world_lighting_offsets::PackAreaLight);
    if (MH_CreateHook(target,reinterpret_cast<void*>(&HookGenerator),reinterpret_cast<void**>(&original))!=MH_OK || !original ||
        MH_CreateHook(areaTarget,reinterpret_cast<void*>(&HookArea),reinterpret_cast<void**>(&originalArea))!=MH_OK || !originalArea ||
        MH_QueueEnableHook(target)!=MH_OK || MH_QueueEnableHook(areaTarget)!=MH_OK) {
        status.store("local light hook preparation failed",std::memory_order_release); return false;
    }
    status.store("awaiting local light generation",std::memory_order_release);
    if (logger) logger("WORLD_LIGHTING_BUILD_VERIFIED GenerateSubLights=0x4A3CE0 caller=0x1A2061 RGB+0xB4 stride0x12C; AreaPack=0x1B0FB0 callers=0x1B0060/0x1B00DB current GPU-record RGB+0x70; directional retained");
    return true;
}
void WorldLightingActivate() noexcept {
    AcquireSRWLockExclusive(&writeBarrier);
    if (!stopped && original && originalArea) enabled.store(true,std::memory_order_release);
    ReleaseSRWLockExclusive(&writeBarrier);
}
void WorldLightingStop(const char* reason) noexcept {
    enabled.store(false,std::memory_order_release);
    AcquireSRWLockExclusive(&writeBarrier); stopped=true;
    enabled.store(false,std::memory_order_release);
    status.store("local lights stopped and resident; native future records",std::memory_order_release);
    ReleaseSRWLockExclusive(&writeBarrier);
    if (logger) {
        char line[512]{};
        std::snprintf(line,sizeof(line),"WORLD_LIGHTING_STOPPED_RESIDENT reason=%s; output writes drained, forwarding code retained; queued rendering may finish, no physical unload",reason?reason:"stop"); logger(line);
    }
}
const char* WorldLightingStatus() noexcept { return status.load(std::memory_order_acquire); }
void WorldLightingFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now=GetTickCount64(); if (reportTime && now-reportTime<2000) return; reportTime=now;
    const auto called=callbacks.load(std::memory_order_relaxed), changed=modified.load(std::memory_order_relaxed), failed=rejected.load(std::memory_order_relaxed);
    const auto areaCalled=areaCallbacks.load(std::memory_order_relaxed),areaChanged=areaModified.load(std::memory_order_relaxed);
    if (called==lastCallbacks && changed==lastModified && failed==lastRejected && areaCalled==lastAreaCallbacks && areaChanged==lastAreaModified) return;
    lastCallbacks=called; lastModified=changed; lastRejected=failed;
    lastAreaCallbacks=areaCalled;lastAreaModified=areaChanged;
    char line[512]{};
    std::snprintf(line,sizeof(line),"WORLD_LIGHTING callbacks=%llu radiance_records=%llu area_callbacks=%llu area_records=%llu rejected=%llu status=%s; callbacks do not prove visible output",called,changed,areaCalled,areaChanged,failed,WorldLightingStatus()); logger(line);
}
#ifdef CS2_WORLD_LIGHTING_CHECKS
void WorldLightingTestConfigure(std::uintptr_t base,WorldLightingTestGenerator generator,WorldLightingTestArea area) noexcept {
    sceneBase=base; original=generator; originalArea=area; stopped=false; enabled.store(true,std::memory_order_release);
}
int WorldLightingTestGenerate(void* outputs,void* matrices,void* source,bool shadows,float scale,std::uintptr_t caller) noexcept {
    return Generate(outputs,matrices,source,shadows,scale,caller);
}
std::uintptr_t WorldLightingTestPackArea(void* context,void* output,int index,std::uintptr_t caller) noexcept {
    return PackArea(context,output,index,caller);
}
#endif
