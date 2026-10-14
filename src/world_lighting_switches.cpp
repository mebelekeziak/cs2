#include "world_lighting_switches.h"
#include "world_lighting_switch_offsets.h"
#include "world_settings.h"
#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace {
using namespace world_lighting_switch_offsets;
// Both complete native routines consume exactly RCX, RDX and R8, no incoming
// floating-point or stack arguments. Their native disabled branch has no return
// value and runs before any output/reference-count mutation.
using GatherFn = void (*)(void*, void*, void*);
GatherFn pointOriginal = nullptr, areaOriginal = nullptr;
WorldLightingSwitchesLogCallback logger = nullptr;
SRWLOCK decisionLock = SRWLOCK_INIT;
std::atomic<bool> enabled{false};
bool stopped = false;
std::atomic<const char*> status{"local lighting switches not initialized"};
std::atomic<unsigned long long> pointCalls{0}, areaCalls{0}, suppressed{0}, forwarded{0};
std::atomic<DWORD> lastOwner{0};
unsigned long long reportedPoint = 0, reportedArea = 0, reportedSuppressed = 0;
ULONGLONG reportTime = 0;

bool Copy(std::uintptr_t address, void* destination, std::size_t bytes) noexcept {
    if (!destination || address < 0x10000 || address > 0x00007fffffffffffULL - bytes) return false;
    __try { std::memcpy(destination, reinterpret_cast<const void*>(address), bytes); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<std::size_t N> bool Match(std::uintptr_t at, const unsigned char (&expected)[N]) noexcept {
    unsigned char observed[N]{};
    return Copy(at, observed, N) && std::memcmp(observed, expected, N) == 0;
}
bool Suppress() noexcept {
    if (!enabled.load(std::memory_order_acquire)) return false;
    const auto settings = WorldGetSettings();
    if (!settings.lightingEnabled || !settings.disableLocalLights) return false;
    AcquireSRWLockShared(&decisionLock);
    const bool result = enabled.load(std::memory_order_acquire);
    if (result) {
        suppressed.fetch_add(1, std::memory_order_relaxed);
        lastOwner.store(GetCurrentThreadId(), std::memory_order_relaxed);
        status.store("local light gathers suppressed", std::memory_order_release);
    }
    ReleaseSRWLockShared(&decisionLock);
    return result;
}
void PointHook(void* binner, void* light, void* context) noexcept {
    pointCalls.fetch_add(1, std::memory_order_relaxed);
    if (!Suppress()) { forwarded.fetch_add(1, std::memory_order_relaxed); pointOriginal(binner, light, context); }
}
void AreaHook(void* binner, void* light, void* context) noexcept {
    areaCalls.fetch_add(1, std::memory_order_relaxed);
    if (!Suppress()) { forwarded.fetch_add(1, std::memory_order_relaxed); areaOriginal(binner, light, context); }
}
}

bool WorldLightingSwitchesPrepare(WorldLightingSwitchesLogCallback callback) noexcept {
    logger = callback;
    const auto scene = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"scenesystem.dll"));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!Copy(scene, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew >= 0x100000 || !Copy(scene + dos.e_lfanew, &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != Timestamp || nt.OptionalHeader.SizeOfImage != ImageSize ||
        !Match(scene + GatherPointSpot, PointBytes) || !Match(scene + GatherArea, AreaBytes) ||
        !Match(scene + PointGate, PointGateBytes) || !Match(scene + AreaGate, AreaGateBytes) ||
        !Match(scene + FrameClear, FrameClearBytes)) {
        status.store("local lighting build verification failed", std::memory_order_release);
        return false;
    }
    void* point = reinterpret_cast<void*>(scene + GatherPointSpot);
    void* area = reinterpret_cast<void*>(scene + GatherArea);
    if (MH_CreateHook(point, reinterpret_cast<void*>(&PointHook), reinterpret_cast<void**>(&pointOriginal)) != MH_OK ||
        !pointOriginal || MH_CreateHook(area, reinterpret_cast<void*>(&AreaHook), reinterpret_cast<void**>(&areaOriginal)) != MH_OK ||
        !areaOriginal || MH_QueueEnableHook(point) != MH_OK || MH_QueueEnableHook(area) != MH_OK) {
        status.store("local lighting hook setup failed", std::memory_order_release);
        return false;
    }
    status.store("awaiting local light gather", std::memory_order_release);
    if (logger) logger("WORLD_LIGHT_SWITCH_BUILD_VERIFIED point_spot=0x19F300 area=0x19FE30 native_lb_enable_lights_false_path=void_return_before_output; no cvar or scene writes");
    return true;
}
void WorldLightingSwitchesActivate() noexcept {
    AcquireSRWLockExclusive(&decisionLock);
    if (!stopped && pointOriginal && areaOriginal) enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&decisionLock);
}
void WorldLightingSwitchesStop(const char* reason) noexcept {
    enabled.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&decisionLock);
    stopped = true;
    enabled.store(false, std::memory_order_release);
    status.store("local lighting switches stopped and resident", std::memory_order_release);
    ReleaseSRWLockExclusive(&decisionLock);
    if (logger) {
        char text[384]{};
        std::snprintf(text, sizeof(text), "WORLD_LIGHT_SWITCH_STOPPED_RESIDENT reason=%s; suppression decisions drained; original gathers retained; no physical unload", reason ? reason : "stop");
        logger(text);
    }
}
const char* WorldLightingSwitchesStatus() noexcept { return status.load(std::memory_order_acquire); }
void WorldLightingSwitchesFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (reportTime && now - reportTime < 2000) return;
    reportTime = now;
    const auto p = pointCalls.load(std::memory_order_relaxed), a = areaCalls.load(std::memory_order_relaxed),
        s = suppressed.load(std::memory_order_relaxed);
    if (p == reportedPoint && a == reportedArea && s == reportedSuppressed) return;
    reportedPoint = p; reportedArea = a; reportedSuppressed = s;
    const auto settings = WorldGetSettings();
    char text[512]{};
    std::snprintf(text, sizeof(text), "WORLD_LIGHT_SWITCH point_spot=%llu area=%llu forwarded=%llu suppressed=%llu enabled=%d disable_local=%d last_owner=%lu status=%s; gather counts do not prove visible output", p, a, forwarded.load(std::memory_order_relaxed), s, settings.lightingEnabled, settings.disableLocalLights, lastOwner.load(std::memory_order_relaxed), WorldLightingSwitchesStatus());
    logger(text);
}
#ifdef CS2_WORLD_LIGHTING_SWITCHES_CHECKS
void WorldLightingSwitchesTestConfigure(WorldLightingSwitchesTestGatherFn point,
    WorldLightingSwitchesTestGatherFn area) noexcept {
    AcquireSRWLockExclusive(&decisionLock);
    pointOriginal = point; areaOriginal = area; stopped = false;
    enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&decisionLock);
}
void WorldLightingSwitchesTestGather(bool area, void* binner, void* light, void* context) noexcept {
    if (area) AreaHook(binner, light, context); else PointHook(binner, light, context);
}
#endif
