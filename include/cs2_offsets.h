#pragma once
#include <cstddef>
#include <cstdint>

// Verified against installed CS2 1.41.9.2 / client 2000935, October 23, 2026.
// Static schema templates and decoded machine operands; no new live dump.
// client.dll SHA256: ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5
// Frozen read-only verifier: tools/inspect_cs2.py; tools/CS2_ENTITY_EVIDENCE.md.
namespace cs2_offsets {
inline constexpr std::uint32_t ClientTimestamp = 0x6ADA7340;
inline constexpr std::uint32_t ClientImageSize = 0x29A0000;
namespace globals {
inline constexpr std::ptrdiff_t dwEntityList = 0x271C8B8;
inline constexpr std::ptrdiff_t dwGameEntitySystem = 0x271C8B8;
inline constexpr std::ptrdiff_t dwGameEntitySystem_highestEntityIndex = 0x2120;
inline constexpr std::ptrdiff_t dwLocalPlayerController = 0x253EEF8;
inline constexpr std::ptrdiff_t dwLocalPlayerPawn = 0x25676D8;
inline constexpr std::ptrdiff_t dwViewMatrix = 0x256CFE0;
}
namespace entity_layout {
inline constexpr std::ptrdiff_t TableOffset = 0x10;
inline constexpr std::ptrdiff_t IdentityStride = 0x70;
inline constexpr std::ptrdiff_t EntityPointerOffset = 0;
inline constexpr std::ptrdiff_t HandleOffset = 0x10;
inline constexpr std::uint32_t IndexMask = 0x7FFF;
inline constexpr std::uint32_t BucketShift = 9;
inline constexpr std::uint32_t BucketMask = 0x1FF;
inline constexpr std::uint32_t BucketCount = 64;
}
namespace CEntityInstance {
inline constexpr std::ptrdiff_t m_pEntity = 0x10;
}
namespace CEntityIdentity {
inline constexpr std::ptrdiff_t m_designerName = 0x20;
inline constexpr std::ptrdiff_t m_flags = 0x30;
}
namespace C_BaseEntity {
inline constexpr std::ptrdiff_t m_pGameSceneNode = 0x330;
inline constexpr std::ptrdiff_t m_pCollision = 0x340;
inline constexpr std::ptrdiff_t m_iHealth = 0x34C;
inline constexpr std::ptrdiff_t m_iMaxHealth = 0x348;
inline constexpr std::ptrdiff_t m_lifeState = 0x354;
inline constexpr std::ptrdiff_t m_iTeamNum = 0x3E7;
}
namespace CBasePlayerController {
inline constexpr std::ptrdiff_t m_iszPlayerName = 0x6FC;
inline constexpr std::ptrdiff_t m_bIsLocalPlayerController = 0x790;
inline constexpr std::ptrdiff_t m_hPawn = 0x6BC;
}
namespace CCSPlayerController {
inline constexpr std::ptrdiff_t m_hPlayerPawn = 0x92C;
inline constexpr std::ptrdiff_t m_sSanitizedPlayerName = 0x878;
inline constexpr std::ptrdiff_t m_iPawnArmor = 0x93C;
inline constexpr std::ptrdiff_t m_iPawnHealth = 0x938;
inline constexpr std::ptrdiff_t m_bPawnIsAlive = 0x934;
}
namespace CGameSceneNode {
inline constexpr std::ptrdiff_t m_vecAbsOrigin = 0xC8;
inline constexpr std::ptrdiff_t m_angAbsRotation = 0xD4;
inline constexpr std::ptrdiff_t m_flAbsScale = 0xE0;
inline constexpr std::ptrdiff_t m_bDormant = 0x103;
inline constexpr std::ptrdiff_t m_nodeToWorld = 0x10;
}
namespace CCollisionProperty {
inline constexpr std::ptrdiff_t m_vecMins = 0x40;
inline constexpr std::ptrdiff_t m_vecMaxs = 0x4C;
}
namespace C_CSPlayerPawn {
inline constexpr std::ptrdiff_t m_ArmorValue = 0x1ED0;
}
namespace C_BasePlayerPawn {
inline constexpr std::ptrdiff_t m_hController = 0x14BC;
inline constexpr std::ptrdiff_t m_hDefaultController = 0x14C0;
}
}
