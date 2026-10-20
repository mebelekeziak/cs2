#pragma once
#include "aim_hitchance.h"
#include <algorithm>
#include <cmath>

// Frozen scalar implementation from commit 2b29385 for differential checks.
namespace aim_hitchance_reference {

namespace {
struct Vector { double x, y, z; };
Vector Copy(const AimVector& v) noexcept { return {v.x, v.y, v.z}; }
Vector Add(Vector a, Vector b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vector Sub(Vector a, Vector b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vector Mul(Vector a, double s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
double Dot(Vector a, Vector b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
bool Finite(const AimVector& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
        std::abs(value.x) < 1000000 && std::abs(value.y) < 1000000 && std::abs(value.z) < 1000000;
}
bool Capsule(const AimCapsule& value) noexcept {
    return Finite(value.start) && Finite(value.end) && std::isfinite(value.radius) &&
        value.radius > 0 && value.radius <= 10000;
}
double Clamp(double value) noexcept { return std::clamp(value, 0.0, 1.0); }

// Minimize |w + s*u - t*v|^2 over the unit square. Its minimum lies at the
// stationary point or on one of four edges, including zero-length segments.
double DistanceSquared(Vector origin, Vector direction, double range, const AimCapsule& capsule) noexcept {
    const double directionLength = std::sqrt(Dot(direction, direction));
    if (!std::isfinite(directionLength) || directionLength < 1e-15) return INFINITY;
    const Vector u = Mul(direction, range / directionLength);
    const Vector v = Sub(Copy(capsule.end), Copy(capsule.start));
    const Vector w = Sub(origin, Copy(capsule.start));
    const double a = Dot(u, u), b = Dot(u, v), c = Dot(v, v), d = Dot(u, w), e = Dot(v, w);
    double minimum = Dot(w, w);
    const auto consider = [&](double s, double t) noexcept {
        const Vector delta = Sub(Add(w, Mul(u, s)), Mul(v, t));
        minimum = std::min(minimum, Dot(delta, delta));
    };
    consider(0, c > 0 ? Clamp(e / c) : 0);
    consider(1, c > 0 ? Clamp((e + b) / c) : 0);
    consider(a > 0 ? Clamp(-d / a) : 0, 0);
    consider(a > 0 ? Clamp((b - d) / a) : 0, 1);
    const double determinant = a * c - b * b;
    if (determinant > a * c * 1e-14) {
        const double s = (b * e - c * d) / determinant;
        const double t = (a * e - b * d) / determinant;
        if (s >= 0 && s <= 1 && t >= 0 && t <= 1) consider(s, t);
    }
    return minimum;
}
bool Intersects(Vector origin, Vector direction, double range, const AimCapsule& capsule) noexcept {
    const double radiusSquared = static_cast<double>(capsule.radius) * capsule.radius;
    return DistanceSquared(origin, direction, range, capsule) <= radiusSquared + std::max(1e-12, radiusSquared * 1e-10);
}
double RadicalInverse(unsigned index, unsigned base) noexcept {
    double value = 0, fraction = 1.0 / base;
    while (index) {
        value += (index % base) * fraction;
        index /= base;
        fraction /= base;
    }
    return value;
}
double Radius(double uniform, const AimHitchanceInput& input) noexcept {
    if (input.forceMaxRadius) return 1;
    if (input.negev && input.recoilIndex < 3) {
        int stage = 3;
        do { --stage; uniform *= uniform; } while (static_cast<float>(stage) > input.recoilIndex);
        return 1 - uniform;
    }
    return uniform;
}
}

bool AimRayIntersectsCapsule(const AimVector& origin, const AimVector& direction,
                             float range, const AimCapsule& capsule) noexcept {
    return Finite(origin) && Finite(direction) && Capsule(capsule) && std::isfinite(range) &&
        range > 0 && range <= 1000000 && Intersects(Copy(origin), Copy(direction), range, capsule);
}

float AimRayCapsuleMissDistance(const AimVector& origin, const AimVector& direction,
                               float range, const AimCapsule& capsule) noexcept {
    if (!Finite(origin) || !Finite(direction) || !Capsule(capsule) || !std::isfinite(range) ||
        range <= 0 || range > 1000000) return -1;
    const double squared = DistanceSquared(Copy(origin), Copy(direction), range, capsule);
    return std::isfinite(squared) ? static_cast<float>(std::max(0.0, std::sqrt(squared) - capsule.radius)) : -1;
}

AimHitchanceResult AimEstimateHitchance(const AimHitchanceInput& input) noexcept {
    AimHitchanceResult result{};
    result.reason = "invalid geometry";
    if (!Finite(input.eye) || !Capsule(input.capsule)) return result;
    result.reason = "invalid spread model";
    if (!std::isfinite(input.inaccuracy) || input.inaccuracy < 0 || input.inaccuracy > 1 ||
        !std::isfinite(input.spread) || input.spread < 0 || input.spread > 1 ||
        !std::isfinite(input.range) || input.range <= 0 || input.range > 1000000 ||
        !std::isfinite(input.recoilIndex) || input.recoilIndex < 0) return result;
    result.reason = "invalid aim angles";
    // Native recoil may carry the eventual shot direction beyond view limits.
    if (!std::isfinite(input.angles.pitch) || std::abs(input.angles.pitch) > 360 ||
        !std::isfinite(input.angles.yaw) || std::abs(input.angles.yaw) > 360 ||
        !std::isfinite(input.angles.roll) || std::abs(input.angles.roll) > 360) return result;
    result.reason = "invalid sample count";
    if (!input.samples || input.samples > 65536) return result;
    constexpr double radians = 0.01745329251994329577, circle = 6.2831853071795864769;
    const double cp = std::cos(input.angles.pitch * radians), sp = std::sin(input.angles.pitch * radians);
    const double cy = std::cos(input.angles.yaw * radians), sy = std::sin(input.angles.yaw * radians);
    const double cr = std::cos(input.angles.roll * radians), sr = std::sin(input.angles.roll * radians);
    const Vector forward{cp * cy, cp * sy, -sp};
    const Vector right{-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp};
    const Vector up{cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};
    const Vector eye = Copy(input.eye);
    for (unsigned index = 1; index <= input.samples; ++index) {
        // Native primary fire draws both radii uniformly, rather than sqrt(U).
        const double radiusA = Radius(RadicalInverse(index, 2), input) * input.inaccuracy;
        const double radiusB = Radius(RadicalInverse(index, 5), input) * input.spread;
        const double angleA = input.fixedSpreadAngle ? circle * .25 : circle * RadicalInverse(index, 3);
        const double angleB = input.fixedSpreadAngle ? circle * .25 : circle * RadicalInverse(index, 7);
        const double x = std::cos(angleA) * radiusA + std::cos(angleB) * radiusB;
        const double y = std::sin(angleA) * radiusA + std::sin(angleB) * radiusB;
        const Vector direction = Add(forward, Add(Mul(right, x), Mul(up, y)));
        // The native consumer multiplies this raw basis sum by weapon range.
        // Intersects accepts world segment length, so retain the native endpoint
        // while normalizing its direction for the geometric distance solver.
        const double length = std::sqrt(Dot(direction, direction));
        result.hits += Intersects(eye, direction, input.range * length, input.capsule);
    }
    result.samples = input.samples;
    result.percent = 100.0f * static_cast<float>(result.hits) / static_cast<float>(result.samples);
    result.valid = true;
    result.reason = "selected capsule geometry";
    return result;
}

} // namespace aim_hitchance_reference
