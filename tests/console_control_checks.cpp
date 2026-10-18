#include "console_control.h"
#include "aim_console.h"
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include <cstring>

namespace {
unsigned failures = 0, queues = 0, clears = 0;
std::vector<std::string> screen;
int bufferIdentity = 0;
bool argsCorrect = true;
bool Valid() noexcept { return true; }
void Expect(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label); failures += !ok;
}
void Listener(void*, const void*, const char* text) { screen.emplace_back(text ? text : "<null>"); }
void Clear(void*, const void*, const void*) { ++clears; screen.clear(); }
void Queue(void* buffer, int slot, const char* text) {
    ++queues;
    argsCorrect &= buffer == &bufferIdentity && slot == 0 && !std::strcmp(text, "clear cs2_project_logs\n");
}
void Print(const char* format, ...) {
    char line[2048]{}; va_list args; va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args); va_end(args);
    ConsoleControlTestMessage(line);
}
}
int main() {
    ConsoleControlSetOnlyOwn(true); ConsoleControlRequestClear();
    Expect(!ConsoleControlAvailable() && !ConsoleControlOnlyOwn() && !ConsoleControlTakeRequest(),
        "unavailable controls ignore requests");
    AimConsoleTestSetTarget(Print, Valid);
    Expect(AimConsoleInitialize(), "project console sink initializes independently");
    ConsoleControlTestStart(Listener, Clear, Queue, &bufferIdentity);
    ConsoleControlTestMessage("native startup");
    AimConsoleWrite("AIM_SHOT #1 HIT one 100% %n");
    Expect(screen.size() == 2, "disabled filter preserves native and project messages");
    ConsoleControlSetOnlyOwn(true);
    ConsoleControlRequestClear(); ConsoleControlRequestClear();
    ConsoleControlFlushRequests(); ConsoleControlFlushRequests();
    Expect(queues == 1 && clears == 0 && argsCorrect,
        "repeated clicks coalesce into one literal command on slot zero; no render-thread clear");
    ConsoleControlTestMessage("native noise");
    ConsoleControlTestMessage(""); ConsoleControlTestMessage(nullptr);
    ConsoleControlTestMessage("prefix AIM_fake");
    Expect(screen.size() == 2, "filter rejects native messages, empty text and embedded project prefixes");
    AimConsoleWrite("AIM_SHOT #2 MISS spread");
    ConsoleControlTestClear(false);
    Expect(screen.empty(), "manual clear while a project clear is queued does not consume its pending replay");
    ConsoleControlTestClear();
    Expect(clears == 2 && screen.size() == 2 && screen[0] == "AIM_SHOT #1 HIT one 100% %n\n" &&
        screen[1] == "AIM_SHOT #2 MISS spread\n", "clear removes native history and replays project history in order");
    ConsoleControlRequestClear(); ConsoleControlFlushRequests(); ConsoleControlTestClear();
    Expect(queues == 2 && screen.size() == 2, "repeated clear does not multiply retained history");
    ConsoleControlTestClear(false);
    Expect(screen.empty(), "manually entered native clear keeps its normal behavior");
    for (int index = 0; index < 300; ++index) {
        char line[64]{}; std::snprintf(line, sizeof(line), "AIM_TEST %d", index); AimConsoleWrite(line);
    }
    ConsoleControlRequestClear(); ConsoleControlFlushRequests(); ConsoleControlTestClear();
    Expect(screen.size() == 256 && screen.front() == "AIM_TEST 44\n" && screen.back() == "AIM_TEST 299\n",
        "history wrap retains exactly the newest 256 lines in chronological order");
    ConsoleControlSetOnlyOwn(false);
    ConsoleControlTestMessage("native restored");
    Expect(screen.back() == "native restored", "disabling the filter immediately restores native messages");
    ConsoleControlSetOnlyOwn(true); ConsoleControlFlushRequests();
    const auto beforeStop = queues;
    ConsoleControlStop();
    ConsoleControlTestMessage("native after stop");
    Expect(!ConsoleControlOnlyOwn() && !ConsoleControlAvailable() && screen.back() == "native after stop",
        "cooperative stop restores native forwarding");
    ConsoleControlTestClear();
    Expect(screen.empty(), "a clear already queued before stop forwards without replay");
    ConsoleControlRequestClear(); ConsoleControlFlushRequests();
    Expect(queues == beforeStop, "stopped controls cannot queue more commands");
    AimConsoleStop(); AimConsoleReplay();
    Expect(screen.empty(), "stopped sink cannot replay retained records");
    return failures ? 1 : 0;
}
