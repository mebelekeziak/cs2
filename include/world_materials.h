#pragma once
#include "world_settings.h"
#include <cstdint>
#include <cstddef>

using WorldMaterialsLogCallback = void (*)(const char*);
bool WorldMaterialsPrepare(WorldMaterialsLogCallback callback) noexcept;
void WorldMaterialsActivate() noexcept;
void WorldMaterialsStop(const char* reason) noexcept;
const char* WorldMaterialsStatus() noexcept;
void WorldMaterialsFlushTelemetry() noexcept;

// Installed DrawListVector_t head. Native append may grow overflow storage.
struct WorldDrawList {
    std::uintptr_t fixed = 0;
    std::int32_t fixedCapacity = 0, fixedCount = 0, overflowCount = 0;
    std::int32_t padding = 0;
    std::uintptr_t overflow = 0;
    std::int32_t overflowCapacity = 0;
    std::uint32_t overflowFlags = 0;
};
static_assert(sizeof(WorldDrawList) == 0x28);

struct WorldFlatMaterial {
    std::uintptr_t material = 0, mode = 0;
    std::uintptr_t type1Stream = 0, type2Stream = 0;
};

// Operates only on new engine-owned packets for this callback's object.
// Exposed for standalone layout/ownership checks; never retains packet pointers.
bool WorldMaterialsApplyNewDraws(const WorldDrawList& before, const WorldDrawList& after,
    std::uintptr_t object, const WorldSettings& settings, const WorldFlatMaterial& flat,
    std::size_t& changed) noexcept;
#ifdef CS2_WORLD_MATERIAL_CHECKS
using WorldMaterialsTestGenerateFn = void (*)(void*, void*, void*, void*);
void WorldMaterialsTestConfigure(std::uintptr_t sceneBase, WorldMaterialsTestGenerateFn fn) noexcept;
void WorldMaterialsTestGenerate(void* desc, void* object, void* args, void* list) noexcept;
#endif
