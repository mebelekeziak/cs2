#pragma once
#include <cstddef>

// Installed AMD64 client 2000935, recertified October 23, 2026.
// Client and animationsystem schema; see inspect_aim_geometry.py.
// These are read-only resource layouts, not cached player bone indices/radii.
namespace aim_geometry_offsets {
inline constexpr std::ptrdiff_t HitboxSet = 0x3FC;
inline constexpr std::ptrdiff_t MeshGroup = 0x348;
inline constexpr std::ptrdiff_t MeshCount = 0x70;
inline constexpr std::ptrdiff_t MeshArray = 0x78;
inline constexpr std::ptrdiff_t MeshMasks = 0x90;
inline constexpr std::ptrdiff_t SetCount = 0x174;
inline constexpr std::ptrdiff_t SetStorage = 0x164;
inline constexpr std::ptrdiff_t SetArray = 0x168;
inline constexpr std::ptrdiff_t SetStride = 0x48;
inline constexpr std::ptrdiff_t SetValue = 0x18;
inline constexpr std::ptrdiff_t SetName = 0;
inline constexpr std::ptrdiff_t BoxCount = 0x10;
inline constexpr std::ptrdiff_t BoxArray = 0x18;
inline constexpr std::ptrdiff_t BoxStride = 0x70;
inline constexpr std::ptrdiff_t BoneName = 0x10;
inline constexpr std::ptrdiff_t Minimum = 0x18;
inline constexpr std::ptrdiff_t Maximum = 0x24;
inline constexpr std::ptrdiff_t Radius = 0x30;
inline constexpr std::ptrdiff_t Shape = 0x3C;
inline constexpr std::ptrdiff_t TranslationOnly = 0x3D;
inline constexpr std::ptrdiff_t AlternateTransform = 0x4A;
inline constexpr unsigned CapsuleShape = 2;
}
