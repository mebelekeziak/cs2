#pragma once
#include <cstdint>
using WorldSkyLogCallback = void (*)(const char* message);
bool WorldSkyPrepare(WorldSkyLogCallback callback) noexcept;
void WorldSkyActivate() noexcept;
void WorldSkyStop(const char* reason) noexcept;
const char* WorldSkyStatus() noexcept;
void WorldSkyFlushTelemetry() noexcept;
#ifdef CS2_WORLD_SKY_CHECKS
using WorldSkyTestRender = std::uintptr_t (*)(void*, void*, void*, int, void*, void*, void*);
using WorldSkyTestSetter = std::uintptr_t (*)(void*, std::uint32_t, const float*);
bool WorldSkyTestConfigure(std::uintptr_t sceneBase, WorldSkyTestRender render, WorldSkyTestSetter setter) noexcept;
std::uintptr_t WorldSkyTestDraw(void* packets, int count) noexcept;
std::uintptr_t WorldSkyTestSet(void* collection, std::uint32_t key, const float* value, std::uintptr_t caller) noexcept;
#endif
