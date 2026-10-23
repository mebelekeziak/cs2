#pragma once
#include <cstddef>
#include <cstdint>
// Installed AMD64 scenesystem.dll fingerprint and operands verified from disk.
// CS2 1.41.9.2, recertified October 23, 2026.
// SHA256 0f17ccdc083290a496e6ac13e4fe54413077ad2b83b7dfd281749cffd2733d9b.
namespace world_lighting_offsets {
inline constexpr std::uint32_t Timestamp = 0x6ADA337D, ImageSize = 0x978000;
inline constexpr std::uintptr_t PackLocalLight = 0x1B14B0;
inline constexpr std::uintptr_t PackThunk = 0x1B14A0;
inline constexpr std::uintptr_t PackReturn = 0x1B2537;
inline constexpr std::uintptr_t BinnerVtable = 0x5FE678;
inline constexpr std::size_t PackSlot = 0x70;
inline constexpr std::size_t GpuStride = 0x1E0, GpuRadiance = 0xA0;
inline constexpr std::uintptr_t OutputStrideInstruction = 0x1B2519;
inline constexpr std::uintptr_t RadianceStore = 0x1B17BB;
inline constexpr unsigned char PackBytes[]{
    0x48,0x8b,0xc4,0x55,0x53,0x48,0x8d,0xa8,0xe8,0xfd,0xff,0xff,
    0x48,0x81,0xec,0x08,0x03,0x00,0x00};
inline constexpr unsigned char ThunkBytes[]{0xe9,0x0b,0x00,0x00,0x00};
inline constexpr unsigned char CallBytes[]{0x41,0xff,0x51,0x70};
inline constexpr unsigned char StrideBytes[]{0x4c,0x69,0xc0,0xe0,0x01,0x00,0x00};
inline constexpr unsigned char RadianceBytes[]{
    0xf2,0x0f,0x11,0x9b,0xa0,0x00,0x00,0x00,
    0xf3,0x0f,0x11,0x93,0xa8,0x00,0x00,0x00};
}
