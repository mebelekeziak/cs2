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
bool colorValid = true;
unsigned coloredCalls = 0;
AimConsoleColor observedColor{};
bool Valid() noexcept { return valid.load(); }
bool ColorValid() noexcept { return colorValid; }
void Observe(const char* format, va_list args) {
    fixedFormat = fixedFormat && std::strcmp(format, "%s") == 0;
    std::vsnprintf(observed, sizeof(observed), format, args);
    ++calls;
    if (block.load()) { SetEvent(entered); WaitForSingleObject(releasePrint, 10000); }
}
void Print(const char* format, ...) {
    va_list args; va_start(args, format); Observe(format, args); va_end(args);
}
void ColorPrint(const AimConsoleColor& color, const char* format, ...) {
    observedColor = color; ++coloredCalls;
    va_list args; va_start(args, format); Observe(format, args); va_end(args);
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

    AimConsoleTestSetTarget(Print, Valid, ColorPrint, ColorValid);
    Expect(AimConsoleInitialize(), "verified color target initializes with plain fallback available");
    struct ColorCase { const char* text; unsigned r, g, b; };
    const ColorCase colors[]{
        {"AIM_SHOT #1 HIT Mayer in head for 40 (60 HP)", 120, 230, 160},
        {"AIM_HIT HIT Mayer in chest for 34 (66 HP) | shot overlap", 120, 230, 160},
        {"AIM_SHOT #2 MISS spread | target HIT victim", 255, 120, 120},
        {"AIM_SHOT #3 UNCONFIRMED shot overlap", 255, 205, 110},
        {"AIM_SHOT cmd #42 UNCONFIRMED unregistered shot", 255, 205, 110},
        {"AIM_SHOT_LOG_READY native listeners registered", 115, 210, 240},
        {"AIM_CONSOLE_READY native sink", 115, 210, 240},
        {"AIM_SHOT_LOG_UNAVAILABLE native verification failed", 255, 120, 120},
        {"AIM_SHOT_LOG_WARNING bounded capacity", 255, 205, 110},
        {"AIM_SHOT_DETAIL outcome=0", 160, 170, 185},
        {"AIM_SHOT_REASON reason=spread", 160, 170, 185},
        {"AIM_SHOT #bad HIT fake format", 160, 170, 185}
    };
    for (const auto& item : colors) {
        const auto before = calls.load(); const auto beforeColor = coloredCalls;
        AimConsoleWrite(item.text);
        Expect(calls == before + 1 && coloredCalls == beforeColor + 1 && fixedFormat &&
            observedColor.r == item.r && observedColor.g == item.g && observedColor.b == item.b && observedColor.a == 255 &&
            std::string(observed) == std::string(item.text) + '\n',
            "native colored record keeps one literal text line and the expected outcome palette");
    }
    const auto beforeReplay = calls.load(); const auto colorsBeforeReplay = coloredCalls;
    AimConsoleReplay();
    Expect(calls == beforeReplay + 12 && coloredCalls == colorsBeforeReplay + 12 &&
        std::strcmp(observed, "AIM_SHOT #bad HIT fake format\n") == 0 && observedColor.r == 160 && fixedFormat,
        "history replay preserves native colors and literal formatting without adding history");
    AimConsoleWrite("AIM_SHOT #4 HIT %n\r\n\x1b[31m");
    Expect(fixedFormat && std::strcmp(observed, "AIM_SHOT #4 HIT %n   [31m\n") == 0,
        "colored printf keeps percent text literal and strips embedded console control codes");
    const auto beforeFallback = coloredCalls;
    colorValid = false;
    AimConsoleWrite("AIM_SHOT #5 MISS no impact");
    Expect(coloredCalls == beforeFallback && std::strcmp(observed, "AIM_SHOT #5 MISS no impact\n") == 0,
        "invalidated optional color target falls back to the verified plain native sink");
    AimConsoleTestSetTarget(Print, Valid, ColorPrint, ColorValid);
    Expect(AimConsoleInitialize(), "missing color availability does not disable console logging");
    AimConsoleWrite("AIM_SHOT #6 MISS no impact");
    Expect(coloredCalls == beforeFallback, "unavailable color at initialization continues through plain output");
    colorValid = true;
    AimConsoleTestSetTarget(Print, Valid, ColorPrint, ColorValid);
    Expect(AimConsoleInitialize(), "colored sink can initialize freshly before shutdown checks");

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
