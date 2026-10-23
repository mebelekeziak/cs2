#pragma once
#include <cstdint>
#include <cstddef>

// Locally reverified installed binaries, October 23, 2026; client 1.41.9.2.
// scenesystem.dll SHA256 0f17ccdc083290a496e6ac13e4fe54413077ad2b83b7dfd281749cffd2733d9b.
// worldrenderer.dll SHA256 e0b1ed04a652f8268fd9d6b75b5a97910ab57197e2cdb8a0a4966703a5ec8322.
// materialsystem2.dll SHA256 2fe9750177006c24aacd0f86b14efbc00d04ca61d86f27f1b07b263eefb06d69.
// tools/verify_world_materials.py independently resolves the consumers.
namespace world_material_offsets {
inline constexpr std::uint32_t Timestamp = 0x6ADA337D;
inline constexpr std::uint32_t ImageSize = 0x978000;
inline constexpr std::uint32_t MaterialTimestamp = 0x6ADA32D5;
inline constexpr std::uint32_t MaterialImageSize = 0x186000;
inline constexpr std::uintptr_t AggregateCallback = 0x41F30;
inline constexpr std::uintptr_t AggregateVtable = 0x5E4518;
inline constexpr std::uintptr_t FlatBinding = 0x9217D8;
inline constexpr std::uintptr_t Type1Stream = 0x921390;
inline constexpr std::uintptr_t Type2Stream = 0x9213A8;
inline constexpr std::size_t PacketSize = 0x70;
inline constexpr std::size_t Material = 0x20, Mode = 0x28, Color = 0x50;
inline constexpr std::size_t ViewInArgs = 0x10, ViewFlags = 0x48, ModeInVtable = 0x30;
inline constexpr unsigned char CallbackBytes[]{
    0x48,0x89,0x6c,0x24,0x20,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x30};
}
