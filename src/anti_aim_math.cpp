#include "anti_aim_math.h"
#include <cmath>
#include <limits>
#include <algorithm>

namespace {
constexpr double radians = 0.01745329251994329577;
struct Vector { double x, y, z; };
struct Basis { Vector forward, left, up; };

bool Finite(const AimAngles& angles) noexcept {
    return std::isfinite(angles.pitch) && std::isfinite(angles.yaw) && std::isfinite(angles.roll);
}
double Wrap(double degrees) noexcept {
    // Reduce BEFORE adding offsets or converting to radians: finite float-max
    // angles otherwise lose both the offset and useful trig precision.
    double wrapped = std::fmod(degrees, 360.0);
    if (wrapped >= 180.0) wrapped -= 360.0;
    if (wrapped < -180.0) wrapped += 360.0;
    return wrapped;
}
Vector Add(Vector a, Vector b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vector Scale(Vector a, double amount) noexcept { return {a.x * amount, a.y * amount, a.z * amount}; }
double Dot(Vector a, Vector b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
Basis AngleBasis(const AimAngles& angles) noexcept {
    const double p = Wrap(angles.pitch) * radians, y = Wrap(angles.yaw) * radians;
    const double r = Wrap(angles.roll) * radians;
    const double cp = std::cos(p), sp = std::sin(p), cy = std::cos(y), sy = std::sin(y);
    const double cr = std::cos(r), sr = std::sin(r);
    return {{cp * cy, cp * sy, -sp},
            {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, sr * cp},
            {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp}};
}
bool WalkForwardSign(const AimAngles& angles, double& sign) noexcept {
    if (!Finite(angles) || Wrap(angles.roll) != 0.0) return false;
    const double pitch = Wrap(angles.pitch);
    // At vertical pitch the horizontal forward is mathematically zero.
    // Do not let platform-specific trig residue choose a movement direction.
    if (std::abs(pitch) == 90.0) return false;
    sign = std::abs(pitch) > 90.0 ? -1.0 : 1.0;
    return true;
}
bool WalkBasis(const AimAngles& angles, Basis& basis) noexcept {
    double sign = 0;
    if (!WalkForwardSign(angles, sign)) return false;
    const double yaw = Wrap(angles.yaw) * radians, cy = std::cos(yaw), sy = std::sin(yaw);
    basis = {{sign * cy, sign * sy, 0}, {-sy, cy, 0}, {0, 0, 1}};
    return true;
}
AntiAimMovement Result(Vector corrected) noexcept {
    constexpr double limit = std::numeric_limits<float>::max();
    if (!std::isfinite(corrected.x) || !std::isfinite(corrected.y) || !std::isfinite(corrected.z) ||
        std::abs(corrected.x) > limit || std::abs(corrected.y) > limit || std::abs(corrected.z) > limit)
        return {};
    return {static_cast<float>(corrected.x), static_cast<float>(corrected.y),
            static_cast<float>(corrected.z), true};
}
AntiAimMovement Transform(const Basis& from, const Basis& to, float forward, float left, float up) noexcept {
    const Vector wish = Add(Add(Scale(from.forward, forward), Scale(from.left, left)), Scale(from.up, up));
    return Result({Dot(wish, to.forward), Dot(wish, to.left), Dot(wish, to.up)});
}
}

bool AntiAimBackwards(const AimAngles& local, AimAngles& command) noexcept {
    if (!Finite(local)) return false;
    command = {AntiAimDownPitch, static_cast<float>(Wrap(Wrap(local.yaw) + 180.0)), 0};
    return true;
}

bool AntiAimBuildAngles(const AimAngles& local, const AntiAimSettings& settings,
                        std::uint32_t sequence, AimAngles& command) noexcept {
    if (!Finite(local) || !sequence) return false;
    const auto s = AntiAimNormalizeSettings(settings);
    const auto phase = (sequence - 1u) / static_cast<unsigned>(s.jitterInterval);
    double yaw = s.yawBase == AntiAimYawBase::View ? Wrap(local.yaw) : 0.0;
    yaw += s.yawOffset;
    switch (s.yawMode) {
    case AntiAimYawMode::Jitter: yaw += (phase & 1u ? 0.5 : -0.5) * s.jitterRange; break;
    case AntiAimYawMode::Spin: yaw += std::fmod(static_cast<double>(sequence - 1u) * s.spinSpeed, 360.0); break;
    case AntiAimYawMode::Random: {
        // Integer hash: the same command period always selects the same angle.
        auto hash = phase + 0x9e3779b9u;
        hash = (hash ^ (hash >> 16)) * 0x85ebca6bu;
        hash = (hash ^ (hash >> 13)) * 0xc2b2ae35u;
        hash ^= hash >> 16;
        yaw += (static_cast<double>(hash) / UINT32_MAX - 0.5) * s.jitterRange;
        break;
    }
    default: break;
    }
    float pitch = AntiAimDownPitch;
    switch (s.pitchMode) {
    case AntiAimPitchMode::Up: pitch = -89; break;
    case AntiAimPitchMode::Level: pitch = 0; break;
    case AntiAimPitchMode::Keep: pitch = static_cast<float>(std::clamp(Wrap(local.pitch), -89.0, 89.0)); break;
    case AntiAimPitchMode::Custom: pitch = s.pitch; break;
    case AntiAimPitchMode::Jitter: pitch = phase & 1u ? s.pitchAlternate : s.pitch; break;
    default: break;
    }
    command = {pitch, static_cast<float>(Wrap(yaw)), 0};
    return true;
}

AntiAimMovement AntiAimCorrectBackwardsMovement(const AimAngles& physical,
                                                 float forward, float left) noexcept {
    if (!std::isfinite(forward) || !std::isfinite(left)) return {};
    double sign = 0;
    if (!WalkForwardSign(physical, sign)) return {};
    // The outgoing pitch is below vertical; yaw +180 negates both horizontal
    // axes. An upside-down physical pitch already negates its forward axis.
    return {sign > 0 ? -forward : forward, -left, 0, true};
}

AntiAimMovement AntiAimCorrectWalkMovement(const AimAngles& from, const AimAngles& to,
                                           float forward, float left) noexcept {
    if (!std::isfinite(forward) || !std::isfinite(left)) return {};
    Basis oldBasis{}, newBasis{};
    if (!WalkBasis(from, oldBasis) || !WalkBasis(to, newBasis)) return {};
    // Preserve exact signed permutations (including tiny axes) at cardinal
    // yaw differences. Repeated history handoffs must not add trig residue.
    double delta = Wrap(Wrap(from.yaw) - Wrap(to.yaw));
    // Opposite headings can differ by a last-bit rounding after storing the
    // wrapped output in a float. Recognize that representable relationship in
    // either direction so the history inverse stays an exact permutation.
    if (static_cast<float>(Wrap(Wrap(from.yaw) + 180.0)) == static_cast<float>(Wrap(to.yaw)) ||
        static_cast<float>(Wrap(Wrap(to.yaw) + 180.0)) == static_cast<float>(Wrap(from.yaw))) delta = -180;
    double fromSign = 0, toSign = 0;
    WalkForwardSign(from, fromSign); WalkForwardSign(to, toSign);
    if (delta == 0 || delta == -180) {
        const float sign = delta == 0 ? 1.0f : -1.0f;
        return {static_cast<float>(fromSign * toSign) * sign * forward, sign * left, 0, true};
    }
    if (delta == 90 || delta == -90) {
        const float sign = delta == 90 ? 1.0f : -1.0f;
        return {-static_cast<float>(toSign) * sign * left, static_cast<float>(fromSign) * sign * forward, 0, true};
    }
    return Transform(oldBasis, newBasis, forward, left, 0);
}

AntiAimMovement AntiAimCorrectMovement3D(const AimAngles& from, const AimAngles& to,
                                         float forward, float left, float up) noexcept {
    if (!Finite(from) || !Finite(to) || !std::isfinite(forward) || !std::isfinite(left) || !std::isfinite(up))
        return {};
    return Transform(AngleBasis(from), AngleBasis(to), forward, left, up);
}
