#include "aim_console.h"
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
// Frozen from the installed AMD64 tier0.dll. tools/inspect_aim_console.py checks
// the named export, complete printf-varargs prologue, and native client callers.
constexpr DWORD Tier0Timestamp = 0x6ACFCBF4;
constexpr DWORD Tier0ImageSize = 0x401000;
constexpr std::uintptr_t MsgRva = 0x7EE60;
constexpr unsigned char MsgPrologue[]{
    0x48, 0x89, 0x4C, 0x24, 0x08, 0x48, 0x89, 0x54, 0x24, 0x10,
    0x4C, 0x89, 0x44, 0x24, 0x18, 0x4C, 0x89, 0x4C, 0x24, 0x20,
    0x57, 0x48, 0x81, 0xEC, 0x40, 0x08, 0x00, 0x00
};
constexpr std::uintptr_t ColorMsgRva = 0x7F710;
constexpr unsigned char ColorMsgPrologue[]{
    0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x53, 0x10, 0x4D, 0x89, 0x43, 0x18,
    0x4D, 0x89, 0x4B, 0x20, 0x53, 0x55, 0x57, 0x48, 0x81, 0xEC, 0x90, 0x08, 0x00, 0x00
};
using NativePrint = void (__cdecl*)(const char*, ...);
using NativeColorPrint = void (__cdecl*)(const AimConsoleColor&, const char*, ...);
SRWLOCK sinkLock = SRWLOCK_INIT;
HMODULE expectedModule = nullptr;
NativePrint nativePrint = nullptr;
NativeColorPrint nativeColorPrint = nullptr;
bool ready = false;
#ifdef CS2_AIM_CONSOLE_CHECKS
bool testTarget = false;
NativePrint testPrint = nullptr;
NativeColorPrint testColorPrint = nullptr;
bool (*testValid)() noexcept = nullptr;
bool (*testColorValid)() noexcept = nullptr;
#endif

