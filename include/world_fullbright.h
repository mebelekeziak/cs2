#pragma once
#include <cstdint>

using WorldFullbrightLogCallback = void (*)(const char*);
bool WorldFullbrightPrepare(WorldFullbrightLogCallback callback) noexcept;
void WorldFullbrightActivate() noexcept;
void WorldFullbrightStop(const char* reason) noexcept;
const char* WorldFullbrightStatus() noexcept;
void WorldFullbrightFlushTelemetry() noexcept;

#ifdef CS2_WORLD_FULLBRIGHT_CHECKS
using WorldFullbrightTestSelector = std::uintptr_t (*)(void*, void*, void*);
using WorldFullbrightTestWriter = void (*)(void*, std::uint32_t, int);
void WorldFullbrightTestConfigure(std::uintptr_t clientBase, std::uintptr_t sceneBase,
    std::uintptr_t normal, std::uintptr_t tools, WorldFullbrightTestSelector selector, WorldFullbrightTestWriter writer) noexcept;
std::uintptr_t WorldFullbrightTestSelect(void* source, void* owner, void* viewport, std::uintptr_t caller) noexcept;
#endif
