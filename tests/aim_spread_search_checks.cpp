#include "aim_spread_search.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

namespace {
int checks=0,failures=0;
constexpr double rad=.01745329251994329577,deg=57.29577951308232088;
void Expect(bool value,const char* label) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n",label); }
}
struct Context { double radius=0; unsigned calls=0; bool fail=false,invalid=false; };
std::uint32_t Hash(std::uint32_t n) noexcept {
    n^=n>>16; n*=0x7feb352dU; n^=n>>15; n*=0x846ca68bU; return n^(n>>16);
}
bool RandomBins(void* opaque,const AimAngles& effective,double& x,double& y,std::uint32_t& seed) noexcept {
    auto& c=*static_cast<Context*>(opaque); ++c.calls;
    if (c.fail) return false;
    if (c.invalid) { x=std::numeric_limits<double>::quiet_NaN(); y=0; return true; }
    const auto pitch=static_cast<int>(std::floor(std::remainder(effective.pitch,360.0)*2));
    const auto yaw=static_cast<int>(std::floor(std::remainder(effective.yaw,360.0)*2));
    seed=Hash(static_cast<std::uint32_t>(pitch)*0x9e3779b9U^static_cast<std::uint32_t>(yaw));
    const double angle=seed*(6.2831853071795864769/4294967296.0);
    const double radius=c.radius*(.25+.75*(Hash(seed+1)/4294967296.0));
    x=std::cos(angle)*radius; y=std::sin(angle)*radius;
    return true;
}
bool Constant(void* opaque,const AimAngles&,double& x,double& y,std::uint32_t& seed) noexcept {
    auto& c=*static_cast<Context*>(opaque); ++c.calls;
    x=c.radius*.6; y=-c.radius*.8; seed=123; return true;
}
bool Continuous(void* opaque,const AimAngles& effective,double& x,double& y,std::uint32_t& seed) noexcept {
    auto& c=*static_cast<Context*>(opaque); ++c.calls;
    x=c.radius*.6*std::sin(effective.pitch*17+effective.yaw*11);
    y=c.radius*.6*std::cos(effective.pitch*7-effective.yaw*23);
    seed=Hash(static_cast<std::uint32_t>((std::sin(effective.pitch)+1)*1000000));
    return true;
}
struct Vector { double x,y,z; };
Vector Shot(const AimAngles& a,double x,double y) {
    const double p=a.pitch*rad,yaw=a.yaw*rad,r=a.roll*rad;
    const double cp=std::cos(p),sp=std::sin(p),cy=std::cos(yaw),sy=std::sin(yaw),cr=std::cos(r),sr=std::sin(r);
    return {cp*cy+x*(-sr*sp*cy+cr*sy)+y*(cr*sp*cy+sr*sy),
        cp*sy+x*(-sr*sp*sy-cr*cy)+y*(cr*sp*sy-sr*cy),-sp-x*sr*cp+y*cr*cp};
}
double Error(Vector a,Vector b) {
    const double x=a.y*b.z-a.z*b.y,y=a.z*b.x-a.x*b.z,z=a.x*b.y-a.y*b.x;
    return std::atan2(std::hypot(std::hypot(x,y),z),a.x*b.x+a.y*b.y+a.z*b.z)*deg;
}
void CheckResult(const AimSpreadSearchInput& input,const AimSpreadSearchResult& result,AimSpreadSample callback,Context context) {
    Expect(result.valid,"search result is valid");
    if (!result.valid) return;
    Expect(std::abs(result.command.pitch)<=89 && std::abs(result.command.yaw)<=180 && result.command.roll==0,"output command pitch/yaw/roll are legal");
    Expect(result.effective.roll==input.punch.roll,"effective roll is the native punch roll");
    const AimAngles nativeEffective{result.command.pitch+input.punch.pitch,
        result.command.yaw+input.punch.yaw,input.punch.roll};
    Expect(result.effective.pitch==nativeEffective.pitch && result.effective.yaw==nativeEffective.yaw,
        "effective angles preserve native float addition before normalization");
    double x=0,y=0; std::uint32_t seed=0;
    callback(&context,nativeEffective,x,y,seed);
    Expect(seed==result.seed,"output seed is independently resampled at output effective angle");
    const double error=Error(Shot(result.effective,x,y),Shot(input.desired,0,0));
    Expect(std::abs(error-result.residualDegrees)<.00001,"reported residual reconstructs actual output seed's ray");
    Expect(Error(Shot(result.predicted,0,0),Shot(result.effective,x,y))<.00002,"predicted world direction reconstructs actual spread ray");
    const auto baseline=AimSpreadCompensate(input.desired,input.punch,0,0);
    callback(&context,baseline.effective,x,y,seed);
    const double initial=Error(Shot(baseline.effective,x,y),Shot(input.desired,0,0));
    Expect(error<=initial+.000001,"actual predicted ray is never worse than baseline");
    Expect(result.evaluations<=input.maxEvaluations && result.evaluations<=8192,"hard callback budget is honored");
}
struct VariantContext {
    double radius=.4;
    unsigned calls=0,primaryCalls=0,alternateCalls=0;
    bool constant=false,failAlternate=false,invalidAlternate=false;
};
bool VariantBins(void* opaque,const AimAngles& effective,unsigned variant,
    double& x,double& y,std::uint32_t& seed) noexcept {
    auto& c=*static_cast<VariantContext*>(opaque);
    ++c.calls;
    if (variant==0) ++c.primaryCalls; else if (variant==1) ++c.alternateCalls; else return false;
    if (variant==1 && c.failAlternate) return false;
    if (variant==1 && c.invalidAlternate) { x=std::numeric_limits<double>::infinity(); y=0; return true; }
    if (c.constant) {
        x=variant==0?0:c.radius*.6; y=variant==0?0:c.radius*.8;
        seed=variant==0?100U:200U;
        return true;
    }
    // Independent hypotheses use the supplied actual float-added angles.
    const auto pitch=static_cast<int>(std::floor(std::remainder(effective.pitch,360.0)*2));
    const auto yaw=static_cast<int>(std::floor(std::remainder(effective.yaw,360.0)*2));
    seed=Hash(static_cast<std::uint32_t>(pitch)*0x9e3779b9U^
        static_cast<std::uint32_t>(yaw)^(variant==0?0x713f198aU:0x29b4ca13U));
    const double angle=seed*(6.2831853071795864769/4294967296.0);
    const double radius=c.radius*(.25+.75*(Hash(seed+1)/4294967296.0));
    x=std::cos(angle)*radius; y=std::sin(angle)*radius;
    return true;
}
double CheckVariants(const AimSpreadSearchInput& input,const AimSpreadSearchResult& result,
    const VariantContext& sampled) {
    Expect(result.valid && result.variants==input.variants,"variant search reports validated hypothesis count");
    if (!result.valid) return 0;
    Expect(result.evaluations==sampled.calls && result.evaluations<=input.maxEvaluations &&
        result.evaluations<=8192,"all hypothesis callback attempts share one hard budget");
    Expect(sampled.primaryCalls==sampled.alternateCalls || input.variants==1,
        "successful candidate sampling evaluates both hypotheses");
    const AimAngles actual{result.command.pitch+input.punch.pitch,
        result.command.yaw+input.punch.yaw,input.punch.roll};
    Expect(actual.pitch==result.effective.pitch && actual.yaw==result.effective.yaw &&
        actual.roll==result.effective.roll,"variant search preserves actual rounded native angles");
    Expect(std::abs(result.command.pitch)<=89 && std::abs(result.command.yaw)<=180 && result.command.roll==0,
        "variant search produces legal command angles");
    auto independent=sampled;
    const auto baseline=AimSpreadCompensate(input.desired,input.punch,0,0);
    double worst=0,baseWorst=0;
    for (unsigned variant=0;variant<input.variants;++variant) {
        const auto& punch=variant==1 && input.alternatePunchValid?input.alternatePunch:input.punch;
        const AimAngles variantActual{result.command.pitch+punch.pitch,
            result.command.yaw+punch.yaw,punch.roll};
        const auto& reportedEffective=variant==0?result.effective:result.effectiveAlternate;
        Expect(variantActual.pitch==reportedEffective.pitch && variantActual.yaw==reportedEffective.yaw &&
            variantActual.roll==reportedEffective.roll,"each hypothesis preserves its own native float punch addition");
        const AimAngles baseActual{baseline.command.pitch+punch.pitch,
            baseline.command.yaw+punch.yaw,punch.roll};
        double x=0,y=0; std::uint32_t seed=0;
        VariantBins(&independent,variantActual,variant,x,y,seed);
        const double error=Error(Shot(variantActual,x,y),Shot(input.desired,0,0));
        const auto predicted=variant==0?result.predicted:result.alternatePredicted;
        Expect(seed==(variant==0?result.seed:result.alternateSeed),
            "each hypothesis output seed independently matches actual command");
        Expect(std::abs(error-(variant==0?result.primaryResidualDegrees:result.alternateResidualDegrees))<.00001,
            "each hypothesis residual independently reconstructs actual shot");
        Expect(Error(Shot(predicted,0,0),Shot(variantActual,x,y))<.00003,
            "each hypothesis predicted direction reconstructs actual spread ray");
        worst=std::fmax(worst,error);
        VariantBins(&independent,baseActual,variant,x,y,seed);
        baseWorst=std::fmax(baseWorst,Error(Shot(baseActual,x,y),Shot(input.desired,0,0)));
    }
    Expect(std::abs(worst-result.residualDegrees)<.00001,"reported dual residual is actual worst hypothesis");
    Expect(worst<=baseWorst+.000001,"dual result never worsens ordinary command's worst hypothesis");
    return baseWorst;
}
}

