#include "third_person_view.h"
#include <cmath>

namespace {
bool Finite(const CameraVector& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
        std::abs(v.x) < 1000000 && std::abs(v.y) < 1000000 && std::abs(v.z) < 1000000;
}
double Radians(float angle) noexcept {
    return std::remainder(static_cast<double>(angle), 360.0) * 0.01745329251994329577;
}
}
bool ThirdPersonDestination(const CameraVector& eye, const CameraVector& angles,
    const ThirdPersonSettings& settings, CameraVector& destination) noexcept {
    if (!Finite(eye) || !std::isfinite(angles.x) || !std::isfinite(angles.y) || !std::isfinite(angles.z)) return false;
    const auto s = ThirdPersonSanitizeSettings(settings);
    const double p = Radians(angles.x), y = Radians(angles.y), r = Radians(angles.z);
    const double cp = std::cos(p), sp = std::sin(p), cy = std::cos(y), sy = std::sin(y);
    const double sr = std::sin(r), cr = std::cos(r);
    const CameraVector result{
        static_cast<float>(eye.x - cp * cy * s.distance + (cr * sy - sr * sp * cy) * s.shoulder),
        static_cast<float>(eye.y - cp * sy * s.distance + (-cr * cy - sr * sp * sy) * s.shoulder),
        static_cast<float>(eye.z + sp * s.distance - sr * cp * s.shoulder + s.height)};
    if (!Finite(result)) return false;
    destination = result;
    return true;
}
bool ThirdPersonClip(const CameraVector& eye, const CameraVector& destination,
    float fraction, CameraVector& output) noexcept {
    if (!Finite(eye) || !Finite(destination) || !std::isfinite(fraction) || fraction < 0 || fraction > 1) return false;
    output = {eye.x + (destination.x - eye.x) * fraction,
        eye.y + (destination.y - eye.y) * fraction,
        eye.z + (destination.z - eye.z) * fraction};
    return true;
}
