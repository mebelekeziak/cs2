#pragma once

#include <cstddef>

// Installed 1.41.9.1 AMD64 client; dynamic named animated bone landmarks.
// No fixed bone indices or authored hitbox-volume centers are assumed.
// tools/inspect_silent_aim_hitboxes.py checks all constants and fingerprints.
namespace silent_aim_hitbox_offsets {
inline constexpr std::ptrdiff_t LookupBone = 0x952940;
inline constexpr std::ptrdiff_t SkeletonVtable = 0x1C41CE0;
// NetworkVar_m_skeletonInstance@CBodyComponentSkeletonInstance: verified
// zero-offset CSkeletonInstance base, observed on both live player pawns.
inline constexpr std::ptrdiff_t PlayerSkeletonVtable = 0x1ACDE48;
inline constexpr std::ptrdiff_t SkeletonQuerySlot = 0x68;
inline constexpr std::ptrdiff_t SkeletonQuery = 0x1C9E70;
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
inline constexpr unsigned char LookupBoneBytes[]{
    0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x89,0x30,0x03,0x00,0x00,
    0x48,0x8b,0xda,0x48,0x8b,0x01,0xff,0x50,0x68,0x48,0x8b,0xc8,0x48,
    0x8b,0xd3,0x48,0x83,0xc4,0x20,0x5b,0xe9,0xca,0x8a,0x16,0x00
};
inline constexpr unsigned char SkeletonQueryBytes[]{0x48,0x8b,0xc1,0xc3};
}
