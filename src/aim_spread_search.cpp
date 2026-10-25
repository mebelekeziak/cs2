#include "aim_spread_search.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace {
constexpr double rad = .01745329251994329577, deg = 57.29577951308232088;
constexpr double pi = 3.14159265358979323846;
constexpr unsigned hardLimit = 8192;
struct Vector { double x, y, z; };
double Wrap(double angle) noexcept { return std::remainder(angle,360.0); }
bool Finite(const AimAngles& a) noexcept {
    return std::isfinite(a.pitch) && std::isfinite(a.yaw) && std::isfinite(a.roll);
}
Vector Direction(const AimAngles& a, double x, double y) noexcept {
    const double p=Wrap(a.pitch)*rad, yaw=Wrap(a.yaw)*rad, r=Wrap(a.roll)*rad;
    const double cp=std::cos(p), sp=std::sin(p), cy=std::cos(yaw), sy=std::sin(yaw);
    const double cr=std::cos(r), sr=std::sin(r), n=std::hypot(1.0,std::hypot(x,y));
    x/=n; y/=n;
    return {cp*cy/n+x*(-sr*sp*cy+cr*sy)+y*(cr*sp*cy+sr*sy),
        cp*sy/n+x*(-sr*sp*sy-cr*cy)+y*(cr*sp*sy-sr*cy), -sp/n-x*sr*cp+y*cr*cp};
}
double Error(Vector a, Vector b) noexcept {
    const double x=a.y*b.z-a.z*b.y,y=a.z*b.x-a.x*b.z,z=a.x*b.y-a.y*b.x;
    return std::atan2(std::hypot(std::hypot(x,y),z),a.x*b.x+a.y*b.y+a.z*b.z)*deg;
}
AimAngles Angles(Vector v) noexcept {
    return {static_cast<float>(-std::atan2(v.z,std::hypot(v.x,v.y))*deg),
        static_cast<float>(std::atan2(v.y,v.x)*deg),0};
}
struct Sample {
    AimAngles command{},effective{},effectiveAlternate{};
    std::array<double,2> x{},y{},errors{};
    double error=0;
    std::array<std::uint32_t,2> seed{};
    bool valid=false;
};
struct Search {
    const AimSpreadSearchInput& input;
    AimSpreadVariantSample callback;
    void* context;
    Vector target;
    unsigned limit=0,count=0;
    Sample best{};
    const AimAngles& Punch(unsigned variant) const noexcept {
        return variant==1 && input.alternatePunchValid?input.alternatePunch:input.punch;
    }
    bool DifferentPunch() const noexcept {
        return input.variants==2 && input.alternatePunchValid &&
            (input.punch.pitch!=input.alternatePunch.pitch || input.punch.yaw!=input.alternatePunch.yaw ||
                input.punch.roll!=input.alternatePunch.roll);
    }
    Sample Evaluate(AimAngles command) noexcept {
        Sample value{};
        if (limit-count<input.variants || !Finite(command) || std::abs(command.pitch)>89 || command.roll!=0) return value;
        command.yaw=static_cast<float>(Wrap(command.yaw));
        value.command=command;
        // Preserve native float component addition. The native seed callback
        // owns normalization; normalizing a double sum before float rounding
        // can select a different half-degree bin.
        value.effective={command.pitch+input.punch.pitch,
            command.yaw+input.punch.yaw,input.punch.roll};
        if (!Finite(value.effective)) return value;
        value.effectiveAlternate=value.effective;
        if (input.variants==2 && input.alternatePunchValid) {
            value.effectiveAlternate={command.pitch+input.alternatePunch.pitch,
                command.yaw+input.alternatePunch.yaw,input.alternatePunch.roll};
            if (!Finite(value.effectiveAlternate)) return value;
        }
        for (unsigned variant=0;variant<input.variants;++variant) {
            const auto& effective=variant==0?value.effective:value.effectiveAlternate;
            ++count;
            if (!callback(context,effective,variant,value.x[variant],value.y[variant],value.seed[variant]) ||
                !std::isfinite(value.x[variant]) || !std::isfinite(value.y[variant]) ||
                std::hypot(value.x[variant],value.y[variant])>
                    input.maxOffset+std::max(1e-10,input.maxOffset*1e-6)) return value;
            value.errors[variant]=Error(Direction(effective,value.x[variant],value.y[variant]),target);
            if (!std::isfinite(value.errors[variant])) return value;
            value.error=std::max(value.error,value.errors[variant]);
        }
        value.valid=true;
        if (!best.valid || value.error<best.error) best=value;
        return value;
    }
    Sample Effective(double pitch,double yaw) noexcept {
        const double commandPitch=Wrap(pitch-Wrap(input.punch.pitch));
        if (std::abs(commandPitch)>89) return {};
        return Evaluate({static_cast<float>(commandPitch),
            static_cast<float>(Wrap(yaw-Wrap(input.punch.yaw))),0});
    }
    bool Satisfied() const noexcept { return best.valid && best.error<=input.stopResidualDegrees; }
    // A small interior margin survives float conversion and punch addition at
    // ordinary native angle magnitudes; Evaluate still resamples the real bin.
    static double InBin(double ideal,double sampled) noexcept {
        const double start=std::floor(Wrap(sampled)*2)*.5;
        const double center=start+.25;
        const double equivalent=center+Wrap(ideal-center);
        return std::clamp(equivalent,start+.0001,start+.4999);
    }
    void ImproveOffset(const Sample& source,double offsetX,double offsetY,unsigned variant=0) noexcept {
        if (!source.valid || count>=limit || Satisfied()) return;
        const auto& punch=Punch(variant);
        const auto& effective=variant==0?source.effective:source.effectiveAlternate;
        const auto inverse=AimSpreadCompensate(input.desired,punch,offsetX,offsetY);
        if (inverse.valid) Evaluate(inverse.command);
        if (Satisfied()) return;
        // Also inspect the inverse constrained to the source's actual seed
        // bins. If elevation is unreachable, its closest elevation is useful
        // as an approximate candidate, evaluated against the real callback.
        const double r=Wrap(punch.roll)*rad;
        const double n=std::hypot(1.0,std::hypot(offsetX,offsetY)), f=1/n;
        const double x=offsetX/n,y=offsetY/n;
        const double b=-x*std::sin(r)+y*std::cos(r);
        const double c=-x*std::cos(r)-y*std::sin(r);
        const double radius=std::hypot(f,b);
        if (!(radius>0)) return;
        const double root=std::asin(std::clamp(-std::sin(input.desired.pitch*rad)/radius,-1.0,1.0));
        const double alpha=std::atan2(b,f), roots[]={alpha-root,alpha-(pi-root)};
        for (double p:roots) {
            const double yaw=Wrap(input.desired.yaw-std::atan2(c,f*std::cos(p)+b*std::sin(p))*deg);
            const double constrainedPitch=InBin(p*deg,effective.pitch);
            const double constrainedYaw=InBin(yaw,effective.yaw);
            double q=Wrap(constrainedPitch-Wrap(punch.pitch));
            // Legal command limits may cut through a seed bin. Its nearest
            // legal boundary is still an actual sampled candidate.
            q=std::clamp(q,-89.0,89.0);
            Evaluate({static_cast<float>(q),static_cast<float>(Wrap(constrainedYaw-Wrap(punch.yaw))),0});
            if (Satisfied() || count>=limit) return;
        }
    }
    void Improve(const Sample& source) noexcept {
        if (!source.valid) return;
        if (DifferentPunch()) {
            // Different recoil rotates both the seed inputs and ray bases.
            // Their local offset separation is not a common-basis lower bound.
            // Try the seam-aware command midpoint of the two individual
            // inverses, then refine each in its own basis. Every proposal is
            // resampled against both hypotheses at its actual float angles.
            const auto first=AimSpreadCompensate(input.desired,input.punch,source.x[0],source.y[0]);
            const auto second=AimSpreadCompensate(input.desired,input.alternatePunch,source.x[1],source.y[1]);
            if (first.valid && second.valid) {
                Evaluate({static_cast<float>((static_cast<double>(first.command.pitch)+second.command.pitch)*.5),
                    static_cast<float>(Wrap(first.command.yaw+Wrap(static_cast<double>(second.command.yaw)-
                        first.command.yaw)*.5)),0});
            }
            ImproveOffset(source,source.x[0],source.y[0],0);
            ImproveOffset(source,source.x[1],source.y[1],1);
            return;
        }
        if (input.variants==2) {
            // A fixed pair cannot place both rays closer than half their
            // separation. Spend refinements on pairs that can beat the best;
            // crossing to other seed bins is explored by separate sampling.
            // Bound the summed pitch/yaw rotation to either edge of each
            // source bin, including partial bins at legal pitch boundaries.
            // Include a rounding margin; candidates are actually resampled.
            const auto travel=[](float angle) noexcept {
                const double position=Wrap(angle)*2;
                const double fraction=position-std::floor(position);
                return .5*std::max(fraction,1-fraction);
            };
            const double lower=std::max(Error({1,source.x[0],source.y[0]},
                {1,source.x[1],source.y[1]})*.5,
                source.error-travel(source.effective.pitch)-travel(source.effective.yaw)-.001);
            if (best.valid && lower>=best.error) return;
            // The sum of unit local rays points at their spherical midpoint.
            // Its inverse balances both angular errors for fixed offsets. A
            // plain arithmetic offset average does not have this property.
            const double a=1/std::hypot(1.0,std::hypot(source.x[0],source.y[0]));
            const double b=1/std::hypot(1.0,std::hypot(source.x[1],source.y[1]));
            ImproveOffset(source,(a*source.x[0]+b*source.x[1])/(a+b),
                (a*source.y[0]+b*source.y[1])/(a+b));
        }
        for (unsigned variant=0;variant<input.variants;++variant)
            ImproveOffset(source,source.x[variant],source.y[variant]);
    }
};
struct YawSpan { unsigned first=0,last=0; };
struct PitchBin {
    double angle=0,cp=0,sp=0;
    std::array<YawSpan,2> spans{};
    unsigned spanCount=0,count=0;
};
double YawAngle(unsigned index) noexcept { return -179.75+index*.5; }
void EligibleYaws(PitchBin& pitch,Vector target,double threshold,double horizontal,double targetYaw) noexcept {
    // For a fixed pitch, forward dot target is A*cos(yaw-center)+B.
    // Solve its one circular interval directly, splitting it only at the yaw
    // seam. This retains the full grid's original ascending index order.
    const double a=std::abs(pitch.cp)*horizontal,b=-pitch.sp*target.z;
    const auto eligible=[&](unsigned index) noexcept {
        const double yaw=YawAngle(index)*rad;
        return pitch.cp*(std::cos(yaw)*target.x+std::sin(yaw)*target.y)+b>=threshold;
    };
    if (a<=1e-15) {
        // Exact polar directions have no meaningful yaw. The tiny native
        // trig residue cannot change a non-boundary decision.
        if (b-a>=threshold) { pitch.spans[0]={0,719}; pitch.spanCount=1; pitch.count=720; }
        else if (b+a>=threshold) {
            // Boundary-only degeneracy: preserve the original predicate.
            for (unsigned first=0;first<720;) {
                if (!eligible(first)) { ++first; continue; }
                unsigned last=first; while (last+1<720 && eligible(last+1)) ++last;
                if (pitch.spanCount<pitch.spans.size()) {
                    pitch.spans[pitch.spanCount++]={first,last}; pitch.count+=last-first+1;
                }
                first=last+1;
            }
        }
        return;
    }
    const double ratio=(threshold-b)/a;
    if (ratio>1+1e-12) return;
    if (ratio< -1-1e-12) { pitch.spans[0]={0,719}; pitch.spanCount=1; pitch.count=720; return; }
    const double half=std::acos(std::clamp(ratio,-1.0,1.0))*deg;
    const double center=Wrap(targetYaw+(pitch.cp<0?180.0:0.0));
    std::array<YawSpan,3> spans{}; unsigned count=0;
    for (int turn=-1;turn<=1;++turn) {
        const double low=center-half+turn*360,high=center+half+turn*360;
        // Include one extra grid point at each analytic edge, then apply the
        // original dot predicate there. Floating trig and acos rounding must
        // never silently add/remove a boundary center.
        int first=std::max(0,static_cast<int>(std::floor((low+179.75)*2))-1);
        int last=std::min(719,static_cast<int>(std::ceil((high+179.75)*2))+1);
        while (first<=last && !eligible(static_cast<unsigned>(first))) ++first;
        while (last>=first && !eligible(static_cast<unsigned>(last))) --last;
        if (first<=last) spans[count++]={static_cast<unsigned>(first),static_cast<unsigned>(last)};
    }
    std::sort(spans.begin(),spans.begin()+count,[](YawSpan lhs,YawSpan rhs) { return lhs.first<rhs.first; });
    for (unsigned i=0;i<count;++i) {
        if (pitch.spanCount && spans[i].first<=pitch.spans[pitch.spanCount-1].last+1)
            pitch.spans[pitch.spanCount-1].last=std::max(pitch.spans[pitch.spanCount-1].last,spans[i].last);
        else if (pitch.spanCount<pitch.spans.size()) pitch.spans[pitch.spanCount++]=spans[i];
    }
    for (unsigned i=0;i<pitch.spanCount;++i) pitch.count+=pitch.spans[i].last-pitch.spans[i].first+1;
}
unsigned YawAtRank(const PitchBin& pitch,unsigned rank) noexcept {
    for (unsigned i=0;i<pitch.spanCount;++i) {
        const unsigned count=pitch.spans[i].last-pitch.spans[i].first+1;
        if (rank<count) return pitch.spans[i].first+rank;
        rank-=count;
    }
    return 0;
}
}

