#include "aim_spread_search.h"
#include "aim_hitchance.h"
#include "fixtures/ssg08_spread_replay.h"
#include "aim_spread_native_reference.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
using namespace spread_replay_reference;
int checks=0,failures=0;
void Expect(bool okay,const char* label,int id=0) {
    ++checks;
    if (!okay) { ++failures; std::printf("FAIL shot %d: %s\n",id,label); }
}
struct SampleContext { const ScoutSpreadReplay& shot; unsigned calls=0; };
bool Sample(void* opaque,const AimAngles& effective,unsigned variant,double& x,double& y,std::uint32_t& seed) noexcept {
    auto& context=*static_cast<SampleContext*>(opaque); ++context.calls;
    if (variant>1) return false;
    const auto& shot=context.shot;
    seed=Seed(effective,shot.tick+static_cast<int>(variant));
    Offset(seed,shot.inaccuracy,shot.spread,x,y);
    return true;
}
Vector SampleRay(SampleContext& context,const AimAngles& command,unsigned variant,std::uint32_t* outSeed=nullptr) noexcept {
    double x=0,y=0;std::uint32_t seed=0;
    Sample(&context,command,variant,x,y,seed); if (outSeed) *outSeed=seed;
    return Ray(command,x,y);
}
bool CapsuleHit(const ScoutSpreadReplay& shot,Vector v) noexcept {
    const double length=std::hypot(std::hypot(v.x,v.y),v.z);
    return AimRayIntersectsCapsule(shot.eye,{static_cast<float>(v.x/length),static_cast<float>(v.y/length),static_cast<float>(v.z/length)},8192,shot.capsule);
}
double Median(std::vector<double> values) {
    std::sort(values.begin(),values.end()); const auto n=values.size();
    return n%2?values[n/2]:(values[n/2-1]+values[n/2])*.5;
}
}
int main() {
    unsigned large=0,nextExplained=0,singleCapsules=0,dualCapsules=0,dualImproved=0;
    std::vector<double> observedBefore,observedNext,singleWorst,dualWorst;
    for (const auto& shot:ScoutSpreadReplays) {
        SampleContext context{shot}; std::uint32_t oldSeed=0;
        const auto old=SampleRay(context,shot.oldCommand,0,&oldSeed);
        Expect(oldSeed==shot.oldSeed,"native logged seed reproduced independently",shot.id);
        Expect(Error(old,Ray(shot.oldPredicted))<.00002,"native logged prediction reproduced independently",shot.id);
        const double currentError=Error(old,Ray(shot.observed));
        const double nextError=Error(SampleRay(context,shot.oldCommand,1),Ray(shot.observed));
        if (currentError>2) { ++large; nextExplained+=nextError<currentError; observedBefore.push_back(currentError); observedNext.push_back(nextError); }
        AimSpreadSearchInput input{};input.desired=shot.desired;
        input.maxOffset=static_cast<double>(shot.inaccuracy)+shot.spread;
        context.calls=0; const auto single=AimSearchSpread(input,Sample,&context);
        Expect(single.valid && single.evaluations==context.calls && context.calls<=8192,"single replay uses counted bounded samples",shot.id);
        input.variants=2;context.calls=0;const auto dual=AimSearchSpread(input,Sample,&context);
        Expect(dual.valid && dual.evaluations==context.calls && context.calls<=8192,"dual replay uses counted bounded samples",shot.id);
        if (!single.valid || !dual.valid) continue;
        const auto target=Ray(input.desired);
        const double baseline=std::max(Error(SampleRay(context,input.desired,0),target),Error(SampleRay(context,input.desired,1),target));
        std::uint32_t primarySeed=0,alternateSeed=0;
        const auto primary=SampleRay(context,dual.command,0,&primarySeed),alternate=SampleRay(context,dual.command,1,&alternateSeed);
        const double firstError=Error(primary,target),secondError=Error(alternate,target),worst=std::max(firstError,secondError);
        Expect(primarySeed==dual.seed && alternateSeed==dual.alternateSeed,"both final seeds resample at returned command",shot.id);
        Expect(Error(primary,Ray(dual.predicted))<.00002 && Error(alternate,Ray(dual.alternatePredicted))<.00002,"both returned predictions reconstruct sampled rays",shot.id);
        Expect(std::abs(firstError-dual.primaryResidualDegrees)<.00002 && std::abs(secondError-dual.alternateResidualDegrees)<.00002 && std::abs(worst-dual.residualDegrees)<.00002,"variant and maximum residuals are independently reconstructed",shot.id);
        Expect(worst<=baseline+.00002,"dual result never worsens ordinary-command worst case",shot.id);
        const auto singleFirst=SampleRay(context,single.command,0),singleNext=SampleRay(context,single.command,1);
        const double singleMaximum=std::max(Error(singleFirst,target),Error(singleNext,target));
        singleWorst.push_back(singleMaximum);dualWorst.push_back(worst);dualImproved+=worst<singleMaximum;
        singleCapsules+=CapsuleHit(shot,singleFirst)&&CapsuleHit(shot,singleNext);
        dualCapsules+=CapsuleHit(shot,primary)&&CapsuleHit(shot,alternate);
    }
    Expect(std::size(ScoutSpreadReplays)==34,"complete captured Scout dataset");
    Expect(large==15 && nextExplained==15,"all fifteen large observed errors favor next-tick seed");
    Expect(Median(observedNext)<.1,"next-tick replay explains large errors to below .1 degree median");
    std::printf("Captured replay: %u large errors, median current %.6f deg / next %.6f deg\n",large,Median(observedBefore),Median(observedNext));
    std::printf("Copied current/next hypotheses: median worst residual single %.6f deg / dual %.6f deg; dual improved %u/%zu; both saved-capsule hits single %u / dual %u\n",Median(singleWorst),Median(dualWorst),dualImproved,std::size(ScoutSpreadReplays),singleCapsules,dualCapsules);
    std::printf("Conditional offline geometry only; no future seed discrimination, server timing, hit rate or live success claim.\n");
    std::printf("aim spread replay: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
