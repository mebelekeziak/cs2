#pragma once
#include "player_chams_settings.h"
#include "world_materials.h"
#include <cstddef>
#include <cstdint>

using PlayerChamsLogCallback = void (*)(const char*);
bool PlayerChamsPrepare(PlayerChamsLogCallback callback) noexcept;
void PlayerChamsActivate() noexcept;
void PlayerChamsStop(const char* reason) noexcept;
void PlayerChamsFlushTelemetry() noexcept;
const char* PlayerChamsStatus() noexcept;

// Native owner handle construction includes the identity's serial correction.
std::uint32_t PlayerChamsOwnerHandle(std::uint32_t identity, std::uint32_t flags) noexcept;
bool PlayerChamsApplyNewDraws(const WorldDrawList& before, const WorldDrawList& after,
    std::uintptr_t object, std::uintptr_t material, std::uintptr_t mode,
    const float (&color)[4], std::size_t& changed) noexcept;
#ifdef CS2_PLAYER_CHAMS_CHECKS
using PlayerChamsGenerateFn = void (*)(void*,void*,void*,void*);
using PlayerChamsPvsFn = bool (*)(void*,void*);
void PlayerChamsTestConfigure(std::uintptr_t clientBase, std::uintptr_t sceneBase,
    PlayerChamsGenerateFn original, PlayerChamsPvsFn pvs = nullptr) noexcept;
void PlayerChamsTestGenerate(void* descriptor,void* object,void* args,void* list) noexcept;
bool PlayerChamsTestPvs(void* data,void* view,void* object,std::uintptr_t caller) noexcept;
#endif
