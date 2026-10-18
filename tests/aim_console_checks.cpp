#include "aim_console.h"
#include <Windows.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {
unsigned failures = 0;
std::atomic<unsigned> calls{0};
std::atomic<bool> valid{true};
std::atomic<bool> block{false};
HANDLE entered = nullptr, releasePrint = nullptr;
char observed[2048]{};
bool fixedFormat = true;
bool Valid() noexcept { return valid.load(); }
void Print(const char* format, ...) {
    fixedFormat = fixedFormat && std::strcmp(format, "%s") == 0;
    va_list args;
    va_start(args, format);
    std::vsnprintf(observed, sizeof(observed), format, args);
    va_end(args);
    ++calls;
    if (block.load()) { SetEvent(entered); WaitForSingleObject(releasePrint, 10000); }
}
void Expect(bool ok, const char* description) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", description);
    failures += !ok;
}
}

int main() {
    AimConsoleStop();
    AimConsoleWrite("AIM_IGNORED");
    Expect(calls == 0, "writes before initialization have no side effects");
    AimConsoleTestSetTarget(Print, Valid);
    valid = false;
    Expect(!AimConsoleInitialize(), "unavailable native target rejects initialization");
    AimConsoleWrite("AIM_IGNORED");
    Expect(calls == 0, "failed initialization never invokes the target");
    valid = true;
    Expect(AimConsoleInitialize(), "verified target initializes");
    AimConsoleWrite(nullptr);
    AimConsoleWrite("");
    Expect(calls == 0, "null and empty records are ignored");
    AimConsoleWrite("AIM_HITCHANCE chance=75% format=%n %s %08x");
    Expect(calls == 1 && fixedFormat &&
        std::strcmp(observed, "AIM_HITCHANCE chance=75% format=%n %s %08x\n") == 0,
        "percent text reaches the printf ABI through one fixed string format");
    AimConsoleWrite("AIM_TEST\r\nsecond\x01\tend");
    Expect(calls == 2 && std::strcmp(observed, "AIM_TEST  second \tend\n") == 0,
        "embedded newlines/control bytes cannot inject additional records");
    std::string longRecord(4096, 'x');
    AimConsoleWrite(longRecord.c_str());
    Expect(calls == 3 && std::strlen(observed) == 1535 && observed[1534] == '\n',
        "long records are bounded with a trailing newline and terminator");
    valid = false;
    AimConsoleWrite("AIM_STALE");
    Expect(calls == 3, "native target validity is rechecked for every write");
    valid = true;

    entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    releasePrint = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Expect(entered && releasePrint, "shutdown synchronization fixtures initialize");
    if (entered && releasePrint) {
        block = true;
        std::thread writer([] { AimConsoleWrite("AIM_BLOCKED"); });
        Expect(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0, "native print entered before concurrent stop");
        HANDLE stopStarted = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE stopDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::thread stopper([&] { SetEvent(stopStarted); AimConsoleStop(); SetEvent(stopDone); });
        WaitForSingleObject(stopStarted, 5000);
        Expect(WaitForSingleObject(stopDone, 100) == WAIT_TIMEOUT,
            "cooperative stop waits while a console call executes");
        SetEvent(releasePrint);
        writer.join();
        stopper.join();
        Expect(WaitForSingleObject(stopDone, 0) == WAIT_OBJECT_0, "stop finishes after the in-progress call returns");
        CloseHandle(stopStarted);
        CloseHandle(stopDone);
        block = false;
        const auto count = calls.load();
        AimConsoleWrite("AIM_AFTER_STOP");
        Expect(calls == count, "stopped sink rejects subsequent records");
    }
    if (entered) CloseHandle(entered);
    if (releasePrint) CloseHandle(releasePrint);
    AimConsoleTestSetTarget(nullptr, Valid);
    Expect(!AimConsoleInitialize(), "missing exported print target fails closed");
    AimConsoleStop();
    return failures ? 1 : 0;
}
