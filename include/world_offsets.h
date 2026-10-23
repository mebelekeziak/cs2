#pragma once
#include <cstddef>
#include <cstdint>
// Recertified October 23, 2026 for installed patch 1.41.9.2 / client 2000935.
// scenesystem.dll SHA256 0f17ccdc083290a496e6ac13e4fe54413077ad2b83b7dfd281749cffd2733d9b.
// client.dll SHA256 ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5.
// See tools/verify_sky_draw.py and CS2_SKY_DRAW_EVIDENCE.md.
namespace world_offsets {
inline constexpr std::uint32_t SceneTimestamp = 0x6ADA337D;
inline constexpr std::uint32_t SceneImageSize = 0x978000;
// Only the sky lighting-kind producer was recertified in client.dll. These
// scoped identities do not enable unverified ESP or movement layouts.
inline constexpr std::uint32_t ClientTimestamp = 0x6ADA7340;
inline constexpr std::uint32_t ClientImageSize = 0x29A0000;
inline constexpr std::ptrdiff_t SkyDescriptorVtable = 0x5F6508;
inline constexpr std::ptrdiff_t SkyRender = 0x154FB0;
inline constexpr std::ptrdiff_t SetFloat4 = 0x4DA70;
inline constexpr std::ptrdiff_t SkyTintReturn = 0x1551A9;
inline constexpr std::ptrdiff_t DrawStride = 0x70;
inline constexpr std::ptrdiff_t DrawSource = 0x18;
inline constexpr std::ptrdiff_t SceneKind = 0xC4;
inline constexpr std::uint32_t VisibleKind = 0xE5FD3C88;
inline constexpr std::uint32_t LightingKind = 0x0881EE9E;
inline constexpr std::ptrdiff_t LightingProducer = 0x28267E;
inline constexpr std::uint32_t SkyTintKey = 0x99E6002A;
inline constexpr unsigned char SkyRenderBytes[]{
    0x45,0x85,0xc9,0x0f,0x8e,0x1f,0x07,0x00,0x00,0x4c,0x8b,0xdc,
    0x55,0x41,0x56,0x49,0x8d,0xab,0x58,0xfc,0xff,0xff,0x48,0x81,0xec,0x98,0x04,0x00,0x00};
inline constexpr unsigned char SetFloat4Bytes[]{
    0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
inline constexpr unsigned char LightingProducerBytes[]{
    0xc7,0x80,0xc4,0x00,0x00,0x00,0x9e,0xee,0x81,0x08};
}