AimSpreadSearchResult AimSearchSpread(const AimSpreadSearchInput& input,
    AimSpreadVariantSample callback,void* context) noexcept {
    AimSpreadSearchResult result{};
    result.reason="invalid spread search inputs";
    if (!callback || !Finite(input.desired) || !Finite(input.punch) || input.desired.roll!=0 ||
        (input.alternatePunchValid && !Finite(input.alternatePunch)) ||
        std::abs(input.desired.pitch)>90 || !std::isfinite(input.maxOffset) || input.maxOffset<0 ||
        !std::isfinite(input.stopResidualDegrees) || input.stopResidualDegrees<0 || input.stopResidualDegrees>180 ||
        input.variants<1 || input.variants>2 || input.maxEvaluations<input.variants) return result;
    result.variants=input.variants;
    const auto baseline=AimSpreadCompensate(input.desired,input.punch,0,0);
    if (!baseline.valid) { result.reason="ordinary command pitch is unrepresentable"; return result; }
    Search search{input,callback,context,Direction(input.desired,0,0),std::min(input.maxEvaluations,hardLimit)};
    const auto original=search.Evaluate(baseline.command);
    if (!original.valid) {
        result.evaluations=search.count;
        result.reason="spread callback failed for ordinary command";
        return result;
    }
    // Chase the measured seed changes first, without assuming that the inverse
    // preserves its source's angle bin. Stop cycles by requiring improvement.
    for (unsigned i=0;i<8 && !search.Satisfied() && search.count<search.limit;++i) {
        const auto before=search.best;
        search.Improve(before);
        if (search.best.error>=before.error) break;
    }
    if (!search.Satisfied() && search.count<search.limit) {
        std::array<PitchBin,720> pitches{};
        unsigned pitchCount=0;
        for (unsigned i=0;i<720;++i) {
            const double p=-179.75+i*.5;
            const double q=Wrap(p-Wrap(input.punch.pitch));
            if (q < -89.25 || q > 89.25) continue;
            // Include partial bins at the legal command pitch boundary.
            const double actual=Wrap(std::clamp(q,-89.0,89.0)+Wrap(input.punch.pitch));
            pitches[pitchCount++]={actual,std::cos(actual*rad),std::sin(actual*rad)};
        }
        // The spread vector cannot rotate the forward ray farther than
        // atan(maxOffset). A bin's center can be up to sqrt(.5^2+.5^2)
        // degrees from an eligible angle; .75 conservatively includes edges.
        const double threshold=std::cos(std::min(180.0,std::atan(input.maxOffset)*deg+.75)*rad);
        const double horizontal=std::hypot(search.target.x,search.target.y);
        const double targetYaw=std::atan2(search.target.y,search.target.x)*deg;
        unsigned total=0;
        for (unsigned p=0;p<pitchCount;++p) {
            EligibleYaws(pitches[p],search.target,threshold,horizontal,targetYaw); total+=pitches[p].count;
        }
        // Dual sources often need only the two screening callbacks: their ray
        // separation already rules out improvement. Select enough centers to
        // use the remaining budget instead of reserving twenty calls per pair.
        // A coprime stride visits dispersed centers first if refinements spend
        // the budget before the complete selected grid can be screened.
        std::array<unsigned,hardLimit/2> centers{};
        const unsigned samples=input.variants==2?
            std::min(total,(search.limit-search.count)/2):
            std::min(total,(search.limit-search.count+3)/4);
        unsigned row=0,rowRank=0,selected=0;
        for (;selected<samples && !search.Satisfied() && search.count<search.limit;++selected) {
            const unsigned targetRank=samples>1?static_cast<unsigned>(
                static_cast<unsigned long long>(selected)*(total-1)/(samples-1)):total/2;
            while (row<pitchCount && targetRank>=rowRank+pitches[row].count) {
                rowRank+=pitches[row].count; ++row;
            }
            if (row==pitchCount) break;
            const unsigned yaw=YawAtRank(pitches[row],targetRank-rowRank);
            if (input.variants==2) centers[selected]=row*720+yaw;
            else {
                const auto source=search.Effective(pitches[row].angle,YawAngle(yaw));
                search.Improve(source);
            }
        }
        if (input.variants==2 && selected) {
            unsigned stride=std::max(1U,static_cast<unsigned>(selected*.6180339887498948));
            while (std::gcd(stride,selected)!=1) ++stride;
            unsigned index=0;
            for (unsigned i=0;i<selected && !search.Satisfied() && search.limit-search.count>=2;++i) {
                const auto center=centers[index];
                const auto source=search.Effective(pitches[center/720].angle,YawAngle(center%720));
                search.Improve(source);
                index=(index+stride)%selected;
            }
        }
    }
    result.command=search.best.command;
    result.effective=search.best.effective;
    result.effectiveAlternate=search.best.effectiveAlternate;
    result.predicted=Angles(Direction(search.best.effective,search.best.x[0],search.best.y[0]));
    result.residualDegrees=static_cast<float>(search.best.error);
    result.primaryResidualDegrees=static_cast<float>(search.best.errors[0]);
    result.seed=search.best.seed[0];
    if (input.variants==2) {
        result.alternatePredicted=Angles(Direction(search.best.effectiveAlternate,search.best.x[1],search.best.y[1]));
        result.alternateResidualDegrees=static_cast<float>(search.best.errors[1]);
        result.alternateSeed=search.best.seed[1];
    }
    result.evaluations=search.count;
    result.valid=true;
    result.reason=search.best.error<=.0002?"self-consistent spread compensation":
        search.Satisfied()?"requested spread residual reached":"best sampled spread compensation";
    return result;
}

AimSpreadSearchResult AimSearchSpread(const AimSpreadSearchInput& input,
    AimSpreadSample callback,void* context) noexcept {
    if (!callback || input.variants!=1) return {};
    struct LegacyContext { AimSpreadSample callback; void* context; } legacy{callback,context};
    const auto bridge=[](void* opaque,const AimAngles& effective,unsigned,
        double& x,double& y,std::uint32_t& seed) noexcept {
        const auto& state=*static_cast<LegacyContext*>(opaque);
        return state.callback(state.context,effective,x,y,seed);
    };
    return AimSearchSpread(input,static_cast<AimSpreadVariantSample>(bridge),&legacy);
}
