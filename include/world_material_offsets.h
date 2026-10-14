#pragma once
#include <cstdint>
#include <cstddef>

// Locally verified installed binaries, October 14, 2026.
// scenesystem.dll SHA256 bd770af13a0b1352d87de26cbcfef9e36ef809771ae6a2e5314d7bc6b341f831.
// worldrenderer.dll SHA256 e026fdb32408d1ac3062ab15beee7f87cc6072ee0c4c9b0910c423e0549512e9.
// tools/verify_world_materials.py independently resolves the consumers.
namespace world_material_offsets {
inline constexpr std::uint32_t Timestamp = 0x6AC7FF95;
inline constexpr std::uint32_t ImageSize = 0x978000;
inline constexpr std::uint32_t MaterialTimestamp = 0x6ABD5B1E;
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
