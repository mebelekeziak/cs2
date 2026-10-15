#pragma once
#include <cstdint>
#include <cstddef>

// Locally reverified installed binaries, October 15, 2026; client 1.41.9.1.
// scenesystem.dll SHA256 040eb851fa7875523346b890f1ed6e158abc59e08bccbd8dc60539435277efa5.
// worldrenderer.dll SHA256 f9eeff50acb10a89ab78c98d2726d3e5e6b5adf7a8b11be2a41cb5ebded11dac.
// materialsystem2.dll SHA256 b4d5c667eb65f13d9193d743a88eb1f9dfa623cc8f745cef764d9977969e38b6.
// tools/verify_world_materials.py independently resolves the consumers.
namespace world_material_offsets {
inline constexpr std::uint32_t Timestamp = 0x6ACFCD4F;
inline constexpr std::uint32_t ImageSize = 0x978000;
inline constexpr std::uint32_t MaterialTimestamp = 0x6ACFCC60;
inline constexpr std::uint32_t MaterialImageSize = 0x186000;
inline constexpr std::uintptr_t AggregateCallback = 0x41F30;
inline constexpr std::uintptr_t AggregateVtable = 0x5E4538;
inline constexpr std::uintptr_t FlatBinding = 0x9217D8;
inline constexpr std::uintptr_t Type1Stream = 0x921390;
inline constexpr std::uintptr_t Type2Stream = 0x9213A8;
inline constexpr std::size_t PacketSize = 0x70;
inline constexpr std::size_t Material = 0x20, Mode = 0x28, Color = 0x50;
inline constexpr std::size_t ViewInArgs = 0x10, ViewFlags = 0x48, ModeInVtable = 0x30;
inline constexpr unsigned char CallbackBytes[]{
    0x48,0x89,0x6c,0x24,0x20,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x30};
}
