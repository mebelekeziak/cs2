#include "aim_spread_search.h"
#include "aim_spread_native_reference.h"
#include "aim_hitchance.h"
#include "fixtures/ssg08_clock_replay.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
using namespace spread_replay_reference;
int checks=0,failures=0;
void Expect(bool okay,const char* label,int id=0) {
    ++checks;if (!okay) {++failures;std::printf("FAIL shot %d: %s\n",id,label);}
}
struct Context { const ScoutClockReplay& shot; int alternateTick=0; unsigned calls=0; };
bool Sample(void* opaque,const AimAngles& effective,unsigned variant,double& x,double& y,std::uint32_t& seed) noexcept {
    auto& context=*static_cast<Context*>(opaque);++context.calls;
    if (variant>1) return false;
    const int tick=variant?context.alternateTick:context.shot.tick;
    seed=Seed(effective,tick);Offset(seed,context.shot.inaccuracy,context.shot.spread,x,y);return true;
}
Vector Model(const ScoutClockReplay& shot,const AimAngles& command,const AimAngles& punch,int tick,
    std::uint32_t* seed=nullptr) noexcept {
    return ModelRay(Effective(command,punch),tick,shot.inaccuracy,shot.spread,seed);
}
bool Hit(const ScoutClockReplay& shot,Vector v) noexcept {
    const double length=std::hypot(std::hypot(v.x,v.y),v.z);
    return AimRayIntersectsCapsule(shot.eye,{static_cast<float>(v.x/length),static_cast<float>(v.y/length),static_cast<float>(v.z/length)},8192,shot.capsule);
}
double Median(std::vector<double> values) {
    std::sort(values.begin(),values.end());const auto n=values.size();return n%2?values[n/2]:(values[n/2-1]+values[n/2])*.5;
}
}
int main() {
    unsigned changed=0,retained=0,improvedObserved=0,exactCurrentPunch=0,oldAccepted=0,newAccepted=0;
    std::vector<double> oldObserved,rawObserved,oldNewModelWorst,newModelWorst;
    for (const auto& shot:ScoutClockReplays) {
        Expect(Seed(shot.oldEffective,shot.tick)==shot.oldSeed && Seed(shot.oldEffective,shot.tick+1)==shot.oldAlternateSeed,
            "both original seed bins independently reproduce",shot.id);
        Expect(Error(ModelRay(shot.oldEffective,shot.tick,shot.inaccuracy,shot.spread),Ray(shot.oldPredicted))<.000025 &&
            Error(ModelRay(shot.oldEffective,shot.tick+1,shot.inaccuracy,shot.spread),Ray(shot.oldAlternatePredicted))<.000025,
            "both original logged rays reconstruct",shot.id);
        const auto reconstructed=Effective(shot.oldCommand,shot.historicalPunch);
        Expect(Error(Ray(reconstructed),Ray(shot.oldEffective))<.000005,
            "reconstructed historical punch composes to validated old effective angles",shot.id);
        Expect(shot.hcAgeMs<=0 && shot.hcAgeMs>=-47 && shot.currentPunch.pitch==0 && shot.currentPunch.yaw==0 && shot.currentPunch.roll==0,
            "current-punch fixture is an identified bounded-age ready snapshot",shot.id);
        exactCurrentPunch+=shot.hcAgeMs==0;
        const bool rawLead=shot.rawTick>shot.tick+1;
        const int alternateTick=rawLead?shot.rawTick:shot.tick+1;
        const auto alternatePunch=rawLead?shot.currentPunch:shot.historicalPunch;
        rawLead?++changed:++retained;
        if (rawLead) {
            const auto observed=Ray(shot.observed);
            const double old=std::min(Error(ModelRay(shot.oldEffective,shot.tick,shot.inaccuracy,shot.spread),observed),
                Error(ModelRay(shot.oldEffective,shot.tick+1,shot.inaccuracy,shot.spread),observed));
            const double raw=Error(Model(shot,shot.oldCommand,shot.currentPunch,shot.rawTick),observed);
            oldObserved.push_back(old);rawObserved.push_back(raw);improvedObserved+=raw<old;
            Expect(shot.rawTick-shot.tick>=2 && shot.rawTick-shot.tick<=5,"capture covers raw history leads two to five ticks",shot.id);
        }
        AimSpreadSearchInput input{};input.desired=shot.desired;input.punch=shot.historicalPunch;
        input.maxOffset=static_cast<double>(shot.inaccuracy)+shot.spread;input.variants=2;
        Context oldContext{shot,shot.tick+1};const auto oldResult=AimSearchSpread(input,Sample,&oldContext);
        Expect(oldResult.valid && oldResult.evaluations==oldContext.calls && oldContext.calls<=8192,
            "original pair search is counted and bounded",shot.id);
        input.alternatePunchValid=rawLead;input.alternatePunch=alternatePunch;
        Context context{shot,alternateTick};const auto result=AimSearchSpread(input,Sample,&context);
        Expect(result.valid && result.evaluations==context.calls && context.calls<=8192,
            "adaptive pair search is counted and bounded",shot.id);
        if (!result.valid || !oldResult.valid) continue;
        const auto target=Ray(input.desired);std::uint32_t primarySeed=0,alternateSeed=0;
        const auto primary=Model(shot,result.command,shot.historicalPunch,shot.tick,&primarySeed);
        const auto alternate=Model(shot,result.command,alternatePunch,alternateTick,&alternateSeed);
        const auto alternateEffective=Effective(result.command,alternatePunch);
        Expect(result.effectiveAlternate.pitch==alternateEffective.pitch && result.effectiveAlternate.yaw==alternateEffective.yaw &&
            result.effectiveAlternate.roll==alternateEffective.roll,"alternate effective angles preserve independent float punch addition",shot.id);
        Expect(primarySeed==result.seed && alternateSeed==result.alternateSeed,"independent punch/tick pairs reproduce both final seeds",shot.id);
        Expect(Error(primary,Ray(result.predicted))<.000025 && Error(alternate,Ray(result.alternatePredicted))<.000025,
            "independent punch/tick pairs reproduce both final directions",shot.id);
        const double primaryError=Error(primary,target),alternateError=Error(alternate,target),worst=std::max(primaryError,alternateError);
        Expect(std::abs(primaryError-result.primaryResidualDegrees)<.000025 && std::abs(alternateError-result.alternateResidualDegrees)<.000025 &&
            std::abs(worst-result.residualDegrees)<.000025,"reported residual is the independently reconstructed maximum",shot.id);
        const auto baseline=AimSpreadCompensate(input.desired,input.punch,0,0);
        const double baselineWorst=std::max(Error(Model(shot,baseline.command,shot.historicalPunch,shot.tick),target),
            Error(Model(shot,baseline.command,alternatePunch,alternateTick),target));
        Expect(result.residualDegrees<=baselineWorst+.000025,"adaptive pair never worsens ordinary-command worst error",shot.id);
        if (!rawLead) Expect(result.seed==oldResult.seed && result.alternateSeed==oldResult.alternateSeed &&
            result.command.pitch==oldResult.command.pitch && result.command.yaw==oldResult.command.yaw,
            "zero/one-tick leads retain the established pair search",shot.id);
        const auto oldPrimary=Model(shot,oldResult.command,shot.historicalPunch,shot.tick);
        const auto oldRaw=Model(shot,oldResult.command,alternatePunch,alternateTick);
        oldNewModelWorst.push_back(std::max(Error(oldPrimary,target),Error(oldRaw,target)));newModelWorst.push_back(worst);
        oldAccepted+=Hit(shot,oldPrimary)&&Hit(shot,oldRaw);newAccepted+=Hit(shot,primary)&&Hit(shot,alternate);
    }
    Expect(std::size(ScoutClockReplays)==30 && changed==18 && retained==12,"complete thirty-shot capture covers changed and retained clocks");
    Expect(exactCurrentPunch==12,"twelve copied current-punch tuples share exact publication uptime");
    Expect(improvedObserved==16 && Median(rawObserved)<.7 && Median(oldObserved)>5,
        "raw/current hypothesis explains sixteen of eighteen later rays and lowers median error");
    std::printf("Captured raw-lead replay: %u/%u observed rays improve; median original current/next %.6f deg / raw-current %.6f deg\n",improvedObserved,changed,Median(oldObserved),Median(rawObserved));
    std::printf("Same adaptive copied hypotheses: median worst residual previous search %.6f deg / adaptive search %.6f deg; both saved-capsule intersections previous %u / adaptive %u\n",Median(oldNewModelWorst),Median(newModelWorst),oldAccepted,newAccepted);
    std::printf("Current-punch tuples are logged three-decimal copies; historical punch is independently validated reconstruction. Offline conditional geometry only, no server-seed or live-success claim.\n");
    std::printf("aim spread clock replay: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
