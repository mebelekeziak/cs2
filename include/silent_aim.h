#pragma once

using SilentAimLogCallback = void (*)(const char*);
// Startup and cooperative stop run outside DllMain. Hooks remain resident.
bool SilentAimPrepare(SilentAimLogCallback callback) noexcept;
void SilentAimActivate(bool commandHookReady = true) noexcept;
void SilentAimStop(const char* reason) noexcept;
const char* SilentAimStatus() noexcept;
// Render owner: reports bounded callback/rejection diagnostics outside hooks.
void SilentAimFlushTelemetry() noexcept;
// Native finalizer owner, after previous anti-aim restoration and movement,
// before anti aim emission and the original native checksum callback.
void SilentAimPrepareCommand(void* input, void* command, bool commandReady = true) noexcept;

#ifdef CS2_AIM_PIPELINE_CHECKS
#include <cstdint>
using AimTestHistoryFn = void (*)(void*, int, void*);
using AimTestRecordFn = void (*)(void*, int);
void SilentAimTestConfigure(std::uintptr_t client, AimTestHistoryFn builder, AimTestRecordFn recorder,
    SilentAimLogCallback logger = nullptr) noexcept;
void SilentAimTestHistory(void* input, void* command) noexcept;
void SilentAimTestForeground(bool foreground) noexcept;
void SilentAimTestSpread(bool prepared) noexcept;
#endif