bool Span(const void* pointer, std::size_t length, HMODULE module, bool executable) noexcept {
    MEMORY_BASIC_INFORMATION memory{};
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    if (!pointer || !length || VirtualQuery(pointer, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE || memory.AllocationBase != module ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    const auto begin = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    if (address < begin || address - begin >= memory.RegionSize ||
        length > memory.RegionSize - (address - begin)) return false;
    const DWORD protection = memory.Protect & 0xFF;
    if (executable) return protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
    return protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
        protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

bool Validate(HMODULE module, NativePrint& print) noexcept {
    print = nullptr;
    // A temporary loader reference held by the caller prevents unmapping while
    // headers, the named export and its executable bytes are inspected/called.
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (!Span(dos, sizeof(*dos), module, false) || dos->e_magic != IMAGE_DOS_SIGNATURE ||
        dos->e_lfanew < static_cast<LONG>(sizeof(*dos)) || dos->e_lfanew > 0x1000) return false;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        reinterpret_cast<std::uintptr_t>(module) + static_cast<unsigned>(dos->e_lfanew));
    if (!Span(nt, sizeof(*nt), module, false) || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt->FileHeader.TimeDateStamp != Tier0Timestamp ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfImage != Tier0ImageSize) return false;
    const auto address = GetProcAddress(module, "Msg");
    if (!address || reinterpret_cast<std::uintptr_t>(address) != reinterpret_cast<std::uintptr_t>(module) + MsgRva ||
        !Span(reinterpret_cast<const void*>(address), sizeof(MsgPrologue), module, true) ||
        std::memcmp(reinterpret_cast<const void*>(address), MsgPrologue, sizeof(MsgPrologue)) != 0) return false;
    print = reinterpret_cast<NativePrint>(address);
    return true;
}

NativeColorPrint ValidateColor(HMODULE module) noexcept {
    const auto address = GetProcAddress(module, "?ConColorMsg@@YAXAEBVColor@@PEBDZZ");
    if (!address || reinterpret_cast<std::uintptr_t>(address) != reinterpret_cast<std::uintptr_t>(module) + ColorMsgRva ||
        !Span(reinterpret_cast<const void*>(address), sizeof(ColorMsgPrologue), module, true) ||
        std::memcmp(reinterpret_cast<const void*>(address), ColorMsgPrologue, sizeof(ColorMsgPrologue)) != 0) return nullptr;
    return reinterpret_cast<NativeColorPrint>(address);
}

const char* Result(const char* message, const char* prefix) noexcept {
    const auto length = std::strlen(prefix);
    if (std::strncmp(message, prefix, length)) return nullptr;
    const char* cursor = message + length;
    if (*cursor < '0' || *cursor > '9') return nullptr;
    while (*cursor >= '0' && *cursor <= '9') ++cursor;
    return *cursor == ' ' ? cursor + 1 : nullptr;
}
AimConsoleColor Color(const char* message) noexcept {
    constexpr AimConsoleColor hit{120, 230, 160, 255}, miss{255, 120, 120, 255},
        pending{255, 205, 110, 255}, status{115, 210, 240, 255}, detail{160, 170, 185, 255};
    if (!std::strncmp(message, "AIM_HIT HIT ", 12)) return hit;
    const char* result = Result(message, "AIM_SHOT #");
    if (!result) result = Result(message, "AIM_SHOT cmd #");
    if (result) {
        if (!std::strncmp(result, "HIT ", 4)) return hit;
        if (!std::strncmp(result, "MISS ", 5)) return miss;
        if (!std::strncmp(result, "UNCONFIRMED ", 12)) return pending;
    }
    if (!std::strncmp(message, "AIM_SHOT_LOG_UNAVAILABLE", 24)) return miss;
    if (!std::strncmp(message, "AIM_SHOT_LOG_WARNING", 20)) return pending;
    if (!std::strncmp(message, "AIM_SHOT_LOG_", 13) || !std::strncmp(message, "AIM_CONSOLE_", 12)) return status;
    return detail;
}

void FormatLine(const char* message, char (&line)[1536]) noexcept {
    std::size_t length = 0;
    while (length < sizeof(line) - 2 && message[length]) {
        const auto ch = static_cast<unsigned char>(message[length]);
        // Each accepted record emits exactly one console line. '%' is preserved
        // as literal text because the only native format string is "%s".
        line[length] = ch < 0x20 && ch != '\t' ? ' ' : static_cast<char>(ch);
        ++length;
    }
    line[length++] = '\n';
    line[length] = '\0';
}
}

bool AimConsoleInitialize() noexcept {
    AcquireSRWLockExclusive(&sinkLock);
    ready = false;
    expectedModule = nullptr;
    nativePrint = nullptr;
    nativeColorPrint = nullptr;
#ifdef CS2_AIM_CONSOLE_CHECKS
    if (testTarget) {
        ready = testPrint && testValid && testValid();
        nativePrint = ready ? testPrint : nullptr;
        nativeColorPrint = ready && testColorPrint && (!testColorValid || testColorValid()) ? testColorPrint : nullptr;
        const bool initialized = ready;
        ReleaseSRWLockExclusive(&sinkLock);
        return initialized;
    }
#endif
    HMODULE module = nullptr;
    if (GetModuleHandleExW(0, L"tier0.dll", &module)) {
        ready = Validate(module, nativePrint);
        if (ready) { expectedModule = module; nativeColorPrint = ValidateColor(module); }
        FreeLibrary(module);
    }
    const bool initialized = ready;
    ReleaseSRWLockExclusive(&sinkLock);
    return initialized;
}

void AimConsoleWrite(const char* message) noexcept {
    if (!message || !*message) return;
    AcquireSRWLockShared(&sinkLock);
    if (!ready) { ReleaseSRWLockShared(&sinkLock); return; }
    HMODULE module = nullptr;
    NativePrint print = nullptr;
    NativeColorPrint colorPrint = nullptr;
#ifdef CS2_AIM_CONSOLE_CHECKS
    if (testTarget) {
        if (testValid && testValid()) {
            print = testPrint;
            if (nativeColorPrint && (!testColorValid || testColorValid())) colorPrint = testColorPrint;
        }
    } else
#endif
    if (GetModuleHandleExW(0, L"tier0.dll", &module)) {
        if (module != expectedModule || !Validate(module, print) || print != nativePrint) print = nullptr;
        else if (nativeColorPrint && ValidateColor(module) == nativeColorPrint) colorPrint = nativeColorPrint;
    }
    if (print) {
        char line[1536]{};
        FormatLine(message, line);
        if (colorPrint) colorPrint(Color(message), "%s", line);
        else print("%s", line);
    }
    if (module) FreeLibrary(module);
    ReleaseSRWLockShared(&sinkLock);
}

void AimConsoleStop() noexcept {
    AcquireSRWLockExclusive(&sinkLock);
    ready = false;
    nativePrint = nullptr;
    nativeColorPrint = nullptr;
    expectedModule = nullptr;
    ReleaseSRWLockExclusive(&sinkLock);
}

#ifdef CS2_AIM_CONSOLE_CHECKS
void AimConsoleTestSetTarget(AimConsoleTestPrint print, bool (*valid)() noexcept,
    AimConsoleTestColorPrint colorPrint, bool (*colorValid)() noexcept) noexcept {
    AcquireSRWLockExclusive(&sinkLock);
    ready = false;
    nativePrint = nullptr;
    nativeColorPrint = nullptr;
    expectedModule = nullptr;
    testTarget = true;
    testPrint = print;
    testValid = valid;
    testColorPrint = colorPrint;
    testColorValid = colorValid;
    ReleaseSRWLockExclusive(&sinkLock);
}
#endif
