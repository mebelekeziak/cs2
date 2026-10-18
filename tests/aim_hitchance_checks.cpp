#include "aim_hitchance.h"
#include "silent_aim_target.h"
#include "aim_geometry_offsets.h"
#include "silent_aim_hitbox_offsets.h"
#include "silent_aim_target_offsets.h"
#include "cs2_offsets.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
int failures = 0, checks = 0;
void Expect(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", label); }
}
bool Near(float a, float b, float tolerance = .0001f) { return std::abs(a - b) <= tolerance; }
template<class T> std::uintptr_t Address(T& value) { return reinterpret_cast<std::uintptr_t>(&value); }
template<class T> void Put(std::uintptr_t at, const T& value) { std::memcpy(reinterpret_cast<void*>(at), &value, sizeof(value)); }

void IntersectionChecks() {
    const AimCapsule sphere{{100, 0, 0}, {100, 0, 0}, 5};
    Expect(AimRayIntersectsCapsule({}, {1,0,0}, 200, sphere), "zero-length capsule is a sphere");
    Expect(AimRayIntersectsCapsule({}, {100,0,0}, 200, sphere), "direction normalization preserves world range");
    Expect(!AimRayIntersectsCapsule({}, {-1,0,0}, 200, sphere), "capsule behind the shooter is rejected");
    Expect(!AimRayIntersectsCapsule({}, {1,0,0}, 94.9f, sphere), "range ends before near sphere boundary");
    Expect(AimRayIntersectsCapsule({}, {1,0,0}, 95, sphere), "shot endpoint on sphere boundary is included");
    Expect(AimRayIntersectsCapsule({100,0,0}, {0,1,0}, 1, sphere), "shot starting inside sphere intersects");
    Expect(!AimRayIntersectsCapsule({}, {}, 1000, sphere), "zero shot direction fails");
    const AimCapsule vertical{{100,0,-10}, {100,0,10}, 5};
    Expect(AimRayIntersectsCapsule({}, {1,0,0}, 200, vertical), "capsule cylinder body intersects");
    Expect(AimRayIntersectsCapsule({0,5,0}, {1,0,0}, 200, vertical), "cylinder tangent is included");
    Expect(!AimRayIntersectsCapsule({0,5.01f,0}, {1,0,0}, 200, vertical), "outside cylinder tangent misses");
    Expect(AimRayIntersectsCapsule({0,0,14}, {1,0,0}, 200, vertical), "spherical capsule endcap intersects");
    Expect(!AimRayIntersectsCapsule({0,0,15.01f}, {1,0,0}, 200, vertical), "outside capsule endcap misses");
    Expect(AimRayIntersectsCapsule({100,0,-100}, {0,0,1}, 200, vertical), "parallel coaxial capsule ray intersects");
    Expect(!AimRayIntersectsCapsule({106,0,-100}, {0,0,1}, 200, vertical), "parallel separated capsule ray misses");
    Expect(AimRayIntersectsCapsule({100,5,-100}, {0,0,1}, 200, vertical), "parallel tangent capsule ray intersects");
    Expect(!AimRayIntersectsCapsule({}, {1,0,0}, 0, vertical), "zero weapon range fails");
    AimCapsule invalid = vertical;
    invalid.radius = -1;
    Expect(!AimRayIntersectsCapsule({}, {1,0,0}, 200, invalid), "negative capsule radius fails");
    invalid.radius = std::numeric_limits<float>::quiet_NaN();
    Expect(!AimRayIntersectsCapsule({}, {1,0,0}, 200, invalid), "nonfinite capsule radius fails");
    invalid = vertical; invalid.start.x = std::numeric_limits<float>::infinity();
    Expect(!AimRayIntersectsCapsule({}, {1,0,0}, 200, invalid), "nonfinite capsule endpoint fails");
    for (int angle = 0; angle < 360; angle += 5) {
        const float rad = angle * .01745329251994329577f;
        const AimVector direction{std::cos(rad), std::sin(rad), 0};
        const AimCapsule rotated{{100*direction.x,100*direction.y,-10}, {100*direction.x,100*direction.y,10},5};
        Expect(AimRayIntersectsCapsule({},direction,200,rotated), "rotational invariance of capsule intersection");
    }
}

