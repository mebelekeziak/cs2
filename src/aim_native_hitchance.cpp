#include "aim_native_hitchance.h"
#include "aim_spread_offsets.h"
#include <Windows.h>
#include <bit>
#include <cmath>

namespace {
constexpr float circle = 6.283185482025146484375f;
bool NativeOffset(const AimSpreadContext& context, const AimHitchanceInput& input,
    bool shotgunPatterns, std::uint32_t seed, std::array<double, 2>& offset) noexcept {
    alignas(16) unsigned char state[aim_spread_offsets::RandomStateSize]{};
    float radiusA = 0, angleA = 0, radiusB = 0, angleB = 0;
    __try {
        if (context.randomCtor(state, std::bit_cast<int>(seed)) != state) return false;
        radiusA = context.randomFloat(state, 0, 1);
        angleA = context.randomFloat(state, 0, circle);
        if (shotgunPatterns) {
            angleB = context.randomFloat(state, 0, circle);
            radiusB = context.randomFloat(state, 0, 1);
        } else {
            radiusB = context.randomFloat(state, 0, 1);
            angleB = context.randomFloat(state, 0, circle);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!std::isfinite(radiusA) || !std::isfinite(radiusB) ||
        radiusA < 0 || radiusA > 1 || radiusB < 0 || radiusB > 1 ||
        !std::isfinite(angleA) || !std::isfinite(angleB) ||
        angleA < 0 || angleA > circle || angleB < 0 || angleB > circle) return false;
    if (input.negev && input.recoilIndex < 3) {
        int stage = 3;
        do { --stage; radiusA *= radiusA; radiusB *= radiusB; }
        while (static_cast<float>(stage) > input.recoilIndex);
        radiusA = 1 - radiusA;
        radiusB = 1 - radiusB;
    }
    if (input.forceMaxRadius) radiusA = radiusB = 1;
    if (input.fixedSpreadAngle) angleA = angleB = circle * .25f;
    // Keep each native distribution multiply/add in float32 before storage.
    const float a = radiusA * input.inaccuracy, b = radiusB * input.spread;
    const float x = std::cos(angleA) * a + std::cos(angleB) * b;
    const float y = std::sin(angleA) * a + std::sin(angleB) * b;
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    offset = {x, y};
    return true;
}
}

bool AimPrepareNativeSpreadPattern(const AimSpreadContext& context,
    const AimHitchanceInput& input, bool shotgunPatterns, AimSpreadPattern& output,
    const char** reason) noexcept {
    output.valid = false;
    if (reason) *reason = "invalid native spread model";
    if (!std::isfinite(input.inaccuracy) || input.inaccuracy < 0 || input.inaccuracy > 1 ||
        !std::isfinite(input.spread) || input.spread < 0 || input.spread > 1 ||
        !std::isfinite(input.recoilIndex) || input.recoilIndex < 0 ||
        !input.samples || input.samples > AimSpreadPattern::Capacity) return false;
    if (reason) *reason = "native random stream unavailable";
    if (!context.randomCtor || !context.randomFloat) return false;
    const unsigned count = input.inaccuracy == 0 && input.spread == 0 ? 1 : input.samples;
    for (unsigned index = 0; index < count; ++index)
        if (!NativeOffset(context, input, shotgunPatterns, index + 1u, output.offsets[index])) return false;
    output.inaccuracy = input.inaccuracy;
    output.spread = input.spread;
    output.recoilIndex = input.recoilIndex;
    output.samples = input.samples;
    output.negev = input.negev;
    output.forceMaxRadius = input.forceMaxRadius;
    output.fixedSpreadAngle = input.fixedSpreadAngle;
    output.valid = true;
    if (reason) *reason = "native random-stream samples";
    return true;
}
