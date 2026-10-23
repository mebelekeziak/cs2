#pragma once
#include <cstddef>
#include <cstdint>

// Installed AMD64 client; tools/inspect_shot_events.py independently verifies
// RTTI, native registration/dispatch/getters and generated pawn-handle fields.
namespace shot_events_offsets {
inline constexpr std::ptrdiff_t Manager = 0x254DC00;
inline constexpr std::ptrdiff_t ManagerVtable = 0x1C337A8;
inline constexpr std::ptrdiff_t EventVtable = 0x1C334F0;
inline constexpr std::ptrdiff_t AddListener = 0x9CAB60;
inline constexpr std::ptrdiff_t FindListener = 0x9E2A20;
inline constexpr std::ptrdiff_t FireClient = 0x9E3790;
inline constexpr std::ptrdiff_t Name = 0x9E8C80;
inline constexpr std::ptrdiff_t Empty = 0x9F25A0;
inline constexpr std::ptrdiff_t Boolean = 0x9E4C90;
inline constexpr std::ptrdiff_t Integer = 0x9E7F50;
inline constexpr std::ptrdiff_t Float = 0x9E7B70;
inline constexpr std::ptrdiff_t String = 0x9EA380;
inline constexpr std::ptrdiff_t Handle = 0x9E5450;
inline constexpr std::ptrdiff_t AddListenerSlot = 0x18;
inline constexpr std::ptrdiff_t FindListenerSlot = 0x20;
inline constexpr std::ptrdiff_t FireClientSlot = 0x40;
inline constexpr std::ptrdiff_t NameSlot = 0x8;
inline constexpr std::ptrdiff_t EmptySlot = 0x28;
inline constexpr std::ptrdiff_t BooleanSlot = 0x30;
inline constexpr std::ptrdiff_t IntegerSlot = 0x38;
inline constexpr std::ptrdiff_t FloatSlot = 0x48;
inline constexpr std::ptrdiff_t StringSlot = 0x50;
inline constexpr std::ptrdiff_t HandleSlot = 0x60;
inline constexpr unsigned char AddListenerBytes[]{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83};
inline constexpr unsigned char FindListenerBytes[]{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57};
inline constexpr unsigned char FireClientBytes[]{0x40,0x53,0x56,0x41,0x54,0x48,0x83,0xEC,0x30,0x48,0x8B,0xF2,0x48,0x8D,0x99,0xE0};
}
