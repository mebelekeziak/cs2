#include "grenade_prediction_runtime.h"
#include "grenade_prediction_offsets.h"
#include "cs2_offsets.h"
#include "aim_weapon_offsets.h"
#include "aim_shoot_origin_offsets.h"
#include "third_person_offsets.h"
#include "movement_jumpbug_offsets.h"
#include "movement_offsets.h"
#include "client_frame.h"
#include <Windows.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
namespace off = grenade_prediction_offsets;
namespace ent = cs2_offsets;
namespace weapon = aim_weapon_offsets;
namespace cam = third_person_offsets;
int checks = 0, failures = 0;
bool inCallback = false;
void Check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}
std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
template<class T> void Put(void* target, std::size_t at, T value) {
    std::memcpy(static_cast<unsigned char*>(target) + at, &value, sizeof(value));
}
constexpr std::uint32_t Handle(unsigned index) { return (4u << 15) | index; }
enum class DuringTrace { None, Stop, Death, ReplaceSystem, RetireHeld, RetireProjectile };
struct Fixture;
Fixture* fixture = nullptr;
bool NativeLaunch(void*, void*, EspVec3*, EspVec3*, bool);
float* NativeTime(float*, int);
void NativeShape(void*, const float*);
void NativeResult(void*);
void NativeFilter(void*, void*, std::uint64_t, std::uint8_t, std::uint8_t);
bool NativeTrace(void*, void*, const void*, const void*, void*, void*);
EspVec3* NativeVelocity(void*, EspVec3*);
std::uint64_t NativeBudgetNow();

