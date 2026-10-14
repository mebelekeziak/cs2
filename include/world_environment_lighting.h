#pragma once
#include <cstddef>
#include <cstdint>

using WorldEnvironmentLightingLogCallback = void (*)(const char*);
bool WorldEnvironmentLightingPrepare(WorldEnvironmentLightingLogCallback callback) noexcept;
void WorldEnvironmentLightingActivate() noexcept;
void WorldEnvironmentLightingStop(const char* reason) noexcept;
const char* WorldEnvironmentLightingStatus() noexcept;
void WorldEnvironmentLightingFlushTelemetry() noexcept;

#ifdef CS2_WORLD_ENVIRONMENT_LIGHTING_CHECKS
using WorldEnvironmentLightingTestCopyFn = void* (*)(void*, const void*, std::size_t);
void WorldEnvironmentLightingTestConfigure(std::uintptr_t base, WorldEnvironmentLightingTestCopyFn copy) noexcept;
void* WorldEnvironmentLightingTestCopy(void* destination, const void* source, std::size_t size, std::uintptr_t caller) noexcept;
#endif
