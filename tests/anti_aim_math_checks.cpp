#include "anti_aim_math.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>

namespace {
constexpr double radians = 0.01745329251994329577;
int failures = 0;
void Expect(bool result, const char* description) {
    std::printf("%s %s\n", result ? "PASS" : "FAIL", description);
    failures += !result;
}
bool Near(double a, double b, double tolerance = 0.000003) {
    return std::abs(a - b) <= tolerance * (1.0 + std::abs(a) + std::abs(b));
}
using Vector = std::array<double, 3>;
double Reduce(float angle) { return std::remainder(static_cast<double>(angle), 360.0); }
// Independent reference: rotate an arbitrary input vector by roll around X,
// then pitch around Y, then yaw around Z, rather than constructing a basis.
Vector Rotate3D(const AimAngles& angles, double forward, double left, double up) {
    const double p = Reduce(angles.pitch) * radians, y = Reduce(angles.yaw) * radians;
    const double r = Reduce(angles.roll) * radians;
    const double rolledY = left * std::cos(r) - up * std::sin(r);
    const double rolledZ = left * std::sin(r) + up * std::cos(r);
    const double pitchedX = forward * std::cos(p) + rolledZ * std::sin(p);
    const double pitchedZ = -forward * std::sin(p) + rolledZ * std::cos(p);
    return {pitchedX * std::cos(y) - rolledY * std::sin(y),
            pitchedX * std::sin(y) + rolledY * std::cos(y), pitchedZ};
}
Vector WalkWish(const AimAngles& angles, double forward, double left) {
    Vector f = Rotate3D(angles, 1, 0, 0), l = Rotate3D(angles, 0, 1, 0);
    const double fl = std::hypot(f[0], f[1]), ll = std::hypot(l[0], l[1]);
    return {forward * f[0] / fl + left * l[0] / ll,
            forward * f[1] / fl + left * l[1] / ll, 0};
}
bool Same(Vector a, Vector b) { return Near(a[0], b[0]) && Near(a[1], b[1]) && Near(a[2], b[2]); }
bool CheckWalk(const AimAngles& from, const AimAngles& to, float forward, float left) {
    const auto corrected = AntiAimCorrectWalkMovement(from, to, forward, left);
    if (!corrected.valid || corrected.up != 0) return false;
    const auto original = WalkWish(from, forward, left), next = WalkWish(to, corrected.forward, corrected.left);
    return Same(original, next) && Near(std::hypot(forward, left), std::hypot(corrected.forward, corrected.left));
}
bool Check3D(const AimAngles& from, const AimAngles& to, float forward, float left, float up) {
    const auto corrected = AntiAimCorrectMovement3D(from, to, forward, left, up);
    if (!corrected.valid) return false;
    const auto original = Rotate3D(from, forward, left, up);
    const auto next = Rotate3D(to, corrected.forward, corrected.left, corrected.up);
    const double magnitude = std::hypot(std::hypot(forward, left), up);
    const double correctedMagnitude = std::hypot(std::hypot(corrected.forward, corrected.left), corrected.up);
    return Same(original, next) && Near(magnitude, correctedMagnitude);
}
}

