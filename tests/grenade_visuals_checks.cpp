#include "grenade_visuals.h"
#include "grenade_area_offsets.h"
#include "cs2_offsets.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace {
namespace off = grenade_area_offsets;
namespace ent = cs2_offsets;
unsigned checks = 0, failures = 0;
void Check(bool condition, const char* label) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", label); }
}
std::uintptr_t Address(const void* value) { return reinterpret_cast<std::uintptr_t>(value); }
template<class T> void Put(void* target, std::size_t offset, const T& value) {
    std::memcpy(static_cast<unsigned char*>(target) + offset, &value, sizeof(value));
}
unsigned Morton(unsigned x, unsigned y, unsigned z) {
    unsigned code = 0;
    for (unsigned bit = 0; bit < 5; ++bit) {
        code |= ((x >> bit) & 1) << (3 * bit);
        code |= ((y >> bit) & 1) << (3 * bit + 1);
        code |= ((z >> bit) & 1) << (3 * bit + 2);
    }
    return code;
}
struct Fixture {
    std::vector<unsigned char> client = std::vector<unsigned char>(ent::ClientImageSize);
    std::array<unsigned char, 0x220> system{}, otherSystem{};
    std::array<unsigned char, 512 * ent::entity_layout::IdentityStride> bucket{};
    std::array<unsigned char, 0x86A0> fire{};
    std::array<unsigned char, 0x1500> smoke{};
    std::array<unsigned char, 0x120> fireScene{}, smokeScene{};
    std::vector<unsigned char> owner = std::vector<unsigned char>(off::SmokeDensity + 2 * off::SmokeDensityBufferBytes);
    static constexpr unsigned FireIndex = 2, SmokeIndex = 3;
    EspVec3 origin{1000, 2000, 64};
    void Bind(unsigned index, void* object, void* scene, std::ptrdiff_t vtable) {
        auto* identity = bucket.data() + index * ent::entity_layout::IdentityStride;
        Put(identity, 0, Address(object));
        Put(identity, ent::entity_layout::HandleOffset, std::uint32_t{(4u << 15) | index});
        Put(object, ent::CEntityInstance::m_pEntity, Address(identity));
        Put(object, 0, Address(client.data()) + vtable);
        Put(object, ent::C_BaseEntity::m_pGameSceneNode, Address(scene));
    }
    void Reset() {
        GrenadeVisualsStop();
        system.fill(0); otherSystem.fill(0); bucket.fill(0); fire.fill(0); smoke.fill(0);
        fireScene.fill(0); smokeScene.fill(0);
        std::fill(owner.begin(), owner.end(), static_cast<unsigned char>(0));
        Put(client.data(), ent::globals::dwGameEntitySystem, Address(system.data()));
        Put(system.data(), ent::entity_layout::TableOffset, Address(bucket.data()));
        Bind(FireIndex, fire.data(), fireScene.data(), off::InfernoVtable);
        Bind(SmokeIndex, smoke.data(), smokeScene.data(), off::SmokeVtable);
        Put(fire.data(), off::FireCount, int{1});
        Put(fire.data(), off::FireHalfWidth, float{30});
        Put(fire.data(), off::FirePositions, EspVec3{100, 200, 30});
        Put(fire.data(), off::FireBurning, std::uint8_t{1});
        Put(smoke.data(), off::SmokeTickBegin, int{100});
        Put(smoke.data(), off::SmokeDidEffect, std::uint8_t{1});
        Put(smoke.data(), off::SmokeEffectSpawned, std::uint8_t{1});
        Put(smoke.data(), off::SmokeDetonation, origin);
        Put(smoke.data(), off::SmokeSimulation + off::SmokeVoxelOwner, Address(owner.data()));
        Put(smoke.data(), off::SmokeSimulation + off::SmokeOrigin, origin);
        Put(smoke.data(), off::SmokeSimulation + off::SmokeBuffer, int{0});
        Voxel(0, 1, 2, 3, 0.5f);
        GrenadeSetSettings({false, false, true, true});
        GrenadeVisualsTestConfigure(Address(client.data()));
    }
    void Voxel(int buffer, unsigned x, unsigned y, unsigned z, float density) {
        const auto code = Morton(x, y, z);
        owner[off::SmokeOccupancy + buffer * off::SmokeOccupancyBytes + (code >> 3)] |= static_cast<unsigned char>(1u << (code & 7));
        Put(owner.data(), off::SmokeDensity + (1 - buffer) * off::SmokeDensityBufferBytes + code * off::SmokeDensityStride, density);
    }
    void OnlySmoke() { GrenadeSetSettings({false, false, true, false}); }
    void OnlyFire() { GrenadeSetSettings({false, false, false, true}); }
    std::vector<GrenadeArea> Sample() { GrenadeVisualsTestFrame(); return GrenadeVisualsTestCapture(); }
};
bool SameEdges(const std::vector<GrenadeEdge>& a, const std::vector<GrenadeEdge>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].a.x != b[i].a.x || a[i].a.y != b[i].a.y || a[i].a.z != b[i].a.z ||
            a[i].b.x != b[i].b.x || a[i].b.y != b[i].b.y || a[i].b.z != b[i].b.z) return false;
    }
    return true;
}
}

