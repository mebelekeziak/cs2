#pragma once

bool ConsoleControlAvailable() noexcept;
bool ConsoleControlOnlyOwn() noexcept;
void ConsoleControlSetOnlyOwn(bool enabled) noexcept;
void ConsoleControlRequestClear() noexcept;
const char* ConsoleControlStatus() noexcept;

// Initialization after MH_Initialize, activation after MH_ApplyQueued. No work
// in DllMain. Requests only enqueue text; native clear/replay runs on its owner.
bool ConsoleControlPrepare() noexcept;
void ConsoleControlActivate() noexcept;
void ConsoleControlFlushRequests() noexcept;
void ConsoleControlStop() noexcept;

// Shared UI/request state, also linked into the game-free menu preview.
void ConsoleControlSetAvailable(bool available) noexcept;
bool ConsoleControlTakeRequest() noexcept;
void ConsoleControlCompleteClear() noexcept;

#ifdef CS2_CONSOLE_CONTROL_CHECKS
using ConsoleTestListener = void (*)(void*, const void*, const char*);
using ConsoleTestClear = void (*)(void*, const void*, const void*);
using ConsoleTestQueue = void (*)(void*, int, const char*);
void ConsoleControlTestStart(ConsoleTestListener listener, ConsoleTestClear clear,
    ConsoleTestQueue queue, void* buffer) noexcept;
void ConsoleControlTestMessage(const char* text) noexcept;
void ConsoleControlTestClear(bool projectRequest = true) noexcept;
#endif
