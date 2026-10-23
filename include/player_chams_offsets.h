#pragma once
#include <cstddef>
#include <cstdint>

// Installed AMD64 client 1.41.9.2. Reproduce with verify_player_chams.py.
namespace player_chams_offsets {
inline constexpr std::uint32_t SceneTimestamp = 0x6ADA337D;
inline constexpr std::uint32_t SceneImageSize = 0x978000;
inline constexpr std::uintptr_t Generate = 0x7D7C0;
inline constexpr std::uintptr_t DescriptorVtable = 0x5E5C90;
inline constexpr std::size_t OwnerHandle = 0xC0;
inline constexpr std::size_t SkeletonInterface = 0x110;
inline constexpr std::size_t SkeletonInterfaceMember = 0x130;
inline constexpr std::size_t PacketSize = 0x70;
inline constexpr std::size_t PacketObject = 0x18;
inline constexpr std::size_t PacketMaterial = 0x20;
inline constexpr std::size_t PacketMode = 0x28;
inline constexpr std::size_t PacketColor = 0x50;
inline constexpr std::size_t ViewInArgs = 0x10;
inline constexpr std::size_t ViewMode = 0x30;
inline constexpr unsigned char GenerateBytes[]{
    0x48,0x8b,0xc4,0x48,0x89,0x58,0x20,0x4c,0x89,0x40,0x18,0x48,0x89,0x50,0x10,0x48,0x89,0x48,0x08
};
}
