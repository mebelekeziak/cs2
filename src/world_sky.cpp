#include "world_sky.h"
#include "world_offsets.h"
#include "world_settings.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using RenderFn = std::uintptr_t (*)(void*, void*, void*, int, void*, void*, void*);
using SetterFn = std::uintptr_t (*)(void*, std::uint32_t, const float*);
RenderFn originalRender = nullptr;
SetterFn originalSetter = nullptr;
std::uintptr_t sceneBase = 0;
DWORD scopeTls = TLS_OUT_OF_INDEXES;
struct DrawScope { std::uint32_t kind; };
WorldSkyLogCallback logger = nullptr;
std::atomic<bool> enabled{false};
std::atomic<const char*> status{"sky controls not initialized"};
std::atomic<unsigned long long> draws{0}, modified{0}, rejected{0};
std::atomic<DWORD> lastOwner{0};
SRWLOCK mutationLock = SRWLOCK_INIT;
bool stopped = false;
unsigned long long reportedDraws = 0, reportedModified = 0, reportedRejected = 0;
ULONGLONG reportTime = 0;

bool Copy(std::uintptr_t address, void* output, std::size_t bytes) noexcept {
    if (!output || address < 0x10000 || address > 0x00007fffffffffffULL - bytes) return false;
    __try { std::memcpy(output, reinterpret_cast<const void*>(address), bytes); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool Read(std::uintptr_t address, T& output) noexcept {
    return Copy(address, &output, sizeof(output));
}

std::uintptr_t SetParameter(void* collection, std::uint32_t key, const float* value, std::uintptr_t caller) noexcept {
    if (!enabled.load(std::memory_order_acquire) || key != world_offsets::SkyTintKey || caller != sceneBase + world_offsets::SkyTintReturn)
        return originalSetter(collection, key, value);
    const auto* scope = scopeTls == TLS_OUT_OF_INDEXES ? nullptr : static_cast<const DrawScope*>(TlsGetValue(scopeTls));
    if (!scope || (scope->kind != world_offsets::VisibleKind && scope->kind != world_offsets::LightingKind))
        return originalSetter(collection, key, value);
    AcquireSRWLockExclusive(&mutationLock);
    // Stop disarms before taking this barrier. The full native Render never
    // holds it, so engine/job waits cannot deadlock the stop barrier.
    const auto settings = WorldGetSettings();
    float output[4]{};
    bool apply = enabled.load(std::memory_order_acquire) && !stopped && settings.skyEnabled;
    if (enabled.load(std::memory_order_acquire) && !stopped && !settings.skyEnabled)
        status.store("sky controls disabled", std::memory_order_release);
    if (apply) {
        apply = Copy(reinterpret_cast<std::uintptr_t>(value), output, sizeof(output));
        const auto* tint = scope->kind == world_offsets::VisibleKind ? settings.skyTint : settings.skyLightingTint;
        for (unsigned i = 0; apply && i < 3; ++i) {
            apply = std::isfinite(output[i]) && output[i] >= 0 && output[i] <= 65536;
            if (apply) output[i] *= tint[i] * settings.skyBrightness;
        }
        if (!apply) rejected.fetch_add(1, std::memory_order_relaxed);
    }
    if (!apply) {
        ReleaseSRWLockExclusive(&mutationLock);
        return originalSetter(collection, key, value);
    }
    // Verified native writer copies all 16 bytes into its own collection. Its
    // possible allocator calls do not contain render/job synchronization.
    const auto result = originalSetter(collection, key, output);
    modified.fetch_add(1, std::memory_order_relaxed);
    status.store("sky draw tint active", std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
    return result;
}
__declspec(noinline) std::uintptr_t HookSetter(void* collection, std::uint32_t key, const float* value) noexcept {
    return SetParameter(collection, key, value, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
std::uintptr_t HookRender(void* descriptor, void* context, void* packets, int count,
                          void* arg5, void* view, void* stats) noexcept {
    draws.fetch_add(1, std::memory_order_relaxed);
    lastOwner.store(GetCurrentThreadId(), std::memory_order_relaxed);
    if (!enabled.load(std::memory_order_acquire))
        return originalRender(descriptor, context, packets, count, arg5, view, stats);
    DrawScope scope{};
    const auto packetsAddress = reinterpret_cast<std::uintptr_t>(packets);
    std::uintptr_t source = 0;
    // The native renderer selects the final packet's source for the batch.
    // Never cache a source across draws or change any source field.
    if (count > 0 && count <= 1048576 && packetsAddress >= 0x10000 &&
        packetsAddress <= 0x00007fffffffffffULL - static_cast<std::uintptr_t>(count) * world_offsets::DrawStride &&
        Read(packetsAddress + static_cast<std::uintptr_t>(count - 1) * world_offsets::DrawStride + world_offsets::DrawSource, source) &&
        source <= 0x00007fffffffffffULL - world_offsets::SceneKind)
        Read(source + world_offsets::SceneKind, scope.kind);
    void* previous = scopeTls == TLS_OUT_OF_INDEXES ? nullptr : TlsGetValue(scopeTls);
    const bool scoped = scopeTls != TLS_OUT_OF_INDEXES && TlsSetValue(scopeTls, &scope);
    if (!scoped) {
        enabled.store(false, std::memory_order_release);
        status.store("sky draw TLS failed; controls disarmed", std::memory_order_release);
    }
    const auto result = originalRender(descriptor, context, packets, count, arg5, view, stats);
    if (scoped && !TlsSetValue(scopeTls, previous)) {
        enabled.store(false, std::memory_order_release);
        status.store("sky draw TLS restoration failed; controls disarmed", std::memory_order_release);
    }
    return result;
}
}

bool WorldSkyPrepare(WorldSkyLogCallback callback) noexcept {
    logger = callback;
    sceneBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"scenesystem.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    unsigned char renderBytes[sizeof(world_offsets::SkyRenderBytes)]{}, setterBytes[sizeof(world_offsets::SetFloat4Bytes)]{};
    std::uintptr_t renderSlot = 0;
    const auto client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER clientDos{}; IMAGE_NT_HEADERS64 clientNt{};
    unsigned char producerBytes[sizeof(world_offsets::LightingProducerBytes)]{};
    if (!sceneBase || !Read(sceneBase, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !Read(sceneBase + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.TimeDateStamp != world_offsets::SceneTimestamp ||
        nt.OptionalHeader.SizeOfImage != world_offsets::SceneImageSize ||
        !Copy(sceneBase + world_offsets::SkyRender, renderBytes, sizeof(renderBytes)) || std::memcmp(renderBytes, world_offsets::SkyRenderBytes, sizeof(renderBytes)) ||
        !Copy(sceneBase + world_offsets::SetFloat4, setterBytes, sizeof(setterBytes)) || std::memcmp(setterBytes, world_offsets::SetFloat4Bytes, sizeof(setterBytes)) ||
        !Read(sceneBase + world_offsets::SkyDescriptorVtable + 8, renderSlot) || renderSlot != sceneBase + world_offsets::SkyRender ||
        !client || !Read(client, clientDos) || clientDos.e_magic != IMAGE_DOS_SIGNATURE || clientDos.e_lfanew <= 0 || clientDos.e_lfanew > 0x100000 ||
        !Read(client + clientDos.e_lfanew, clientNt) || clientNt.Signature != IMAGE_NT_SIGNATURE || clientNt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        clientNt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || clientNt.FileHeader.TimeDateStamp != cs2_offsets::ClientTimestamp ||
        clientNt.OptionalHeader.SizeOfImage != cs2_offsets::ClientImageSize ||
        !Copy(client + world_offsets::LightingProducer, producerBytes, sizeof(producerBytes)) || std::memcmp(producerBytes, world_offsets::LightingProducerBytes, sizeof(producerBytes))) {
        status.store("installed sky draw build verification failed", std::memory_order_release); return false;
    }
    if (scopeTls == TLS_OUT_OF_INDEXES) scopeTls = TlsAlloc();
    if (scopeTls == TLS_OUT_OF_INDEXES) { status.store("sky draw TLS allocation failed", std::memory_order_release); return false; }
    const auto renderTarget = reinterpret_cast<void*>(sceneBase + world_offsets::SkyRender);
    const auto setterTarget = reinterpret_cast<void*>(sceneBase + world_offsets::SetFloat4);
    if (MH_CreateHook(renderTarget, reinterpret_cast<void*>(&HookRender), reinterpret_cast<void**>(&originalRender)) != MH_OK || !originalRender ||
        MH_CreateHook(setterTarget, reinterpret_cast<void*>(&HookSetter), reinterpret_cast<void**>(&originalSetter)) != MH_OK || !originalSetter ||
        MH_QueueEnableHook(renderTarget) != MH_OK || MH_QueueEnableHook(setterTarget) != MH_OK) {
        status.store("sky draw hook preparation failed", std::memory_order_release); return false;
    }
    status.store("awaiting sky draw", std::memory_order_release);
    if (logger) logger("WORLD_SKY_BUILD_VERIFIED scenesystem Render=0x154FB0 args=7 SetFloat4=0x4DA70 caller=0x1551A9 key=0x99E6002A; per-draw local parameters, scene fields untouched");
    return true;
}
void WorldSkyActivate() noexcept {
    AcquireSRWLockExclusive(&mutationLock);
    if (!stopped && originalRender && originalSetter && scopeTls != TLS_OUT_OF_INDEXES) enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
}
void WorldSkyStop(const char* reason) noexcept {
    enabled.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&mutationLock);
    stopped = true;
    enabled.store(false, std::memory_order_release);
    status.store("sky stopped and resident; future draws use native parameters", std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
    if (logger) {
        char message[512]{};
        std::snprintf(message, sizeof(message), "WORLD_SKY_STOPPED_RESIDENT reason=%s; modified parameter writers drained, pending GPU draws may finish; forwarding callbacks and TLS retained, no physical unload", reason ? reason : "stop");
        logger(message);
    }
}
const char* WorldSkyStatus() noexcept { return status.load(std::memory_order_acquire); }
void WorldSkyFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (reportTime && now - reportTime < 2000) return;
    reportTime = now;
    const auto called = draws.load(std::memory_order_relaxed), applied = modified.load(std::memory_order_relaxed), failed = rejected.load(std::memory_order_relaxed);
    if (called == reportedDraws && applied == reportedModified && failed == reportedRejected) return;
    reportedDraws = called; reportedModified = applied; reportedRejected = failed;
    char message[512]{};
    std::snprintf(message, sizeof(message), "WORLD_SKY draws=%llu parameter_updates=%llu rejected=%llu last_owner=%lu status=%s; callback activity does not prove visible output", called, applied, failed, lastOwner.load(std::memory_order_relaxed), WorldSkyStatus());
    logger(message);
}
#ifdef CS2_WORLD_SKY_CHECKS
bool WorldSkyTestConfigure(std::uintptr_t base, WorldSkyTestRender render, WorldSkyTestSetter setter) noexcept {
    if (scopeTls == TLS_OUT_OF_INDEXES) scopeTls = TlsAlloc();
    sceneBase = base; originalRender = render; originalSetter = setter; stopped = false;
    enabled.store(scopeTls != TLS_OUT_OF_INDEXES, std::memory_order_release);
    return scopeTls != TLS_OUT_OF_INDEXES;
}
std::uintptr_t WorldSkyTestDraw(void* packets, int count) noexcept { return HookRender(nullptr, nullptr, packets, count, nullptr, nullptr, nullptr); }
std::uintptr_t WorldSkyTestSet(void* collection, std::uint32_t key, const float* value, std::uintptr_t caller) noexcept { return SetParameter(collection, key, value, caller); }
#endif