struct Fixture {
    std::vector<unsigned char> client = std::vector<unsigned char>(ent::ClientImageSize);
    std::array<unsigned char, 0x220> system{}, otherSystem{};
    std::array<unsigned char, 512 * ent::entity_layout::IdentityStride> bucket{};
    std::array<unsigned char, 0x2400> pawn{}, enemy{}, teammate{}, held{}, enemyHeld{};
    std::array<unsigned char, 0x1000> controller{};
    std::array<unsigned char, 0x1500> projectile{}, secondProjectile{};
    std::array<unsigned char, 0x120> scene{}, secondScene{};
    std::array<unsigned char, 0x80> services{};
    std::array<unsigned char, 0x60> gravityData{};
    std::array<unsigned char, 0x60> molotovFuseData{}, molotovSlopeData{};
    float time = 100.75f;
    EspVec3 launchPosition{0, 0, 200}, launchVelocity{100, 0, 0};
    EspVec3 projectileVelocity{100, 0, 0}, secondVelocity{80, 0, 0};
    int launches = 0, traces = 0, filters = 0, shapes = 0;
    int velocityCalls = 0;
    DWORD traceDelayMs = 0;
    std::uint64_t fakeBudgetUs = 1;
    bool negativeGroupSeen = false;
    bool failFrameClock = false;
    int failedClockCalls = 0;
    bool nativeArguments = true, launchWrites = true;
    DuringTrace duringTrace = DuringTrace::None;
    std::uintptr_t ignored = 0;
    static constexpr unsigned Pawn = 1, Controller = 2, Held = 3, Enemy = 4,
        EnemyHeld = 5, Projectile = 6, Teammate = 7, SecondProjectile = 8;
    void Bind(unsigned index, void* object, std::ptrdiff_t table = 0) {
        auto* identity = bucket.data() + index * ent::entity_layout::IdentityStride;
        Put(identity, 0, Address(object));
        Put(identity, ent::entity_layout::HandleOffset, Handle(index));
        Put(identity, off::IdentityWorldGroup, int{0});
        Put(object, ent::CEntityInstance::m_pEntity, Address(identity));
        Put(object, 0, Address(client.data()) + table);
    }
    void Reset() {
        GrenadePredictionStop(); inCallback = false; fixture = this;
        std::fill(client.begin(), client.end(), static_cast<unsigned char>(0));
        system.fill(0); otherSystem.fill(0); bucket.fill(0);
        pawn.fill(0); enemy.fill(0); teammate.fill(0); held.fill(0); enemyHeld.fill(0);
        controller.fill(0); projectile.fill(0); scene.fill(0); services.fill(0);
        secondProjectile.fill(0); secondScene.fill(0);
        gravityData.fill(0);
        molotovFuseData.fill(0); molotovSlopeData.fill(0);
        time = 100.75f; launches = traces = filters = shapes = velocityCalls = 0; negativeGroupSeen = false; traceDelayMs = 0;
        fakeBudgetUs = 1;
        failFrameClock = false; failedClockCalls = 0;
        nativeArguments = launchWrites = true; duringTrace = DuringTrace::None; ignored = 0;
        launchPosition = {0, 0, 200}; launchVelocity = {100, 0, 0};
        projectileVelocity = {100, 0, 0}; secondVelocity = {80, 0, 0};
        Put(client.data(), ent::globals::dwGameEntitySystem, Address(system.data()));
        Put(client.data(), ent::globals::dwLocalPlayerPawn, Address(pawn.data()));
        Put(client.data(), ent::globals::dwLocalPlayerController, Address(controller.data()));
        Put(client.data(), cam::TraceManager, Address(system.data()));
        Put(client.data(), movement_offsets::TickIntervalConstant, float{1.0f/64});
        Put(client.data(), movement_jumpbug_offsets::Gravity + weapon::CvarValuePointer, Address(gravityData.data()));
        Put(gravityData.data(), weapon::CvarValue, float{800});
        Put(client.data(), off::MolotovFuse + weapon::CvarValuePointer, Address(molotovFuseData.data()));
        Put(molotovFuseData.data(), weapon::CvarValue, float{3.5f});
        Put(client.data(), off::MolotovMaxSlope + weapon::CvarValuePointer, Address(molotovSlopeData.data()));
        Put(molotovSlopeData.data(), weapon::CvarValue, float{30});
        Put(system.data(), ent::entity_layout::TableOffset, Address(bucket.data()));
        Bind(Pawn, pawn.data(), aim_shoot_origin_offsets::PawnVtable); Bind(Controller, controller.data());
        Bind(Held, held.data(), off::WeaponTables[0].rva);
        Bind(Enemy, enemy.data(), aim_shoot_origin_offsets::PawnVtable);
        Bind(Teammate, teammate.data(), aim_shoot_origin_offsets::PawnVtable);
        Bind(EnemyHeld, enemyHeld.data(), off::WeaponTables[0].rva);
        Bind(Projectile, projectile.data(), off::ProjectileTables[0].rva);
        Put(controller.data(), ent::CCSPlayerController::m_hPlayerPawn, Handle(Pawn));
        Put(pawn.data(), ent::C_BaseEntity::m_iHealth, int{100});
        Put(pawn.data(), ent::C_BaseEntity::m_iTeamNum, std::uint8_t{2});
        Put(enemy.data(), ent::C_BaseEntity::m_iTeamNum, std::uint8_t{3});
        Put(teammate.data(), ent::C_BaseEntity::m_iTeamNum, std::uint8_t{2});
        Put(pawn.data(), weapon::WeaponServices, Address(services.data()));
        Put(services.data(), 0, Address(client.data()) + weapon::ServicesVtable);
        Put(services.data(), weapon::ServicePawn, Address(pawn.data()));
        Put(services.data(), weapon::ActiveWeapon, Handle(Held));
        Put(held.data(), off::Held, std::uint8_t{1});
        Put(held.data(), off::PinPulled, std::uint8_t{1});
        Put(enemyHeld.data(), off::Held, std::uint8_t{1});
        Put(enemyHeld.data(), off::PinPulled, std::uint8_t{1});
        Put(projectile.data(), off::Thrower, Handle(Enemy));
        Put(projectile.data(), ent::C_BaseEntity::m_pGameSceneNode, Address(scene.data()));
        Put(scene.data(), ent::CGameSceneNode::m_vecAbsOrigin, EspVec3{0, 0, 200});
        Put(projectile.data(), off::AbsVelocity, EspVec3{100, 0, 0});
        Put(projectile.data(), off::ProjectileSpawnTime, float{100});
        Put(projectile.data(), off::DetonateTime, float{101.5f});
        Put(projectile.data(), off::ActualGravityScale, float{.4f});
        Put(projectile.data(), off::GravityScale, float{.4f});
        Put(projectile.data(), off::Elasticity, float{.45f});
        GrenadeSettings settings; settings.prediction = true; settings.enemyPrediction = true;
        GrenadeSetSettings(settings);
        const GrenadePredictionTestNative native{NativeLaunch, NativeTime, NativeShape, NativeResult, NativeFilter, NativeTrace, NativeVelocity, NativeBudgetNow};
        GrenadePredictionTestConfigure(Address(client.data()), native);
    }
    std::vector<GrenadePredictionVisual> Sample() {
        inCallback = true; GrenadePredictionTestFrame(); inCallback = false;
        return GrenadePredictionTestCapture();
    }
    void LocalOnly() { auto s = GrenadeGetSettings(); s.enemyPrediction = false; GrenadeSetSettings(s); }
    void EnemyOnly() { auto s = GrenadeGetSettings(); s.prediction = false; GrenadeSetSettings(s); }
    void Retire(unsigned index) { Put(bucket.data() + index * ent::entity_layout::IdentityStride, ent::entity_layout::HandleOffset, Handle(index) + (1u << 15)); }
    void Remove(unsigned index) { Put(bucket.data() + index * ent::entity_layout::IdentityStride, 0, std::uintptr_t{0}); }
    void AddSecond(bool local = false, GrenadeKind kind = GrenadeKind::HE) {
        secondProjectile = projectile; secondScene = scene;
        const auto table = kind == GrenadeKind::Incendiary ? off::ProjectileTables[4].rva : off::ProjectileTables[static_cast<unsigned>(kind)].rva;
        Bind(SecondProjectile, secondProjectile.data(), table);
        Put(secondProjectile.data(), ent::C_BaseEntity::m_pGameSceneNode, Address(secondScene.data()));
        Put(secondProjectile.data(), off::Thrower, Handle(local ? Pawn : Enemy));
        Put(secondProjectile.data(), off::IncendiaryFlag, static_cast<std::uint8_t>(kind == GrenadeKind::Incendiary));
    }
    void Release() {
        Put(held.data(), off::Held, std::uint8_t{0});
        Put(held.data(), off::PinPulled, std::uint8_t{0});
        Put(held.data(), off::ThrowTime, time);
        Put(services.data(), weapon::ActiveWeapon, UINT32_MAX);
    }
};
bool NativeLaunch(void* held, void* pawn, EspVec3* position, EspVec3* velocity, bool current) {
    auto& f = *fixture; ++f.launches;
    f.nativeArguments &= Address(held) == Address(f.held.data()) && Address(pawn) == Address(f.pawn.data()) && current;
    if (f.launchWrites) { *position = f.launchPosition; *velocity = f.launchVelocity; }
    return false; // Native false still supplies valid current launch parameters.
}
float* NativeTime(float* output, int group) {
    if (fixture->failFrameClock && group == 1) { ++fixture->failedClockCalls; return nullptr; }
    fixture->negativeGroupSeen |= group < 0; *output = fixture->time; return output;
}
EspVec3* NativeVelocity(void* entity, EspVec3* output) {
    auto& f = *fixture; ++f.velocityCalls;
    if (Address(entity) == Address(f.projectile.data())) *output = f.projectileVelocity;
    else if (Address(entity) == Address(f.secondProjectile.data())) *output = f.secondVelocity;
    else { f.nativeArguments = false; return nullptr; }
    return output;
}
std::uint64_t NativeBudgetNow() { return fixture->fakeBudgetUs; }
bool SamePath(const GrenadePath& a, const GrenadePath& b) {
    if (a.kind != b.kind || a.end != b.end || a.nodes.size() != b.nodes.size() || a.finalAge != b.finalAge ||
        a.finalPosition.x != b.finalPosition.x || a.finalPosition.y != b.finalPosition.y || a.finalPosition.z != b.finalPosition.z) return false;
    for (std::size_t i = 0; i < a.nodes.size(); ++i) {
        const auto& x = a.nodes[i]; const auto& y = b.nodes[i];
        if (x.position.x != y.position.x || x.position.y != y.position.y || x.position.z != y.position.z || x.time != y.time || x.impact != y.impact)
            return false;
    }
    return true;
}
void NativeShape(void*, const float* hull) {
    ++fixture->shapes;
    for (int i = 0; i < 6; ++i) fixture->nativeArguments &= hull[i] == (i < 3 ? -off::HullExtent : off::HullExtent);
}
void NativeResult(void* result) {
    std::memset(result, 0, cam::TraceSize);
    Put(result, cam::TraceFraction, float{1});
    Put(result, movement_jumpbug_offsets::TraceNormal, EspVec3{0, 0, 1});
}
void NativeFilter(void*, void* ignored, std::uint64_t mask, std::uint8_t layer, std::uint8_t flags) {
    ++fixture->filters; fixture->ignored = Address(ignored);
    fixture->nativeArguments &= flags == 0x0F && mask == off::GrenadeMask && layer == off::GrenadeLayer;
}
bool NativeTrace(void* manager, void*, const void* start, const void* end, void*, void*) {
    auto& f = *fixture; ++f.traces;
    if (f.traceDelayMs) { f.fakeBudgetUs += static_cast<std::uint64_t>(f.traceDelayMs) * 1000; f.traceDelayMs = 0; }
    f.nativeArguments &= Address(manager) == Address(f.system.data()) && start && end;
    const auto action = f.duringTrace; f.duringTrace = DuringTrace::None;
    if (action == DuringTrace::Stop) GrenadePredictionStop();
    if (action == DuringTrace::Death) Put(f.pawn.data(), ent::C_BaseEntity::m_iHealth, int{0});
    if (action == DuringTrace::ReplaceSystem) Put(f.client.data(), ent::globals::dwGameEntitySystem, Address(f.otherSystem.data()));
    if (action == DuringTrace::RetireHeld) f.Retire(Fixture::Held);
    if (action == DuringTrace::RetireProjectile) f.Retire(Fixture::Projectile);
    return false; // Unobstructed trace has initialized fraction one.
}
}

