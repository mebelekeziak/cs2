#include "console_control.h"
#include "console_control_offsets.h"
#include "aim_console.h"
#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>

namespace {
namespace off = console_control_offsets;
using Listener = void (*)(void*, const void*, const char*);
using Clear = void (*)(void*, const void*, const void*);
using Queue = void (*)(void*, int, const char*);
Listener originalListener = nullptr;
Clear originalClear = nullptr;
Queue queueText = nullptr;
void* commandBuffer = nullptr;
HMODULE expectedEngine = nullptr;
SRWLOCK controlLock = SRWLOCK_INIT;
bool prepared = false;
std::atomic<bool> replayPending{false};
#ifdef CS2_CONSOLE_CONTROL_CHECKS
bool testRuntime = false;
#endif

void HookListener(void* self, const void* context, const char* text) noexcept {
    // This is only the native console listener. Other logging listeners and
    // workspace file output still receive the original native records.
    if (!ConsoleControlAvailable() || !ConsoleControlOnlyOwn() ||
        (text && !std::strncmp(text, "AIM_", 4))) originalListener(self, context, text);
}
void HookClear(void* self, const void* context, const void* args) noexcept {
    AcquireSRWLockShared(&controlLock);
    // The native callback uses the same count/vector to read argv[0]. A fixed
    // ignored argument identifies our queued command, so an unrelated manual
    // clear cannot consume its replay and leave the later queued clear empty.
    int count = 0;
    const char* const* values = nullptr;
    if (args) {
        const auto bytes = static_cast<const unsigned char*>(args);
        std::memcpy(&count, bytes + off::ArgCount, sizeof(count));
        std::memcpy(&values, bytes + off::ArgValues, sizeof(values));
    }
    const bool projectRequest = count == 2 && values && values[0] && values[1] &&
        !std::strcmp(values[0], "clear") && !std::strcmp(values[1], "cs2_project_logs");
    originalClear(self, context, args);
    if (ConsoleControlAvailable() && projectRequest && replayPending.exchange(false)) {
        AimConsoleReplay();
        ConsoleControlCompleteClear();
    }
    ReleaseSRWLockShared(&controlLock);
}
bool Span(HMODULE module, std::uintptr_t rva, std::size_t length, bool code) noexcept {
    MEMORY_BASIC_INFORMATION memory{};
    const auto at = reinterpret_cast<std::uintptr_t>(module) + rva;
    if (VirtualQuery(reinterpret_cast<void*>(at), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE || memory.AllocationBase != module ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto begin = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    if (at < begin || at - begin >= memory.RegionSize || length > memory.RegionSize - (at - begin)) return false;
    const DWORD protection = memory.Protect & 0xff;
    return code ? protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY : protection == PAGE_READONLY || protection == PAGE_READWRITE ||
        protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE;
}
template<std::size_t N>
bool Match(HMODULE module, std::uintptr_t rva, const unsigned char (&bytes)[N]) noexcept {
    return Span(module, rva, N, true) && !std::memcmp(
        reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(module) + rva), bytes, N);
}
bool Validate(HMODULE module) noexcept {
    if (!Span(module, 0, sizeof(IMAGE_DOS_HEADER), false)) return false;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < static_cast<LONG>(sizeof(*dos)) || dos->e_lfanew > 0x1000 ||
        !Span(module, dos->e_lfanew, sizeof(IMAGE_NT_HEADERS64), false)) return false;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<std::uintptr_t>(module) + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt->FileHeader.TimeDateStamp == off::Timestamp && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt->OptionalHeader.SizeOfImage == off::ImageSize &&
        Match(module, off::Listener, off::ListenerBytes) && Match(module, off::Clear, off::ClearBytes) &&
        Match(module, off::Queue, off::QueueBytes) && Match(module, off::QueueLock, off::QueueLockBytes) &&
        Match(module, off::QueueUnlock, off::QueueUnlockBytes) && Match(module, off::QueueCaller, off::QueueCallerBytes) &&
        Match(module, off::ClearRegistration, off::ClearRegistrationBytes) && Match(module, off::ClearName, off::ClearNameBytes) &&
        Match(module, off::ListenerRoute, off::ListenerRouteBytes) && Span(module, off::Buffer, 0x32EB0, false);
}
}

