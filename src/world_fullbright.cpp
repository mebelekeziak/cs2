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
using SetterFn = void (*)(void*, std::uint32_t, int);
SetterFn original = nullptr;
SetterFn originalClient = nullptr;
std::uintptr_t engine = 0, client = 0;
WorldFullbrightLogCallback logger = nullptr;
std::atomic<bool> enabled{false};
std::atomic<const char*> status{"fullbright not initialized"};
SRWLOCK mutationLock = SRWLOCK_INIT;
bool stopped = false;
std::atomic<unsigned long long> calls{0}, modified{0};
std::atomic<DWORD> owner{0};
unsigned long long reportedCalls = 0, reportedModified = 0;
ULONGLONG reportTime = 0;

bool Read(std::uintptr_t address, void* output, std::size_t size) noexcept {
    if (!output || address < 0x10000 || address > 0x00007fffffffffffULL - size) return false;
    __try { std::memcpy(output, reinterpret_cast<const void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<std::size_t N> bool Match(std::uintptr_t address, const unsigned char (&expected)[N]) noexcept {
    unsigned char observed[N]{};
    return Read(address, observed, N) && !std::memcmp(observed, expected, N);
}
void Set(void* context, std::uint32_t key, int value, std::uintptr_t caller, bool clientCall = false) noexcept {
    const auto native = clientCall ? originalClient : original;
    const bool expectedCaller = clientCall ? caller == client + off::ClientSetterReturn || caller == client + off::ClientAuxReturn :
        caller == engine + off::SetterReturn;
    if (!enabled.load(std::memory_order_acquire) || key != off::FullbrightKey || !expectedCaller) {
        native(context, key, value); return;
    }
    calls.fetch_add(1, std::memory_order_relaxed);
    AcquireSRWLockExclusive(&mutationLock);
    const bool apply = enabled.load(std::memory_order_acquire) && !stopped && WorldGetSettings().fullbrightEnabled;
    if (!apply) {
        if (!stopped) status.store("fullbright disabled", std::memory_order_release);
        ReleaseSRWLockExclusive(&mutationLock);
        native(context, key, value); return;
    }
    // The inspected helper only edits the caller's integer context collection
    // (and may allocate an overflow node). No render/job call occurs under this
    // stop barrier. Never change the shared cvar, its flags or source entities.
    native(context, key, 1);
    modified.fetch_add(1, std::memory_order_relaxed);
    owner.store(GetCurrentThreadId(), std::memory_order_relaxed);
    status.store("native fullbright render parameter active", std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
}
__declspec(noinline) void HookSet(void* context, std::uint32_t key, int value) noexcept {
    Set(context, key, value, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
__declspec(noinline) void HookSetClient(void* context, std::uint32_t key, int value) noexcept {
    Set(context, key, value, reinterpret_cast<std::uintptr_t>(_ReturnAddress()), true);
}
bool ValidImage(std::uintptr_t base, std::uint32_t timestamp, std::uint32_t imageSize) noexcept {
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    return Read(base, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
        dos.e_lfanew < 0x100000 && Read(base + dos.e_lfanew, &nt, sizeof(nt)) && nt.Signature == IMAGE_NT_SIGNATURE &&
        nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 && nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt.FileHeader.TimeDateStamp == timestamp && nt.OptionalHeader.SizeOfImage == imageSize;
}
}

bool WorldFullbrightPrepare(WorldFullbrightLogCallback callback) noexcept {
    logger = callback;
    engine = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"engine2.dll"));
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    if (!ValidImage(engine, off::Timestamp, off::ImageSize) || !ValidImage(client, off::ClientTimestamp, off::ClientImageSize) ||
        !Match(engine + off::Setter, off::SetterBytes) ||
        !Match(engine + off::Producer, off::ProducerBytes) || !Match(engine + off::CvarRead, off::CvarReadBytes) ||
        !Match(engine + off::Registration, off::RegistrationBytes) || !Match(client + off::ClientSetter, off::ClientSetterBytes) ||
        !Match(client + off::ClientProducer, off::ClientProducerBytes) || !Match(client + off::ClientCvarRead, off::ClientCvarReadBytes) ||
        !Match(client + off::ClientRegistration, off::ClientRegistrationBytes) ||
        !Match(client + off::ClientAuxProducer, off::ClientAuxProducerBytes) ||
        !Match(client + off::ClientAuxRegistration, off::ClientAuxRegistrationBytes)) {
        status.store("fullbright build verification failed", std::memory_order_release); return false;
    }
    auto* target = reinterpret_cast<void*>(engine + off::Setter);
    auto* clientTarget = reinterpret_cast<void*>(client + off::ClientSetter);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookSet), reinterpret_cast<void**>(&original)) != MH_OK ||
        !original || MH_CreateHook(clientTarget, reinterpret_cast<void*>(&HookSetClient), reinterpret_cast<void**>(&originalClient)) != MH_OK ||
        !originalClient || MH_QueueEnableHook(target) != MH_OK || MH_QueueEnableHook(clientTarget) != MH_OK) {
        status.store("fullbright hook preparation failed", std::memory_order_release); return false;
    }
    status.store("awaiting native fullbright render parameter", std::memory_order_release);
    if (logger) logger("WORLD_FULLBRIGHT_BUILD_VERIFIED engine SetInt=0x2485A0 caller=0x243420 client SetInt=0x16A9D0 callers=0xE5B0E4/0x799718 key=0xC6767B23 mat_fullbright=1; native render contexts only, no cvar writes");
    return true;
}
void WorldFullbrightActivate() noexcept {
    AcquireSRWLockExclusive(&mutationLock);
    if (!stopped && original && originalClient) enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
}
void WorldFullbrightStop(const char* reason) noexcept {
    enabled.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&mutationLock);
    stopped = true;
    enabled.store(false, std::memory_order_release);
    status.store("fullbright stopped and resident; future contexts use native values", std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
    if (logger) {
        char message[384]{};
        std::snprintf(message, sizeof(message), "WORLD_FULLBRIGHT_STOPPED_RESIDENT reason=%s; parameter writes drained, pending native frames may finish; forwarding hook retained, no physical unload", reason ? reason : "stop");
        logger(message);
    }
}
const char* WorldFullbrightStatus() noexcept { return status.load(std::memory_order_acquire); }
void WorldFullbrightFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (reportTime && now - reportTime < 2000) return;
    reportTime = now;
    const auto c = calls.load(std::memory_order_relaxed), m = modified.load(std::memory_order_relaxed);
    if (c == reportedCalls && m == reportedModified) return;
    reportedCalls = c; reportedModified = m;
    char message[384]{};
    std::snprintf(message, sizeof(message), "WORLD_FULLBRIGHT contexts=%llu modified=%llu last_owner=%lu status=%s; callback activity does not prove visible output", c, m, owner.load(std::memory_order_relaxed), WorldFullbrightStatus());
    logger(message);
}
#ifdef CS2_WORLD_FULLBRIGHT_CHECKS
void WorldFullbrightTestConfigure(std::uintptr_t base, WorldFullbrightTestSetter setter) noexcept {
    AcquireSRWLockExclusive(&mutationLock);
    engine = client = base; original = originalClient = setter; stopped = false;
    enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
}
void WorldFullbrightTestSet(void* context, std::uint32_t key, int value, std::uintptr_t caller) noexcept { Set(context, key, value, caller); }
void WorldFullbrightTestSetClient(void* context, std::uint32_t key, int value, std::uintptr_t caller) noexcept { Set(context, key, value, caller, true); }
#endif
