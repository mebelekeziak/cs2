#include "movement_math.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr float radians = std::numbers::pi_v<float> / 180.0f;
bool Valid(float value) noexcept { return std::isfinite(value); }
float Wrap(float angle) noexcept { return std::remainder(angle, 2.0f * std::numbers::pi_v<float>); }
}

StrafeOutput OptimizeAirStrafe(const StrafeInput& input, const AirPhysics& physics) noexcept {
    StrafeOutput out{};
    if (!Valid(input.velocityX) || !Valid(input.velocityY) || !Valid(input.viewYaw) ||
        !Valid(input.forward) || !Valid(input.left) || !Valid(physics.wishSpeed) ||
        !Valid(physics.wishSpeedCap) || !Valid(physics.acceleration) || !Valid(physics.surfaceFriction) ||
        !Valid(physics.interval) || physics.wishSpeed <= 0 || physics.wishSpeedCap <= 0 ||
        physics.acceleration < 0 || physics.surfaceFriction < 0 || physics.interval <= 0 ||
        physics.interval > 0.1f) return out;
    out.speed = std::hypot(input.velocityX, input.velocityY);
    if (!Valid(out.speed) || out.speed > 100000) return out;
    const float camera = input.viewYaw * radians;
    const bool directionHeld = std::hypot(input.forward, input.left) > 0.01f;
    const float desired = camera + (directionHeld ? std::atan2(input.left, input.forward) : 0.0f);
    const float cap = std::min(physics.wishSpeedCap, physics.wishSpeed);
    const float available = physics.acceleration * physics.wishSpeed * physics.surfaceFriction * physics.interval;
    if (!Valid(available)) return out;
    float wish = desired;
    if (out.speed > 0.01f && available > 0) {
        const float velocityYaw = std::atan2(input.velocityY, input.velocityX);
        // The capped projection determines the permitted speed gain while the
        // engine acceleration term uses uncapped wish speed. Maximizing the
        // resulting squared speed gives projection max(0, cap - available).
        const float projection = std::max(0.0f, cap - available);
        const float angle = std::acos(std::clamp(projection / out.speed, 0.0f, 1.0f));
        const float delta = Wrap(desired - velocityYaw);
        const float side = std::abs(delta) > 0.0001f ? (delta > 0 ? 1.0f : -1.0f) :
            ((input.sequence & 1) ? 1.0f : -1.0f);
        wish = velocityYaw + side * angle;
    }
    const float relative = Wrap(wish - camera);
    out.forward = std::cos(relative);
    out.left = std::sin(relative);
    out.wishYaw = Wrap(wish) / radians;
    const float dot = input.velocityX * std::cos(wish) + input.velocityY * std::sin(wish);
    out.accelerationStep = std::min(available, std::max(0.0f, cap - dot));
    out.valid = true;
    return out;
}
