#pragma once
#include <cstdint>

using AntiAimLogCallback = void (*)(const char*);
bool AntiAimPrepare(AntiAimLogCallback callback) noexcept;
void AntiAimActivate(bool commandHookReady) noexcept;
void AntiAimStop(const char* reason) noexcept;
const char* AntiAimStatus() noexcept;
// Render owner: bounded diagnostics include callbacks and precise rejection fields.
void AntiAimFlushTelemetry() noexcept;
// Engine finalizer owner: restore a previous edit before movement planning,
// then apply the new edit before the native movement checksum is rebuilt.
bool AntiAimBeginCommand(void* input, void* command) noexcept;
void AntiAimFinishCommand(void* input, void* command) noexcept;
void AntiAimObserveIngest(void* services, void* command) noexcept;
// Movement planner: obtain this sequence's physical predecessor, independent
// of the latest prediction ingestion. Failure leaves the copied axes intact.
bool AntiAimPhysicalHistory(std::uintptr_t services, std::uintptr_t command, float& forward, float& left) noexcept;