void SamplingChecks() {
    AimHitchanceInput input{};
    input.capsule = {{1000,0,0}, {1000,0,0}, 10}; input.range = 2000; input.samples = 1024;
    auto result = AimEstimateHitchance(input);
    Expect(result.valid && result.samples == 1024 && result.hits == 1024 && result.percent == 100,
        "zero spread centered target gives all hits and evaluates all samples");
    input.angles.yaw = 5;
    result = AimEstimateHitchance(input);
    Expect(result.valid && result.hits == 0 && result.samples == 1024, "zero spread off-target gives zero hits without early exit");
    input.angles = {}; input.inaccuracy = .02f;
    result = AimEstimateHitchance(input);
    // Sphere angular silhouette has tangent-plane radius R/sqrt(D^2-R^2).
    const float tangentRadius = 10.0f/std::sqrt(1000.0f*1000.0f-10.0f*10.0f);
    const float expected = 100*tangentRadius/input.inaccuracy;
    Expect(result.valid && Near(result.percent,expected,.15f), "uniform radial inaccuracy matches analytic sphere probability, not uniform-area disk");
    const auto repeated = AimEstimateHitchance(input);
    Expect(result.hits == repeated.hits && result.percent == repeated.percent,
        "identical copied inputs have deterministic results without global RNG state");
    input.spread = input.inaccuracy; input.inaccuracy = 0;
    result = AimEstimateHitchance(input);
    Expect(result.valid && Near(result.percent,expected,.2f), "independent weapon-spread radius has the same uniform law");
    input.inaccuracy = .01f; input.spread = .01f; input.forceMaxRadius = true;
    result = AimEstimateHitchance(input);
    const float forcedExpected = static_cast<float>(200*std::asin(tangentRadius/.02f)/3.14159265358979323846);
    Expect(result.valid && Near(result.percent,forcedExpected,1.2f), "two fixed radii match analytic independent-angle circular convolution");
    input.forceMaxRadius = false; input.inaccuracy = .02f; input.spread = 0; input.negev = true; input.recoilIndex = 0;
    result = AimEstimateHitchance(input);
    const float negevExpected = 100*(1-std::pow(1-tangentRadius/input.inaccuracy,1.0f/8));
    Expect(result.valid && Near(result.percent,negevExpected,.15f), "early Negev applies all three repeated radius squares before inversion");
    input.recoilIndex = 2.5f;
    result = AimEstimateHitchance(input);
    const float negevLateExpected = 100*(1-std::sqrt(1-tangentRadius/input.inaccuracy));
    Expect(result.valid && Near(result.percent,negevLateExpected,.15f), "fractional Negev recoil applies native do-while boundary");
    input.recoilIndex = 3;
    result = AimEstimateHitchance(input);
    Expect(result.valid && Near(result.percent,expected,.15f), "Negev recoil at three resumes ordinary radial law");
    input.forceMaxRadius = true; input.recoilIndex = 0;
    Expect(AimEstimateHitchance(input).hits == 0, "forced maximum radius overrides early Negev shaping");
    input.negev = false; input.forceMaxRadius = false; input.fixedSpreadAngle = true;
    input.capsule = {{1000,0,10}, {1000,0,10}, 1};
    result = AimEstimateHitchance(input);
    Expect(result.valid && result.percent > 9 && result.percent < 11,
        "native fixed-angle override sends positive tangent-plane Y along up");
    input.forceMaxRadius=true; input.inaccuracy=.5f; input.range=1000;
    input.capsule={{1000,0,500},{1000,0,500},1};
    Expect(AimEstimateHitchance(input).hits==1024,
        "native range multiplies raw spread direction and preserves its off-axis endpoint");
    input.capsule={{1002,0,501},{1002,0,501},.1f};
    Expect(AimEstimateHitchance(input).hits==0,"native finite raw spread segment stops at its actual endpoint");
    input.forceMaxRadius=false;
    input.fixedSpreadAngle = false; input.inaccuracy = 0; input.capsule = {{0,0,-100}, {0,0,-100},5}; input.angles = {92,0,0};
    Expect(AimEstimateHitchance(input).hits == 1024, "recoil angles beyond view pitch limits remain valid shot directions");
    input.angles = {}; input.capsule = {{1000,0,0}, {1000,0,0},10}; input.range = 900;
    Expect(AimEstimateHitchance(input).hits == 0, "sampling obeys current weapon range");
    input.range = 2000; input.inaccuracy = .01f; input.spread = .02f;
    // Independently integrate the relative-angle CDF over a dense radius grid.
    double integral = 0;
    constexpr int grid = 512;
    for (int i = 0; i < grid; ++i) for (int j = 0; j < grid; ++j) {
        const double a = input.inaccuracy * (i+.5)/grid, b = input.spread * (j+.5)/grid;
        const double q = tangentRadius;
        integral += q >= a+b ? 1 : q <= std::abs(a-b) ? 0 :
            1-std::acos((q*q-a*a-b*b)/(2*a*b))/3.14159265358979323846;
    }
    input.samples = 65536;
    result = AimEstimateHitchance(input);
    Expect(result.valid && Near(result.percent,static_cast<float>(100*integral/(grid*grid)),.3f),
        "four-dimensional sampler agrees with independent radius/relative-angle integration");
    for (unsigned samples : {64u,128u,256u,512u,1024u}) {
        input.samples = samples; result = AimEstimateHitchance(input);
        Expect(result.valid && result.samples == samples && result.hits <= result.samples && result.percent >= 0 && result.percent <= 100,
            "all supported interactive sample budgets report complete bounded counts");
    }
    input.samples = 0; Expect(!AimEstimateHitchance(input).valid, "zero sample count is unavailable");
    input.samples = 65537; Expect(!AimEstimateHitchance(input).valid, "unbounded sample count is unavailable");
    input.samples = 256; input.inaccuracy = -1; Expect(!AimEstimateHitchance(input).valid, "negative inaccuracy is unavailable");
    input.inaccuracy = .01f; input.spread = std::numeric_limits<float>::quiet_NaN();
    Expect(!AimEstimateHitchance(input).valid, "nonfinite live spread is unavailable");
    input.spread = .01f; input.recoilIndex = -1; Expect(!AimEstimateHitchance(input).valid, "negative recoil index is unavailable");
    input.recoilIndex = 0; input.angles.pitch = std::numeric_limits<float>::infinity();
    Expect(!AimEstimateHitchance(input).valid, "nonfinite combined shot angles are unavailable");
}

