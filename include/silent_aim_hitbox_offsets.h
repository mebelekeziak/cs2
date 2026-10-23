#pragma once

#include <cstddef>

// Installed 1.41.9.2 / 2000935 AMD64 client, recertified October 23, 2026.
// Dynamic named animated bone landmarks.
// No fixed bone indices or authored hitbox-volume centers are assumed.
// tools/inspect_silent_aim_hitboxes.py checks all constants and fingerprints.
namespace silent_aim_hitbox_offsets {
inline constexpr std::ptrdiff_t LookupBone = 0x953770;
inline constexpr std::ptrdiff_t SkeletonVtable = 0x1C43C10;
// NetworkVar_m_skeletonInstance@CBodyComponentSkeletonInstance: verified
// zero-offset CSkeletonInstance base, observed on historical live player pawns.
inline constexpr std::ptrdiff_t PlayerSkeletonVtable = 0x1ACFE58;
inline constexpr std::ptrdiff_t SkeletonQuerySlot = 0x68;
inline constexpr std::ptrdiff_t SkeletonQuery = 0x1C9E50;
inline constexpr std::ptrdiff_t ModelState = 0x140;
inline constexpr std::ptrdiff_t ModelHandle = 0xA0;
inline constexpr std::ptrdiff_t BoneArray = 0x1C0;
inline constexpr std::ptrdiff_t BoneCount = 0x178;
inline constexpr std::ptrdiff_t BoneStride = 0x20;
inline constexpr std::ptrdiff_t Position = 0;
inline constexpr char HeadName[] = "head_0";
inline constexpr char ChestName[] = "spine_3";
inline constexpr char StomachName[] = "spine_0";
inline constexpr char PelvisName[] = "pelvis";
inline constexpr char LowerChestName[] = "spine_1";
inline constexpr char MiddleChestName[] = "spine_2";
inline constexpr char LeftUpperArmName[] = "arm_upper_l";
inline constexpr char RightUpperArmName[] = "arm_upper_r";
inline constexpr char LeftForearmName[] = "arm_lower_l";
inline constexpr char RightForearmName[] = "arm_lower_r";
inline constexpr char LeftHandName[] = "hand_l";
inline constexpr char RightHandName[] = "hand_r";
inline constexpr char LeftThighName[] = "leg_upper_l";
inline constexpr char RightThighName[] = "leg_upper_r";
inline constexpr char LeftCalfName[] = "leg_lower_l";
inline constexpr char RightCalfName[] = "leg_lower_r";
inline constexpr char LeftFootName[] = "ankle_l";
inline constexpr char RightFootName[] = "ankle_r";
inline constexpr unsigned char LookupBoneBytes[]{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x89,0x30,0x03,0x00,0x00,0x48,0x8B,0xDA,0x48,0x8B,0x01,0xFF,0x50,0x68,0x48,0x8B,0xC8,0x48,0x8B,0xD3,0x48,0x83,0xC4,0x20,0x5B,0xE9,0xDA,0x8A,0x16,0x00};
inline constexpr unsigned char SkeletonQueryBytes[]{0x48,0x8B,0xC1,0xC3};
}
