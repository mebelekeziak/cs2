#pragma once
#include "third_person_settings.h"

struct CameraVector {
    float x = 0, y = 0, z = 0;
    bool operator==(const CameraVector&) const = default;
};
// Angles are pitch/yaw/roll in degrees, with positive pitch looking down.
// Positive shoulder moves right; height is along world Z. No command edits.
bool ThirdPersonDestination(const CameraVector& eye, const CameraVector& angles,
    const ThirdPersonSettings& settings, CameraVector& destination) noexcept;
// A hull sweep clips the entire eye-to-camera displacement, including offsets.
// Invalid input leaves the output unchanged.
bool ThirdPersonClip(const CameraVector& eye, const CameraVector& destination,
    float fraction, CameraVector& output) noexcept;
