#pragma once
#include <cstdint>
using WorldLightingLogCallback = void (*)(const char*);
bool WorldLightingPrepare(WorldLightingLogCallback callback) noexcept;
void WorldLightingActivate() noexcept;
void WorldLightingStop(const char* reason) noexcept;
const char* WorldLightingStatus() noexcept;
void WorldLightingFlushTelemetry() noexcept;
#ifdef CS2_WORLD_LIGHTING_CHECKS
using WorldLightingTestGenerator = int (*)(void*,void*,void*,bool,float);
using WorldLightingTestArea = std::uintptr_t (*)(void*,void*,int);
void WorldLightingTestConfigure(std::uintptr_t base, WorldLightingTestGenerator original,WorldLightingTestArea area=nullptr) noexcept;
int WorldLightingTestGenerate(void* outputs,void* matrices,void* source,bool shadows,float scale,std::uintptr_t caller) noexcept;
std::uintptr_t WorldLightingTestPackArea(void* context,void* output,int index,std::uintptr_t caller) noexcept;
#endif
