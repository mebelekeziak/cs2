#pragma once
#include <cstdint>
using GrenadeEffectsLogCallback = void (*)(const char*);
bool GrenadeEffectsPrepare(GrenadeEffectsLogCallback logger) noexcept;
void GrenadeEffectsActivate() noexcept;
void GrenadeEffectsStop() noexcept;
const char* GrenadeEffectsStatus() noexcept;

#ifdef CS2_GRENADE_EFFECTS_CHECKS
using GrenadeEffectsSmokeFn = std::uintptr_t (*)(void*, void*, void*, int, void*, void*, void*);
using GrenadeEffectsFlashFn = std::uintptr_t (*)(void*, std::uint32_t, const float*);
using GrenadeEffectsOpacityFn = float (*)(void*);
void GrenadeEffectsTestConfigure(std::uintptr_t client, GrenadeEffectsSmokeFn smoke,
    GrenadeEffectsFlashFn flash, GrenadeEffectsOpacityFn opacity) noexcept;
std::uintptr_t GrenadeEffectsTestSmoke(void*, void*, void*, int, void*, void*, void*) noexcept;
std::uintptr_t GrenadeEffectsTestFlash(void*, std::uint32_t, const float*, std::uintptr_t caller) noexcept;
float GrenadeEffectsTestOpacity(void* pawn, std::uintptr_t caller) noexcept;
#endif
