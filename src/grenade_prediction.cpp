#include "grenade_prediction.h"
#include <algorithm>
#include <cmath>

namespace {
bool Finite(const EspVec3& p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        std::abs(p.x) < 1000000 && std::abs(p.y) < 1000000 && std::abs(p.z) < 1000000;
}
EspVec3 Add(EspVec3 a, EspVec3 b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
EspVec3 Scale(EspVec3 a,float s) noexcept { return {a.x*s,a.y*s,a.z*s}; }
float Dot(EspVec3 a, EspVec3 b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
bool RestKind(GrenadeKind k) noexcept { return k==GrenadeKind::Smoke || k==GrenadeKind::Decoy; }
bool FireKind(GrenadeKind k) noexcept { return k==GrenadeKind::Molotov || k==GrenadeKind::Incendiary; }
bool Valid(const GrenadeState& s,const GrenadePhysics& p) noexcept {
    return static_cast<unsigned>(s.kind)<=static_cast<unsigned>(GrenadeKind::Incendiary) &&
        Finite(s.position) && Finite(s.velocity) && std::isfinite(s.age) && s.age>=0 && s.age<1000000 &&
        std::isfinite(p.gravity) && p.gravity>=0 && p.gravity<=100000 &&
        std::isfinite(p.elasticity) && p.elasticity>=0 && p.elasticity<=1 &&
        std::isfinite(p.elasticityLimit) && p.elasticityLimit>=0 && p.elasticityLimit<=1 &&
        std::isfinite(p.overbounce) && p.overbounce>=0 && p.overbounce<=4 &&
        std::isfinite(p.clipEpsilon) && p.clipEpsilon>=0 && p.clipEpsilon<=1 &&
        std::isfinite(p.groundSpeedScale) && p.groundSpeedScale>=0 && p.groundSpeedScale<=1 &&
        std::isfinite(p.stopSpeed) && p.stopSpeed>=0 && p.stopSpeed<=10000 &&
        std::isfinite(p.restSpeed) && p.restSpeed>=0 && p.restSpeed<=10000 &&
        std::isfinite(p.fuseTime) && p.fuseTime<=120 &&
        std::isfinite(p.minRestTime) && p.minRestTime>=0 && p.minRestTime<=120 &&
        std::isfinite(p.fireNormalZ) && p.fireNormalZ>=-1 && p.fireNormalZ<=1 &&
        std::isfinite(p.groundNormalZ) && p.groundNormalZ>=0 && p.groundNormalZ<=1 &&
        std::isfinite(p.softGroundNormalZ) && p.softGroundNormalZ>=0 && p.softGroundNormalZ<=1 &&
        std::isfinite(p.fastSpeed) && p.fastSpeed>=0 && p.fastSpeed<=10000 &&
        std::isfinite(p.fastNormalDot) && p.fastNormalDot>=-1 && p.fastNormalDot<=1 &&
        std::isfinite(p.fastDampingBias) && p.fastDampingBias>=0 && p.fastDampingBias<=2 &&
        std::isfinite(p.thinkInterval) && p.thinkInterval>=0 && p.thinkInterval<=10 &&
        std::isfinite(p.firstThinkTime) && p.firstThinkTime>=0 && p.firstThinkTime<=120 &&
        std::isfinite(p.collisionSkin) && p.collisionSkin>=0 && p.collisionSkin<=1 &&
        std::isfinite(p.step) && p.step>=0.0001f && p.step<=0.1f &&
        std::isfinite(p.maxTime) && p.maxTime>0 && p.maxTime<=30 &&
        p.maxSteps>0 && p.maxSteps<=4096 && p.maxBounces>0 && p.maxBounces<=128;
}
}

GrenadePath GrenadeSimulate(const GrenadeState& initial,const GrenadePhysics& p,
    GrenadeTraceFn trace,void* context) {
    GrenadePath out;
    out.kind=initial.kind;
    if (!trace || !Valid(initial,p)) return out;
    EspVec3 position=initial.position,velocity=initial.velocity;
    float age=initial.age,elapsed=0;
    bool grounded=false;
    EspVec3 contactNormal{};
    bool haveContact=false;
    double thinkIndex=0;
    if (p.thinkInterval>0 && age>p.firstThinkTime)
        thinkIndex=std::ceil((static_cast<double>(age)-p.firstThinkTime)/p.thinkInterval-0.000001);
    auto NextThink=[&]() { return static_cast<float>(p.firstThinkTime+thinkIndex*p.thinkInterval); };
    std::size_t bounces=0;
    out.nodes.reserve(std::min<std::size_t>(p.maxSteps+p.maxBounces+1,4225));
    out.nodes.push_back({position,age,false});
    auto Finish=[&](GrenadeEnd end) {
        out.end=end; out.finalPosition=position; out.finalAge=age;
    };
    for (std::size_t step=0;step<p.maxSteps;++step) {
        const bool poll=p.thinkInterval==0 || age>=NextThink();
        const bool expired=p.strictFuse?age>p.fuseTime:age>=p.fuseTime;
        if (poll && p.fuseTime>=0 && expired) { Finish(GrenadeEnd::Detonated); return out; }
        if (poll && RestKind(initial.kind) && age>=p.minRestTime && Dot(velocity,velocity)<=p.restSpeed*p.restSpeed) {
            Finish(GrenadeEnd::Settled); return out;
        }
        if (poll && p.thinkInterval>0) ++thinkIndex;
        if (elapsed>=p.maxTime) { Finish(GrenadeEnd::Budget); return out; }
        float remaining=std::min(p.step,p.maxTime-elapsed);
        if (p.thinkInterval>0) remaining=std::min(remaining,std::max(0.0f,NextThink()-age));
        else if (p.fuseTime>=0 && !p.strictFuse) remaining=std::min(remaining,p.fuseTime-age);
        const float stepEndAge=age+remaining,stepEndElapsed=elapsed+remaining;
        // A stopped grenade stays on its support until its fuse/rest check.
        if (grounded && Dot(velocity,velocity)==0) {
            age=stepEndAge; elapsed=stepEndElapsed;
            continue;
        }
        unsigned collisions=0;
        bool firstSegment=true;
        while (remaining>0) {
            EspVec3 delta=Scale(velocity,remaining);
            if (!p.nativeStepGravity || firstSegment) delta.z-=0.5f*p.gravity*remaining*remaining;
            if (p.nativeStepGravity && firstSegment) velocity.z-=p.gravity*remaining;
            firstSegment=false;
            const EspVec3 destination=Add(position,delta);
            if (!Finite(destination)) { Finish(GrenadeEnd::Invalid); return out; }
            GrenadeTrace hit;
            bool traced=trace(context,position,destination,hit);
            // Some hull queries report a touching plane again while moving
            // away. Retry that one contact from a query-only separation; a
            // failed retry remains an unknown endpoint, never a landing.
            if (traced && !hit.startSolid && hit.fraction==0 && haveContact && p.collisionSkin>0 &&
                Finite(hit.normal) && Dot(hit.normal,contactNormal)>.99f && Dot(delta,contactNormal)>=0) {
                const auto offset=Scale(contactNormal,p.collisionSkin);
                hit={}; traced=trace(context,Add(position,offset),Add(destination,offset),hit);
            }
            if (!traced || hit.startSolid ||
                !std::isfinite(hit.fraction) || hit.fraction<0 || hit.fraction>1 ||
                !std::isfinite(hit.elasticity) || hit.elasticity<0 || hit.elasticity>1) {
                Finish(GrenadeEnd::TraceFailure); return out;
            }
            const float consumed=remaining*hit.fraction;
            position=Add(position,Scale(delta,hit.fraction));
            if (!p.nativeStepGravity) velocity.z-=p.gravity*consumed;
            age+=consumed; elapsed+=consumed;
            remaining-=consumed;
            if (hit.fraction==1) break;
            if (!Finite(hit.normal)) { Finish(GrenadeEnd::TraceFailure); return out; }
            const float normalLength=std::sqrt(Dot(hit.normal,hit.normal));
            if (normalLength<0.5f || normalLength>1.5f) { Finish(GrenadeEnd::TraceFailure); return out; }
            const EspVec3 normal=Scale(hit.normal,1/normalLength);
            out.nodes.push_back({position,age,true});
            if (FireKind(initial.kind) && normal.z>=p.fireNormalZ) {
                Finish(GrenadeEnd::Detonated); return out;
            }
            if (++bounces>p.maxBounces || ++collisions>8) { Finish(GrenadeEnd::Budget); return out; }
            const float incoming=Dot(velocity,normal);
            if (incoming>=0 && hit.fraction==0) { Finish(GrenadeEnd::TraceFailure); return out; }
            const float adjustment=std::max(-incoming*p.overbounce,0.0f)+p.clipEpsilon;
            velocity=Scale(Add(velocity,Scale(normal,adjustment)),
                std::clamp(p.elasticity*hit.elasticity,0.0f,p.elasticityLimit));
            const float speedSquared=Dot(velocity,velocity);
            haveContact=true; contactNormal=normal;
            // A positive stopSpeed is an explicit adapter-selected ground rule.
            grounded=normal.z>p.groundNormalZ ||
                (normal.z>p.softGroundNormalZ && speedSquared<p.stopSpeed*p.stopSpeed);
            if (grounded) {
                if (p.fastSpeed>0 && speedSquared>p.fastSpeed*p.fastSpeed) {
                    const float d=Dot(velocity,normal)/std::sqrt(speedSquared);
                    if (d>p.fastNormalDot) velocity=Scale(velocity,std::max(0.0f,p.fastDampingBias-d));
                }
                velocity=Scale(velocity,p.groundSpeedScale);
                if (speedSquared<p.stopSpeed*p.stopSpeed) velocity={};
            }
            if (!Finite(velocity)) { Finish(GrenadeEnd::Invalid); return out; }
            if (grounded && Dot(velocity,velocity)==0) {
                age+=remaining; elapsed+=remaining; remaining=0;
                break;
            }
        }
        // Split collision fractions can otherwise leave an artificial residual
        // smaller than one float ulp and schedule a duplicate native sweep.
        age=stepEndAge; elapsed=stepEndElapsed;
        out.nodes.push_back({position,age,false});
    }
    // A deadline reached on the last permitted step is still a known endpoint.
    const bool poll=p.thinkInterval==0 || age>=NextThink();
    if (poll && p.fuseTime>=0 && (p.strictFuse?age>p.fuseTime:age>=p.fuseTime)) Finish(GrenadeEnd::Detonated);
    else if (poll && RestKind(initial.kind) && age>=p.minRestTime && Dot(velocity,velocity)<=p.restSpeed*p.restSpeed)
        Finish(GrenadeEnd::Settled);
    else Finish(GrenadeEnd::Budget);
    return out;
}
