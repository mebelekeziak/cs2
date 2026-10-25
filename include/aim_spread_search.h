#pragma once
#include "aim_spread_math.h"
#include <cstddef>
#include <cstdint>

using AimSpreadSample = bool (*)(void* context, const AimAngles& effective,
    double& offsetX, double& offsetY, std::uint32_t& seed) noexcept;
using AimSpreadVariantSample = bool (*)(void* context, const AimAngles& effective,
    unsigned variant, double& offsetX, double& offsetY, std::uint32_t& seed) noexcept;
struct AimSpreadSearchInput {
    AimAngles desired{}, punch{};
    double maxOffset = 0;
    unsigned maxEvaluations = 8192;
    unsigned variants = 1;
    bool alternatePunchValid = false;
    AimAngles alternatePunch{};
    double stopResidualDegrees = .0002;
};
struct AimSpreadSearchResult {
    AimAngles command{}, effective{}, predicted{};
    float residualDegrees = 0;
    float primaryResidualDegrees = 0, alternateResidualDegrees = 0;
    AimAngles alternatePredicted{};
    AimAngles effectiveAlternate{};
    std::uint32_t seed = 0;
    std::uint32_t alternateSeed = 0;
    unsigned variants = 1;
    unsigned evaluations = 0;
    bool valid = false;
    const char* reason = "not evaluated";
    unsigned nativeSeedCalls = 0, nativeRandomStreams = 0, nativeCacheHits = 0;
    double elapsedMicroseconds = 0;
};

// Bounded copied-value search. The callback must sample the actual native shot
// for its supplied raw float-added effective angles, including native
// normalization and half-degree seed bins. Every prediction is resampled after
// command rounding and native punch.
// maxOffset bounds hypot(x,y); maxEvaluations is capped at 8192. A successful
// result is never worse than the sampled ordinary recoil-compensated command.
// Variant sampling accepts one or two hypotheses and minimizes their maximum
// angular error. Every callback attempt consumes the shared evaluation budget;
// baseline sampling requires a budget of at least variants. The legacy callback
// accepts only variants=1. Predicted/seed describe variant zero; alternate fields
// describe variant one. residualDegrees is the maximum of their actual errors.
// With alternatePunchValid, variant one uses alternatePunch for its native
// float addition and direction basis; otherwise both variants use punch.
// stopResidualDegrees may stop a fully sampled candidate inside a caller's
// geometric tolerance. It does not certify target intersection; callers must
// still test each returned ray against their actual target geometry.
AimSpreadSearchResult AimSearchSpread(const AimSpreadSearchInput& input,
    AimSpreadSample sample, void* context) noexcept;
AimSpreadSearchResult AimSearchSpread(const AimSpreadSearchInput& input,
    AimSpreadVariantSample sample, void* context) noexcept;
inline AimSpreadSearchResult AimSearchSpread(const AimSpreadSearchInput& input,
    std::nullptr_t, void* context) noexcept {
    return AimSearchSpread(input,static_cast<AimSpreadSample>(nullptr),context);
}