int main() {
    AimSpreadSearchInput input{};
    input.desired={-12,179.8f,0}; input.punch={2,-5,21}; input.maxOffset=.7;
    Context fixed{.7};
    auto result=AimSearchSpread(input,Constant,&fixed);
    CheckResult(input,result,Constant,fixed);
    Expect(result.residualDegrees<=.0002 && result.seed==123 && result.evaluations<=5,"constant offset finds self-consistent inverse quickly");
    unsigned improved=0,changedSeeds=0;
    for (int i=0;i<12;++i) {
        input.desired={static_cast<float>(i*4-24),static_cast<float>(i*29-165),0};
        input.punch={2,-3,static_cast<float>(i*7-35)};
        input.maxOffset=.06+i*.035;
        Context context{input.maxOffset};
        const auto baseline=AimSpreadCompensate(input.desired,input.punch,0,0);
        double bx=0,by=0; std::uint32_t baseSeed=0;
        RandomBins(&context,baseline.effective,bx,by,baseSeed);
        const double initial=Error(Shot(baseline.effective,bx,by),Shot(input.desired,0,0));
        context.calls=0;
        result=AimSearchSpread(input,RandomBins,&context);
        CheckResult(input,result,RandomBins,context);
        Expect(context.calls==result.evaluations,"each real callback attempt is counted");
        improved+=result.valid && result.residualDegrees<initial*.25;
        changedSeeds+=result.valid && result.seed!=baseSeed;
    }
    Expect(improved==12,"angle-bin search substantially reduces twelve airborne scout-like random spreads");
    Expect(changedSeeds>=10,"compensation resamples changed seeds instead of trusting the baseline seed");
    input.desired={88.9f,-179.9f,0}; input.punch={1,17,43}; input.maxOffset=2;
    Context nearPole{2};
    result=AimSearchSpread(input,RandomBins,&nearPole);
    CheckResult(input,result,RandomBins,nearPole);
    input.desired={-20,179.99f,0}; input.punch={3,-179,11}; input.maxOffset=.8;
    Context seam{.8};
    result=AimSearchSpread(input,RandomBins,&seam);
    CheckResult(input,result,RandomBins,seam);
    input.maxEvaluations=1;
    Context single{.8}; result=AimSearchSpread(input,RandomBins,&single);
    CheckResult(input,result,RandomBins,single);
    Expect(result.evaluations==1,"one-evaluation search safely returns baseline");
    input.maxEvaluations=17;
    Context small{.8}; result=AimSearchSpread(input,RandomBins,&small);
    CheckResult(input,result,RandomBins,small);
    input.maxEvaluations=100000;
    Context large{.8}; result=AimSearchSpread(input,RandomBins,&large);
    Expect(result.valid && result.evaluations<=8192,"caller cannot increase hard evaluation cap");
    input.maxEvaluations=8192; input.maxOffset=0;
    Context zero{}; result=AimSearchSpread(input,RandomBins,&zero);
    Expect(result.valid && result.evaluations==1 && result.residualDegrees<=.0002,"zero spread exits after one real sample");
    const struct RoundingCase { float desiredYaw,punchYaw,effectiveYaw; } rounding[] = {
        {180, .000001f, 180}, {-180,-.000001f,-180},
        {-103.50000762939453f,76.99999237060547f,256.5f},
        {103.50000762939453f,-76.99999237060547f,-256.5f}
    };
    for (const auto& value:rounding) {
        input.desired={0,value.desiredYaw,0}; input.punch={0,value.punchYaw,0};
        Context roundingContext{};
        result=AimSearchSpread(input,RandomBins,&roundingContext);
        CheckResult(input,result,RandomBins,roundingContext);
        Expect(result.valid && result.effective.yaw==value.effectiveYaw,
            "seam and half-degree boundary use native float rounding before normalization");
        const auto inverse=AimSpreadCompensate(input.desired,input.punch,0,0);
        Expect(inverse.valid && inverse.effective.yaw==value.effectiveYaw,
            "pure inverse preserves native float-added effective yaw at seed boundary");
        double x=0,y=0; std::uint32_t nativeSeed=0;
        const AimAngles nativeEffective{0,value.effectiveYaw,0};
        RandomBins(&roundingContext,nativeEffective,x,y,nativeSeed);
        Expect(result.seed==nativeSeed,"output seed matches independently normalized native float sum");
    }
    input.desired={}; input.punch={};
    input.maxOffset=.5;
    Context fail{.5,0,true}; result=AimSearchSpread(input,RandomBins,&fail);
    Expect(!result.valid && result.evaluations==1,"baseline callback failure returns invalid");
    Context invalid{.5,0,false,true}; result=AimSearchSpread(input,RandomBins,&invalid);
    Expect(!result.valid && result.evaluations==1,"nonfinite callback offset returns invalid");
    Context tooLarge{1}; result=AimSearchSpread(input,Constant,&tooLarge);
    Expect(!result.valid,"callback offset exceeding supplied radius is rejected");
    Context unused{};
    Expect(!AimSearchSpread(input,nullptr,&unused).valid,"missing callback fails");
    input.maxOffset=std::numeric_limits<double>::quiet_NaN();
    Expect(!AimSearchSpread(input,RandomBins,&unused).valid,"NaN offset bound fails before callback");
    input.maxOffset=.5; input.desired.pitch=std::numeric_limits<float>::infinity();
    Expect(!AimSearchSpread(input,RandomBins,&unused).valid,"nonfinite desired direction fails");
    input.desired={88,0,0}; input.punch={-4,0,0};
    Expect(!AimSearchSpread(input,RandomBins,&unused).valid,"unrepresentable ordinary command fails");
    input.desired={}; input.punch={}; input.maxEvaluations=0;
    Expect(!AimSearchSpread(input,RandomBins,&unused).valid,"zero evaluation budget fails");
    Expect(unused.calls==0,"invalid inputs do not invoke native sampling callback");
    input={}; input.desired={-12,179.8f,0}; input.punch={2,-5,21};
    input.maxOffset=.4; input.variants=2;
    VariantContext fixedDual{}; fixedDual.constant=true;
    result=AimSearchSpread(input,VariantBins,&fixedDual);
    const double constantBase=CheckVariants(input,result,fixedDual);
    Expect(result.evaluations>2 && result.residualDegrees<constantBase*.51,
        "exact primary baseline does not terminate while alternate misses; midpoint balances both");
    Expect(std::abs(result.primaryResidualDegrees-result.alternateResidualDegrees)<.00005,
        "constant dual offsets use spherical midpoint minimax solution");
    unsigned dualImproved=0;
    for (int i=0;i<12;++i) {
        input.desired={static_cast<float>(i*4-24),static_cast<float>(i*29-165),0};
        input.punch={2,-3,static_cast<float>(i*7-35)};
        input.maxOffset=.06+i*.035;
        VariantContext context{}; context.radius=input.maxOffset;
        result=AimSearchSpread(input,VariantBins,&context);
        const double initial=CheckVariants(input,result,context);
        dualImproved+=result.valid && result.residualDegrees<initial*.7;
    }
    Expect(dualImproved>=10,"dual search usefully reduces representative airborne spread uncertainty");
    input.desired={88.9f,-179.9f,0}; input.punch={1,17,43}; input.maxOffset=2;
    VariantContext dualPole{}; dualPole.radius=2;
    result=AimSearchSpread(input,VariantBins,&dualPole);
    CheckVariants(input,result,dualPole);
    input.desired={-20,179.99f,0}; input.punch={3,-179,11}; input.maxOffset=.8;
    input.maxEvaluations=3;
    VariantContext dualTiny{}; dualTiny.radius=.8;
    result=AimSearchSpread(input,VariantBins,&dualTiny);
    CheckVariants(input,result,dualTiny);
    Expect(result.evaluations==2,"odd small budget returns fully sampled dual baseline");
    input.maxEvaluations=17;
    VariantContext dualSmall{}; dualSmall.radius=.8;
    result=AimSearchSpread(input,VariantBins,&dualSmall);
    CheckVariants(input,result,dualSmall);
    input.maxEvaluations=100000;
    VariantContext dualLarge{}; dualLarge.radius=.8;
    result=AimSearchSpread(input,VariantBins,&dualLarge);
    CheckVariants(input,result,dualLarge);
    Expect(result.evaluations==8192,
        "large dual grid uses the available budget rather than reserving unused refinement slots");
    input.desired={}; input.punch={}; input.maxOffset=0; input.maxEvaluations=8192;
    VariantContext dualZero{}; dualZero.radius=0;
    result=AimSearchSpread(input,VariantBins,&dualZero);
    Expect(result.valid && result.evaluations==2 && result.residualDegrees<=.0002,
        "dual zero spread exits only after both real samples");
    input.maxOffset=.4;
    VariantContext altFailure{}; altFailure.failAlternate=true;
    result=AimSearchSpread(input,VariantBins,&altFailure);
    Expect(!result.valid && result.evaluations==2,"alternate baseline callback failure invalidates search");
    VariantContext altInvalid{}; altInvalid.invalidAlternate=true;
    result=AimSearchSpread(input,VariantBins,&altInvalid);
    Expect(!result.valid && result.evaluations==2,"nonfinite alternate spread invalidates baseline");
    VariantContext dualUnused{};
    input.maxEvaluations=1;
    Expect(!AimSearchSpread(input,VariantBins,&dualUnused).valid,"budget must hold every baseline hypothesis");
    input.maxEvaluations=8192; input.variants=0;
    Expect(!AimSearchSpread(input,VariantBins,&dualUnused).valid,"zero hypotheses rejected");
    input.variants=3;
    Expect(!AimSearchSpread(input,VariantBins,&dualUnused).valid,"unsupported hypothesis count rejected");
    input.variants=2;
    Expect(!AimSearchSpread(input,static_cast<AimSpreadVariantSample>(nullptr),&dualUnused).valid,
        "missing variant callback rejected");
    Expect(!AimSearchSpread(input,RandomBins,&unused).valid,"legacy callback cannot silently ignore alternate");
    Expect(dualUnused.calls==0 && unused.calls==0,"invalid variant input does not invoke sampling");
    input={}; input.variants=2; input.maxOffset=.4; input.alternatePunchValid=true;
    const auto cancel=AimSpreadCompensate({}, {},.24,.32);
    input.alternatePunch=cancel.command; input.alternatePunch.yaw+=2;
    VariantContext compensatedPair{}; compensatedPair.constant=true;
    result=AimSearchSpread(input,VariantBins,&compensatedPair);
    const double differingBaseline=CheckVariants(input,result,compensatedPair);
    Expect(result.residualDegrees<differingBaseline*.51 && std::abs(result.residualDegrees-1)<.00005,
        "different recoil balances actual world rays without invalid local-separation pruning");
    for (int i=0;i<6;++i) {
        input.desired={static_cast<float>(i*6-15),static_cast<float>(i*55-179),0};
        input.punch={2,-3,static_cast<float>(i*7-35)};
        input.alternatePunch={static_cast<float>(2.4-i*.15),static_cast<float>(-2.3+i*.08),
            static_cast<float>(45-i*13)};
        input.maxOffset=.06+i*.035;
        VariantContext context{}; context.radius=input.maxOffset;
        result=AimSearchSpread(input,VariantBins,&context);
        CheckVariants(input,result,context);
    }
    input.desired={88.9f,-179.9f,0}; input.punch={1,17,43};
    input.alternatePunch={-1,16,-21}; input.maxOffset=2;
    VariantContext differentPole{}; differentPole.radius=2;
    result=AimSearchSpread(input,VariantBins,&differentPole);
    CheckVariants(input,result,differentPole);
    input.desired={0,179.5f,0}; input.punch={}; input.alternatePunch={0,76.99999237060547f,-33};
    input.maxOffset=0; input.maxEvaluations=2;
    VariantContext differentRounding{}; differentRounding.radius=0;
    result=AimSearchSpread(input,VariantBins,&differentRounding);
    CheckVariants(input,result,differentRounding);
    Expect(result.evaluations==2 && result.effective.yaw==179.5f && result.effectiveAlternate.yaw==256.5f,
        "per-hypothesis raw float sum reaches the native half-degree seed bin before normalization");
    input.desired={0,180,0}; input.alternatePunch={0,.000001f,19}; input.maxEvaluations=3;
    VariantContext differentSeam{}; differentSeam.radius=0;
    result=AimSearchSpread(input,VariantBins,&differentSeam);
    CheckVariants(input,result,differentSeam);
    Expect(result.effectiveAlternate.yaw==180 && result.evaluations==2,
        "alternate native float addition preserves seam rounding with an odd tiny budget");
    input.desired={-20,179.99f,0}; input.punch={3,-179,11}; input.alternatePunch={1.2f,-179.6f,-16};
    input.maxOffset=.8; input.maxEvaluations=100000;
    VariantContext differentLarge{}; differentLarge.radius=.8;
    result=AimSearchSpread(input,VariantBins,&differentLarge);
    CheckVariants(input,result,differentLarge);
    input.maxEvaluations=8192;
    VariantContext differentUnused{};
    input.alternatePunch.pitch=std::numeric_limits<float>::quiet_NaN();
    Expect(!AimSearchSpread(input,VariantBins,&differentUnused).valid,
        "nonfinite enabled alternate pitch rejects search before callback");
    input.alternatePunch.pitch=0; input.alternatePunch.yaw=std::numeric_limits<float>::infinity();
    Expect(!AimSearchSpread(input,VariantBins,&differentUnused).valid,
        "nonfinite enabled alternate yaw rejects search before callback");
    input.alternatePunch.yaw=0; input.alternatePunch.roll=std::numeric_limits<float>::infinity();
    Expect(!AimSearchSpread(input,VariantBins,&differentUnused).valid,
        "nonfinite enabled alternate roll rejects search before callback");
    Expect(differentUnused.calls==0,"invalid alternate punch never invokes either callback");
    input.alternatePunchValid=false; input.maxOffset=0;
    VariantContext ignoredAlternate{}; ignoredAlternate.radius=0;
    result=AimSearchSpread(input,VariantBins,&ignoredAlternate);
    CheckVariants(input,result,ignoredAlternate);
    Expect(result.effectiveAlternate.pitch==result.effective.pitch &&
        result.effectiveAlternate.yaw==result.effective.yaw && result.effectiveAlternate.roll==result.effective.roll,
        "disabled alternate punch retains the default shared-punch behavior");
    input={}; input.variants=2; input.maxOffset=.02;
    VariantContext strictTolerance{}; strictTolerance.radius=.02; strictTolerance.constant=true;
    const auto strict=AimSearchSpread(input,VariantBins,&strictTolerance);
    input.stopResidualDegrees=.75;
    VariantContext looseTolerance{}; looseTolerance.radius=.02; looseTolerance.constant=true;
    const auto loose=AimSearchSpread(input,VariantBins,&looseTolerance);
    CheckVariants(input,loose,looseTolerance);
    Expect(loose.evaluations>2 && loose.evaluations<strict.evaluations && loose.residualDegrees<=.75 &&
        loose.primaryResidualDegrees<=.75 && loose.alternateResidualDegrees<=.75,
        "looser target tolerance exits only after both actual rays satisfy it");
    input.stopResidualDegrees=2;
    VariantContext baselineTolerance{}; baselineTolerance.radius=.02; baselineTolerance.constant=true;
    const auto acceptedBaseline=AimSearchSpread(input,VariantBins,&baselineTolerance);
    Expect(acceptedBaseline.valid && acceptedBaseline.evaluations==2 && acceptedBaseline.residualDegrees>1,
        "caller tolerance can accept fully sampled ordinary-command baseline without grid work");
    input={}; input.desired={4,179.9f,0}; input.punch={2,5,11};input.maxOffset=.1;
    Context defaultStop{.1},explicitStop{.1};
    const auto defaultResult=AimSearchSpread(input,RandomBins,&defaultStop);
    input.stopResidualDegrees=.0002;
    const auto explicitResult=AimSearchSpread(input,RandomBins,&explicitStop);
    Expect(defaultResult.command.pitch==explicitResult.command.pitch &&
        defaultResult.command.yaw==explicitResult.command.yaw && defaultResult.seed==explicitResult.seed &&
        defaultResult.evaluations==explicitResult.evaluations,
        "default stop tolerance retains the original center-search result and budget");
    for (const auto direction:{AimAngles{-88.9f,-179.99f,0},AimAngles{0,179.99f,0},AimAngles{88.9f,91,0}}) {
        input={};input.desired=direction;input.maxOffset=.05;
        Context continuous{.05};result=AimSearchSpread(input,Continuous,&continuous);
        CheckResult(input,result,Continuous,continuous);
        Expect(result.evaluations==continuous.calls,
            "arbitrary callback varying inside a seed bin is resampled for each candidate");
    }
    input={}; input.maxOffset=.1;
    Context invalidTolerance{.1};
    for (double tolerance:{-1.0,180.01,std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN()}) {
        input.stopResidualDegrees=tolerance;
        Expect(!AimSearchSpread(input,RandomBins,&invalidTolerance).valid,
            "invalid residual tolerance rejects search");
    }
    Expect(invalidTolerance.calls==0,"invalid residual tolerance cannot invoke callbacks");
    std::printf("aim spread search: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
