#pragma once

struct AimVector { float x = 0, y = 0, z = 0; };
struct AimAngles { float pitch = 0, yaw = 0, roll = 0; };
struct AimSolution {
    AimAngles angles{};
    float separation = 0;
    bool valid = false;
};

// Copied values only. FOV is angular distance from the current view direction.
AimSolution SilentAimSolve(const AimVector& eye, const AimVector& point,
                           const AimAngles& view, float fov) noexcept;

// Native firing adds punch to command angles. Keep outgoing roll zero and
// model the resulting bullet basis, including native punch roll. Never clamp
// an unrepresentable pitch into a different shot.
bool SilentAimCompensate(const AimAngles& desired, const AimAngles& punch,
                         AimAngles& command, AimAngles& effective) noexcept;