bool ClientFrameSubscribe(ClientFrameCallback, ClientFrameLogCallback) noexcept { return true; }
bool ClientFrameInCallback() noexcept { return inCallback; }
GrenadePredictionDrawStats GrenadePredictionDraw(std::span<const GrenadePredictionVisual>,
    const EspMatrix4x4&, float, float, const GrenadeSettings&) { return {}; }

int main() {
    Fixture f;
    f.Reset();
    auto paths = f.Sample();
    auto enemyPath = std::find_if(paths.begin(), paths.end(), [](const auto& value) { return value.enemy; });
    auto localPath = std::find_if(paths.begin(), paths.end(), [](const auto& value) { return !value.enemy; });
    Check(paths.size() == 2 && enemyPath != paths.end() && localPath != paths.end(), "local primed and actual enemy projectile publish independently");
    Check(f.launches == 1 && f.traces > 0 && f.filters == f.traces && f.shapes == f.traces && f.nativeArguments &&
        f.velocityCalls > 0 && (f.ignored == Address(f.projectile.data()) || f.ignored == Address(f.pawn.data())),
        "launch and hull calls preserve native receivers and arguments even with false native returns");
    Check(enemyPath != paths.end() && enemyPath->path.nodes.front().time == .75f &&
        enemyPath->path.finalAge >= 1.5f && enemyPath->path.finalAge < 1.8f &&
        std::abs(enemyPath->path.finalPosition.x - (enemyPath->path.finalAge - .75f) * 100) < .1f,
        "enemy prediction consumes elapsed spawn age and remaining fuse with native think cadence");
    if (!paths.empty()) paths[0].path.nodes[0].position.x = 999;
    const auto copiedAgain = GrenadePredictionTestCapture();
    Check(!copiedAgain.empty() && copiedAgain[0].path.nodes[0].position.x != 999, "render capture owns a deep copy of published trajectory data");
    const auto pawn = f.pawn, held = f.held, enemy = f.enemy;
    const auto projectile = f.projectile;
    const auto scene = f.scene;
    const auto bucket = f.bucket;
    f.Sample();
    Check(pawn == f.pawn && held == f.held && enemy == f.enemy && projectile == f.projectile && scene == f.scene && bucket == f.bucket,
        "prediction never writes native entity, scene or identity bytes");

    f.Reset(); f.LocalOnly(); Put(f.held.data(), off::PinPulled, std::uint8_t{0});
    Check(f.Sample().empty() && f.launches == 0, "unprimed held grenade has no prediction");
    Put(f.held.data(), off::ThrowTime, float{1});
    Check(f.Sample().size() == 1, "released pin with scheduled throw still has local preview");
    Put(f.held.data(), off::Held, std::uint8_t{0});
    Check(f.Sample().size() == 1, "committed scheduled release retains its prepared path while projectile creation is pending");
    f.Reset(); f.LocalOnly(); f.launchWrites = false;
    Check(f.Sample().empty(), "unwritten native launch output is rejected");
    f.Reset(); f.LocalOnly(); Put(f.held.data(), off::PinPulled, std::uint8_t{2});
    Check(f.Sample().empty(), "malformed pin flag is rejected");
    f.Reset(); f.LocalOnly(); Put(f.services.data(), weapon::ServicePawn, Address(f.enemy.data()));
    Check(f.Sample().empty(), "foreign weapon-service owner is rejected");
    f.Reset(); f.LocalOnly(); f.Retire(Fixture::Held);
    Check(f.Sample().empty(), "stale active weapon serial is rejected");
    f.Reset(); f.LocalOnly(); f.launchVelocity.x = std::numeric_limits<float>::quiet_NaN();
    Check(f.Sample().empty(), "nonfinite launch output is rejected");
    f.Reset(); Put(f.gravityData.data(), weapon::CvarValue, std::numeric_limits<float>::quiet_NaN());
    Check(f.Sample().empty(), "nonfinite native gravity cvar rejects all predictions");
    f.Reset(); Put(f.client.data(), movement_jumpbug_offsets::Gravity + weapon::CvarValuePointer, std::uintptr_t{0});
    Check(f.Sample().empty(), "missing native gravity storage rejects all predictions");
    for (const auto& entry : off::WeaponTables) {
        f.Reset(); f.LocalOnly(); Put(f.held.data(), 0, Address(f.client.data()) + entry.rva);
        paths = f.Sample();
        Check(paths.size() == 1 && paths[0].path.kind == static_cast<GrenadeKind>(entry.kind), "all six native held grenade classes retain their icon kind");
    }
    f.Reset(); f.LocalOnly(); Put(f.client.data(), cam::TraceManager, std::uintptr_t{0});
    Check(f.Sample().empty(), "missing native trace manager clears path");

    f.Reset(); f.EnemyOnly(); Put(f.projectile.data(), 0, Address(f.client.data()) + off::WeaponTables[0].rva);
    Check(f.Sample().empty(), "enemy held or primed weapons never publish a projectile path");
    f.Reset(); f.EnemyOnly(); Put(f.projectile.data(), off::Thrower, Handle(Fixture::Teammate));
    Check(f.Sample().empty(), "teammate projectile is excluded");
    Put(f.projectile.data(), off::Thrower, Handle(Fixture::Pawn));
    Check(f.Sample().empty(), "local thrown projectile is excluded from enemy paths");
    f.Reset(); f.EnemyOnly(); Put(f.projectile.data(), off::Thrower, UINT32_MAX);
    Put(f.projectile.data(), off::Owner, Handle(Fixture::Enemy));
    paths = f.Sample();
    Check(paths.size() == 1 && paths[0].enemy, "native owner fallback resolves an enemy pawn when thrower handle is absent");
    const auto ownedPath = paths.empty() ? GrenadePath{} : paths[0].path;
    Put(f.projectile.data(), off::Owner, UINT32_MAX);
    paths = f.Sample();
    Check(paths.size() == 1 && SamePath(paths[0].path, ownedPath), "temporary missing ownership retains an already observed complete enemy path");
    f.Reset(); f.EnemyOnly(); Put(f.projectile.data(), off::Thrower, UINT32_MAX); Put(f.projectile.data(), off::Owner, UINT32_MAX);
    Check(f.Sample().empty(), "new projectile with missing thrower and owner is rejected");
    Put(f.projectile.data(), off::OriginalThrower, Handle(Fixture::Enemy));
    Check(f.Sample().empty(), "original thrower alone does not replace the verified native ownership getter");
    f.Reset(); f.EnemyOnly(); Put(f.projectile.data(), off::Thrower, UINT32_MAX);
    Put(f.projectile.data(), off::Owner, Handle(Fixture::EnemyHeld));
    Put(f.enemyHeld.data(), ent::C_BaseEntity::m_iTeamNum, std::uint8_t{3});
    Check(f.Sample().empty(), "weapon owner is rejected even when it carries an enemy team byte");
    f.Reset(); f.EnemyOnly(); Put(f.projectile.data(), off::Thrower, Handle(Fixture::EnemyHeld));
    Put(f.enemyHeld.data(), ent::C_BaseEntity::m_iTeamNum, std::uint8_t{3});
    Put(f.projectile.data(), off::Owner, Handle(Fixture::Enemy));
    Check(f.Sample().size() == 1, "non-pawn thrower is skipped before a valid native owner fallback");
    f.Reset(); f.EnemyOnly(); Put(f.projectile.data(), off::ExplodeTick, int{1});
    Check(f.Sample().empty(), "detonated projectile tick suppresses path");
    Put(f.projectile.data(), off::ExplodeTick, int{0}); Put(f.projectile.data(), off::ExplodeBegan, std::uint8_t{1});
    Check(f.Sample().empty(), "effect-begun projectile suppresses path");
    f.Reset(); f.EnemyOnly(); f.time = 102;
    Check(f.Sample().empty(), "expired projectile deadline suppresses path");
    f.Reset(); f.EnemyOnly(); Put(f.projectile.data(), off::DetonateTime, float{100.8f});
    paths = f.Sample();
    Check(paths.size() == 1 && paths[0].path.finalAge > 1.5f,
        "unverified client detonation-time storage does not replace native kind timer");
    f.Reset(); f.EnemyOnly(); f.time = 99;
    Check(f.Sample().empty(), "future projectile spawn time is rejected");
    f.Reset(); f.EnemyOnly(); f.Retire(Fixture::Enemy);
    Check(f.Sample().empty(), "stale thrower serial is rejected");
    f.Reset(); f.EnemyOnly(); Put(f.scene.data(), ent::CGameSceneNode::m_bDormant, std::uint8_t{1});
    Check(f.Sample().empty(), "dormant enemy projectile is rejected");
    f.Reset(); f.EnemyOnly(); Put(f.projectile.data(), off::ActualGravityScale, std::numeric_limits<float>::quiet_NaN());
    Put(f.projectile.data(), off::GravityScale, std::numeric_limits<float>::quiet_NaN());
    Check(f.Sample().empty(), "invalid projectile gravity scale is rejected");
    f.Reset(); f.EnemyOnly(); Put(f.projectile.data(), off::ActualGravityScale, float{0});
    paths = f.Sample();
    Check(paths.size() == 1 && std::abs(paths[0].path.finalPosition.z - 200) < .001f,
        "native zero actual gravity remains zero without fallback to legacy gravity scale");
    f.Reset(); f.EnemyOnly(); Put(f.projectile.data(), off::Elasticity, std::numeric_limits<float>::quiet_NaN());
    Check(f.Sample().empty(), "invalid projectile elasticity is rejected");
    f.Reset(); f.EnemyOnly(); Put(f.projectile.data(), 0, Address(f.client.data()) + off::ProjectileTables[4].rva);
    Put(f.projectile.data(), off::IncendiaryFlag, std::uint8_t{1});
    paths = f.Sample();
    Check(paths.size() == 1 && paths[0].path.kind == GrenadeKind::Incendiary, "native incendiary discriminator selects correct final icon");

    f.Reset(); f.EnemyOnly();
    Put(f.bucket.data() + Fixture::Pawn * ent::entity_layout::IdentityStride, off::IdentityWorldGroup, int{-1});
    Put(f.bucket.data() + Fixture::Projectile * ent::entity_layout::IdentityStride, off::IdentityWorldGroup, int{-1});
    paths = f.Sample();
    Check(paths.size() == 1 && f.negativeGroupSeen, "all-ones world-group bit pattern reaches the native clock without signed-slot rejection");
    f.Reset(); f.EnemyOnly();
    Put(f.projectile.data(), off::AbsVelocity, EspVec3{std::numeric_limits<float>::quiet_NaN(), 0, 0});
    f.projectileVelocity = {240, 0, 0};
    paths = f.Sample();
    Check(paths.size() == 1 && f.velocityCalls == 1 &&
        std::abs(paths[0].path.finalPosition.x - (paths[0].path.finalAge - .75f) * 240) < .1f,
        "native velocity getter supplies refreshed velocity instead of stale raw cache bytes");
    f.Reset(); f.EnemyOnly(); f.projectileVelocity.x = std::numeric_limits<float>::quiet_NaN();
    Check(f.Sample().empty(), "new projectile with invalid native velocity output is rejected");

    f.Reset(); f.LocalOnly(); f.Remove(Fixture::Projectile);
    paths = f.Sample();
    const auto preparedPath = paths.empty() ? GrenadePath{} : paths[0].path;
    Check(paths.size() == 1 && preparedPath.Terminal(), "primed local throw captures a complete prepared path");
    f.Release(); f.time = 100.78f;
    f.Bind(Fixture::Projectile, f.projectile.data(), off::ProjectileTables[0].rva);
    Put(f.projectile.data(), off::Thrower, Handle(Fixture::Pawn));
    Put(f.projectile.data(), off::ProjectileSpawnTime, float{100.77f});
    Put(f.scene.data(), ent::CGameSceneNode::m_vecAbsOrigin, EspVec3{500, 0, 200});
    f.projectileVelocity = {30, 0, 0};
    paths = f.Sample();
    Check(paths.size() == 1 && !paths[0].enemy && SamePath(paths[0].path, preparedPath),
        "released local projectile inherits the complete prepared path with enemy prediction disabled");
    f.time = 101.4f; Put(f.scene.data(), ent::CGameSceneNode::m_vecAbsOrigin, EspVec3{600, 0, 100});
    paths = f.Sample();
    Check(paths.size() == 1 && SamePath(paths[0].path, preparedPath), "switching away preserves the full local trajectory without shortening");
    f.time = 104;
    paths = f.Sample();
    Check(paths.size() == 1 && SamePath(paths[0].path, preparedPath), "live local projectile keeps its full path beyond predicted fuse time");
    Put(f.projectile.data(), off::ExplodeTick, int{1});
    Check(f.Sample().empty(), "local path ends at actual detonation instead of predicted timer expiry");

    // Exercise the native quick-throw transition repeatedly without resetting
    // feature state or sampling an intervening pin=1/throwTime=0 command.
    f.Reset(); f.LocalOnly(); f.Remove(Fixture::Projectile);
    paths = f.Sample();
    GrenadePath repeatedPrepared = paths.empty() ? GrenadePath{} : paths[0].path;
    Check(paths.size() == 1 && repeatedPrepared.Terminal(), "first throw in a continuous twenty-throw sequence has a complete prepared path");
    for (unsigned throwIndex = 0; throwIndex < 20; ++throwIndex) {
        if (throwIndex) {
            f.time += .05f;
            f.launchPosition = {static_cast<float>(throwIndex) * 300, 0, 200 + static_cast<float>(throwIndex) * 10};
            Put(f.held.data(), off::Held, std::uint8_t{1});
            Put(f.held.data(), off::PinPulled, std::uint8_t{0});
            Put(f.held.data(), off::ThrowTime, f.time);
            Put(f.services.data(), weapon::ActiveWeapon, Handle(Fixture::Held));
            paths = f.Sample();
            Check(paths.size() == 1 && paths[0].path.Terminal() && paths[0].path.nodes.front().position.x == f.launchPosition.x,
                "changed positive native throw time admits the next quick throw on the same weapon handle");
            repeatedPrepared = paths.empty() ? GrenadePath{} : paths[0].path;
        }
        f.Release(); f.time += .02f;
        f.projectile.fill(0);
        f.Bind(Fixture::Projectile, f.projectile.data(), off::ProjectileTables[0].rva);
        Put(f.bucket.data() + Fixture::Projectile * ent::entity_layout::IdentityStride,
            ent::entity_layout::HandleOffset, ((throwIndex + 4u) << 15) | Fixture::Projectile);
        Put(f.projectile.data(), ent::C_BaseEntity::m_pGameSceneNode, Address(f.scene.data()));
        Put(f.projectile.data(), off::Thrower, Handle(Fixture::Pawn));
        Put(f.projectile.data(), off::ProjectileSpawnTime, f.time - .01f);
        Put(f.projectile.data(), off::ActualGravityScale, float{.4f});
        Put(f.projectile.data(), off::Elasticity, float{.45f});
        Put(f.scene.data(), ent::CGameSceneNode::m_vecAbsOrigin, EspVec3{10000, 50, 200});
        paths = f.Sample();
        Check(paths.size() == 1 && !paths[0].enemy && SamePath(paths[0].path, repeatedPrepared),
            "each recycled projectile identity receives its own full prepared path across twenty consecutive throws");
        f.time += .03f;
        Put(f.projectile.data(), off::ExplodeTick, int{1});
        Check(f.Sample().empty(), "each consecutive local trajectory ends at its own actual detonation marker");
        f.Remove(Fixture::Projectile);
    }

    f.Reset(); f.LocalOnly(); f.Release(); Put(f.projectile.data(), off::Thrower, Handle(Fixture::Pawn));
    paths = f.Sample();
    Check(paths.size() == 1 && !paths[0].enemy, "an observed local thrown grenade is shown without a preceding primed sample");

    f.Reset(); f.LocalOnly();
    Put(f.held.data(), off::PinPulled, std::uint8_t{0});
    Put(f.held.data(), off::ThrowTime, f.time);
    Put(f.projectile.data(), off::Thrower, Handle(Fixture::Pawn));
    Put(f.projectile.data(), off::ProjectileSpawnTime, f.time);
    Put(f.scene.data(), ent::CGameSceneNode::m_vecAbsOrigin, EspVec3{500, 0, 200});
    paths = f.Sample();
    const auto sameFrameReleased = paths.size() == 1 ? paths[0].path : GrenadePath{};
    Check(paths.size() == 1 && !paths[0].enemy && sameFrameReleased.Terminal() &&
        sameFrameReleased.nodes.front().position.x == f.launchPosition.x && sameFrameReleased.nodes.front().time == 0,
        "first released-held snapshot and local projectile in the same owner frame merge into one full released preview");
    f.Release(); f.time += .1f;
    paths = f.Sample();
    Check(paths.size() == 1 && SamePath(paths[0].path, sameFrameReleased), "same-frame released forecast stays attached after switching away");
    Put(f.projectile.data(), off::ExplodeTick, int{1});
    Check(f.Sample().empty(), "same-frame release handoff clears at its projectile actual end");

    f.Reset(); f.LocalOnly(); f.Remove(Fixture::Projectile);
    paths = f.Sample();
    const auto cancelledPath = paths.empty() ? GrenadePath{} : paths[0].path;
    Put(f.held.data(), off::Held, std::uint8_t{0});
    Put(f.held.data(), off::PinPulled, std::uint8_t{1});
    Put(f.held.data(), off::ThrowTime, float{0});
    Put(f.services.data(), weapon::ActiveWeapon, UINT32_MAX);
    f.Bind(Fixture::Projectile, f.projectile.data(), off::ProjectileTables[0].rva);
    Put(f.projectile.data(), off::Thrower, Handle(Fixture::Pawn));
    Put(f.projectile.data(), off::ProjectileSpawnTime, float{100.77f});
    Put(f.scene.data(), ent::CGameSceneNode::m_vecAbsOrigin, EspVec3{500, 0, 200});
    f.time = 100.78f;
    paths = f.Sample();
    Check(paths.size() == 1 && !SamePath(paths[0].path, cancelledPath) && paths[0].path.nodes.front().position.x == 500,
        "a cancelled prepared preview cannot attach to an unrelated new local grenade of the same kind");

    f.Reset(); f.LocalOnly(); f.Remove(Fixture::Projectile);
    paths = f.Sample();
    const auto delayedPreparedPath = paths.empty() ? GrenadePath{} : paths[0].path;
    f.Release(); f.time = 100.78f;
    f.Bind(Fixture::Projectile, f.projectile.data(), off::ProjectileTables[0].rva);
    Put(f.projectile.data(), off::Thrower, Handle(Fixture::Pawn));
    Put(f.projectile.data(), off::ProjectileSpawnTime, float{100.77f});
    Put(f.projectile.data(), ent::C_BaseEntity::m_pGameSceneNode, std::uintptr_t{0});
    paths = f.Sample();
    Check(paths.size() == 1 && SamePath(paths[0].path, delayedPreparedPath),
        "new local identity receives the saved prepared path before its scene input becomes available");
    Put(f.projectile.data(), ent::C_BaseEntity::m_pGameSceneNode, Address(f.scene.data()));
    Put(f.scene.data(), ent::CGameSceneNode::m_vecAbsOrigin, EspVec3{500, 0, 200});
    f.time = 100.8f;
    paths = f.Sample();
    Check(paths.size() == 1 && SamePath(paths[0].path, delayedPreparedPath),
        "prepared transfer still succeeds when a newly observed projectile acquires its first scene input on a later frame");

    for (float unavailableSpawn : {0.0f, 500.0f}) {
        f.Reset(); f.LocalOnly(); f.Remove(Fixture::Projectile);
        paths = f.Sample();
        const auto savedPrepared = paths.empty() ? GrenadePath{} : paths[0].path;
        const int beforeTransferTraces = f.traces;
        f.Release(); f.time = 100.78f;
        f.Bind(Fixture::Projectile, f.projectile.data(), off::ProjectileTables[0].rva);
        Put(f.projectile.data(), off::Thrower, Handle(Fixture::Pawn));
        Put(f.projectile.data(), off::ProjectileSpawnTime, unavailableSpawn);
        Put(f.projectile.data(), ent::C_BaseEntity::m_pGameSceneNode, std::uintptr_t{0});
        paths = f.Sample();
        Check(paths.size() == 1 && SamePath(paths[0].path, savedPrepared) && f.traces == beforeTransferTraces,
            "new live local identity receives its prepared path despite zero or future spawn time and absent scene input without resimulation");
        Put(f.projectile.data(), off::ExplodeTick, int{1});
        Check(f.Sample().empty(), "prepared path handed to an input-incomplete projectile still clears at actual detonation");
    }

    f.Reset(); f.LocalOnly(); f.Remove(Fixture::Projectile);
    paths = f.Sample();
    const auto slowOwnerPrepared = paths.empty() ? GrenadePath{} : paths[0].path;
    f.Release(); f.time = 100.78f;
    f.Bind(Fixture::Projectile, f.projectile.data(), off::ProjectileTables[0].rva);
    Put(f.projectile.data(), off::Thrower, UINT32_MAX); Put(f.projectile.data(), off::Owner, UINT32_MAX);
    Put(f.projectile.data(), off::ProjectileSpawnTime, float{0});
    Put(f.projectile.data(), ent::C_BaseEntity::m_pGameSceneNode, std::uintptr_t{0});
    f.Sample(); // Records the physical identity before ownership is available.
    Sleep(1100);
    f.time += 1.15f; Put(f.projectile.data(), off::Owner, Handle(Fixture::Pawn));
    paths = f.Sample();
    Check(paths.size() == 1 && SamePath(paths[0].path, slowOwnerPrepared),
        "committed prepared handoff survives ownership arriving after one second with zero spawn time and absent scene input");
    Put(f.projectile.data(), off::ExplodeTick, int{1});
    Check(f.Sample().empty(), "slow-ownership handoff ends at actual projectile detonation");

    f.Reset(); f.LocalOnly();
    Put(f.held.data(), off::PinPulled, std::uint8_t{0});
    Put(f.held.data(), off::ThrowTime, float{0});
    Put(f.projectile.data(), off::Thrower, UINT32_MAX); Put(f.projectile.data(), off::Owner, UINT32_MAX);
    Put(f.scene.data(), ent::CGameSceneNode::m_vecAbsOrigin, EspVec3{500, 0, 200});
    Check(f.Sample().empty(), "old same-kind physical projectile is recorded neutrally before a later prepared throw");
    Put(f.held.data(), off::PinPulled, std::uint8_t{1});
    paths = f.Sample();
    const auto laterPrepared = paths.empty() ? GrenadePath{} : paths[0].path;
    f.Release(); f.time = 100.78f;
    Put(f.projectile.data(), off::Owner, Handle(Fixture::Pawn));
    f.AddSecond(true);
    Put(f.secondProjectile.data(), off::ProjectileSpawnTime, float{0});
    Put(f.secondProjectile.data(), ent::C_BaseEntity::m_pGameSceneNode, std::uintptr_t{0});
    paths = f.Sample();
    const auto olderOrdinary = std::find_if(paths.begin(), paths.end(), [](const auto& value) {
        return !value.path.nodes.empty() && value.path.nodes.front().position.x == 500;
    });
    const auto savedOlderOrdinary = olderOrdinary == paths.end() ? GrenadePath{} : olderOrdinary->path;
    Check(paths.size() == 2 && olderOrdinary != paths.end() && std::count_if(paths.begin(), paths.end(), [&](const auto& value) {
        return SamePath(value.path, laterPrepared);
    }) == 1, "neutral birth frame prevents an older newly-owned grenade stealing the fresh projectile prepared path");
    Put(f.secondProjectile.data(), off::ExplodeTick, int{1});
    paths = f.Sample();
    Check(paths.size() == 1 && SamePath(paths[0].path, savedOlderOrdinary),
        "ending the fresh grenade leaves the older formerly-ownerless grenade on its own ordinary trajectory");

    f.Reset(); f.LocalOnly(); Put(f.projectile.data(), off::Thrower, Handle(Fixture::Pawn));
    Put(f.scene.data(), ent::CGameSceneNode::m_vecAbsOrigin, EspVec3{500, 0, 200});
    paths = f.Sample();
    const auto oldFlight = std::find_if(paths.begin(), paths.end(), [](const auto& value) {
        return !value.path.nodes.empty() && value.path.nodes.front().position.x == 500;
    });
    const auto alreadyObservedFlight = oldFlight == paths.end() ? GrenadePath{} : oldFlight->path;
    f.Release(); f.time = 100.78f;
    paths = f.Sample();
    Check(!alreadyObservedFlight.nodes.empty() && std::any_of(paths.begin(), paths.end(), [&](const auto& value) {
        return SamePath(value.path, alreadyObservedFlight);
    }), "an older already-observed local grenade of the same kind cannot consume a later prepared throw");

    f.Reset(); f.EnemyOnly();
    paths = f.Sample();
    const auto beforeClockFailure = paths.empty() ? GrenadePath{} : paths[0].path;
    Put(f.bucket.data() + Fixture::Pawn * ent::entity_layout::IdentityStride, off::IdentityWorldGroup, int{1});
    Put(f.bucket.data() + Fixture::Projectile * ent::entity_layout::IdentityStride, off::IdentityWorldGroup, int{1});
    f.failFrameClock = true;
    paths = f.Sample();
    Check(paths.size() == 1 && SamePath(paths[0].path, beforeClockFailure),
        "retaining a complete projectile path and scanning its lifecycle does not require a fresh native clock");
    Put(f.projectile.data(), off::ExplodeTick, int{1});
    Check(f.Sample().empty(), "actual grenade end remains observable while the frame clock is unavailable");

    f.Reset(); f.EnemyOnly();
    paths = f.Sample();
    const auto fullEnemyPath = paths.empty() ? GrenadePath{} : paths[0].path;
    f.time = 101.2f; f.projectileVelocity = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
    Put(f.projectile.data(), off::ActualGravityScale, std::numeric_limits<float>::quiet_NaN());
    Put(f.projectile.data(), off::Thrower, UINT32_MAX); Put(f.projectile.data(), off::Owner, UINT32_MAX);
    paths = f.Sample();
    Check(paths.size() == 1 && SamePath(paths[0].path, fullEnemyPath), "temporary owner, velocity and physics loss preserves an observed complete path");
    f.time = 109;
    paths = f.Sample();
    Check(paths.size() == 1 && SamePath(paths[0].path, fullEnemyPath), "a still-observed enemy projectile survives well beyond predicted deadline");
    Put(f.projectile.data(), off::ExplodeBegan, std::uint8_t{1});
    Check(f.Sample().empty(), "actual effect start clears the retained enemy path even with missing owner data");

    for (bool tick : {false, true}) {
        f.Reset(); f.EnemyOnly(); Put(f.projectile.data(), 0, Address(f.client.data()) + off::ProjectileTables[2].rva);
        Put(f.projectile.data(), off::ActualGravityScale, float{0}); f.projectileVelocity = {};
        f.time = 101.25f;
        Check(f.Sample().size() == 1, "settled smoke path is captured before actual deployment");
        if (tick) Put(f.projectile.data(), off::SmokeTickBegin, int{10});
        else Put(f.projectile.data(), off::SmokeDidEffect, std::uint8_t{1});
        Put(f.projectile.data(), off::SmokeEffectSpawned, std::uint8_t{0});
        Check(f.Sample().empty(), "smoke deployment clears path before the later effect-spawned flag");
    }
    f.Reset(); f.EnemyOnly(); Put(f.projectile.data(), 0, Address(f.client.data()) + off::ProjectileTables[4].rva);
    Check(f.Sample().size() == 1, "flying molotov publishes a path");
    f.Remove(Fixture::Projectile);
    Check(f.Sample().empty(), "molotov projectile removal clears its path at ignition without waiting for fire end");

    f.Reset(); f.EnemyOnly(); f.AddSecond();
    Put(f.secondScene.data(), ent::CGameSceneNode::m_vecAbsOrigin, EspVec3{0, 40, 200});
    paths = f.Sample();
    const auto simultaneous = paths;
    Check(paths.size() == 2, "independent thrown grenades retain separate complete paths");
    f.time = 101.3f; Put(f.scene.data(), ent::CGameSceneNode::m_vecAbsOrigin, EspVec3{50, 0, 100});
    Put(f.secondScene.data(), ent::CGameSceneNode::m_vecAbsOrigin, EspVec3{40, 40, 100});
    paths = f.Sample();
    Check(paths.size() == 2 && simultaneous.size() == 2 &&
        std::all_of(simultaneous.begin(), simultaneous.end(), [&](const auto& original) {
            return std::any_of(paths.begin(), paths.end(), [&](const auto& current) { return SamePath(original.path, current.path); });
        }), "both complete paths stay unchanged as their projectiles move");
    Put(f.projectile.data(), off::ExplodeTick, int{1});
    Check(f.Sample().size() == 1, "one grenade ending leaves the other path intact");
    f.Remove(Fixture::SecondProjectile);
    Check(f.Sample().empty(), "the second path ends independently when its full handle disappears");

    f.Reset(); f.EnemyOnly(); f.Remove(Fixture::Projectile); f.AddSecond();
    paths = f.Sample();
    const auto lateIndexPath = paths.empty() ? GrenadePath{} : paths[0].path;
    f.Bind(Fixture::Projectile, f.projectile.data(), off::ProjectileTables[0].rva);
    f.time = 100.8f; f.traceDelayMs = 8;
    paths = f.Sample();
    Check(paths.size() == 1 && SamePath(paths[0].path, lateIndexPath),
        "full entity scan preserves a later tracked grenade when an earlier new grenade exhausts tracing time");

    f.Reset(); f.EnemyOnly(); paths = f.Sample();
    const auto beforeClockRollback = paths.empty() ? GrenadePath{} : paths[0].path;
    f.time = 99;
    paths = f.Sample();
    Check(paths.size() == 1 && SamePath(paths[0].path, beforeClockRollback),
        "native clock rollback preserves an already observed complete path until its actual grenade end");

    for (auto action : {DuringTrace::Death, DuringTrace::ReplaceSystem, DuringTrace::RetireHeld}) {
        f.Reset(); f.LocalOnly(); f.duringTrace = action;
        Check(f.Sample().empty(), "source changes during native trace prevent local publication");
        if (action==DuringTrace::ReplaceSystem) Check(f.traces==1, "changed entity system aborts before another native sweep");
    }
    f.Reset(); f.LocalOnly(); Put(f.client.data(), movement_offsets::TickIntervalConstant, float{0});
    Check(f.Sample().empty(), "unavailable native tick interval rejects prediction without a guessed rate");
    f.Reset(); f.EnemyOnly(); f.duringTrace = DuringTrace::RetireProjectile;
    Check(f.Sample().empty(), "projectile retirement during trace prevents stale publication");
    f.Reset(); f.LocalOnly(); f.duringTrace = DuringTrace::Stop;
    Check(f.Sample().empty() && std::strstr(GrenadePredictionStatus(), "stopped"), "reentrant cooperative stop avoids deadlock and publication");
    f.Reset(); f.Sample(); GrenadePredictionStop();
    Check(GrenadePredictionTestCapture().empty(), "stop clears copied paths immediately");
    Check(f.Sample().empty(), "stopped owner cannot republish");
    f.Reset(); f.Sample(); Put(f.pawn.data(), ent::C_BaseEntity::m_iHealth, int{0});
    Check(GrenadePredictionTestCapture().empty(), "render capture rejects local death before next sample");
    f.Reset(); f.Sample(); f.Retire(Fixture::Pawn);
    Check(GrenadePredictionTestCapture().empty(), "render capture rejects a reused pawn identity immediately");
    f.Reset(); f.Sample(); Sleep(175);
    Check(GrenadePredictionTestCapture().empty(), "copied paths expire without a fresh native frame");
    f.Reset(); f.Sample(); GrenadeSetSettings({});
    Check(f.Sample().empty(), "disabling both predictions clears publication");
    f.Reset(); f.Sample(); Put(f.client.data(), ent::globals::dwLocalPlayerPawn, std::uintptr_t{0});
    Check(GrenadePredictionTestCapture().empty(), "render capture rejects disconnected local source");
    f.Reset(); f.Sample(); GrenadePredictionTestConfigure(0, {});
    Check(GrenadePredictionTestCapture().empty(), "missing client module clears publication");
    GrenadePredictionStop();
    std::printf("Grenade prediction runtime: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
