#pragma once
#include <cstdint>

using WorldLightingSwitchesLogCallback = void (*)(const char*);
bool WorldLightingSwitchesPrepare(WorldLightingSwitchesLogCallback callback) noexcept;
void WorldLightingSwitchesActivate() noexcept;
void WorldLightingSwitchesStop(const char* reason) noexcept;
const char* WorldLightingSwitchesStatus() noexcept;
void WorldLightingSwitchesFlushTelemetry() noexcept;

#ifdef CS2_WORLD_LIGHTING_SWITCHES_CHECKS
using WorldLightingSwitchesTestGatherFn = void (*)(void*, void*, void*);
void WorldLightingSwitchesTestConfigure(WorldLightingSwitchesTestGatherFn point,
    WorldLightingSwitchesTestGatherFn area) noexcept;
void WorldLightingSwitchesTestGather(bool area, void* binner, void* light, void* context) noexcept;
#endif