int main() {
    Fixture f;
    f.Reset();
    auto areas = f.Sample();
    Check(areas.size() == 2 && areas[0].kind == GrenadeAreaKind::Molotov && areas[1].kind == GrenadeAreaKind::Smoke,
        "native owner independently captures ignited fire and deployed smoke");
    Check(areas.size() == 2 && !areas[0].edges.empty() && areas[1].edges.size() == 4,
        "one flame cell and one occupied smoke voxel produce separate bounded footprints");
    const auto fireBefore = f.fire;
    const auto smokeBefore = f.smoke;
    const auto ownerBefore = f.owner;
    f.Sample();
    Check(f.fire == fireBefore && f.smoke == smokeBefore && f.owner == ownerBefore,
        "sampling never writes native grenade or simulation bytes");
    GrenadeVisualsStop();
    Check(GrenadeVisualsTestCapture().empty(), "cooperative stop clears published snapshots immediately");
    GrenadeVisualsTestFrame();
    Check(GrenadeVisualsTestCapture().empty(), "stopped owner callbacks cannot republish footprints");

    f.Reset(); f.OnlyFire();
    areas = f.Sample();
    Check(areas.size() == 1 && areas[0].kind == GrenadeAreaKind::Molotov, "fire aura does not require smoke aura");
    for (int invalid : {-1, 0, 65, 1000000}) {
        Put(f.fire.data(), off::FireCount, invalid);
        Check(f.Sample().empty(), "invalid or unignited fire count clears previous footprint");
    }
    Put(f.fire.data(), off::FireCount, int{64});
    Check(f.Sample().size() == 1, "maximum native flame-cell count is accepted with inactive cells ignored");
    Put(f.fire.data(), off::FirePostEffect, std::uint8_t{1});
    Check(f.Sample().empty(), "post-effect inferno immediately loses its aura");
    Put(f.fire.data(), off::FirePostEffect, std::uint8_t{0});
    Put(f.fire.data(), off::FireBurning, std::uint8_t{0});
    Check(f.Sample().empty(), "extinguished cells never contribute an aura");
    Put(f.fire.data(), off::FireBurning, std::uint8_t{2});
    Check(f.Sample().empty(), "malformed flame burning flags are rejected");
    Put(f.fire.data(), off::FireBurning, std::uint8_t{1});
    for (float radius : {0.0f, -1.0f, 1025.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        Put(f.fire.data(), off::FireHalfWidth, radius);
        Check(f.Sample().empty(), "nonfinite or unsupported native flame radius is rejected");
    }
    Put(f.fire.data(), off::FireHalfWidth, float{30});
    Put(f.fire.data(), off::FirePositions, EspVec3{std::numeric_limits<float>::quiet_NaN(), 0, 0});
    Check(f.Sample().empty(), "nonfinite burning-cell world position is rejected");

    f.Reset(); f.OnlySmoke();
    areas = f.Sample();
    Check(areas.size() == 1 && areas[0].kind == GrenadeAreaKind::Smoke, "smoke aura does not require fire aura");
    if (areas.size() == 1) {
        bool exact = areas[0].edges.size() == 4;
        for (const auto& edge : areas[0].edges) {
            for (const auto& point : {edge.a, edge.b}) {
                exact &= (point.x == 700 || point.x == 720) && (point.y == 1720 || point.y == 1740) && point.z == f.origin.z;
            }
        }
        Check(exact, "Morton XYZ voxel decodes into the actual twenty-unit XY footprint at detonation height");
        const auto before = areas[0].edges;
        GrenadeSetSettings({true, true, true, false});
        areas = f.Sample();
        Check(areas.size() == 1 && SameEdges(before, areas[0].edges), "smoke and flash removal do not erase native aura geometry");
    }
    Put(f.smoke.data(), off::SmokeTickBegin, int{0});
    Put(f.smoke.data(), off::SmokeDidEffect, std::uint8_t{0});
    Check(f.Sample().empty(), "airborne smoke before detonation has no aura");
    Put(f.smoke.data(), off::SmokeDidEffect, std::uint8_t{1});
    Check(f.Sample().size() == 1, "native did-effect state independently proves deployment");
    Put(f.smoke.data(), off::SmokeDidEffect, std::uint8_t{2});
    Check(f.Sample().empty(), "invalid smoke did-effect flag is rejected");
    Put(f.smoke.data(), off::SmokeDidEffect, std::uint8_t{1});
    Put(f.smoke.data(), off::SmokeEffectSpawned, std::uint8_t{0});
    Check(f.Sample().empty(), "unspawned smoke effect clears its aura even after detonation tick");
    Put(f.smoke.data(), off::SmokeEffectSpawned, std::uint8_t{1});
    Put(f.smoke.data(), off::SmokeSimulation + off::SmokeBuffer, int{2});
    Check(f.Sample().empty(), "unsupported native smoke buffer is rejected");
    Put(f.smoke.data(), off::SmokeSimulation + off::SmokeBuffer, int{0});
    Put(f.smoke.data(), off::SmokeSimulation + off::SmokeOrigin, EspVec3{0, std::numeric_limits<float>::infinity(), 0});
    Check(f.Sample().empty(), "nonfinite smoke simulation origin is rejected");

    f.Reset(); f.OnlySmoke();
    const auto occupiedCode = Morton(1, 2, 3);
    for (float invalid : {0.0f, -0.5f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        Put(f.owner.data(), off::SmokeDensity + off::SmokeDensityBufferBytes + occupiedCode * off::SmokeDensityStride, invalid);
        Check(f.Sample().empty(), "occupancy without finite positive density never creates smoke footprint");
    }
    Put(f.owner.data(), off::SmokeDensity + occupiedCode * off::SmokeDensityStride, float{1});
    Check(f.Sample().empty(), "current-buffer density is ignored when native rendering consumes the opposite buffer");
    std::fill(f.owner.begin(), f.owner.end(), static_cast<unsigned char>(0));
    Put(f.smoke.data(), off::SmokeSimulation + off::SmokeBuffer, int{1});
    f.Voxel(1, 25, 20, 17, 1.0f);
    areas = f.Sample();
    Check(areas.size() == 1 && areas[0].edges.size() == 4 && areas[0].edges[0].a.x >= 1180 && areas[0].edges[0].a.y >= 2080,
        "buffer one uses its own occupancy and buffer zero density with high Morton coordinate bits");
    f.Voxel(1, 25, 20, 18, 0.25f);
    Check(f.Sample().size() == 1 && f.Sample()[0].edges.size() == 4, "vertically stacked smoke voxels share one ground column boundary");
    f.Voxel(1, 26, 20, 18, 0.25f);
    areas = f.Sample();
    Check(areas.size() == 1 && areas[0].edges.size() == 6, "adjacent smoke columns omit the shared interior edge");
    f.Voxel(1, 29, 20, 18, 0.25f);
    areas = f.Sample();
    Check(areas.size() == 1 && areas[0].edges.size() == 10, "separated smoke islands retain independent border edges");
    std::fill(f.owner.begin() + off::SmokeOccupancy + off::SmokeOccupancyBytes,
        f.owner.begin() + off::SmokeOccupancy + 2 * off::SmokeOccupancyBytes, static_cast<unsigned char>(0));
    Check(f.Sample().empty(), "density without current occupancy never leaves a stale footprint");

    f.Reset(); f.OnlySmoke();
    Put(f.smokeScene.data(), ent::CGameSceneNode::m_bDormant, std::uint8_t{1});
    Check(f.Sample().empty(), "dormant grenade scene nodes cannot publish geometry");
    Put(f.smokeScene.data(), ent::CGameSceneNode::m_bDormant, std::uint8_t{0});
    Put(f.smoke.data(), ent::C_BaseEntity::m_pGameSceneNode, std::uintptr_t{0});
    Check(f.Sample().empty(), "missing scene node is rejected");
    Put(f.smoke.data(), ent::C_BaseEntity::m_pGameSceneNode, Address(f.smokeScene.data()));
    Put(f.smoke.data(), 0, Address(f.client.data()) + off::InfernoVtable);
    Check(f.Sample().empty(), "other exact class vtables cannot be interpreted as smoke");
    Put(f.smoke.data(), 0, Address(f.client.data()) + off::SmokeVtable);
    Put(f.smoke.data(), ent::CEntityInstance::m_pEntity, std::uintptr_t{0});
    Check(f.Sample().empty(), "missing entity-to-identity back-reference is rejected");
    auto* identity = f.bucket.data() + Fixture::SmokeIndex * ent::entity_layout::IdentityStride;
    Put(f.smoke.data(), ent::CEntityInstance::m_pEntity, Address(identity));
    Put(identity, ent::entity_layout::HandleOffset, std::uint32_t{(4u << 15) | Fixture::FireIndex});
    Check(f.Sample().empty(), "serialized handle index must resolve to its exact table slot");
    Put(identity, ent::entity_layout::HandleOffset, std::uint32_t{UINT32_MAX});
    Check(f.Sample().empty(), "invalid full serialized handles are rejected");
    Put(identity, ent::entity_layout::HandleOffset, std::uint32_t{(5u << 15) | Fixture::SmokeIndex});
    Check(f.Sample().size() == 1, "a newly serialized grenade identity is sampled afresh without cached pointer state");
    Put(f.smoke.data(), off::SmokeSimulation + off::SmokeVoxelOwner, std::uintptr_t{0});
    Check(f.Sample().empty(), "missing embedded simulation owner clears previous grenade publication");

    f.Reset(); f.OnlySmoke();
    f.Sample();
    Put(f.client.data(), ent::globals::dwGameEntitySystem, Address(f.otherSystem.data()));
    Check(GrenadeVisualsTestCapture().empty(), "entity-system change invalidates copied snapshots before another native frame");
    Check(f.Sample().empty(), "replacement empty entity system never inherits old geometry");
    f.Reset(); f.OnlySmoke();
    f.Sample();
    std::this_thread::sleep_for(std::chrono::milliseconds(270));
    Check(GrenadeVisualsTestCapture().empty(), "snapshots expire when native frame updates stop");
    Check(f.Sample().size() == 1, "fresh owner sample recovers after publication expiry");
    GrenadeSetSettings({true, true, false, false});
    Check(f.Sample().empty(), "turning both auras off clears geometry while removal toggles stay enabled");
    GrenadeVisualsStop();
    Check(std::strstr(GrenadeVisualsStatus(), "stopped and resident") != nullptr, "stop reports retained resident code accurately");
    GrenadeResetSettings();
    std::printf("Grenade visual checks: %u, failures: %u\n", checks, failures);
    return failures ? 1 : 0;
}
