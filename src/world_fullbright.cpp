#include "world_fullbright.h"
#include "world_fullbright_offsets.h"
#include "world_settings.h"
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace {
namespace off = world_fullbright_offsets;
using SelectFn = std::uintptr_t (*)(void*, void*, void*);
using SetFn = void (*)(void*, std::uint32_t, int);
SelectFn original = nullptr;
SetFn setInteger = nullptr;
std::uintptr_t client = 0, scene = 0;
WorldFullbrightLogCallback logger = nullptr;
SRWLOCK mutationLock = SRWLOCK_INIT;
std::atomic<bool> enabled{false}, everApplied{false};
bool stopped = false;
std::atomic<const char*> status{"fullbright not initialized"};
std::atomic<unsigned long long> calls{0}, modified{0}, restored{0}, rejected{0};
std::atomic<DWORD> ownerThread{0};
unsigned long long reportedCalls = 0, reportedModified = 0, reportedRestored = 0, reportedRejected = 0;
ULONGLONG reportTime = 0;
#ifdef CS2_WORLD_FULLBRIGHT_CHECKS
bool testing = false;
std::uintptr_t testDefault = 0, testTools = 0;
#endif
bool Read(std::uintptr_t address, void* output, std::size_t size) noexcept {
    if (!output || address < 0x10000 || address > 0x00007fffffffffffULL - size) return false;
    __try { std::memcpy(output, reinterpret_cast<const void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool Read(std::uintptr_t address, T& output) noexcept { return Read(address, &output, sizeof(output)); }
template<std::size_t N> bool Match(std::uintptr_t address, const unsigned char (&expected)[N]) noexcept {
    unsigned char observed[N]{};
    return Read(address, observed, N) && !std::memcmp(observed, expected, N);
}
bool Image(std::uintptr_t base, std::uint32_t timestamp, std::uint32_t imageSize) noexcept {
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    return Read(base, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
        dos.e_lfanew < 0x100000 && Read(base + dos.e_lfanew, nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
        nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 && nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt.FileHeader.TimeDateStamp == timestamp && nt.OptionalHeader.SizeOfImage == imageSize;
}
bool Current() noexcept {
#ifdef CS2_WORLD_FULLBRIGHT_CHECKS
    if (testing) return true;
#endif
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) == client &&
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"scenesystem.dll")) == scene;
}
std::uintptr_t DefaultDescriptor() noexcept {
#ifdef CS2_WORLD_FULLBRIGHT_CHECKS
    if (testing) return testDefault;
#endif
    return client + off::DefaultDescriptor;
}
std::uintptr_t ToolsDescriptor() noexcept {
#ifdef CS2_WORLD_FULLBRIGHT_CHECKS
    if (testing) return testTools;
#endif
    return client + off::ToolsDescriptor;
}
bool DescriptorsReady() noexcept {
    std::uint32_t normal = 0, tools = 0;
    return Read(DefaultDescriptor(), normal) && normal == off::DefaultToken &&
        Read(ToolsDescriptor(), tools) && tools == off::ToolsToken;
}
bool Collection(void* owner, void*& collection) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(owner);
    std::uintptr_t table = 0, accessor = 0;
    if (address < 0x10000 || address > 0x00007fffffffffffULL - off::SceneContext - off::IntegerCollection ||
        !Read(address, table) || !Read(table + off::SceneContextAccessorSlot, accessor) ||
        accessor != scene + off::SceneContextAccessor) return false;
    // The exact native virtual method is LEA RAX,[RCX+SceneContext]; RET.
    // Compatible native subclasses sharing that method keep the same layout.
    collection = reinterpret_cast<void*>(address + off::SceneContext + off::IntegerCollection);
    return true;
}
bool Write(void* collection, int mode) noexcept {
    __try { setInteger(collection, off::ToolsModeKey, mode); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::uintptr_t Select(void* source, void* owner, void* viewport, std::uintptr_t caller) noexcept {
    // Native work may dispatch/wait for jobs. Never hold our barrier over it.
    const auto native = original(source, owner, viewport);
    if (caller != client + off::SelectorReturn) return native;
    calls.fetch_add(1, std::memory_order_relaxed);
    if (!enabled.load(std::memory_order_acquire) && !everApplied.load(std::memory_order_acquire)) return native;
    if (!Current() || (native != DefaultDescriptor() && native != ToolsDescriptor())) return native;
    void* collection = nullptr;
    if (!DescriptorsReady() || !Collection(owner, collection)) {
        rejected.fetch_add(1, std::memory_order_relaxed);
        if (enabled.load(std::memory_order_acquire)) status.store("fullbright view contract rejected", std::memory_order_release);
        return native;
    }
    AcquireSRWLockExclusive(&mutationLock);
    const bool apply = enabled.load(std::memory_order_acquire) && !stopped && WorldGetSettings().fullbrightEnabled;
    auto result = native;
    if (apply) {
        // Select BOTH the material variant and its GPU-visible integer from
        // this same callback snapshot. No capability/cvar/global-mode writes.
        if (Write(collection, off::ToolsModeValue)) {
            everApplied.store(true, std::memory_order_release);
            result = ToolsDescriptor();
            modified.fetch_add(1, std::memory_order_relaxed);
            ownerThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
            status.store("fullbright ToolsVis view and lighting mode active", std::memory_order_release);
        } else {
            rejected.fetch_add(1, std::memory_order_relaxed);
            status.store("fullbright parameter write failed; native descriptor retained", std::memory_order_release);
        }
    } else if (everApplied.load(std::memory_order_acquire) && native == DefaultDescriptor()) {
        // CSceneView may reuse its inline context. Native default selection
        // skips this key when ToolsVisModes is unavailable. Normalize its
        // documented default 0 on the CURRENT owner, including after stop;
        // never retain a scene pointer or assume the map was cleared.
        if (Write(collection, off::NativeDefaultMode)) {
            restored.fetch_add(1, std::memory_order_relaxed);
            if (!stopped) status.store("fullbright disabled; native default view restored", std::memory_order_release);
        } else {
            rejected.fetch_add(1, std::memory_order_relaxed);
            status.store(stopped ? "fullbright stopped and resident; native-default restoration failed" :
                "fullbright disabled; native-default restoration failed", std::memory_order_release);
        }
    } else if (!stopped) status.store("fullbright disabled", std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
    return result;
}
__declspec(noinline) std::uintptr_t HookSelect(void* source, void* owner, void* viewport) noexcept {
    return Select(source, owner, viewport, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
}

bool WorldFullbrightPrepare(WorldFullbrightLogCallback callback) noexcept {
    logger = callback;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    scene = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"scenesystem.dll"));
    std::uintptr_t accessor = 0;
    if (!Image(client, off::ClientTimestamp, off::ClientImageSize) || !Image(scene, off::SceneTimestamp, off::SceneImageSize) ||
        !Match(client + off::Selector, off::SelectorBytes) || !Match(client + off::SelectorCaller, off::SelectorCallerBytes) ||
        !Match(client + off::ContextSetter, off::ContextSetterBytes) ||
        !Match(scene + off::SceneContextAccessor, off::SceneContextAccessorBytes) ||
        !Read(scene + off::SceneViewVtable + off::SceneContextAccessorSlot, accessor) || accessor != scene + off::SceneContextAccessor) {
        status.store("fullbright view build verification failed", std::memory_order_release); return false;
    }
    setInteger = reinterpret_cast<SetFn>(client + off::ContextSetter);
    auto* target = reinterpret_cast<void*>(client + off::Selector);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookSelect), reinterpret_cast<void**>(&original)) != MH_OK ||
        !original || MH_QueueEnableHook(target) != MH_OK) {
        status.store("fullbright view hook preparation failed", std::memory_order_release); return false;
    }
    status.store("awaiting native rendering view selection", std::memory_order_release);
    if (logger) logger("WORLD_FULLBRIGHT_BUILD_VERIFIED revision=2 native view selector; paired ToolsVis descriptor and g_nToolsVisMode=1; native-default mode=0 restoration on current owner, no capability/cvar writes");
    return true;
}
void WorldFullbrightActivate() noexcept {
    AcquireSRWLockExclusive(&mutationLock);
    if (!stopped && original && setInteger) enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
}
void WorldFullbrightStop(const char* reason) noexcept {
    enabled.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&mutationLock);
    stopped = true;
    enabled.store(false, std::memory_order_release);
    status.store("fullbright stopped and resident; future default views normalize native mode", std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
    if (logger) {
        char message[384]{};
        std::snprintf(message, sizeof(message), "WORLD_FULLBRIGHT_STOPPED_RESIDENT reason=%s; admitted writes drained; pending views may finish; forwarding/default normalization callback retained, no physical unload", reason ? reason : "stop");
        logger(message);
    }
}
const char* WorldFullbrightStatus() noexcept { return status.load(std::memory_order_acquire); }
void WorldFullbrightFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (reportTime && now - reportTime < 2000) return;
    reportTime = now;
    const auto c = calls.load(), m = modified.load(), r = restored.load(), x = rejected.load();
    if (c == reportedCalls && m == reportedModified && r == reportedRestored && x == reportedRejected) return;
    reportedCalls = c; reportedModified = m; reportedRestored = r; reportedRejected = x;
    char message[448]{};
    std::snprintf(message, sizeof(message), "WORLD_FULLBRIGHT revision=2 views=%llu modified=%llu native_default_restores=%llu rejected=%llu last_owner=%lu status=%s; paired view selection does not establish visible output", c, m, r, x, ownerThread.load(), WorldFullbrightStatus());
    logger(message);
}
#ifdef CS2_WORLD_FULLBRIGHT_CHECKS
void WorldFullbrightTestConfigure(std::uintptr_t clientBase, std::uintptr_t sceneBase,
    std::uintptr_t normal, std::uintptr_t tools, WorldFullbrightTestSelector selector, WorldFullbrightTestWriter writer) noexcept {
    AcquireSRWLockExclusive(&mutationLock);
    testing = true; client = clientBase; scene = sceneBase; testDefault = normal; testTools = tools;
    original = selector; setInteger = writer; stopped = false; everApplied.store(false);
    enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
}
std::uintptr_t WorldFullbrightTestSelect(void* source, void* owner, void* viewport, std::uintptr_t caller) noexcept {
    return Select(source, owner, viewport, caller);
}
#endif
