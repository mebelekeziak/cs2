#pragma once
#include <cstddef>
#include <cstdint>

// Installed Windows AMD64 CS2 client 2000935; see tools/CS2_GRENADE_PREDICTION_EVIDENCE.md.
namespace grenade_prediction_offsets {
inline constexpr std::uint32_t ClientTimestamp = 0x6ADA7340;
inline constexpr std::uint32_t ClientImageSize = 0x29A0000;
inline constexpr std::ptrdiff_t IdentityWorldGroup = 0x38;
inline constexpr std::ptrdiff_t GetAbsVelocity = 0x219AC0;
inline constexpr std::ptrdiff_t Launch = 0x7c7940;
inline constexpr std::ptrdiff_t Held = 0x1f22;
inline constexpr std::ptrdiff_t PinPulled = 0x1f23;
inline constexpr std::ptrdiff_t ThrowTime = 0x1f28;
inline constexpr std::ptrdiff_t ThrowStrength = 0x1f30;
inline constexpr std::ptrdiff_t ProjectileSpawnTime = 0x12e8;
inline constexpr std::ptrdiff_t Owner = 0x520;
inline constexpr std::ptrdiff_t Thrower = 0x1290;
inline constexpr std::ptrdiff_t OriginalThrower = 0x12ac;
inline constexpr std::ptrdiff_t DetonateTime = 0x1270;
inline constexpr std::ptrdiff_t ExplodeTick = 0x12d8;
inline constexpr std::ptrdiff_t ExplodeBegan = 0x12fc;
inline constexpr std::ptrdiff_t AbsVelocity = 0x3f8;
inline constexpr std::ptrdiff_t GravityScale = 0x540;
inline constexpr std::ptrdiff_t ActualGravityScale = 0x564;
inline constexpr std::ptrdiff_t Elasticity = 0x53c;
inline constexpr std::ptrdiff_t IncendiaryFlag = 0x1348;
inline constexpr std::ptrdiff_t SmokeTickBegin = 0x1360;
inline constexpr std::ptrdiff_t SmokeDidEffect = 0x1364;
inline constexpr std::ptrdiff_t SmokeEffectSpawned = 0x13AA;
inline constexpr std::ptrdiff_t ProjectileLive = 0x126a;
inline constexpr std::ptrdiff_t Bounces = 0x12c8;
inline constexpr std::ptrdiff_t InitialPosition = 0x12b0;
inline constexpr std::ptrdiff_t InitialVelocity = 0x12bc;
inline constexpr float HullExtent = 2.0f;
inline constexpr float LaunchHullExtent = 2.02f;
inline constexpr std::uint64_t LaunchMask = 0x200003001;
inline constexpr std::uint8_t LaunchLayer = 4;
inline constexpr std::uint64_t GrenadeMask = 0x200003001;
inline constexpr std::uint64_t GrenadeInteractsAs = 0x8000020000;
inline constexpr std::uint8_t GrenadeLayer = 16;
inline constexpr std::uint8_t GrenadeFlags39 = 0x4B;
inline constexpr float InitialGravityScale = 0.4f;
inline constexpr float InitialElasticity = 0.45f;
inline constexpr float MaximumElasticity = 0.9f;
inline constexpr float Overbounce = 2.0f;
inline constexpr float ClipEpsilon = 0.03125f;
inline constexpr float GroundNormalZ = 0.7f;
inline constexpr float SoftGroundNormalZ = 0.1f;
inline constexpr float StopSpeed = 20.0f;
inline constexpr float FastFloorSpeedSquared = 96000.0f;
inline constexpr float FastFloorDotThreshold = 0.5f;
inline constexpr float HighExplosiveFuse = 1.5f;
inline constexpr float FlashFuse = 1.5f;
inline constexpr float SmokeMinimumAge = 1.188f;
inline constexpr float SmokeRestSpeed = 0.1f;
inline constexpr float DecoyFirstThink = 2.0f;
inline constexpr float DecoyThinkInterval = 0.2f;
inline constexpr float DecoyRestSpeed = 0.2f;
inline constexpr std::ptrdiff_t MolotovFuse = 0x24C9180;
inline constexpr std::ptrdiff_t MolotovMaxSlope = 0x24C9190;
// Launch returns whether stashed parameters were used; false still fills outputs.
// bool Launch(void* weapon, void* pawn, float* position, float* velocity, bool currentAngles).
inline constexpr unsigned char LaunchBytes[]{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x8D,0x6C,0x24,0xC1,0x48,0x81,0xEC,0xE0,0x00,0x00,0x00,0x49,0x8B,0xF1,0x4D,0x8B,0xF0};
// EspVec3* GetAbsVelocity(void* entity, EspVec3* output); frame owner only.
inline constexpr unsigned char GetAbsVelocityBytes[]{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x30,0x80,0xB9,0x26,0x05,0x00,0x00,0x05,0x48,0x8B,0xFA,0x48,0x8B,0xD9,0x0F,0x85,0x88,0x00,0x00,0x00,0x48,0x8B,0x49};
struct KindTable { std::ptrdiff_t rva; unsigned kind; };
inline constexpr KindTable WeaponTables[]{
    {0x1C02758,0}, {0x1C018D0,1}, {0x1C064F0,2},
    {0x1BFEE88,3}, {0x1C046E8,4}, {0x1C05560,5},
};
inline constexpr KindTable ProjectileTables[]{
    {0x1D0E4A0,0}, {0x1D0D048,1}, {0x1BD4120,2},
    {0x1BD11F8,3}, {0x1BD3720,4},
};
}
