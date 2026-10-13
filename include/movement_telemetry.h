#pragma once
#include "movement_settings.h"

// Producers enqueue fixed-size records; callback/file work only runs in Drain.
void MovementTelemetrySetCallback(MovementLogCallback callback) noexcept;
void MovementTelemetryPush(const char* message) noexcept;
void MovementTelemetryDrain(unsigned maximumRecords = 8) noexcept;
unsigned long long MovementTelemetryDropped() noexcept;
