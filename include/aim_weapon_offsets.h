#pragma once
#include <cstddef>
#include <cstdint>

// Installed AMD64 client contracts; see tools/CS2_AIM_WEAPON_EVIDENCE.md.
namespace aim_weapon_offsets {
inline constexpr std::ptrdiff_t Spread = 0x80D490;
inline constexpr std::ptrdiff_t Inaccuracy = 0x80C1A0;
inline constexpr std::ptrdiff_t PrimaryCooldown = 0x94DE60;
inline constexpr std::ptrdiff_t CurrentTime = 0x75D250;
inline constexpr std::ptrdiff_t AimPunch = 0x8526B0;
inline constexpr std::ptrdiff_t SpreadSlot = 0xBD0;
inline constexpr std::ptrdiff_t InaccuracySlot = 0xCF8;
inline constexpr std::ptrdiff_t WeaponServices = 0x12F0;
inline constexpr std::ptrdiff_t ActiveWeapon = 0x60;
inline constexpr std::ptrdiff_t ServicePawn = 0x38;
inline constexpr std::ptrdiff_t ServicesVtable = 0x1C14C78;
inline constexpr std::ptrdiff_t Data = 0x388;
inline constexpr std::ptrdiff_t Mode = 0x1A00;
inline constexpr std::ptrdiff_t RecoilIndex = 0x1A28;
inline constexpr std::ptrdiff_t InReload = 0x1A3C;
inline constexpr std::ptrdiff_t BurstMode = 0x1A2C;
inline constexpr std::ptrdiff_t BurstRemaining = 0x1F24;
inline constexpr std::ptrdiff_t Clip = 0x1928;
inline constexpr std::ptrdiff_t NextAttack = 0xD0;
inline constexpr std::ptrdiff_t NumBullets = 0x730;
inline constexpr std::ptrdiff_t FullAuto = 0x72D;
inline constexpr std::ptrdiff_t Range = 0x830;
inline constexpr std::ptrdiff_t IsRevolver = 0x71E;
inline constexpr std::ptrdiff_t PunchServices = 0x1598;
inline constexpr std::ptrdiff_t PunchVtable = 0x1C0A048;
inline constexpr std::ptrdiff_t GameRules = 0x25651D0;
inline constexpr std::ptrdiff_t FreezePeriod = 0x40;
inline constexpr std::ptrdiff_t Defusing = 0x1E9A;
inline constexpr std::ptrdiff_t PlayerState = 0x14E4;
inline constexpr std::ptrdiff_t WaitForNoAttack = 0x1EBC;
inline constexpr std::ptrdiff_t ShotsFired = 0x1EB0;
inline constexpr std::ptrdiff_t Flags = 0x3F4;
inline constexpr std::ptrdiff_t DebugMaxRadius = 0x25BECA8;
inline constexpr std::ptrdiff_t DebugFixedAngle = 0x25BECB8;
inline constexpr std::ptrdiff_t CvarValuePointer = 0x8;
inline constexpr std::ptrdiff_t CvarValue = 0x58;
inline constexpr std::ptrdiff_t IdentityScene = 0x38;
inline constexpr unsigned char SpreadBytes[]{0x48,0x63,0x91,0x00,0x1A,0x00,0x00,0x48,0x8B,0x81,0x88,0x03,0x00,0x00,0x85,0xD2,0x78,0x10,0x48,0x83,0xFA,0x02,0x73,0x0A,0xF3,0x0F,0x10,0x84,0x90,0x50,0x07,0x00,0x00,0xC3,0xF3,0x0F,0x10,0x80,0x50,0x07,0x00,0x00,0xC3};
inline constexpr unsigned char InaccuracyBytes[]{0x48,0x89,0x5C,0x24,0x10,0x55,0x56,0x57,0x48,0x81,0xEC,0xB0,0x00,0x00,0x00,0x44,0x0F,0x29,0x84,0x24,0x80,0x00,0x00,0x00,0x49,0x8B,0xF0,0x48,0x8B,0xEA,0x48,0x8B,0xF9};
inline constexpr unsigned char PrimaryCooldownBytes[]{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x51,0x10,0x48,0x8B,0xD9,0x48,0x8D,0x4C,0x24,0x38,0x8B,0x52,0x38,0xE8,0xB6,0xFD,0x15,0x00};
inline constexpr unsigned char CurrentTimeBytes[]{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x85,0xD2,0x74,0x1A,0x48,0x8B};
inline constexpr unsigned char AimPunchBytes[]{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x68,0x18,0x48,0x89,0x70,0x20,0x57,0x48,0x83,0xEC,0x70,0x48,0x8B,0xEA,0x41,0x0F,0xB6,0xF0};
struct WeaponTable { std::ptrdiff_t rva; const char* name; bool negev; };
inline constexpr WeaponTable Tables[]{
    {0x1BDDC20, "C_AK47", false},
    {0x1BDEB08, "C_WeaponAug", false},
    {0x1BDF998, "C_WeaponAWP", false},
    {0x1BE0828, "C_WeaponBizon", false},
    {0x1BE16B8, "C_WeaponFamas", false},
    {0x1BE2548, "C_WeaponFiveSeven", false},
    {0x1BE33D8, "C_WeaponG3SG1", false},
    {0x1BE4268, "C_WeaponGalilAR", false},
    {0x1BE50F8, "C_WeaponGlock", false},
    {0x1BE5F88, "C_WeaponHKP2000", false},
    {0x1BE6E18, "C_WeaponUSPSilencer", false},
    {0x1BE7CB8, "C_WeaponM4A1", false},
    {0x1BE8B48, "C_WeaponM4A1Silencer", false},
    {0x1BE99E8, "C_WeaponMAC10", false},
    {0x1BEB708, "C_WeaponMP5SD", false},
    {0x1BEC598, "C_WeaponMP7", false},
    {0x1BED428, "C_WeaponMP9", false},
    {0x1BEE2B8, "C_WeaponNegev", true},
    {0x1BEF148, "C_WeaponP250", false},
    {0x1BEFFD8, "C_WeaponP90", false},
    {0x1BF0E68, "C_WeaponSCAR20", false},
    {0x1BF1CF8, "C_WeaponSG556", false},
    {0x1BF2B88, "C_WeaponSSG08", false},
    {0x1BF3A18, "C_WeaponTec9", false},
    {0x1BF48A8, "C_WeaponUMP45", false},
    {0x1BF5738, "C_WeaponM249", false},
    {0x1BFB1A0, "C_WeaponCZ75a", false},
    {0x1BFC090, "C_DEagle", false},
    {0x1BFEA20, "C_WeaponElite", false},
};
}
