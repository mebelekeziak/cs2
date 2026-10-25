#pragma once
#include <cstdint>

using WorldFullbrightLogCallback = void (*)(const char*);
bool WorldFullbrightPrepare(WorldFullbrightLogCallback callback) noexcept;
void WorldFullbrightActivate() noexcept;
void WorldFullbrightStop(const char* reason) noexcept;
const char* WorldFullbrightStatus() noexcept;
void WorldFullbrightFlushTelemetry() noexcept;

#ifdef CS2_WORLD_FULLBRIGHT_CHECKS
using WorldFullbrightTestSetter = void (*)(void*, std::uint32_t, int);
void WorldFullbrightTestConfigure(std::uintptr_t base, WorldFullbrightTestSetter setter) noexcept;
void WorldFullbrightTestSet(void* context, std::uint32_t key, int value, std::uintptr_t caller) noexcept;
void WorldFullbrightTestSetClient(void* context, std::uint32_t key, int value, std::uintptr_t caller) noexcept;
#endif
