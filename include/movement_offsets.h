#pragma once
#include <cstddef>
#include <cstdint>

// Installed client SHA256 5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49.
// Metadata, RTTI, callers and generated protobuf code checked locally; see
// tools/CS2_MOVEMENT_EVIDENCE.md. These are build identities, not match defaults.
namespace movement_offsets {
inline constexpr std::ptrdiff_t Input = 0x257B640;
inline constexpr std::ptrdiff_t InputVtable = 0x1C9DB20;
inline constexpr std::ptrdiff_t Finalize = 0xCECF90;
inline constexpr std::ptrdiff_t ModernJump = 0x8C34F0;
inline constexpr unsigned char ModernJumpBytes[]{
    0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xd1,
    0x48,0x81,0xec,0x88,0x00,0x00,0x00,0x8b,0x9a,0xd4,0x00,0x00,0x00,0x4c,0x8b,0xf2
};
inline constexpr std::ptrdiff_t EffectiveMaxSpeed = 0x8CDEF0;
inline constexpr unsigned char MaxSpeedBytes[]{
    0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0xf9
};
inline constexpr unsigned char FinalizeBytes[]{
    0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,
    0x4c,0x89,0x74,0x24,0x20,0x55,0x48,0x8d,0x6c,0x24,0xe0,0x48,0x81,0xec,0x20,0x01,0x00
};
inline constexpr std::ptrdiff_t AirCap = 0x25325B0;
inline constexpr std::ptrdiff_t AirAccelerate = 0x2564E90;
inline constexpr std::ptrdiff_t GroundAccelerate = 0x2564E80;
inline constexpr std::ptrdiff_t GroundFriction = 0x2564EF0;
inline constexpr std::ptrdiff_t StopSpeed = 0x2564DF0;
inline constexpr std::ptrdiff_t GroundAccelerationSpeedFloor = 0x1BB7194;
inline constexpr std::ptrdiff_t AutoBunnyhop = 0x2532758;
inline constexpr std::ptrdiff_t JumpPenalty = 0x25325A0;
inline constexpr std::ptrdiff_t JumpPrecision = 0x2532640;
inline constexpr std::ptrdiff_t LegacyJump = 0x2532670;
inline constexpr std::ptrdiff_t BhopWindow = 0x25324B0;
inline constexpr std::ptrdiff_t Ingest = 0xA77390;
inline constexpr std::ptrdiff_t AllocateSubtick = 0x4F74A0;
inline constexpr std::ptrdiff_t AddAllocatedSubtick = 0x1233010;
inline constexpr unsigned char AllocateBytes[]{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
inline constexpr unsigned char AddAllocatedBytes[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20};
inline constexpr unsigned char IngestBytes[]{
    0x48,0x89,0x54,0x24,0x10,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x68
};
inline constexpr std::ptrdiff_t TickIntervalConstant = 0x1AC6064;
inline constexpr std::ptrdiff_t StaminaScaleConstant = 0x1AC6098;
inline constexpr std::ptrdiff_t SurfaceManager = 0x254E100;
inline constexpr std::ptrdiff_t CvarValuePointer = 0x8;
inline constexpr std::ptrdiff_t CvarValue = 0x58;
namespace pawn {
inline constexpr std::ptrdiff_t Flags = 0x3F4;
inline constexpr std::ptrdiff_t Velocity = 0x3F8;
inline constexpr std::ptrdiff_t ActualMoveType = 0x526;
inline constexpr std::ptrdiff_t WaterLevel = 0x528;
inline constexpr std::ptrdiff_t MovementServices = 0x1330;
inline constexpr std::ptrdiff_t GroundFrictionScale = 0x538;
}
namespace controller {
inline constexpr std::ptrdiff_t TickBase = 0x6B8;
}
namespace services {
inline constexpr std::ptrdiff_t Pawn = 0x38;
inline constexpr std::ptrdiff_t ModernJumpState = 0x6C8;
inline constexpr std::ptrdiff_t MaxSpeed = 0x1AC;
inline constexpr std::ptrdiff_t Friction = 0x26C;
inline constexpr std::ptrdiff_t SurfaceData = 0x278;
inline constexpr std::ptrdiff_t Stamina = 0x694;
inline constexpr std::ptrdiff_t Buttons = 0x58;
inline constexpr std::ptrdiff_t CommandForward = 0x1A0;
inline constexpr std::ptrdiff_t CommandLeft = 0x1A4;
inline constexpr std::ptrdiff_t ModernActual = 0x6D8;
inline constexpr std::ptrdiff_t ModernUsable = 0x6E0;
inline constexpr std::ptrdiff_t ModernLanded = 0x6E8;
}
namespace move_data {
inline constexpr std::ptrdiff_t Velocity = 0x38;
inline constexpr std::ptrdiff_t Tick = 0xD4;
inline constexpr std::ptrdiff_t Fraction = 0xDC;
}
inline constexpr std::uint32_t OnGround = 1, WaterJump = 4, Frozen = 32;
inline constexpr std::uint8_t Walk = 2;
inline constexpr std::uint64_t Jump = 2;
inline constexpr std::uint64_t Forward = 0x8, Back = 0x10, Left = 0x200, Right = 0x400;
inline constexpr std::uint64_t Directions = Forward | Back | Left | Right;
namespace command {
inline constexpr std::ptrdiff_t Sequence = 0x8;
inline constexpr std::ptrdiff_t Presence = 0x20;
inline constexpr std::ptrdiff_t Base = 0x40;
inline constexpr std::ptrdiff_t Buttons = 0x60;
}
namespace base {
inline constexpr std::ptrdiff_t Presence = 0x10;
inline constexpr std::ptrdiff_t SubtickSize = 0x20;
inline constexpr std::ptrdiff_t SubtickCapacity = 0x24;
inline constexpr std::ptrdiff_t SubtickRep = 0x28;
inline constexpr std::ptrdiff_t Angles = 0x40;
inline constexpr std::ptrdiff_t Tick = 0x54;
inline constexpr std::ptrdiff_t Forward = 0x58;
inline constexpr std::ptrdiff_t Left = 0x5C;
inline constexpr std::uint32_t ForwardPresent = 0x40, LeftPresent = 0x80;
}
}
