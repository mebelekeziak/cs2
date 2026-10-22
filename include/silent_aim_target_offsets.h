#pragma once

#include <cstddef>

// Installed Windows AMD64 client 1.41.9.1 / 2000934, recertified October 21, 2026.
// Named C_BaseModelEntity schema field plus native eye-position consumers.
// This is the live float3 at the interpolation wrapper's start, not the
// CNetworkViewOffsetVector serialization schema's component offsets.
// Frozen verifier: tools/inspect_silent_aim_targets.py.
namespace silent_aim_target_offsets {
inline constexpr std::ptrdiff_t ViewOffset = 0xF60;
}
