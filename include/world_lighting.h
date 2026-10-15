#pragma once
#include <cstdint>
using WorldLightingLogCallback = void (*)(const char*);
bool WorldLightingPrepare(WorldLightingLogCallback callback) noexcept;
void WorldLightingActivate() noexcept;
void WorldLightingStop(const char* reason) noexcept;
const char* WorldLightingStatus() noexcept;
void WorldLightingFlushTelemetry() noexcept;
#ifdef CS2_WORLD_LIGHTING_CHECKS
using WorldLightingTestPack = std::uintptr_t (*)(void*, void*, void*);
void WorldLightingTestConfigure(std::uintptr_t base, WorldLightingTestPack original) noexcept;
std::uintptr_t WorldLightingTestPackLight(void* binner, void* light, void* output, std::uintptr_t caller) noexcept;
#endif
