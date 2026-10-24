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
// Quantized steering starts with a safe digital command. The request upgrades
// that command to a full-forward movement basis at finalization, using the
// existing restore/history/camera owner. Call after successful movement Apply;
// analogTransition is its sole time-zero analog event. A rejection leaves the
// digital fallback intact and suppresses ordinary anti aim for this command.
// Requests belong to one Begin/Finish scope and never survive its next rebuild.
bool AntiAimCanOrientMovement(void* input, void* command) noexcept;
bool AntiAimRequestMovementYaw(void* input, void* command, float worldYaw,
                               std::uintptr_t analogTransition) noexcept;
