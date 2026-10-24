#include "aim_spread_math.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double radians = 0.01745329251994329577;
constexpr double degrees = 57.29577951308232088;
constexpr double maximumResidual = .0002;
struct Vector { double x, y, z; };
double Wrap(double angle) noexcept { return std::remainder(angle, 360.0); }
bool Finite(const AimAngles& angles) noexcept {
    return std::isfinite(angles.pitch) && std::isfinite(angles.yaw) && std::isfinite(angles.roll);
}
Vector Direction(const AimAngles& angles, double x, double y, double forward = 1) noexcept {
    const double p = Wrap(angles.pitch) * radians, yaw = Wrap(angles.yaw) * radians;
    const double r = Wrap(angles.roll) * radians;
    const double cp = std::cos(p), sp = std::sin(p), cy = std::cos(yaw), sy = std::sin(yaw);
    const double cr = std::cos(r), sr = std::sin(r);
    // Keep this forward reconstruction independent of the inverse's reduction.
    return {forward*cp*cy + x*(-sr*sp*cy + cr*sy) + y*(cr*sp*cy + sr*sy),
            forward*cp*sy + x*(-sr*sp*sy - cr*cy) + y*(cr*sp*sy - sr*cy),
            -forward*sp - x*sr*cp + y*cr*cp};
}
double Error(Vector a, Vector b) noexcept {
    const Vector cross{a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
    return std::atan2(std::hypot(std::hypot(cross.x, cross.y), cross.z),
                     a.x*b.x+a.y*b.y+a.z*b.z) * degrees;
}
}

AimSpreadCompensation AimSpreadCompensate(const AimAngles& desired,
    const AimAngles& punch, double offsetX, double offsetY) noexcept {
    AimSpreadCompensation result{};
    result.reason = "invalid spread compensation inputs";
    if (!Finite(desired) || !Finite(punch) || desired.roll != 0 || std::abs(desired.pitch) > 90 ||
        !std::isfinite(offsetX) || !std::isfinite(offsetY)) return result;
    // Dividing by the raw direction length before solving avoids overflow even
    // for finite offsets much larger than weapon spreads.
    const double length = std::hypot(1.0, std::hypot(offsetX, offsetY));
    if (!std::isfinite(length)) return result;
    const double roll = Wrap(punch.roll) * radians;
    const double x = offsetX / length, y = offsetY / length, forward = 1.0 / length;
    const double b = -x * std::sin(roll) + y * std::cos(roll);
    const double c = -x * std::cos(roll) - y * std::sin(roll);
    const double radius = std::hypot(forward, b);
    result.reason = "spread direction is unreachable";
    if (!(radius > 0)) return result;
    const double targetZ = -std::sin(desired.pitch * radians);
    const double sine = targetZ / radius;
    if (!std::isfinite(sine) || std::abs(sine) > 1 + 8 * std::numeric_limits<double>::epsilon()) return result;
    const double alpha = std::atan2(b, forward);
    const double root = std::asin(std::clamp(sine, -1.0, 1.0));
    const double roots[] = {alpha - root, alpha - (3.14159265358979323846 - root)};
    const Vector target = Direction(desired, 0, 0);
    const double baselinePitch = Wrap(static_cast<double>(desired.pitch) - Wrap(punch.pitch));
    const double baselineYaw = Wrap(static_cast<double>(desired.yaw) - Wrap(punch.yaw));
    double bestScore = std::numeric_limits<double>::infinity();
    for (double pitch : roots) {
        const double commandPitch = Wrap(pitch * degrees - Wrap(punch.pitch));
        if (commandPitch < -89 || commandPitch > 89) continue;
        const double radial = forward * std::cos(pitch) + b * std::sin(pitch);
        // At an exactly vertical ray yaw is unconstrained; the desired yaw is
        // still a stable choice because atan2(0,0) returns zero.
        const double effectiveYaw = Wrap(static_cast<double>(desired.yaw) - std::atan2(c, radial) * degrees);
        AimSpreadCompensation candidate{};
        candidate.command = {static_cast<float>(commandPitch),
            static_cast<float>(Wrap(effectiveYaw - Wrap(punch.yaw))), 0};
        // Native adds angle components as floats before its seed helper
        // normalizes them. Wrapping the double sum first can change a seed bin
        // at the yaw seam or a half-degree boundary after float rounding.
        candidate.effective = {candidate.command.pitch + punch.pitch,
            candidate.command.yaw + punch.yaw, punch.roll};
        if (!Finite(candidate.effective)) continue;
        // Verify the actual rounded command/effective values returned to the
        // caller. A mathematical solution is insufficient if rounding loses it.
        const double actualResidual = Error(Direction(candidate.effective, x, y, forward), target);
        if (!std::isfinite(actualResidual) || actualResidual > maximumResidual) continue;
        const double dp = Wrap(candidate.command.pitch - baselinePitch);
        const double dy = Wrap(candidate.command.yaw - baselineYaw);
        const double score = dp*dp + dy*dy;
        if (score >= bestScore) continue;
        bestScore = score;
        candidate.residualDegrees = static_cast<float>(actualResidual);
        candidate.valid = true;
        candidate.reason = "known spread offset compensated";
        result = candidate;
    }
    return result;
}