namespace ent = cs2_offsets;
namespace geom = aim_geometry_offsets;
namespace bone = silent_aim_hitbox_offsets;
struct ActorStorage {
    std::array<unsigned char,0x1000> controller{};
    std::array<unsigned char,0x2100> pawn{};
    std::array<unsigned char,0x500> scene{};
    std::array<unsigned char,0x80> collision{};
};
struct GeometryFixture {
    std::array<unsigned char,0x2200> system{};
    std::vector<unsigned char> bucket = std::vector<unsigned char>(512*ent::entity_layout::IdentityStride);
    ActorStorage local{}, enemy{};
    std::array<unsigned char,0x400> model{};
    std::array<unsigned char,0x200> mesh{};
    std::array<unsigned char,2*0x48> sets{};
    std::array<unsigned char,3*0x70> boxes{};
    std::array<std::uintptr_t,16> vtable{};
    std::array<float,8> pose{1000,0,64,1,0,0,0,1};
    std::uintptr_t modelPointer = 0, meshPointer = 0;
    std::uint64_t meshMask = 1;
    std::array<char,32> setName{}, headName{}, chestName{};
    AimSource source{};
    AimTarget target{};
    AimActor Actor(ActorStorage& storage, unsigned index, unsigned team) {
        const auto controller = Address(storage.controller), pawn = Address(storage.pawn), scene = Address(storage.scene);
        const auto identity = reinterpret_cast<std::uintptr_t>(bucket.data()) + index*ent::entity_layout::IdentityStride;
        const auto pawnIdentity = identity + ent::entity_layout::IdentityStride;
        const std::uint32_t controllerHandle = 0x8000|index, pawnHandle = 0x8000|(index+1);
        Put(identity,controller); Put(identity+ent::entity_layout::HandleOffset,controllerHandle);
        Put(pawnIdentity,pawn); Put(pawnIdentity+ent::entity_layout::HandleOffset,pawnHandle);
        Put(controller+ent::CEntityInstance::m_pEntity,identity); Put(pawn+ent::CEntityInstance::m_pEntity,pawnIdentity);
        Put(controller+ent::CCSPlayerController::m_hPlayerPawn,pawnHandle);
        Put(pawn+ent::C_BasePlayerPawn::m_hController,controllerHandle);
        Put(pawn+ent::C_BaseEntity::m_iHealth,100);
        Put(pawn+ent::C_BaseEntity::m_iTeamNum,static_cast<std::uint8_t>(team));
        Put(pawn+ent::C_BaseEntity::m_pGameSceneNode,scene);
        Put(pawn+ent::C_BaseEntity::m_pCollision,Address(storage.collision));
        Put(scene+ent::CGameSceneNode::m_flAbsScale,1.0f);
        Put(pawn+silent_aim_target_offsets::ViewOffset,AimVector{0,0,64});
        AimActor result{};
        result.controller=controller; result.pawn=pawn; result.scene=scene; result.collision=Address(storage.collision);
        result.controllerHandle=controllerHandle; result.pawnHandle=pawnHandle; result.team=static_cast<std::uint8_t>(team);
        return result;
    }
    GeometryFixture() {
        Put(Address(system)+ent::entity_layout::TableOffset,reinterpret_cast<std::uintptr_t>(bucket.data()));
        target.local=Actor(local,1,3); target.enemy=Actor(enemy,3,2);
        target.eye={0,0,64}; target.point={1000,0,64}; target.solution.valid=true;
        source={Address(system),Address(local.controller),Address(local.pawn)};
        modelPointer=Address(model); meshPointer=Address(mesh);
        target.enemy.modelHandle=Address(modelPointer); target.enemy.model=modelPointer;
        target.enemy.boneArray=Address(pose); target.enemy.boneIndex=0;
        target.enemy.skeletonVtable=Address(vtable); target.enemy.skeletonQuery=0x1234;
        vtable[bone::SkeletonQuerySlot/sizeof(std::uintptr_t)]=target.enemy.skeletonQuery;
        Put(Address(enemy.scene),Address(vtable));
        Put(Address(enemy.scene)+bone::ModelState+bone::ModelHandle,Address(modelPointer));
        Put(Address(enemy.scene)+bone::BoneArray,Address(pose)); Put(Address(model)+bone::BoneCount,1);
        Put(Address(model)+geom::MeshCount,1); Put(Address(model)+geom::MeshArray,Address(meshPointer));
        Put(Address(model)+geom::MeshMasks,Address(meshMask)); Put(Address(enemy.scene)+geom::MeshGroup,std::uint64_t{1});
        Put(Address(mesh)+geom::SetCount,2); Put(Address(mesh)+geom::SetStorage,std::uint32_t{2});
        Put(Address(mesh)+geom::SetArray,Address(sets));
        strcpy_s(setName.data(),setName.size(),"cstrike");
        strcpy_s(headName.data(),headName.size(),"head_0");
        strcpy_s(chestName.data(),chestName.size(),"spine_3");
        for(int i=0;i<2;++i) {
            const auto set=Address(sets)+i*geom::SetStride+geom::SetValue;
            Put(set+geom::SetName,Address(setName)); Put(set+geom::BoxCount,2); Put(set+geom::BoxArray,Address(boxes));
        }
        Put(Address(boxes)+geom::BoneName,Address(headName));
        const auto box=Address(boxes)+geom::BoxStride;
        Put(box+geom::BoneName,Address(chestName)); Put(box+geom::Minimum,AimVector{2.5f,-.6f,-6});
        Put(box+geom::Maximum,AimVector{2.5f,-.6f,6}); Put(box+geom::Radius,5.0f);
        Put(box+geom::Shape,std::uint8_t{2});
    }
    bool Read(AimTargetVolume& volume,const char*& reason) { return SilentAimReadTargetVolume(source,target,volume,reason); }
};

