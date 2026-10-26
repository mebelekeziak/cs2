#pragma once
#include <cstdint>

using WorldFullbrightLogCallback = void (*)(const char*);
bool WorldFullbrightPrepare(WorldFullbrightLogCallback callback) noexcept;
void WorldFullbrightActivate() noexcept;
void WorldFullbrightStop(const char* reason) noexcept;
const char* WorldFullbrightStatus() noexcept;
void WorldFullbrightFlushTelemetry() noexcept;

#ifdef CS2_WORLD_FULLBRIGHT_CHECKS
using WorldFullbrightTestWriter = void (*)(void*, std::uint32_t, int);
using WorldFullbrightTestFloat4 = void (*)(void*, std::uint32_t, const float*);
void WorldFullbrightTestConfigure(std::uintptr_t base, WorldFullbrightTestWriter writer, WorldFullbrightTestFloat4 vectorWriter) noexcept;
void WorldFullbrightTestSet(void* collection, std::uint32_t key, int value, std::uintptr_t caller) noexcept;
bool WorldFullbrightTestSourceRoot(void* collection) noexcept;
#endif
