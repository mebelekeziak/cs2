#pragma once
#include <cstddef>
#include <cstdint>

// Installed 1.41.9.2 client; independently verified by inspect_silent_aim.py.
// Recertified October 23, 2026 for client 2000935 / Steam build 26144732.
// See tools/CS2_SILENT_AIM_EVIDENCE.md for producer, consumer and timing.
namespace silent_aim_offsets {
inline constexpr std::ptrdiff_t Input = 0x257D780;
inline constexpr std::ptrdiff_t InputVtable = 0x1C9FA58;
inline constexpr std::ptrdiff_t HistoryBuilder = 0xD03790;
inline constexpr std::ptrdiff_t HistorySlot = 25;
inline constexpr unsigned char HistoryBuilderBytes[]{
    0x48,0x8b,0xc4,0x4c,0x89,0x40,0x18,0x48,0x89,0x48,0x08,0x55,0x53,0x57,0x41,0x55,
    0x48,0x8d,0xa8,0x08,0xff,0xff,0xff,0x48,0x81,0xec,0xd8,0x01,0x00,0x00
};
namespace command {
inline constexpr std::ptrdiff_t Presence = 0x20;
inline constexpr std::ptrdiff_t HistorySize = 0x30;
inline constexpr std::ptrdiff_t HistoryCapacity = 0x34;
inline constexpr std::ptrdiff_t HistoryRep = 0x38;
inline constexpr std::ptrdiff_t Attack1Index = 0x4C;
inline constexpr std::uint32_t Attack1Present = 0x20;
}
namespace history {
inline constexpr std::ptrdiff_t Presence = 0x10;
inline constexpr std::ptrdiff_t Angles = 0x18;
inline constexpr std::uint32_t AnglesPresent = 0x1;
}
namespace angle {
inline constexpr std::ptrdiff_t Presence = 0x10;
inline constexpr std::ptrdiff_t Pitch = 0x18;
inline constexpr std::ptrdiff_t Yaw = 0x1C;
inline constexpr std::ptrdiff_t Roll = 0x20;
inline constexpr std::uint32_t AllPresent = 0x7;
}
namespace repeated {
inline constexpr std::ptrdiff_t Allocated = 0;
inline constexpr std::ptrdiff_t Entries = 0x8;
}
}
