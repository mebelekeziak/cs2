#pragma once
#include "aim_occlusion.h"
struct AimWeaponSnapshot;
struct AimAutowallContext { std::uintptr_t client = 0; bool prepared = false; };
// Synchronous native command owner only; all trace storage is destroyed before return.
bool AimAutowallPrepare(std::uintptr_t client, AimAutowallContext& context) noexcept;
AimOcclusionResult AimAutowallCheck(const AimAutowallContext& context, const AimSource& source,
    const AimTarget& target, const AimTargetVolume& volume, const AimWeaponSnapshot& weapon) noexcept;
#ifdef CS2_AIM_AUTOWALL_CHECKS
struct AimAutowallTestCalls {
    void* (*initialize)(void*);
    void (*destroy)(void*);
    void* (*filter)(void*, void*, std::uint64_t, std::uint8_t, std::uint8_t);
    void (*trace)(void*, const void*, const void*, void*, int, bool);
    bool (*step)(void*, void*, void*, int, void*);
    void (*extract)(void*, void*, float, void*);
};
void AimAutowallTestConfigure(std::uintptr_t client, AimAutowallTestCalls calls) noexcept;
#endif
