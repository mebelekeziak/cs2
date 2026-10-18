#pragma once
#include <cstddef>
#include <cstdint>

// Verified installed AMD64 client. See tools/CS2_AUTOFIRE_EVIDENCE.md.
namespace aim_autofire_offsets {
inline constexpr std::uint32_t BaseAnglesPresent = 0x4;
inline constexpr std::ptrdiff_t RecordHistory = 0xD055C0;
inline constexpr unsigned char RecordHistoryBytes[]{
    0x40,0x53,0x57,0x48,0x81,0xEC,0xC8,0x00,0x00,0x00,0x80,0xB9,0xBD,0x0B,0x00,0x00,0x00,
    0x8B,0xDA,0x48,0x8B,0xF9
};
inline constexpr std::ptrdiff_t InputEventCount = 0x284;
inline constexpr std::ptrdiff_t InputNativeButtons = 0x258;
inline constexpr std::ptrdiff_t InputEvents = 0x288;
inline constexpr std::ptrdiff_t InputEventStride = 0x20;
inline constexpr int InputEventCapacity = 32;
inline constexpr std::ptrdiff_t EventButton = 0x8;
inline constexpr std::ptrdiff_t EventPressed = 0x10;
inline constexpr std::ptrdiff_t InputPrimaryHeld = 0xBC8;
inline constexpr std::ptrdiff_t InputPrimaryIndex = 0xBCC;
inline constexpr std::ptrdiff_t InputHistoryCount = 0xBD8;
inline constexpr std::ptrdiff_t InputEventsProcessed = 0xBF0;
inline constexpr std::uint64_t PrimaryAttack = 1;
}
