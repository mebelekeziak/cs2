#pragma once
#include "grenade_prediction.h"
#include <cstdint>

// All calls and temporary entity ownership belong to the client frame owner.
// No engine pointer escapes GrenadeNativePredict, including on failure.
struct GrenadeNativeApi {
    std::uintptr_t client = 0;
    void* (*create)(int, void*, float) = nullptr;
    void (*simulate)(void*, const EspVec3*, const EspVec3*) = nullptr;
    void (*remove)(void*) = nullptr;
    const EspVec3* (*position)(void*) = nullptr;
    bool (*copy)(std::uintptr_t, void*, std::size_t) noexcept = nullptr;
};
bool GrenadeNativePredict(const GrenadeNativeApi&, std::uintptr_t weapon,
    const GrenadeState&, GrenadePath&);
