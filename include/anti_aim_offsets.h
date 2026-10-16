#pragma once
#include <cstddef>
#include <cstdint>

// Installed AMD64 client; independently verified by tools/inspect_anti_aim.py.
// See tools/CS2_ANTI_AIM_EVIDENCE.md for camera reset ownership and ABI.
namespace anti_aim_offsets {
inline constexpr std::ptrdiff_t Input = 0x257B640;
inline constexpr std::ptrdiff_t InputVtable = 0x1C9DB20;
inline constexpr std::ptrdiff_t Replay = 0xCEA280;
inline constexpr std::ptrdiff_t ReplaySlot = 8;
inline constexpr std::ptrdiff_t InputAngles = 0x688;
inline constexpr unsigned char ReplayBytes[]{
    0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0xf2,0x26,0xe8,0xff,
    0x33,0xc0,0xc6,0x83,0xbd,0x0b,0x00,0x00,0x00
};
inline constexpr std::uint64_t Attack = 1, Attack2 = 0x800, Use = 0x20;
namespace base {
inline constexpr std::ptrdiff_t Up = 0x60;
inline constexpr std::uint32_t UpPresent = 0x100;
}
}
