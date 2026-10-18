#pragma once
#include "shot_log.h"

using ShotLoggingCallback = void (*)(const char*);
bool ShotLoggingPrepare(ShotLoggingCallback logger) noexcept;
void ShotLoggingActivate() noexcept;
// Native command owner; listener registration and local identity snapshots.
void ShotLoggingPump() noexcept;
// Metadata contains copied values only; the runtime assigns its lifecycle.
void ShotLoggingCapture(const ShotLogMetadata& metadata) noexcept;
void ShotLoggingStop(const char* reason) noexcept;
// Render owner: settles/drains (including after Stop) outside tracker locks.
void ShotLoggingFlushTelemetry() noexcept;

#ifdef CS2_SHOT_LOGGING_CHECKS
#include <cstdint>
void ShotLoggingTestReset() noexcept;
void ShotLoggingTestClock(std::uint64_t now) noexcept;
void ShotLoggingTestSource(std::uint32_t controller, std::uint32_t pawn) noexcept;
#endif