int main() {
    const AimAngles local{53, 25, 0};
    AimAngles command{};
    Expect(AntiAimBackwards(local, command) && command.pitch == 0 && command.yaw == -155 && command.roll == 0 &&
           local.pitch == 53 && local.yaw == 25 && local.roll == 0,
           "backwards command is local yaw plus 180 with zero pitch; local view remains untouched");
    bool backwards = true;
    for (float yaw : {-1080.0f, -540.0f, -360.0f, -180.0f, -0.0f, 0.0f, 179.999f, 180.0f, 360.0f, 540.0f, 1080.0f}) {
        backwards &= AntiAimBackwards({89, yaw, 0}, command) && command.yaw >= -180 && command.yaw < 180;
        const auto corrected = AntiAimCorrectWalkMovement({89, yaw, 0}, command, 0.4f, -0.75f);
        backwards &= corrected.valid && Near(corrected.forward, -0.4) && Near(corrected.left, 0.75);
    }
    Expect(backwards, "180 rotation and yaw seams reverse both native forward and positive-left axes");
    auto diagonal = AntiAimCorrectWalkMovement({}, {0, 45, 0}, 1, 1);
    Expect(diagonal.valid && Near(diagonal.forward, std::sqrt(2.0)) && Near(diagonal.left, 0),
           "rotation preserves diagonal analog magnitude without clipping individual axes");
    bool dense = true;
    constexpr float pitches[] = {-539, -270.00003f, -269.99997f, -181, -180, -91, -90.00001f,
        -89.99999f, -89, -30, 0, 30, 89, 89.99999f, 90.00001f, 91, 180, 269.99997f, 270.00003f, 539};
    for (float oldPitch : pitches) for (float newPitch : pitches)
        for (int yaw = -720; yaw <= 720; yaw += 15) {
            dense &= CheckWalk({oldPitch, static_cast<float>(yaw), 0}, {newPitch, static_cast<float>(71 - yaw), 0}, 0.37f, -0.81f);
            dense &= CheckWalk({oldPitch, static_cast<float>(yaw), 0}, {newPitch, static_cast<float>(71 - yaw), 0}, 1, 1);
        }
    Expect(dense, "dense independent horizontal world projection covers wrapped, upside-down and near-vertical pitch");
    const float maximum = std::numeric_limits<float>::max();
    bool huge = true;
    for (float yaw : {maximum, -maximum, 1.0e20f, -1.0e20f, 1.0e10f, -1.0e10f}) {
        huge &= AntiAimBackwards({13, yaw, 0}, command) && CheckWalk({13, yaw, 0}, command, 0.5f, -0.25f);
        huge &= Check3D({yaw, -yaw, yaw}, {-yaw, yaw, -yaw}, 0.2f, -0.8f, 0.35f);
    }
    Expect(huge, "finite float-max angles are reduced before offset and trig");
    bool randomWalk = true, random3D = true, roundtrip = true;
    std::mt19937 random(0x18aa57u);
    std::uniform_real_distribution<float> angle(-100000.0f, 100000.0f), axis(-1.0f, 1.0f);
    for (int i = 0; i < 100000; ++i) {
        const AimAngles from{angle(random), angle(random), angle(random)};
        const AimAngles to{angle(random), angle(random), angle(random)};
        const float f = axis(random), l = axis(random), u = axis(random);
        random3D &= Check3D(from, to, f, l, u);
        const auto rotated = AntiAimCorrectMovement3D(from, to, f, l, u);
        const auto restored = AntiAimCorrectMovement3D(to, from, rotated.forward, rotated.left, rotated.up);
        roundtrip &= restored.valid && Near(restored.forward, f) && Near(restored.left, l) && Near(restored.up, u);
        const AimAngles oldWalk{from.pitch, from.yaw, 0}, newWalk{to.pitch, to.yaw, 0};
        const bool singular = std::abs(Reduce(oldWalk.pitch)) == 90 || std::abs(Reduce(newWalk.pitch)) == 90;
        if (!singular) randomWalk &= CheckWalk(oldWalk, newWalk, f, l);
    }
    Expect(randomWalk, "100000 deterministic arbitrary yaw/pitch WALK cases preserve world wish and analog magnitude");
    Expect(random3D && roundtrip, "100000 independent 3D world projections and inverse transforms preserve all axes and magnitude");
    bool vertical = true;
    for (float pitch : {-450.0f, -270.0f, -90.0f, 90.0f, 270.0f, 450.0f}) {
        vertical &= !AntiAimCorrectWalkMovement({pitch, 25, 0}, {}, 1, 0).valid;
        vertical &= !AntiAimCorrectWalkMovement({}, {pitch, -179, 0}, 1, 0).valid;
        vertical &= Check3D({pitch, 25, 73}, {-pitch, -179, -15}, 0.75f, -0.3f, 1);
    }
    Expect(vertical, "3D math handles exact vertical views; planar WALK explicitly refuses its degenerate horizontal basis");
    Expect(!AntiAimCorrectWalkMovement({0, 25, 1}, {}, 1, 0).valid &&
           !AntiAimCorrectWalkMovement({}, {0, 25, 90}, 1, 0).valid &&
           CheckWalk({31, 25, 360}, {-45, 70, -720}, 0.25f, -0.9f),
           "WALK rejects nonzero roll but accepts equivalent complete rotations");
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    bool invalid = true;
    for (float value : {nan, inf, -inf}) {
        for (const AimAngles bad : {AimAngles{value, 0, 0}, AimAngles{0, value, 0}, AimAngles{0, 0, value}}) {
            command = {1, 2, 3};
            invalid &= !AntiAimBackwards(bad, command) && command.pitch == 1 && command.yaw == 2 && command.roll == 3;
            invalid &= !AntiAimCorrectWalkMovement(bad, {}, 1, 0).valid && !AntiAimCorrectWalkMovement({}, bad, 1, 0).valid;
            invalid &= !AntiAimCorrectMovement3D(bad, {}, 1, 0, 0).valid && !AntiAimCorrectMovement3D({}, bad, 1, 0, 0).valid;
        }
        invalid &= !AntiAimCorrectWalkMovement({}, {}, value, 0).valid && !AntiAimCorrectWalkMovement({}, {}, 0, value).valid;
        invalid &= !AntiAimCorrectMovement3D({}, {}, value, 0, 0).valid &&
                   !AntiAimCorrectMovement3D({}, {}, 0, value, 0).valid && !AntiAimCorrectMovement3D({}, {}, 0, 0, value).valid;
    }
    Expect(invalid, "all nonfinite angle/axis inputs fail without mutating the caller's view");
    Expect(!AntiAimCorrectWalkMovement({}, {0, 45, 0}, maximum, maximum).valid &&
           !AntiAimCorrectMovement3D({}, {0, 45, 0}, maximum, maximum, 0).valid,
           "unrepresentable corrected axes fail instead of overflowing to infinity");
    const auto stopped = AntiAimCorrectWalkMovement({89, -179, 0}, {-89, 179, 0}, 0, 0);
    Expect(stopped.valid && stopped.forward == 0 && stopped.left == 0 && stopped.up == 0,
           "zero analog input stays zero with arbitrary supported view changes");
    return failures ? 1 : 0;
}
