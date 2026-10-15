#include "world_environment_lighting.h"
#include "world_environment_lighting_offsets.h"
#include "world_settings.h"
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace world_environment_lighting_offsets;
using CopyFn = void* (*)(void*, const void*, std::size_t);
CopyFn originalCopy = nullptr;
std::uintptr_t sceneBase = 0;
WorldEnvironmentLightingLogCallback logger = nullptr;
SRWLOCK mutationLock = SRWLOCK_INIT;
std::atomic<bool> enabled{false};
bool stopped = false;
std::atomic<const char*> status{"environment lighting not initialized"};
std::atomic<unsigned long long> frames{0}, modified{0}, rejected{0};
std::atomic<DWORD> lastOwner{0};
unsigned long long reportedFrames = 0, reportedModified = 0, reportedRejected = 0;
ULONGLONG reportTime = 0;

bool Read(std::uintptr_t address, void* output, std::size_t size) noexcept {
    if (!output || address < 0x10000 || address > 0x00007fffffffffffULL - size) return false;
    __try { std::memcpy(output, reinterpret_cast<const void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<std::size_t N> bool Match(std::uintptr_t address, const unsigned char (&bytes)[N]) noexcept {
    unsigned char observed[N]{};
    return Read(address, observed, N) && std::memcmp(observed, bytes, N) == 0;
}
// The exact native checkpoint constructs three contiguous per-view packets on
// its own stack. Require the source and following variant to belong to this
// callback's current thread, so no scene/entity or borrowed output is written.
bool OwnedFrame(void* destination, const void* source, std::size_t size) noexcept {
    if (size != BufferSize) return false;
    ULONG_PTR lower = 0, upper = 0;
    GetCurrentThreadStackLimits(&lower, &upper);
    const auto from = reinterpret_cast<std::uintptr_t>(source);
    const auto to = reinterpret_cast<std::uintptr_t>(destination);
    return lower < upper && from >= lower && from <= upper &&
        upper - from >= BufferSize * 2 && to == from + BufferSize;
}
bool WriteFrame(std::uintptr_t frame, const float (&sun)[3], bool sunChanged,
                float directional, bool directionalChanged,
                const float (&ambient)[3], bool ambientChanged) noexcept {
    __try {
        if (sunChanged) std::memcpy(reinterpret_cast<void*>(frame + SunRgb), sun, sizeof(sun));
        if (directionalChanged) std::memcpy(reinterpret_cast<void*>(frame + DirectionalLightmap), &directional, sizeof(directional));
        if (ambientChanged) std::memcpy(reinterpret_cast<void*>(frame + AmbientRgb), ambient, sizeof(ambient));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void* CopyFrame(void* destination, const void* source, std::size_t size, std::uintptr_t caller) noexcept {
    if (!enabled.load(std::memory_order_acquire) || caller != sceneBase + CopyReturn || size != BufferSize)
        return originalCopy(destination, source, size);
    frames.fetch_add(1, std::memory_order_relaxed);
    const auto settings = WorldGetSettings();
    if (!settings.lightingEnabled) return originalCopy(destination, source, size);
    const bool sunChanged = settings.sunLightBrightness != 1 || settings.sunLightTint[0] != 1 ||
        settings.sunLightTint[1] != 1 || settings.sunLightTint[2] != 1;
    const bool directionalChanged = settings.directionalLightmapsMode != 0;
    const bool ambientChanged = settings.ambientLightEnabled && settings.ambientLightBrightness > 0 &&
        (settings.ambientLightTint[0] > 0 || settings.ambientLightTint[1] > 0 || settings.ambientLightTint[2] > 0);
    if (!sunChanged && !directionalChanged && !ambientChanged) return originalCopy(destination, source, size);
    AcquireSRWLockExclusive(&mutationLock);
    bool apply = enabled.load(std::memory_order_acquire) && !stopped && OwnedFrame(destination, source, size);
    const auto frame = reinterpret_cast<std::uintptr_t>(source);
    float sun[3]{}, ambient[3]{}, directional = settings.directionalLightmapsMode == 1 ? 0.0f : 1.0f;
    float nativeDirectional = 0;
    if (apply && directionalChanged) {
        apply = Read(frame + DirectionalLightmap, &nativeDirectional, sizeof(nativeDirectional)) &&
            (nativeDirectional == 0 || nativeDirectional == 1);
    }
    if (apply && sunChanged) {
        apply = Read(frame + SunRgb, sun, sizeof(sun));
        for (unsigned i = 0; apply && i < 3; ++i) {
            apply = std::isfinite(sun[i]) && sun[i] >= 0 && sun[i] <= 65536;
            if (apply) sun[i] *= settings.sunLightTint[i] * settings.sunLightBrightness;
        }
    }
    if (apply && ambientChanged) {
        apply = Read(frame + AmbientRgb, ambient, sizeof(ambient));
        for (unsigned i = 0; apply && i < 3; ++i) {
            apply = std::isfinite(ambient[i]) && std::fabs(ambient[i]) <= 65536;
            if (apply) ambient[i] += settings.ambientLightTint[i] * settings.ambientLightBrightness;
        }
    }
    if (apply) apply = WriteFrame(frame, sun, sunChanged, directional, directionalChanged, ambient, ambientChanged);
    if (!apply) {
        rejected.fetch_add(1, std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&mutationLock);
        return originalCopy(destination, source, size);
    }
    // The only native operation inside this stop barrier is the inspected pure
    // memory-copy helper. Full render/engine job routines never hold this lock.
    // Preserve the changed base packet for its later GPU submission, and let
    // the native helper propagate it to all its requested shadow/view variants.
    void* result = originalCopy(destination, source, size);
    modified.fetch_add(1, std::memory_order_relaxed);
    lastOwner.store(GetCurrentThreadId(), std::memory_order_relaxed);
    status.store("sun, ambient fill and directional lightmap view constants active", std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
    return result;
}
__declspec(noinline) void* HookCopy(void* destination, const void* source, std::size_t size) noexcept {
    return CopyFrame(destination, source, size, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
}

bool WorldEnvironmentLightingPrepare(WorldEnvironmentLightingLogCallback callback) noexcept {
    logger = callback;
    sceneBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"scenesystem.dll"));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!Read(sceneBase, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew >= 0x100000 || !Read(sceneBase + dos.e_lfanew, &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != Timestamp || nt.OptionalHeader.SizeOfImage != ImageSize ||
        !Match(sceneBase + CopyBuffer, CopyBytes) || !Match(sceneBase + BufferProducer, ProducerBytes) ||
        !Match(sceneBase + BufferUpload, UploadBytes) ||
        !Match(sceneBase + DirectionalProducer, DirectionalBytes) ||
        !Match(sceneBase + DirectionalStore, DirectionalStoreBytes) || !Match(sceneBase + SunProducer, SunBytes) ||
        !Match(sceneBase + AmbientProducer, AmbientProducerBytes) || !Match(sceneBase + AmbientStore, AmbientStoreBytes)) {
        status.store("environment lighting build verification failed", std::memory_order_release);
        return false;
    }
    void* target = reinterpret_cast<void*>(sceneBase + CopyBuffer);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookCopy), reinterpret_cast<void**>(&originalCopy)) != MH_OK ||
        !originalCopy || MH_QueueEnableHook(target) != MH_OK) {
        status.store("environment lighting hook preparation failed", std::memory_order_release);
        return false;
    }
    status.store("awaiting native lighting view constants", std::memory_order_release);
    if (logger) logger("WORLD_ENV_LIGHT_BUILD_VERIFIED copy=0x569120 caller=0x1B0CD1 buffer=0x7DE0 sun_rgb=0x79A0 ambient_rgb=0x120 directional_lightmaps=0x40; freshly constructed native stack output only, no scene/cvar writes");
    return true;
}
void WorldEnvironmentLightingActivate() noexcept {
    AcquireSRWLockExclusive(&mutationLock);
    if (!stopped && originalCopy) enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
}
void WorldEnvironmentLightingStop(const char* reason) noexcept {
    enabled.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&mutationLock);
    stopped = true;
    enabled.store(false, std::memory_order_release);
    status.store("environment lighting stopped and resident", std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
    if (logger) {
        char text[384]{};
        std::snprintf(text, sizeof(text), "WORLD_ENV_LIGHT_STOPPED_RESIDENT reason=%s; stack writes/copies drained, pending native GPU submissions may finish; forwarding hook retained, no physical unload", reason ? reason : "stop");
        logger(text);
    }
}
const char* WorldEnvironmentLightingStatus() noexcept { return status.load(std::memory_order_acquire); }
void WorldEnvironmentLightingFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (reportTime && now - reportTime < 2000) return;
    reportTime = now;
    const auto f = frames.load(std::memory_order_relaxed), m = modified.load(std::memory_order_relaxed),
        r = rejected.load(std::memory_order_relaxed);
    if (f == reportedFrames && m == reportedModified && r == reportedRejected) return;
    reportedFrames = f; reportedModified = m; reportedRejected = r;
    char text[512]{};
    std::snprintf(text, sizeof(text), "WORLD_ENV_LIGHT views=%llu modified=%llu rejected=%llu last_owner=%lu status=%s; constant updates do not prove visible output", f, m, r, lastOwner.load(std::memory_order_relaxed), WorldEnvironmentLightingStatus());
    logger(text);
}
#ifdef CS2_WORLD_ENVIRONMENT_LIGHTING_CHECKS
void WorldEnvironmentLightingTestConfigure(std::uintptr_t base, WorldEnvironmentLightingTestCopyFn copy) noexcept {
    AcquireSRWLockExclusive(&mutationLock);
    sceneBase = base; originalCopy = copy; stopped = false;
    enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
}
void* WorldEnvironmentLightingTestCopy(void* destination, const void* source, std::size_t size, std::uintptr_t caller) noexcept {
    return CopyFrame(destination, source, size, caller);
}
#endif
