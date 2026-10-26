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
using SetFn = void (*)(void*, std::uint32_t, int);
using Float4Fn = void (*)(void*, std::uint32_t, const float*);
SetFn original = nullptr;
Float4Fn setFloat4 = nullptr;
std::uintptr_t client = 0;
WorldFullbrightLogCallback logger = nullptr;
SRWLOCK mutationLock = SRWLOCK_INIT;
std::atomic<bool> enabled{false};
bool stopped = false;
std::atomic<const char*> status{"fullbright not initialized"};
std::atomic<unsigned long long> calls{0}, modified{0}, rejected{0};
std::atomic<DWORD> ownerThread{0};
unsigned long long reportedCalls = 0, reportedModified = 0, reportedRejected = 0;
ULONGLONG reportTime = 0;
#ifdef CS2_WORLD_FULLBRIGHT_CHECKS
bool testing = false;
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
bool Image(std::uintptr_t base) noexcept {
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    return Read(base, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
        dos.e_lfanew < 0x100000 && Read(base + dos.e_lfanew, nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
        nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 && nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt.FileHeader.TimeDateStamp == off::ClientTimestamp && nt.OptionalHeader.SizeOfImage == off::ClientImageSize;
}
bool Current() noexcept {
#ifdef CS2_WORLD_FULLBRIGHT_CHECKS
    if (testing) return true;
#endif
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) == client;
}
bool SourceRoot(void* collection, void*& root) noexcept {
    const auto integers = reinterpret_cast<std::uintptr_t>(collection);
    if (integers < 0x10000 + off::SourceRootToIntegers) return false;
    const auto source = integers - off::SourceRootToIntegers;
    ULONG_PTR lower = 0, upper = 0;
    GetCurrentThreadStackLimits(&lower, &upper);
    // The verified caller passes its own RBP+0x6C0 integer collection.
    // Its root is RBP+0x5B0, never a retained/shared scene or parent context.
    if (source < lower || integers >= upper || upper - integers < sizeof(std::uintptr_t)) return false;
    root = reinterpret_cast<void*>(source);
    return true;
}
bool ApplyRecipe(void* root, void* integers) noexcept {
    const float white[4]{1, 1, 1, 1};
    __try {
        // Exact lighting inputs used by the native mat_fullbright==1 branch.
        // Native helpers synchronously copy/update existing local parameters.
        setFloat4(root, off::AmbientKey, white);
        original(integers, off::DirectKey, 0);
        original(integers, off::IndirectKey, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void Set(void* collection, std::uint32_t key, int value, std::uintptr_t caller) noexcept {
    if (key != off::FullbrightKey || caller != client + off::ClientSetterReturn || !enabled.load(std::memory_order_acquire)) {
        original(collection, key, value); return;
    }
    calls.fetch_add(1, std::memory_order_relaxed);
    AcquireSRWLockExclusive(&mutationLock);
    const bool apply = enabled.load(std::memory_order_acquire) && !stopped && WorldGetSettings().fullbrightEnabled;
    if (!apply) {
        if (!stopped) status.store("fullbright disabled; native source inputs forwarded", std::memory_order_release);
        ReleaseSRWLockExclusive(&mutationLock);
        original(collection, key, value); return;
    }
    void* root = nullptr;
    if (Current() && SourceRoot(collection, root) && ApplyRecipe(root, collection)) {
        modified.fetch_add(1, std::memory_order_relaxed);
        ownerThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        status.store("fullbright ambient lighting active; native renderer preserved", std::memory_order_release);
    } else {
        rejected.fetch_add(1, std::memory_order_relaxed);
        status.store("fullbright source lighting update failed", std::memory_order_release);
    }
    // This key is only the callback marker. Preserve its exact native value,
    // allowing the native selector/capability policy to choose its own mode.
    original(collection, key, value);
    ReleaseSRWLockExclusive(&mutationLock);
}
__declspec(noinline) void HookSet(void* collection, std::uint32_t key, int value) noexcept {
    Set(collection, key, value, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
}

bool WorldFullbrightPrepare(WorldFullbrightLogCallback callback) noexcept {
    logger = callback;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    if (!Image(client) || !Match(client + off::ClientSetter, off::ClientSetterBytes) ||
        !Match(client + off::ClientProducer, off::ClientProducerBytes) ||
        !Match(client + off::SourceFloat4Setter, off::SourceFloat4SetterBytes) ||
        !Match(client + off::SourceRecipe, off::SourceRecipeBytes) ||
        !Match(client + off::SourceReset, off::SourceResetBytes) ||
        !Match(client + off::SourceLightingReset, off::SourceLightingResetBytes)) {
        status.store("fullbright source build verification failed", std::memory_order_release); return false;
    }
    setFloat4 = reinterpret_cast<Float4Fn>(client + off::SourceFloat4Setter);
    auto* target = reinterpret_cast<void*>(client + off::ClientSetter);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookSet), reinterpret_cast<void**>(&original)) != MH_OK ||
        !original || MH_QueueEnableHook(target) != MH_OK) {
        status.store("fullbright source hook preparation failed", std::memory_order_release); return false;
    }
    status.store("awaiting native lighting source producer", std::memory_order_release);
    if (logger) logger("WORLD_FULLBRIGHT_BUILD_VERIFIED revision=3 native fullbright lighting recipe AmbientColor=(1,1,1,1) DirectLighting=0 IndirectLighting=0; native rendering selection and marker value preserved, no capability/cvar writes");
    return true;
}
void WorldFullbrightActivate() noexcept {
    AcquireSRWLockExclusive(&mutationLock);
    if (!stopped && original && setFloat4) enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
}
void WorldFullbrightStop(const char* reason) noexcept {
    enabled.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&mutationLock);
    stopped = true;
    enabled.store(false, std::memory_order_release);
    status.store("fullbright stopped and resident; future source inputs stay native", std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
    if (logger) {
        char message[384]{};
        std::snprintf(message, sizeof(message), "WORLD_FULLBRIGHT_STOPPED_RESIDENT reason=%s; admitted source writers drained, pending views may finish; forwarding callback retained, no physical unload", reason ? reason : "stop");
        logger(message);
    }
}
const char* WorldFullbrightStatus() noexcept { return status.load(std::memory_order_acquire); }
void WorldFullbrightFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (reportTime && now - reportTime < 2000) return;
    reportTime = now;
    const auto c = calls.load(), m = modified.load(), x = rejected.load();
    if (c == reportedCalls && m == reportedModified && x == reportedRejected) return;
    reportedCalls = c; reportedModified = m; reportedRejected = x;
    char message[448]{};
    std::snprintf(message, sizeof(message), "WORLD_FULLBRIGHT revision=3 source_frames=%llu modified=%llu rejected=%llu last_owner=%lu status=%s; source inputs do not establish visible output", c, m, x, ownerThread.load(), WorldFullbrightStatus());
    logger(message);
}
#ifdef CS2_WORLD_FULLBRIGHT_CHECKS
void WorldFullbrightTestConfigure(std::uintptr_t base, WorldFullbrightTestWriter writer, WorldFullbrightTestFloat4 vectorWriter) noexcept {
    AcquireSRWLockExclusive(&mutationLock);
    testing = true; client = base; original = writer; setFloat4 = vectorWriter; stopped = false;
    enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
}
void WorldFullbrightTestSet(void* collection, std::uint32_t key, int value, std::uintptr_t caller) noexcept {
    Set(collection, key, value, caller);
}
bool WorldFullbrightTestSourceRoot(void* collection) noexcept { void* root = nullptr; return SourceRoot(collection, root); }
#endif
