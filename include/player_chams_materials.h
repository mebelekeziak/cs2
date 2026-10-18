#pragma once
#include "player_chams_settings.h"
#include <cstdint>

using PlayerChamsMaterialsLogCallback = void (*)(const char*);
bool PlayerChamsMaterialsInitialize(PlayerChamsMaterialsLogCallback logger = nullptr) noexcept;
void PlayerChamsMaterialsOwnerFrame() noexcept;
bool PlayerChamsMaterialsResolve(PlayerChamsStyle style, std::uintptr_t view,
    std::uintptr_t& material, std::uintptr_t& mode) noexcept;
void PlayerChamsMaterialsStop() noexcept;
const char* PlayerChamsMaterialsStatus() noexcept;

#ifdef CS2_PLAYER_CHAMS_MATERIAL_CHECKS
struct PlayerChamsMaterialTestApi {
    void* (*construct)(void*, unsigned);
    void* (*root)(void*);
    void (*destroy)(void*);
    bool (*parse)(void*, void*, const char*, const void*, const char*, unsigned);
    void* (*create)(void*, void**, const char*, void*, unsigned, bool);
    std::uintptr_t (*mode)(void*, void*);
    bool (*preload)(const char*);
};
void PlayerChamsMaterialsTestConfigure(const PlayerChamsMaterialTestApi& api) noexcept;
#endif
