#pragma once
#include <cstddef>
#include <cstdint>

// Installed AMD64 client.dll, recertified October 23, 2026 (client 2000935).
// tools/inspect_grenade_areas.py and tools/CS2_GRENADE_AREAS_EVIDENCE.md.
namespace grenade_area_offsets {
inline constexpr std::uint32_t ClientTimestamp = 0x6ADA7340;
inline constexpr std::uint32_t ClientImageSize = 0x29A0000;
inline constexpr std::ptrdiff_t InfernoVtable = 0x1CAE650;
inline constexpr std::ptrdiff_t SmokeVtable = 0x1BD4120;
inline constexpr std::ptrdiff_t FirePositions = 0x1108;
inline constexpr std::ptrdiff_t FireBurning = 0x1708;
inline constexpr std::ptrdiff_t FireCount = 0x1A48;
inline constexpr std::ptrdiff_t FirePostEffect = 0x1A54;
inline constexpr std::ptrdiff_t FireHalfWidth = 0x866C;
inline constexpr unsigned MaxFires = 64;
inline constexpr std::ptrdiff_t SmokeTickBegin = 0x1360;
inline constexpr std::ptrdiff_t SmokeDidEffect = 0x1364;
inline constexpr std::ptrdiff_t SmokeDetonation = 0x1378;
inline constexpr std::ptrdiff_t SmokeEffectSpawned = 0x13AA;
inline constexpr std::ptrdiff_t SmokeSimulation = 0x13B8;
inline constexpr std::ptrdiff_t SmokeVoxelOwner = 0x70;
inline constexpr std::ptrdiff_t SmokeBuffer = 0x100;
inline constexpr std::ptrdiff_t SmokeOrigin = 0xE8;
inline constexpr std::ptrdiff_t SmokeOccupancy = 0x1008;
inline constexpr std::ptrdiff_t SmokeDensity = 0x3008;
inline constexpr unsigned SmokeOccupancyBytes = 0x1000;
inline constexpr unsigned SmokeDensityBufferBytes = 0x80000;
inline constexpr unsigned SmokeDensityStride = 16;
inline constexpr unsigned SmokeGridSize = 32;
inline constexpr unsigned SmokeVoxelCount = 32768;
inline constexpr float SmokeCellSize = 20.0f;
inline constexpr float SmokeGridCenter = 16.0f;
inline constexpr float SmokeCellCenter = 10.0f;
}
