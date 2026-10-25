#include "aim_spread_search.h"
#include "aim_hitchance.h"
#include "aim_spread_native_reference.h"
#include "fixtures/pistol_spread_replay.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
using namespace spread_replay_reference;
unsigned checks = 0, failures = 0;
void Check(bool condition, const char* label, int id) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL pistol shot %d: %s\n", id, label); }
}
struct Context { const PistolSpreadReplay& shot; unsigned calls = 0, selectedVariant = 0; bool chosen = false; };
int AlternateTick(const PistolSpreadReplay& shot) {
    return shot.rawTick > shot.tick + 1 ? shot.rawTick : shot.tick + 1;
}
bool Sample(void* opaque, const AimAngles& effective, unsigned variant,
    double& x, double& y, std::uint32_t& seed) noexcept {
    auto& context = *static_cast<Context*>(opaque);
    ++context.calls;
    if (variant >= (context.chosen ? 1U : 2U)) return false;
    const unsigned model = context.chosen ? context.selectedVariant : variant;
    seed = Seed(effective, model ? AlternateTick(context.shot) : context.shot.tick);
    Offset(seed, context.shot.inaccuracy, context.shot.spread, x, y);
    return variant < 2;
}
double TargetTolerance(const PistolSpreadReplay& shot) {
    const double x=(static_cast<double>(shot.capsule.start.x)+shot.capsule.end.x)*.5-shot.eye.x;
    const double y=(static_cast<double>(shot.capsule.start.y)+shot.capsule.end.y)*.5-shot.eye.y;
    const double z=(static_cast<double>(shot.capsule.start.z)+shot.capsule.end.z)*.5-shot.eye.z;
    return std::max(.0002,std::atan2(shot.capsule.radius*.5,std::hypot(std::hypot(x,y),z))*deg);
}
bool Hit(const PistolSpreadReplay& shot, Vector ray) {
    return AimRayIntersectsCapsule(shot.eye,
        {static_cast<float>(ray.x), static_cast<float>(ray.y), static_cast<float>(ray.z)},
        8192, shot.capsule);
}
}
int main() {
    unsigned explained = 0, oldBoth = 0, newBoth = 0;
    unsigned productionBoth = 0, chosenHits = 0, chosenWithoutOther = 0;
    unsigned productionCalls = 0, chosenCalls = 0;
    double oldWorstTotal = 0, newWorstTotal = 0;
    for (const auto& shot : PistolSpreadReplays) {
        const auto oldEffective = Effective(shot.oldCommand, shot.historicalPunch);
        std::uint32_t oldSeed = 0;
        const auto oldRay = ModelRay(oldEffective, shot.tick, shot.inaccuracy, shot.spread, &oldSeed);
        Check(oldSeed == shot.oldSeed, "logged original seed independently reproduced", shot.id);
        Check(Error(oldRay, Ray(shot.oldPredicted)) < .00003,
            "logged original prediction independently reproduced", shot.id);
        const auto rawRay = ModelRay(Effective(shot.oldCommand, shot.currentPunch),
            shot.rawTick, shot.inaccuracy, shot.spread);
        const double oldError = Error(oldRay, Ray(shot.observed));
        const double rawError = Error(rawRay, Ray(shot.observed));
        if (shot.source == 0) {
            Check(rawError < oldError * .2 && rawError < .8,
                "raw timestamp explains each captured unambiguous Deagle miss", shot.id);
            ++explained;
        }
        Context context{shot};
        AimSpreadSearchInput input{};
        input.desired = shot.desired; input.punch = shot.historicalPunch;
        input.maxOffset = static_cast<double>(shot.inaccuracy) + shot.spread;
        input.variants = 2;
        input.alternatePunchValid = shot.rawTick > shot.tick + 1;
        input.alternatePunch = shot.currentPunch;
        const auto result = AimSearchSpread(input, Sample, &context);
        Check(result.valid && result.variants == 2 && result.evaluations == context.calls && context.calls <= 8192,
            "both pistol timing hypotheses share the bounded counted budget", shot.id);
        if (!result.valid) continue;
        const auto first = ModelRay(Effective(result.command, input.punch), shot.tick, shot.inaccuracy, shot.spread);
        const auto second = ModelRay(Effective(result.command,
            input.alternatePunchValid ? input.alternatePunch : input.punch),
            AlternateTick(shot), shot.inaccuracy, shot.spread);
        const auto target = Ray(shot.desired);
        const double worst = std::max(Error(first, target), Error(second, target));
        const auto baseline = AimSpreadCompensate(shot.desired, input.punch, 0, 0);
        const double baselineWorst = std::max(Error(ModelRay(Effective(baseline.command, input.punch),
            shot.tick, shot.inaccuracy, shot.spread), target),
            Error(ModelRay(Effective(baseline.command, input.alternatePunchValid ? input.alternatePunch : input.punch),
                AlternateTick(shot), shot.inaccuracy, shot.spread), target));
        Check(worst <= baselineWorst + .00003, "pistol result preserves the dual-model baseline guarantee", shot.id);
        Check(Error(first, Ray(result.predicted)) < .00003 &&
            Error(second, Ray(result.alternatePredicted)) < .00003 &&
            std::abs(worst - result.residualDegrees) < .00003,
            "returned rounded angles independently reconstruct both predicted rays and residual", shot.id);
        const auto oldSecond = ModelRay(Effective(shot.oldCommand,
            input.alternatePunchValid ? input.alternatePunch : input.punch),
            AlternateTick(shot), shot.inaccuracy, shot.spread);
        oldWorstTotal += std::max(Error(oldRay, target), Error(oldSecond, target));
        newWorstTotal += worst;
        oldBoth += Hit(shot, oldRay) && Hit(shot, oldSecond);
        newBoth += Hit(shot, first) && Hit(shot, second);
        input.maxEvaluations = 4096; input.stopResidualDegrees = TargetTolerance(shot);
        context.calls = 0;
        const auto productionDual = AimSearchSpread(input, Sample, &context);
        const auto dualFirst = ModelRay(Effective(productionDual.command, input.punch), shot.tick, shot.inaccuracy, shot.spread);
        const auto alternativePunch = input.alternatePunchValid ? input.alternatePunch : input.punch;
        const auto dualSecond = ModelRay(Effective(productionDual.command, alternativePunch),
            AlternateTick(shot), shot.inaccuracy, shot.spread);
        productionBoth += Hit(shot, dualFirst) && Hit(shot, dualSecond);
        productionCalls += productionDual.evaluations;
        const unsigned selected = shot.rawTick > shot.tick ? 1U : 0U;
        const int chosenTick = selected ? AlternateTick(shot) : shot.tick;
        const auto chosenPunch = selected ? alternativePunch : input.punch;
        input.punch = chosenPunch; input.variants = 1; input.alternatePunchValid = false;
        Context chosenContext{shot, 0, selected, true};
        const auto chosen = AimSearchSpread(input, Sample, &chosenContext);
        std::uint32_t chosenSeed = 0;
        const auto chosenRay = ModelRay(Effective(chosen.command, chosenPunch), chosenTick,
            shot.inaccuracy, shot.spread, &chosenSeed);
        const double chosenResidual = Error(chosenRay, target);
        const auto chosenBaseline = AimSpreadCompensate(shot.desired, chosenPunch, 0, 0);
        const double chosenBaseError = Error(ModelRay(Effective(chosenBaseline.command, chosenPunch), chosenTick,
            shot.inaccuracy, shot.spread), target);
        Check(chosen.valid && chosen.variants == 1 && chosen.evaluations == chosenContext.calls &&
            chosen.evaluations <= 4096 && chosenSeed == chosen.seed && chosenResidual <= chosenBaseError + .00003 &&
            std::abs(chosenResidual - chosen.residualDegrees) < .00003 && Error(chosenRay, Ray(chosen.predicted)) < .00003,
            "chosen indexed clock preserves bounded callbacks, native rounded seed/ray and selected-model baseline", shot.id);
        const auto otherPunch = selected ? shot.historicalPunch : alternativePunch;
        const auto otherRay = ModelRay(Effective(chosen.command, otherPunch), selected ? shot.tick : AlternateTick(shot),
            shot.inaccuracy, shot.spread);
        chosenHits += Hit(shot, chosenRay); chosenWithoutOther += Hit(shot, chosenRay) && !Hit(shot, otherRay);
        chosenCalls += chosen.evaluations;
        std::printf("pistol source=%d id=%d observed mismatch old=%.6f raw=%.6f; new modeled worst=%.6f\n",
            shot.source, shot.id, oldError, rawError, worst);
    }
    Check(explained == 3, "complete three-miss Deagle evidence retained", 0);
    Check(newWorstTotal < oldWorstTotal, "dual timing search improves copied pistol worst-case aggregate", 0);
    Check(chosenHits > productionBoth && chosenWithoutOther > 0,
        "selected indexed ray permits saved-capsule solutions without waiting for the unselected random ray", 0);
    for (const auto& earlier : PistolEarlierPublications) {
        const auto& shot = PistolSpreadReplays[earlier.shotIndex];
        std::uint32_t seed = 0;
        const auto original = ModelRay(Effective(earlier.command, shot.historicalPunch),
            shot.tick, earlier.inaccuracy, shot.spread, &seed);
        Check(seed == earlier.seed && Error(original, Ray(earlier.predicted)) < .00003,
            "earlier same-command pre-shot accuracy publication independently reproduced", shot.id);
        const auto finalEffective = Effective(shot.oldCommand, shot.currentPunch);
        const double beforePenalty = Error(ModelRay(finalEffective, shot.rawTick,
            earlier.inaccuracy, shot.spread), Ray(shot.observed));
        const double afterPenalty = Error(ModelRay(finalEffective, shot.rawTick,
            shot.inaccuracy, shot.spread), Ray(shot.observed));
        Check(beforePenalty < .2 && beforePenalty < afterPenalty * .2,
            "copied earlier accuracy explains observed ray better than later same-command shot penalty", shot.id);
    }
    std::printf("Copied pistol model: worst residual total %.6f -> %.6f; both saved-capsule hits %u -> %u / %zu\n",
        oldWorstTotal, newWorstTotal, oldBoth, newBoth, std::size(PistolSpreadReplays));
    std::printf("Production 4096-callback copied comparison: dual both-capsule %u / %zu, chosen ray %u / %zu; chosen hit with other miss %u; total callbacks dual %u / chosen %u\n",
        productionBoth, std::size(PistolSpreadReplays), chosenHits, std::size(PistolSpreadReplays),
        chosenWithoutOther, productionCalls, chosenCalls);
    std::printf("Offline model/receipt comparison only; no authoritative seed or new live hit-rate claim.\n");
    std::printf("pistol replay: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