bool ConsoleControlPrepare() noexcept {
    ConsoleControlSetAvailable(false);
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(0, L"engine2.dll", &module)) return false;
    if (!Validate(module)) { FreeLibrary(module); return false; }
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    auto listener = reinterpret_cast<void*>(base + off::Listener);
    auto clear = reinterpret_cast<void*>(base + off::Clear);
    const bool created = MH_CreateHook(listener, reinterpret_cast<void*>(&HookListener),
        reinterpret_cast<void**>(&originalListener)) == MH_OK &&
        MH_CreateHook(clear, reinterpret_cast<void*>(&HookClear), reinterpret_cast<void**>(&originalClear)) == MH_OK;
    prepared = created && MH_QueueEnableHook(listener) == MH_OK && MH_QueueEnableHook(clear) == MH_OK;
    if (prepared) {
        expectedEngine = module;
        commandBuffer = reinterpret_cast<void*>(base + off::Buffer);
        queueText = reinterpret_cast<Queue>(base + off::Queue);
    }
    // Hooks and trampolines remain resident through shutdown; engine identity
    // is checked again before queue submissions. No permanent loader reference.
    FreeLibrary(module);
    return prepared;
}
void ConsoleControlActivate() noexcept { ConsoleControlSetAvailable(prepared); }
void ConsoleControlFlushRequests() noexcept {
    AcquireSRWLockShared(&controlLock);
    if (ConsoleControlAvailable() && ConsoleControlTakeRequest()) {
        HMODULE module = nullptr;
        bool current = false;
#ifdef CS2_CONSOLE_CONTROL_CHECKS
        if (testRuntime) current = true;
        else
#endif
        if (GetModuleHandleExW(0, L"engine2.dll", &module)) {
            current = module == expectedEngine && Match(module, off::Queue, off::QueueBytes) &&
                Match(module, off::QueueLock, off::QueueLockBytes) && Match(module, off::QueueUnlock, off::QueueUnlockBytes);
        }
        if (current) {
            replayPending.store(true);
            queueText(commandBuffer, 0, "clear cs2_project_logs\n");
        } else ConsoleControlSetAvailable(false);
        if (module) FreeLibrary(module);
    }
    ReleaseSRWLockShared(&controlLock);
}
void ConsoleControlStop() noexcept {
    AcquireSRWLockExclusive(&controlLock);
    ConsoleControlSetAvailable(false);
    replayPending.store(false);
    ReleaseSRWLockExclusive(&controlLock);
}
#ifdef CS2_CONSOLE_CONTROL_CHECKS
void ConsoleControlTestStart(ConsoleTestListener listener, ConsoleTestClear clear,
    ConsoleTestQueue queue, void* buffer) noexcept {
    ConsoleControlStop();
    testRuntime = true;
    originalListener = listener; originalClear = clear;
    queueText = queue; commandBuffer = buffer;
    prepared = true;
    ConsoleControlActivate();
}
void ConsoleControlTestMessage(const char* text) noexcept { HookListener(nullptr, nullptr, text); }
void ConsoleControlTestClear(bool projectRequest) noexcept {
    alignas(8) unsigned char args[0x448]{};
    const int count = projectRequest ? 2 : 1;
    const char* values[]{"clear", "cs2_project_logs"};
    const char* const* pointer = values;
    std::memcpy(args + off::ArgCount, &count, sizeof(count));
    std::memcpy(args + off::ArgValues, &pointer, sizeof(pointer));
    HookClear(nullptr, nullptr, args);
}
#endif
