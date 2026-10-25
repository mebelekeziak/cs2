#pragma once
#include <cstddef>
#include <cstdint>

// Installed Windows AMD64 build 26144732. Native sky resource/draw evidence:
// tools/CS2_SKYBOX_EVIDENCE.md and tools/verify_skybox.py.
namespace world_skybox_offsets {
inline constexpr std::uint32_t FilesystemTimestamp = 0x6ADA32A6;
inline constexpr std::uint32_t FilesystemImageSize = 0x22C000;
inline constexpr std::uintptr_t FilesystemVtable = 0x1B0CA0;
inline constexpr std::uintptr_t AddSearchPath = 0x53BF0;
inline constexpr std::size_t AddSearchPathSlot = 0xF8;
inline constexpr std::size_t SceneBytes = 0x110;
inline constexpr std::size_t SceneMaterial = 0xD0;
inline constexpr std::size_t BindingReferences = 0x20;
inline constexpr std::uint64_t MaterialResourceType = 0x74616D76; // vmat
inline constexpr std::uint64_t TextureResourceType = 0x78657476; // vtex
inline constexpr unsigned char AddSearchPathBytes[]{
    0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,
    0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x50
};
}
