#include "aim_spread.h"
#include "aim_spread_native_reference.h"
#include "fixtures/ssg08_spread_replay.h"
#include "fixtures/ssg08_clock_replay.h"
#include "fixtures/pistol_spread_replay.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// AimSpreadSolve does not call this; its containing translation unit also
// defines read/still-current routines which need the ordinary link contract.
bool AimWeaponStillCurrent(const AimWeaponContext&,const AimSource&,const AimWeaponSnapshot&) noexcept { return true; }

namespace {
using namespace spread_replay_reference;
struct Fixture {
    const char* group; int id; AimSpreadSearchInput input;
    int ticks[2]; float inaccuracy,spread; AimVector eye; AimCapsule capsule;
    unsigned selectedVariant=0;bool chosen=false;
};
unsigned Model(const Fixture& f,unsigned variant) noexcept { return f.chosen?f.selectedVariant:variant; }
AimSpreadSearchInput SearchInput(const Fixture& f) noexcept {
    auto input=f.input;
    if (f.chosen) {
        if (f.selectedVariant==1 && input.alternatePunchValid) input.punch=input.alternatePunch;
        input.alternatePunchValid=false;
    }
    return input;
}
struct NativeCounters { unsigned seeds=0,streams=0; } nativeCounters;
struct NativeRandom {
    std::int32_t state=1,index=0;std::array<std::int32_t,32> shuffle{};
    void Step() noexcept { state=static_cast<std::int32_t>((static_cast<std::int64_t>(state)*16807)%2147483647); }
};
static_assert(sizeof(NativeRandom)==136);
std::uint32_t NativeSeed(void*,const AimAngles* effective,int tick) {
    ++nativeCounters.seeds;return Seed(*effective,tick)-1u;
}
void* NativeCtor(void* stream,int seed) {
    ++nativeCounters.streams;
    NativeRandom r{};
    r.state=seed==(-2147483647-1)?1:static_cast<std::int32_t>(std::max<std::int64_t>(1,
        std::abs(static_cast<std::int64_t>(seed))));
    for (int i=39;i>=0;--i) { r.Step();if (i<32) r.shuffle[i]=r.state; }
    r.index=r.shuffle[0];std::memcpy(stream,&r,sizeof(r));return stream;
}
float NativeFloat(void* stream,float low,float high) {
    auto& r=*static_cast<NativeRandom*>(stream);
    r.Step();const auto slot=r.index/67108864;
    r.index=r.shuffle[slot];r.shuffle[slot]=r.state;
    const float unit=std::min(static_cast<float>(r.index)*0x1p-31f,0.9999998807907104f);
    return low+unit*(high-low);
}
struct Context { const Fixture& fixture;unsigned calls=0; };
bool Sample(void* opaque,const AimAngles& effective,unsigned variant,
    double& x,double& y,std::uint32_t& seed) noexcept {
    auto& context=*static_cast<Context*>(opaque); ++context.calls;
    const auto& f=context.fixture;
    if (variant>=f.input.variants) return false;
    // Match the real native adapter's private stream calls. Its cached mode
    // below executes AimSpreadSolve itself rather than a duplicate cache.
    seed=NativeSeed(nullptr,&effective,f.ticks[Model(f,variant)])+1u;
    alignas(16) unsigned char stream[136]{};NativeCtor(stream,std::bit_cast<int>(seed));
    constexpr float circle=6.283185482025146484375f;
    const float radiusA=NativeFloat(stream,0,1),angleA=NativeFloat(stream,0,circle);
    const float angleB=NativeFloat(stream,0,circle),radiusB=NativeFloat(stream,0,1);
    const float a=radiusA*f.inaccuracy,b=radiusB*f.spread;
    x=std::cos(angleA)*a+std::cos(angleB)*b;y=std::sin(angleA)*a+std::sin(angleB)*b;
    return true;
}
AimSpreadSearchResult NativeSolve(const Fixture& f) noexcept {
    AimSpreadContext native{};native.client=1;
    native.seed=NativeSeed;native.randomCtor=NativeCtor;native.randomFloat=NativeFloat;
    AimSpreadSnapshot snapshot{};snapshot.valid=true;snapshot.pattern=true;
    snapshot.time.tick=f.ticks[0];snapshot.alternateTime.tick=f.ticks[1];
    snapshot.punch=f.input.punch;snapshot.alternatePunch=f.input.alternatePunch;
    snapshot.rawClockAlternative=f.input.alternatePunchValid;snapshot.seedVariants=f.input.variants;
    snapshot.selectedVariant=f.selectedVariant;
    snapshot.weapon.inaccuracy=f.inaccuracy;snapshot.weapon.spread=f.spread;
    return AimSpreadSolve(native,snapshot,f.input.desired,f.input.stopResidualDegrees,f.input.maxEvaluations);
}
bool Hit(const Fixture& f,Vector ray) noexcept {
    const auto n=std::hypot(std::hypot(ray.x,ray.y),ray.z);
    return AimRayIntersectsCapsule(f.eye,{static_cast<float>(ray.x/n),
        static_cast<float>(ray.y/n),static_cast<float>(ray.z/n)},8192,f.capsule);
}
double Quantile(std::vector<double> values,double fraction) {
    std::sort(values.begin(),values.end());
    return values.empty()?0:values[static_cast<std::size_t>((values.size()-1)*fraction)];
}
double Tolerance(const Fixture& f) noexcept {
    const double x=(static_cast<double>(f.capsule.start.x)+f.capsule.end.x)*.5-f.eye.x;
    const double y=(static_cast<double>(f.capsule.start.y)+f.capsule.end.y)*.5-f.eye.y;
    const double z=(static_cast<double>(f.capsule.start.z)+f.capsule.end.z)*.5-f.eye.z;
    return std::max(.0002,std::atan2(f.capsule.radius*.5,std::hypot(std::hypot(x,y),z))*deg);
}
std::vector<Fixture> Fixtures() {
    std::vector<Fixture> result;
    for (const auto& s:ScoutSpreadReplays) for (unsigned variants=1;variants<=2;++variants) {
        Fixture f{variants==1?"scout-single":"scout-dual",s.id,{},
            {s.tick,s.tick+1},s.inaccuracy,s.spread,s.eye,s.capsule};
        f.input.desired=s.desired; f.input.variants=variants;
        f.input.maxOffset=static_cast<double>(s.inaccuracy)+s.spread; result.push_back(f);
    }
    for (const auto& s:ScoutClockReplays) {
        const bool lead=s.rawTick>s.tick+1;
        Fixture f{"scout-clocks",s.id,{}, {s.tick,lead?s.rawTick:s.tick+1},
            s.inaccuracy,s.spread,s.eye,s.capsule};
        f.input.desired=s.desired; f.input.punch=s.historicalPunch; f.input.variants=2;
        f.input.alternatePunchValid=lead;
        f.input.alternatePunch=lead?s.currentPunch:s.historicalPunch;
        f.input.maxOffset=static_cast<double>(s.inaccuracy)+s.spread; result.push_back(f);
    }
    for (const auto& s:PistolSpreadReplays) for (unsigned variants=1;variants<=2;++variants) {
        const bool lead=s.rawTick>s.tick+1;
        Fixture f{variants==1?"pistol-single":"pistol-dual",s.id,{},
            {s.tick,lead?s.rawTick:s.tick+1},s.inaccuracy,s.spread,s.eye,s.capsule};
        f.input.desired=s.desired; f.input.punch=s.historicalPunch; f.input.variants=variants;
        f.input.alternatePunchValid=variants==2 && lead;
        f.input.alternatePunch=lead?s.currentPunch:s.historicalPunch;
        f.input.maxOffset=static_cast<double>(s.inaccuracy)+s.spread; result.push_back(f);
    }
    // The first Scout capture lacks raw indexed timestamps. Its chosen-next
    // fixtures are an explicit replay hypothesis, not fabricated clock data.
    for (const auto& s:ScoutSpreadReplays) {
        Fixture f{"chosen-scout-next",s.id,{}, {s.tick,s.tick+1},s.inaccuracy,s.spread,s.eye,s.capsule};
        f.input.desired=s.desired;f.input.maxOffset=static_cast<double>(s.inaccuracy)+s.spread;
        f.selectedVariant=1;f.chosen=true;result.push_back(f);
    }
    for (const auto& s:ScoutClockReplays) {
        const bool raw=s.rawTick>s.tick+1;
        Fixture f{"chosen-clocks",s.id,{}, {s.tick,raw?s.rawTick:s.tick+1},s.inaccuracy,s.spread,s.eye,s.capsule};
        f.input.desired=s.desired;f.input.punch=s.historicalPunch;
        f.input.alternatePunchValid=raw;f.input.alternatePunch=raw?s.currentPunch:s.historicalPunch;
        f.input.maxOffset=static_cast<double>(s.inaccuracy)+s.spread;
        f.selectedVariant=s.rawTick>s.tick?1U:0U;f.chosen=true;result.push_back(f);
    }
    for (const auto& s:PistolSpreadReplays) {
        const bool raw=s.rawTick>s.tick+1;
        Fixture f{"chosen-pistol",s.id,{}, {s.tick,raw?s.rawTick:s.tick+1},s.inaccuracy,s.spread,s.eye,s.capsule};
        f.input.desired=s.desired;f.input.punch=s.historicalPunch;
        f.input.alternatePunchValid=raw;f.input.alternatePunch=raw?s.currentPunch:s.historicalPunch;
        f.input.maxOffset=static_cast<double>(s.inaccuracy)+s.spread;
        f.selectedVariant=s.rawTick>s.tick?1U:0U;f.chosen=true;result.push_back(f);
    }
    return result;
}
struct Observation { const char* group; double us,residual; unsigned calls,nativeCalls,cacheHits; bool hit; };
}
int main(int argc,char** argv) {
    const char* label=argc>1?argv[1]:"current";
    const unsigned repetitions=argc>2?std::clamp(static_cast<unsigned>(std::atoi(argv[2])),1U,100U):7;
    const bool tolerance=argc>3 && std::strcmp(argv[3],"tolerance")==0;
    const bool cases=argc>4 && std::strcmp(argv[4],"cases")==0;
    const unsigned budget=argc>5?std::clamp(static_cast<unsigned>(std::atoi(argv[5])),2U,8192U):8192;
    const bool cache=argc>6 && std::strcmp(argv[6],"cache")==0;
    AimSpreadTestRuntime(true);
    auto fixtures=Fixtures();
    std::vector<Observation> observations; unsigned failures=0;
    std::printf("Spread search benchmark %s; reference parent 882d399d910fb2e4285c3eae5917c5df2d6d4a5e; independent native SHA1/ran1; %zu fixtures x %u; budget=%u; stop=%s; native-cache=%s\n",
        label,fixtures.size(),repetitions,budget,tolerance?"half-radius inscribed sphere":"default .0002 degrees",cache?"actual AimSpreadSolve":"off");
    // One untimed pass warms code/data. No mutable game RNG or game process.
    for (auto& f:fixtures) {
        f.input.maxEvaluations=budget;
        if (tolerance) f.input.stopResidualDegrees=Tolerance(f);
        const auto input=SearchInput(f);
        Context context{f};if (cache) (void)NativeSolve(f);else (void)AimSearchSpread(input,Sample,&context);
    }
    for (unsigned repeat=0;repeat<repetitions;++repeat) for (auto& f:fixtures) {
        const auto start=std::chrono::steady_clock::now();
        Context context{f};nativeCounters={};
        const auto input=SearchInput(f);
        const auto result=cache?NativeSolve(f):AimSearchSpread(input,Sample,&context);
        const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
        bool hit=result.valid; double worst=0;
        const auto baseline=AimSpreadCompensate(input.desired,input.punch,0,0);
        double baseWorst=0;
        for (unsigned variant=0;variant<input.variants;++variant) {
            const auto& punch=variant==1 && input.alternatePunchValid?input.alternatePunch:input.punch;
            const int tick=f.ticks[Model(f,variant)];
            std::uint32_t actualSeed=0;
            const auto ray=ModelRay(Effective(result.command,punch),tick,f.inaccuracy,f.spread,&actualSeed);
            const auto predicted=variant==0?result.predicted:result.alternatePredicted;
            failures+=actualSeed!=(variant==0?result.seed:result.alternateSeed) || Error(ray,Ray(predicted))>.00003;
            worst=std::max(worst,Error(ray,Ray(f.input.desired))); hit=hit && Hit(f,ray);
            baseWorst=std::max(baseWorst,Error(ModelRay(Effective(baseline.command,punch),tick,
                f.inaccuracy,f.spread),Ray(f.input.desired)));
        }
        const unsigned calls=cache?result.evaluations:context.calls;
        failures+=!result.valid || result.evaluations!=calls || calls>8192 || calls>input.maxEvaluations ||
            nativeCounters.seeds!=nativeCounters.streams ||
            (cache && (result.nativeSeedCalls!=nativeCounters.seeds || result.nativeRandomStreams!=nativeCounters.streams ||
                result.nativeCacheHits+nativeCounters.seeds!=calls)) ||
            std::abs(worst-result.residualDegrees)>.00003 || worst>baseWorst+.00003 ||
            (worst<=f.input.stopResidualDegrees && !hit);
        observations.push_back({f.group,us,worst,calls,nativeCounters.seeds,result.nativeCacheHits,hit});
        if (repeat==0 && cases) std::printf("case,%s,%d,%u,%.9f,%d,%.9g,%.9g,%08x,%08x\n",f.group,
            f.id,calls,worst,hit,result.command.pitch,result.command.yaw,result.seed,result.alternateSeed);
    }
    for (const char* group:{"scout-single","scout-dual","scout-clocks","pistol-single","pistol-dual","dual-policy",
            "chosen-scout-next","chosen-clocks","chosen-pistol","chosen-policy","all"}) {
        std::vector<double> times,residuals,calls,nativeCalls,cacheHits; unsigned hits=0;
        for (const auto& o:observations) if (std::strcmp(group,"all")==0 || std::strcmp(group,o.group)==0 ||
            (std::strcmp(group,"dual-policy")==0 && !std::strstr(o.group,"single") && !std::strstr(o.group,"chosen")) ||
            (std::strcmp(group,"chosen-policy")==0 && std::strstr(o.group,"chosen"))) {
            times.push_back(o.us); residuals.push_back(o.residual);calls.push_back(o.calls);hits+=o.hit;
            nativeCalls.push_back(o.nativeCalls);cacheHits.push_back(o.cacheHits);
        }
        std::printf("%s: us median/p95/max %.1f/%.1f/%.1f; callbacks median/p95 %.0f/%.0f; SHA1+RNG median/p95 %.0f/%.0f; cache-hits median %.0f; residual median/p95 %.6f/%.6f deg; capsules %u/%zu\n",
            group,Quantile(times,.5),Quantile(times,.95),Quantile(times,1),Quantile(calls,.5),Quantile(calls,.95),
            Quantile(nativeCalls,.5),Quantile(nativeCalls,.95),Quantile(cacheHits,.5),
            Quantile(residuals,.5),Quantile(residuals,.95),hits,times.size());
    }
    std::printf("Independent final residual, baseline and budget checks: %u failures. Offline copied geometry; no live FPS or hit-rate claim.\n",failures);
    return failures?1:0;
}
