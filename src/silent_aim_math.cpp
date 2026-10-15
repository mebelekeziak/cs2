#include "silent_aim_math.h"
#include <cmath>

namespace {
constexpr double radians = 0.01745329251994329577;
constexpr double degrees = 57.29577951308232088;
bool Finite(const AimVector& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
        std::abs(value.x) < 1000000 && std::abs(value.y) < 1000000 && std::abs(value.z) < 1000000;
}
}

AimSolution SilentAimSolve(const AimVector& eye, const AimVector& point,
                           const AimAngles& view, float fov) noexcept {
    AimSolution result{};
    if (!Finite(eye) || !Finite(point) || !std::isfinite(view.pitch) ||
        !std::isfinite(view.yaw) || !std::isfinite(view.roll) ||
        std::abs(view.pitch) > 89 || std::abs(view.yaw) > 360 ||
        !std::isfinite(fov) || fov <= 0 || fov > 30) return result;
    const double x = static_cast<double>(point.x) - eye.x;
    const double y = static_cast<double>(point.y) - eye.y;
    const double z = static_cast<double>(point.z) - eye.z;
    const double horizontal = std::hypot(x, y), length = std::hypot(horizontal, z);
    if (length < 0.001) return result;
    const double pitch = -std::atan2(z, horizontal) * degrees;
    if (std::abs(pitch) > 89) return result;
    const double cp = std::cos(view.pitch * radians), sp = std::sin(view.pitch * radians);
    const double cy = std::cos(view.yaw * radians), sy = std::sin(view.yaw * radians);
    const double vx = cp * cy, vy = cp * sy, vz = -sp;
    const double crossX = vy * z - vz * y;
    const double crossY = vz * x - vx * z;
    const double crossZ = vx * y - vy * x;
    // atan2 stays accurate for nearly aligned directions, unlike acos(dot).
    const double separation = std::atan2(std::hypot(std::hypot(crossX, crossY), crossZ),
                                         vx * x + vy * y + vz * z) * degrees;
    if (separation > fov) return result;
    result.angles = {static_cast<float>(pitch), static_cast<float>(std::atan2(y, x) * degrees), 0};
    result.separation = static_cast<float>(separation);
    result.valid = true;
    return result;
}