void GeometryChecks() {
    GeometryFixture fixture;
    AimTargetVolume volume{};
    const char* reason="";
    Expect(fixture.Read(volume,reason) && volume.valid && !std::strcmp(reason,"ready"), "current authored selected capsule can be copied");
    Expect(Near(volume.capsule.start.x,1002.5f) && Near(volume.capsule.start.y,-.6f) && Near(volume.capsule.start.z,58) &&
        Near(volume.capsule.end.z,70) && Near(volume.capsule.radius,5), "authored endpoints/radius replace guessed bone-centered geometry");
    Expect(Near(volume.center.x,1002.5f) && Near(volume.center.z,64) && volume.box==Address(fixture.boxes)+geom::BoxStride,
        "selected bone name finds its own record and authored capsule center");
    Expect(SilentAimTargetVolumeValid(fixture.source,fixture.target,volume), "fresh resource and pose snapshot validates");
    const auto enemy=Address(fixture.enemy.scene), mesh=Address(fixture.mesh), box=volume.box;
    const auto mutate=[&](auto at,auto changed,auto original,const char* label) {
        Put(at,changed); Expect(!SilentAimTargetVolumeValid(fixture.source,fixture.target,volume),label); Put(at,original);
    };
    mutate(enemy+geom::MeshGroup,std::uint64_t{2},std::uint64_t{1},"mesh group change invalidates copied geometry");
    mutate(enemy+geom::HitboxSet,std::uint8_t{1},std::uint8_t{0},"current hitbox set change invalidates geometry");
    mutate(mesh+geom::SetArray,std::uintptr_t{0},Address(fixture.sets),"resource set storage replacement invalidates geometry");
    mutate(mesh+geom::SetStorage,std::uint32_t{0},std::uint32_t{2},"resource storage flag invalidates geometry");
    mutate(box+geom::Radius,10.0f,5.0f,"in-place authored radius change invalidates geometry");
    mutate(Address(fixture.model)+bone::BoneCount,0,1,"changed model bone count invalidates geometry");
    mutate(Address(fixture.local.pawn)+silent_aim_target_offsets::ViewOffset,AimVector{0,0,32},AimVector{0,0,64},
        "changed local eye invalidates old shot geometry");
    fixture.pose[4]=.1f;
    Expect(!SilentAimTargetVolumeValid(fixture.source,fixture.target,volume),"same-pointer pose orientation mutation invalidates geometry");
    fixture.pose[4]=0;
    fixture.pose[0]+=1;
    Expect(!SilentAimTargetValid(fixture.source,fixture.target),"same-pointer pose position mutation invalidates target candidate");
    fixture.pose[0]-=1;
    fixture.chestName[0]='x';
    Expect(!SilentAimTargetVolumeValid(fixture.source,fixture.target,volume),"in-place resource bone name change invalidates selection");
    fixture.chestName[0]='s';
    fixture.setName[0]='x';
    Expect(!SilentAimTargetVolumeValid(fixture.source,fixture.target,volume),"in-place damage set name change invalidates geometry");
    fixture.setName[0]='c';
    fixture.meshMask=2;
    Expect(!fixture.Read(volume,reason) && !volume.valid && SilentAimTargetValid(fixture.source,fixture.target),
        "disabled mesh cannot supply hitchance but preserves manual landmark target");
    fixture.meshMask=1;
    Put(box+geom::Shape,std::uint8_t{1});
    Expect(!fixture.Read(volume,reason) && !volume.valid && !std::strcmp(reason,"unsupported hitbox shape/space"),
        "unverified box shape has no fabricated capsule fallback");
    Put(box+geom::Shape,std::uint8_t{2}); Put(box+geom::AlternateTransform,std::uint8_t{1});
    Expect(!fixture.Read(volume,reason),"alternate scene transform is rejected without adopting bone-space transform");
    Put(box+geom::AlternateTransform,std::uint8_t{0}); Put(box+geom::TranslationOnly,std::uint8_t{1});
    Expect(!fixture.Read(volume,reason),"translation-only metadata is explicitly unavailable");
    Put(box+geom::TranslationOnly,std::uint8_t{0}); fixture.pose[3]=2;
    Expect(!fixture.Read(volume,reason) && !std::strcmp(reason,"unsupported scaled hitbox pose"),
        "unverified scaled-player radius behavior is explicitly unavailable");
    fixture.pose[3]=1; fixture.pose[7]=0;
    Expect(!fixture.Read(volume,reason),"malformed quaternion cannot manufacture geometry");
    const float rootHalf=std::sqrt(.5f);
    fixture.pose[6]=rootHalf; fixture.pose[7]=rootHalf;
    Expect(fixture.Read(volume,reason) && Near(volume.center.x,1000.6f) && Near(volume.center.y,2.5f),
        "animated bone quaternion rotates authored local endpoints");
    Put(enemy+geom::HitboxSet,std::uint8_t{1});
    Expect(fixture.Read(volume,reason) && volume.setIndex==1,
        "current selected hitbox set is read each time instead of assuming first set");
    Put(enemy+geom::HitboxSet,std::uint8_t{2});
    Expect(!fixture.Read(volume,reason),"out-of-range selected hitbox set is unavailable");
    Put(enemy+geom::HitboxSet,std::uint8_t{0});
    const auto pawnIdentity=reinterpret_cast<std::uintptr_t>(fixture.bucket.data())+4*ent::entity_layout::IdentityStride;
    Put(pawnIdentity+ent::entity_layout::HandleOffset,std::uint32_t{0x10004});
    Expect(!fixture.Read(volume,reason) && !std::strcmp(reason,"stale target"),"recycled pawn handle cannot reuse previous geometry");
}
}

int main() {
    IntersectionChecks(); SamplingChecks(); GeometryChecks();
    std::printf("Hitchance checks: %d; failures: %d\n",checks,failures);
    return failures ? 1 : 0;
}
