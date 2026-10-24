#pragma once
#include "silent_aim_math.h"
#include "anti_aim_settings.h"
#include <cstdint>

struct AntiAimMovement {
    float forward = 0, left = 0, up = 0;
    bool valid = false;
};

struct AntiAimDigitalBasis {
    float yaw = 0, forward = 0, left = 0;
    bool valid = false;
};
// Same full-speed world wish, expressed using one of eight exact digital
// directions. Choose the equivalent command yaw nearest the physical camera
// (at most 22.5 degrees away), avoiding 180-degree Forward-only strafe flips.
AntiAimDigitalBasis AntiAimChooseDigitalBasis(float physicalYaw, float wishYaw) noexcept;

// Copied command value; native positive pitch points down. Kept inside the
// installed client's ordinary pitch bounds, away from the vertical singularity.
inline constexpr float AntiAimDownPitch = 89.0f;

// Copied values only; no local camera writes. Output yaw is in [-180,180),
// pitch looks down at AntiAimDownPitch, and roll is zero.
// Failure leaves command unchanged.
bool AntiAimBackwards(const AimAngles& local, AimAngles& command) noexcept;

// Sequence-based patterns are deterministic across command rebuilds and do not
// advance with render frames, prediction replays or wall-clock time.
bool AntiAimBuildAngles(const AimAngles& local, const AntiAimSettings& settings,
                        std::uint32_t sequence, AimAngles& command) noexcept;

// Exact WALK signed-axis permutation for AntiAimBackwards' fixed view change.
// Pass the physical view in either direction: this transform is its own inverse.
// No trig residue, axis mixing, clipping, or drift; rejects the same unsupported
// physical bases as AntiAimCorrectWalkMovement and rejects nonfinite axes.
AntiAimMovement AntiAimCorrectBackwardsMovement(const AimAngles& physical,
                                                 float forward, float left) noexcept;

// WALK uses individually flattened, normalized forward/positive-left vectors.
// Preserves world wish vector and analog length without per-axis clipping.
// Arbitrary wrapped yaw/pitch is supported except exactly vertical forward;
// nonzero roll is rejected because its flattened basis is not orthonormal.
// Failure returns valid=false and zero axes. Runtime must preserve its input.
AntiAimMovement AntiAimCorrectWalkMovement(const AimAngles& from, const AimAngles& to,
                                           float forward, float left) noexcept;

// Complete orthonormal Source angle-basis transform, including vertical views
// and roll. This is pure 3D math, NOT the installed WALK movement model, and is
// not proof of noclip/ladder/swimming semantics. No game memory is accessed.
AntiAimMovement AntiAimCorrectMovement3D(const AimAngles& from, const AimAngles& to,
                                         float forward, float left, float up) noexcept;
