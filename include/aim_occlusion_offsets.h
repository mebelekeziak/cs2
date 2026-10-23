#pragma once
#include <cstddef>
#include <cstdint>

// Installed AMD64 client; tools/inspect_aim_occlusion.py independently checks
// the native BulletServices filter producer and result consumers.
// Reverified for client 2000935 / Steam build 26144732, October 23, 2026.
namespace aim_occlusion_offsets {
inline constexpr std::ptrdiff_t InitializeFilter = 0x352cc0;
inline constexpr std::uint64_t ShotMask = 0x1C300B;
inline constexpr std::uint64_t ShotExclude = 0x4000000000;
inline constexpr std::uint8_t ShotLayer = 3;
inline constexpr std::uint8_t ShotFlags = 0x0F;
inline constexpr std::uint8_t ShotFlags39 = 0x02;
inline constexpr std::ptrdiff_t TraceEntity = 0x08;
inline constexpr std::ptrdiff_t TraceHitFlag = 0xBB;
inline constexpr unsigned char InitializeFilterBytes[]{
    0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,
    0x83,0xec,0x20,0x0f,0xb6,0x41,0x39,0x33,0xff,0x24,0xc9,0xc7,
    0x41,0x34,0xff,0xff,0x00,0x0f
};
}
