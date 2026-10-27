#include "grenade_prediction.h"
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
int checks=0,failures=0;
void Expect(bool ok,const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n",label); }
}
bool Near(float a,float b,float epsilon=0.001f) { return std::abs(a-b)<epsilon; }
bool Open(void*,const EspVec3&,const EspVec3&,GrenadeTrace& hit) noexcept {
    hit={}; return true;
}
struct Floor { unsigned calls=0; float elasticity=1; EspVec3 normal{0,0,1}; };
bool Plane(void* data,const EspVec3& a,const EspVec3& b,GrenadeTrace& hit) noexcept {
    auto& plane=*static_cast<Floor*>(data);
    ++plane.calls; hit={};
    if (b.z<0 && a.z>=0) {
        hit.fraction=a.z/(a.z-b.z); hit.normal=plane.normal; hit.elasticity=plane.elasticity;
    }
    return true;
}
bool TouchingPlane(void* data,const EspVec3& a,const EspVec3& b,GrenadeTrace& hit) noexcept {
    if (a.z==0 && b.z>0) {
        ++static_cast<Floor*>(data)->calls;
        hit={}; hit.fraction=0; hit.normal={0,0,1}; return true;
    }
    return Plane(data,a,b,hit);
}
enum class Bad { Failure, Solid, Nan, Range, Normal, Elasticity, ZeroProgress };
bool BadTrace(void* data,const EspVec3&,const EspVec3&,GrenadeTrace& hit) noexcept {
    hit={};
    switch (*static_cast<Bad*>(data)) {
    case Bad::Failure: return false;
    case Bad::Solid: hit.startSolid=true; break;
    case Bad::Nan: hit.fraction=std::numeric_limits<float>::quiet_NaN(); break;
    case Bad::Range: hit.fraction=1.1f; break;
    case Bad::Normal: hit.fraction=.5f; break;
    case Bad::Elasticity: hit.elasticity=-1; break;
    case Bad::ZeroProgress: hit.fraction=0; hit.normal={0,0,1}; break;
    }
    return true;
}
GrenadePhysics Physics() {
    GrenadePhysics p;
    p.gravity=10; p.elasticity=.5f; p.groundNormalZ=.7f; p.fireNormalZ=.7f;
    p.step=.01f; p.fuseTime=1; p.maxTime=3; return p;
}
}
int main() {
    auto p=Physics();
    GrenadeState s{GrenadeKind::HE,{1,2,30},{10,5,20},0};
    auto path=GrenadeSimulate(s,p,Open);
    Expect(path.Terminal() && path.end==GrenadeEnd::Detonated,"timed grenade reaches known detonation endpoint");
    Expect(Near(path.finalPosition.x,11) && Near(path.finalPosition.y,7) && Near(path.finalPosition.z,45),
        "trapezoidal gravity reproduces analytic free-flight parabola");
    Expect(Near(path.finalAge,1),"timer stops exactly at fuse deadline");
    bool monotonic=true; for (std::size_t i=1;i<path.nodes.size();++i) monotonic &= path.nodes[i].time>=path.nodes[i-1].time;
    Expect(monotonic && !path.nodes.empty(),"copied path times are monotonic");
    s.age=.75f; path=GrenadeSimulate(s,p,Open);
    Expect(Near(path.finalPosition.x,3.5f) && Near(path.finalPosition.z,34.6875f),"enemy prediction retains elapsed fuse age");
    Expect(Near(path.finalAge,1),"enemy fuse is not reset to a complete new second");
    s.age=2; path=GrenadeSimulate(s,p,Open);
    Expect(path.Terminal() && path.nodes.size()==1 && Near(path.finalPosition.x,1),"already expired fuse does not move projectile");

    p.gravity=0; p.step=.1f; p.fuseTime=.1f;
    s={GrenadeKind::HE,{0,0,.25f},{10,0,-10},0};
    Floor floor;
    path=GrenadeSimulate(s,p,Plane,&floor);
    Expect(path.Terminal() && floor.calls==2,"collision traces unused part of the same integration step");
    Expect(Near(path.finalPosition.x,.625f) && Near(path.finalPosition.z,.375f),"bounce reflection applies elasticity and fractional remainder");
    unsigned impacts=0; for (const auto& n:path.nodes) impacts+=n.impact?1:0;
    Expect(impacts==1,"path records one exact impact node");
    floor={}; floor.elasticity=.5f;
    path=GrenadeSimulate(s,p,Plane,&floor);
    Expect(Near(path.finalPosition.x,.4375f) && Near(path.finalPosition.z,.1875f),"trace surface elasticity combines with projectile elasticity");

    p.collisionSkin=.001f; floor={};
    path=GrenadeSimulate(s,p,TouchingPlane,&floor);
    Expect(path.Terminal() && floor.calls==3,"outgoing zero-fraction touching contact gets one separated query retry");
    Expect(Near(path.finalPosition.x,.625f) && Near(path.finalPosition.z,.375f),
        "query separation leaves the actual simulated trajectory and terminal endpoint unchanged");

    p=Physics(); p.gravity=10; p.step=.1f; p.fuseTime=.1f;
    p.nativeStepGravity=true; p.clipEpsilon=.03125f;
    floor={};
    path=GrenadeSimulate(s,p,Plane,&floor);
    const float nativeFraction=.25f/1.05f;
    const float nativeRemainder=.1f*(1-nativeFraction);
    Expect(path.Terminal() && floor.calls==2,"native collision step consumes remainder with one additional sweep");
    Expect(Near(path.finalPosition.x,nativeFraction+5*nativeRemainder) &&
        Near(path.finalPosition.z,5.515625f*nativeRemainder),
        "native gravity precedes reflection, normal bias is applied, and remainder receives no second gravity impulse");
    p.gravity=0; p.elasticity=1; p.elasticityLimit=.9f; p.clipEpsilon=0;
    floor={};
    path=GrenadeSimulate(s,p,Plane,&floor);
    Expect(Near(path.finalPosition.x,.925f) && Near(path.finalPosition.z,.675f),
        "adapter elasticity limit clamps combined bounce elasticity");
    p=Physics(); p.gravity=0; p.step=.01f; p.fuseTime=.01f; p.clipEpsilon=.03125f;
    p.fastSpeed=std::sqrt(96000.0f); p.fastNormalDot=.5f; p.fastDampingBias=1.5f; p.stopSpeed=300;
    s={GrenadeKind::HE,{0,0,.5f},{0,0,-1000},0}; floor={};
    path=GrenadeSimulate(s,p,Plane,&floor);
    Expect(path.Terminal() && Near(path.finalPosition.z,250.0078125f*.0095f),
        "fast floor damping uses normalized outgoing direction and pre-damping speed for stop decision");
    p.fastSpeed=0; p.stopSpeed=20; p.softGroundNormalZ=.1f;
    s={GrenadeKind::HE,{0,0,.01f},{-10,0,-10},0}; floor={}; floor.normal={.9797959f,0,.2f};
    path=GrenadeSimulate(s,p,Plane,&floor);
    Expect(path.Terminal() && Near(path.finalPosition.z,0),"slow impact on the soft ground slope stops using adapter thresholds");

    p=Physics(); p.stopSpeed=100; p.minRestTime=.2f; p.restSpeed=.1f;
    s={GrenadeKind::Smoke,{0,0,.25f},{10,0,-10},0}; floor={};
    path=GrenadeSimulate(s,p,Plane,&floor);
    Expect(path.Terminal() && path.end==GrenadeEnd::Settled && path.finalAge>=.2f && path.finalAge<.211f,
        "smoke rests on support until its minimum detonation age");
    Expect(Near(path.finalPosition.z,0),"resting endpoint stays on the support plane");
    s.kind=GrenadeKind::HE;
    path=GrenadeSimulate(s,p,Plane,&floor);
    Expect(path.end==GrenadeEnd::Detonated && Near(path.finalAge,1) && Near(path.finalPosition.z,0),
        "timed grenade settles physically and waits for fuse at the same endpoint");
    s.kind=GrenadeKind::Molotov; p.stopSpeed=0;
    path=GrenadeSimulate(s,p,Plane,&floor);
    Expect(path.end==GrenadeEnd::Detonated && path.finalAge<.1f && Near(path.finalPosition.z,0),
        "fire grenade detonates on an adapter-approved surface slope");
    s.kind=GrenadeKind::Incendiary;
    path=GrenadeSimulate(s,p,Plane,&floor);
    Expect(path.Terminal() && path.finalAge<.1f,"incendiary shares fire impact rule");
    s={GrenadeKind::Decoy,{0,0,1},{0,0,0},.5f};
    path=GrenadeSimulate(s,p,Open);
    Expect(path.end==GrenadeEnd::Settled && path.nodes.size()==1,"decoy uses age-qualified rest rule");

    p=Physics(); p.gravity=0; p.step=.1f; p.thinkInterval=.2f; p.firstThinkTime=.1f;
    p.fuseTime=.5f; p.strictFuse=true;
    s={GrenadeKind::HE,{},{10,0,0},0};
    path=GrenadeSimulate(s,p,Open);
    Expect(path.Terminal() && Near(path.finalAge,.7f) && Near(path.finalPosition.x,7),
        "native strict deadline is checked at explicit think polls rather than continuously");
    s.age=.35f;
    path=GrenadeSimulate(s,p,Open);
    Expect(path.Terminal() && Near(path.finalAge,.7f) && Near(path.finalPosition.x,3.5f),
        "enemy prediction preserves think schedule phase from projectile age");
    s={GrenadeKind::Smoke,{},{},.35f}; p.fuseTime=-1; p.minRestTime=.4f;
    path=GrenadeSimulate(s,p,Open);
    Expect(path.end==GrenadeEnd::Settled && Near(path.finalAge,.5f),"stationary rest detonation waits for next native think poll");
    p=Physics(); p.gravity=0; p.fuseTime=-1; p.restSpeed=.1f; p.minRestTime=1.188f; p.step=1.0f/64;
    s={GrenadeKind::Smoke,{},{},0};
    path=GrenadeSimulate(s,p,Open);
    Expect(path.end==GrenadeEnd::Settled && Near(path.finalAge,1.203125f),
        "native smoke checks each simulation tick and respects its minimum age");
    p.minRestTime=0; p.restSpeed=.2f; p.firstThinkTime=2; p.thinkInterval=.2f;
    s.kind=GrenadeKind::Decoy;
    path=GrenadeSimulate(s,p,Open);
    Expect(path.end==GrenadeEnd::Settled && Near(path.finalAge,2),"native decoy starts rest checks at its initial think time");

    s={GrenadeKind::HE,{0,0,10},{0,0,-10},0};
    for (Bad bad:{Bad::Failure,Bad::Solid,Bad::Nan,Bad::Range,Bad::Normal,Bad::Elasticity}) {
        path=GrenadeSimulate(s,p,BadTrace,&bad);
        Expect(path.end==GrenadeEnd::TraceFailure && !path.Terminal(),"unavailable or invalid native trace never claims final landing");
    }
    auto bad=Bad::ZeroProgress;
    path=GrenadeSimulate(s,p,BadTrace,&bad);
    Expect(!path.Terminal() && path.nodes.size()<=11,"zero-progress trace terminates within collision budget");

    p=Physics(); p.maxSteps=1; p.fuseTime=10;
    path=GrenadeSimulate(s,p,Open);
    Expect(path.end==GrenadeEnd::Budget && !path.Terminal(),"step budget is a partial path rather than a landing");
    p.maxSteps=1024; p.maxTime=.05f;
    path=GrenadeSimulate(s,p,Open);
    Expect(path.end==GrenadeEnd::Budget && !path.Terminal() && Near(path.finalAge,.05f),"time horizon bounds the prediction");
    p=Physics(); p.fuseTime=.0000001f;
    path=GrenadeSimulate(s,p,Open);
    Expect(path.Terminal() && Near(path.finalAge,p.fuseTime,.00000001f),"sub-microsecond deadline does not stall");
    p=Physics(); p.maxSteps=100000;
    Expect(GrenadeSimulate(s,p,Open).end==GrenadeEnd::Invalid,"unbounded caller step count rejects");
    p=Physics(); p.step=0;
    Expect(GrenadeSimulate(s,p,Open).end==GrenadeEnd::Invalid,"zero integration step rejects");
    p=Physics(); p.strictFuse=true; p.nativeStepGravity=true; p.thinkInterval=0;
    p.fuseTime=.125f; p.step=.0625f;
    s={GrenadeKind::Molotov,{},{10,0,0},0};
    path=GrenadeSimulate(s,p,Open);
    Expect(path.Terminal() && Near(path.finalAge,.1875f) && Near(path.finalPosition.x,1.875f),
        "native fire strict fuse polls every physics tick and advances beyond an exactly equal deadline");
    p=Physics(); s.age=-1;
    Expect(GrenadeSimulate(s,p,Open).end==GrenadeEnd::Invalid,"negative projectile age rejects");
    s.age=0; s.velocity.x=std::numeric_limits<float>::infinity();
    Expect(GrenadeSimulate(s,p,Open).end==GrenadeEnd::Invalid,"nonfinite copied inputs reject");
    s.velocity={};
    Expect(GrenadeSimulate(s,p,nullptr).end==GrenadeEnd::Invalid,"missing native trace rejects");
    std::printf("Grenade prediction: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
