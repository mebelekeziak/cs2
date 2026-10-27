#pragma once
#include <cstddef>
#include <cstdint>

// Installed AMD64 client 2000935 / Steam 26144732, October 27, 2026.
// Read-only schema, RTTI and native operand verification: tools/inspect_esp_items.py.
namespace esp_item_offsets {
inline constexpr std::ptrdiff_t WeaponServices = 0x12F0;
inline constexpr std::ptrdiff_t ActiveWeapon = 0x60;
inline constexpr std::ptrdiff_t Item = 0x12E0;
inline constexpr std::ptrdiff_t DefinitionIndex = 0x1BA;
inline constexpr std::ptrdiff_t OwnerHandle = 0x520;
inline constexpr std::ptrdiff_t C4Vtable = 0x1BDD1D0;
inline constexpr std::ptrdiff_t PlantedC4Vtable = 0x1C9A9B0;
inline constexpr std::ptrdiff_t C4BombPlanted = 0x1F5B;
inline constexpr std::ptrdiff_t BombTicking = 0x1288;
inline constexpr std::ptrdiff_t BombExploded = 0x12BD;
inline constexpr std::ptrdiff_t BombDefused = 0x12DC;
}
