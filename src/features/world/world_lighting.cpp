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
using namespace world_lighting_offsets;
// The native caller ignores RAX; preserve its complete register value.
using PackFn = std::uintptr_t (*)(void*, void*, void*);
PackFn original = nullptr;
std::uintptr_t sceneBase = 0;
WorldLightingLogCallback logger = nullptr;
std::atomic<bool> enabled{false};
std::atomic<const char*> status{"local light controls not initialized"};
std::atomic<unsigned long long> callbacks{0}, modified{0}, rejected{0}, nonzero{0}, neutral{0};
SRWLOCK writeBarrier = SRWLOCK_INIT;
bool stopped = false;
unsigned long long reportedCallbacks = 0, reportedModified = 0, reportedRejected = 0;
ULONGLONG reportTime = 0;

bool Copy(std::uintptr_t address, void* output, std::size_t size) noexcept {
    if (!output || address < 0x10000 || address > 0x00007fffffffffffULL - size) return false;
    __try { std::memcpy(output, reinterpret_cast<const void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool Read(std::uintptr_t address, T& value) noexcept { return Copy(address, &value, sizeof(value)); }
template<std::size_t N> bool Match(std::uintptr_t address, const unsigned char (&bytes)[N]) noexcept {
    unsigned char observed[N]{};
    return Copy(address, observed, N) && std::memcmp(observed, bytes, N) == 0;
}
bool Store(std::uintptr_t address, const float (&rgb)[3]) noexcept {
    __try { std::memcpy(reinterpret_cast<void*>(address), rgb, sizeof(rgb)); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void ApplyRadiance(void* output) noexcept {
    AcquireSRWLockExclusive(&writeBarrier);
    const auto settings = WorldGetSettings();
    if (!enabled.load(std::memory_order_acquire) || stopped || !settings.lightingEnabled) {
        if (!stopped && enabled.load(std::memory_order_acquire))
            status.store("local light controls disabled", std::memory_order_release);
        ReleaseSRWLockExclusive(&writeBarrier);
        return;
    }
    if (settings.localLightBrightness == 1 && settings.localLightTint[0] == 1 &&
        settings.localLightTint[1] == 1 && settings.localLightTint[2] == 1) {
        neutral.fetch_add(1, std::memory_order_relaxed);
        status.store("local light controls neutral", std::memory_order_release);
        ReleaseSRWLockExclusive(&writeBarrier);
        return;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(output);
    float rgb[3]{};
    bool valid = address >= 0x10000 && address <= 0x00007fffffffffffULL - GpuStride &&
        Copy(address + GpuRadiance, rgb, sizeof(rgb));
    bool hasRadiance = false, changed = false;
    for (unsigned channel = 0; valid && channel < 3; ++channel) {
        valid = std::isfinite(rgb[channel]) && rgb[channel] >= 0;
        if (valid) {
            const float native = rgb[channel];
            hasRadiance |= native != 0;
            rgb[channel] *= settings.localLightTint[channel] * settings.localLightBrightness;
            changed |= native != rgb[channel];
            valid = std::isfinite(rgb[channel]);
        }
    }
    // The exact caller owns this freshly packed GPU record until its upload.
    // Validate the full float3, then change only twelve radiance bytes. Never
    // write persistent sources, geometry, or reflection-volume constants.
    if (valid && changed) valid = Store(address + GpuRadiance, rgb);
    if (valid) {
        nonzero.fetch_add(hasRadiance, std::memory_order_relaxed);
        modified.fetch_add(changed, std::memory_order_relaxed);
        status.store("native local GPU light radiance active", std::memory_order_release);
    } else rejected.fetch_add(1, std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&writeBarrier);
}
std::uintptr_t Pack(void* binner, void* light, void* output, std::uintptr_t caller) noexcept {
    const bool selected = enabled.load(std::memory_order_acquire) && caller == sceneBase + PackReturn;
    // Native rendering remains outside the stop barrier. A stop during this
    // call causes ApplyRadiance to retain the freshly generated native result.
    const auto result = original(binner, light, output);
    if (selected) {
        callbacks.fetch_add(1, std::memory_order_relaxed);
        ApplyRadiance(output);
    }
    return result;
}
__declspec(noinline) std::uintptr_t HookPack(void* binner, void* light, void* output) noexcept {
    return Pack(binner, light, output, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
}
bool WorldLightingPrepare(WorldLightingLogCallback callback) noexcept {
    logger = callback;
    sceneBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"scenesystem.dll"));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::uintptr_t slot = 0;
    if (!Read(sceneBase, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !Read(sceneBase + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != Timestamp || nt.OptionalHeader.SizeOfImage != ImageSize ||
        !Match(sceneBase + PackLocalLight, PackBytes) || !Match(sceneBase + PackThunk, ThunkBytes) ||
        !Match(sceneBase + PackReturn - sizeof(CallBytes), CallBytes) ||
        !Match(sceneBase + OutputStrideInstruction, StrideBytes) || !Match(sceneBase + RadianceStore, RadianceBytes) ||
        !Read(sceneBase + BinnerVtable + PackSlot, slot) || slot != sceneBase + PackThunk) {
        status.store("installed local GPU light build verification failed", std::memory_order_release);
        return false;
    }
    const auto target = reinterpret_cast<void*>(sceneBase + PackLocalLight);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookPack), reinterpret_cast<void**>(&original)) != MH_OK ||
        !original || MH_QueueEnableHook(target) != MH_OK) {
        status.store("local GPU light hook preparation failed", std::memory_order_release);
        return false;
    }
    status.store("awaiting native local GPU light packing", std::memory_order_release);
    if (logger) logger("WORLD_LIGHTING_BUILD_VERIFIED pack=0x1B14B0 caller=0x1B2537 GPU_RGB+0xA0 stride0x1E0; final g_BarnLights output only, cubemap packing untouched");
    return true;
}
void WorldLightingActivate() noexcept {
    AcquireSRWLockExclusive(&writeBarrier);
    if (!stopped && original) enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&writeBarrier);
}
void WorldLightingStop(const char* reason) noexcept {
    enabled.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&writeBarrier);
    stopped = true;
    enabled.store(false, std::memory_order_release);
    status.store("local lights stopped and resident; native future records", std::memory_order_release);
    ReleaseSRWLockExclusive(&writeBarrier);
    if (logger) {
        char line[512]{};
        std::snprintf(line, sizeof(line), "WORLD_LIGHTING_STOPPED_RESIDENT reason=%s; GPU record writes drained, forwarding retained; queued rendering may finish, no physical unload", reason ? reason : "stop");
        logger(line);
    }
}
const char* WorldLightingStatus() noexcept { return status.load(std::memory_order_acquire); }
void WorldLightingFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (reportTime && now - reportTime < 2000) return;
    reportTime = now;
    const auto called = callbacks.load(std::memory_order_relaxed), changed = modified.load(std::memory_order_relaxed),
        failed = rejected.load(std::memory_order_relaxed);
    if (called == reportedCallbacks && changed == reportedModified && failed == reportedRejected) return;
    reportedCallbacks = called; reportedModified = changed; reportedRejected = failed;
    const auto settings = WorldGetSettings();
    char line[768]{};
    std::snprintf(line, sizeof(line), "WORLD_LIGHTING gpu_callbacks=%llu gpu_nonzero=%llu gpu_rgb_changed=%llu neutral=%llu rejected=%llu enabled=%d tint=(%.3f,%.3f,%.3f) brightness=%.3f status=%s; changed native GPU records do not prove visible output", called,
        nonzero.load(std::memory_order_relaxed), changed, neutral.load(std::memory_order_relaxed), failed,
        settings.lightingEnabled, settings.localLightTint[0], settings.localLightTint[1], settings.localLightTint[2],
        settings.localLightBrightness, WorldLightingStatus());
    logger(line);
}
#ifdef CS2_WORLD_LIGHTING_CHECKS
void WorldLightingTestConfigure(std::uintptr_t base, WorldLightingTestPack pack) noexcept {
    AcquireSRWLockExclusive(&writeBarrier);
    sceneBase = base; original = pack; stopped = false;
    enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&writeBarrier);
}
std::uintptr_t WorldLightingTestPackLight(void* binner, void* light, void* output, std::uintptr_t caller) noexcept {
    return Pack(binner, light, output, caller);
}
#endif
