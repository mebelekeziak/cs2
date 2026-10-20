#include "aim_autowall.h"
#include "aim_autowall_offsets.h"
#include "aim_damage_offsets.h"
#include "aim_occlusion_offsets.h"
#include "aim_weapon.h"
#include "cs2_offsets.h"
#include "third_person_offsets.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
namespace off = aim_autowall_offsets;
unsigned checks = 0, failures = 0, destroys = 0, steps = 0, traces = 0;
void Check(bool ok, const char* label) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", label); } }
template<class T> void Put(void* at, std::size_t offset, T value) { std::memcpy(static_cast<unsigned char*>(at)+offset, &value, sizeof(value)); }
template<class T> T Get(const void* at, std::size_t offset) { T value{}; std::memcpy(&value, static_cast<const unsigned char*>(at)+offset, sizeof(value)); return value; }
std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
unsigned char* memory = nullptr;
std::array<unsigned char, 0x900> weaponData{};
std::array<unsigned char, 64> local{}, victim{}, manager{}, hitbox{}, unknown{}, foreign{};
bool current = true, block = false, fault = false, managerChange = false, pawnChange = false, geometryChange = false;
bool destroyFault = false, foreignBody = false, unknownGroup = false, noVictim = false;
int walls = 1, corrupt = 0;
float loss = 10;
float rayScale = 1;
bool initFail = false;
void* Initialize(void* data) {
    Check(Address(data) % 16 == 0, "native data aligned");
    Put(data, off::Hits, Address(data) + 0x18);
    Put(data, off::HitCapacity, 128);
    Put(data, off::Segments, Address(data) + 0x1c38);
    Put(data, off::SegmentCapacity, 8);
    return initFail ? nullptr : data;
}
void Destroy(void*) { ++destroys; if (destroyFault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr); }
void* Filter(void* output, void* pawn, std::uint64_t mask, std::uint8_t layer, std::uint8_t flags) {
    Check(pawn == local.data() && mask == aim_occlusion_offsets::ShotMask && layer == 3 && flags == 15, "exact native shot filter");
    Check(Address(output) % 8 == 0, "filter alignment");
    Put(output, 0x10, std::uint64_t{0x100}); return output;
}
void Trace(void* data, const void* eye, const void* delta, void* filter, int count, bool append) {
    ++traces;
    auto nativeDelta = Get<AimVector>(delta,0);
    nativeDelta.x *= rayScale; nativeDelta.y *= rayScale; nativeDelta.z *= rayScale;
    Put(data, off::DirectionVector, nativeDelta);
    Check(Get<AimVector>(eye,0).x == 10 && Get<AimVector>(delta,0).x == 90 &&
        Get<AimVector>(delta,0).y == 0 && Get<AimVector>(delta,0).z == 0, "eye and displacement ABI");
    Check(count == 4 && append, "native penetration limit and initial append mode");
    Check(Get<std::uint64_t>(filter,0x10) == (aim_occlusion_offsets::ShotExclude | 0x100) &&
        (Get<unsigned char>(filter,0x39) & 2), "shot exclusions preserved");
    Put(data, off::HitCount, walls * 2 + 1);
    Put(data, off::SegmentCount, walls * 2 + 1);
    for (int i = 0; i < walls*2+1; ++i) {
        auto* segment = reinterpret_cast<unsigned char*>(Get<std::uintptr_t>(data, off::Segments)) + i*off::SegmentSize;
        Put(segment, 0, i * .1f); Put(segment,4, (i+1) * .1f);
        Put(segment,off::SegmentFirst, static_cast<std::uint16_t>(i));
        Put(segment,off::SegmentLast, static_cast<std::uint16_t>(i));
        Put(segment,off::SegmentFlags, static_cast<unsigned char>(i & 1));
    }
    auto* segments = reinterpret_cast<unsigned char*>(Get<std::uintptr_t>(data,off::Segments));
    if (corrupt == 1) Put(data, off::SegmentCount, off::MaxSegments+1);
    if (corrupt == 2) Put(segments, 4, std::numeric_limits<float>::quiet_NaN());
    if (corrupt == 3) Put(segments, off::SegmentLast, std::uint16_t{999});
    if (corrupt == 4) Put(segments, 4, -.1f);
    if (corrupt == 5) Put(data, off::SegmentCapacity, 0);
    if (corrupt == 6) Put(data, off::HitCount, 0);
    if (corrupt == 7) Put(segments, 4, 1.1f);
    if (corrupt == 8) Put(data, off::Segments, std::uintptr_t{1});
    if (fault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    if (managerChange) Put(memory, third_person_offsets::TraceManager, std::uintptr_t{0});
    if (pawnChange) Put(memory, cs2_offsets::globals::dwLocalPlayerPawn, std::uintptr_t{0});
    if (geometryChange) current = false;
}
bool Step(void*, void* state, void* segment, int team, void* impacts) {
    ++steps;
    Check(team == 2 && !impacts, "native shooter team and no effects output");
    const bool solid = (Get<unsigned char>(segment,off::SegmentFlags) & 1) != 0;
    if (solid) {
        const int remaining = Get<int>(state,0x10);
        if (block || remaining <= 0) return true;
        Put(state,0,Get<float>(state,0)-loss); Put(state,0x10,remaining-1);
    } else Put(state,0,Get<float>(state,0)*.99f);
    return false;
}
void Extract(void* data, void* result, float fraction, void* collision) {
    Check(Address(result) % 16 == 0, "impact alignment");
    const auto index = (Address(collision)-Get<std::uintptr_t>(data,off::Hits))/off::HitSize;
    Put(result,aim_occlusion_offsets::TraceEntity, index == static_cast<unsigned>(walls*2) && !noVictim ? Address(victim.data()) :
        foreignBody && index == 0 ? Address(foreign.data()) : std::uintptr_t{0});
    Put(result,aim_damage_offsets::TraceHitbox, index == static_cast<unsigned>(walls*2) || (foreignBody && index==0) ?
        Address(unknownGroup ? unknown.data() : hitbox.data()) : std::uintptr_t{0});
    Check(std::isfinite(fraction) && fraction >= 0 && fraction <= 1, "fraction ABI");
}
void Reset() {
    destroys = steps = traces = 0; current = true;
    block = fault = managerChange = pawnChange = geometryChange = destroyFault = foreignBody = unknownGroup = noVictim = initFail = false;
    corrupt = 0; walls = 1; loss = 10; rayScale = 1;
    Put(memory,third_person_offsets::TraceManager,Address(manager.data()));
    Put(memory,cs2_offsets::globals::dwLocalPlayerPawn,Address(local.data()));
    Put(memory,aim_damage_offsets::UnknownHitboxPointer,Address(unknown.data()));
    Put(hitbox.data(),aim_damage_offsets::HitboxGroup,2);
    Put(weaponData.data(),aim_damage_offsets::Damage,100);
    Put(weaponData.data(),aim_damage_offsets::RangeModifier,.99f);
    Put(weaponData.data(),off::Penetration,2.0f);
}
}
bool SilentAimTargetVolumeValid(const AimSource&,const AimTarget&,const AimTargetVolume&) noexcept { return current; }
int main() {
    memory = static_cast<unsigned char*>(VirtualAlloc(nullptr,cs2_offsets::ClientImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if (!memory) return 2;
    const auto client = Address(memory);
    AimAutowallTestConfigure(client,{Initialize,Destroy,Filter,Trace,Step,Extract});
    IMAGE_DOS_HEADER dos{}; dos.e_magic=IMAGE_DOS_SIGNATURE; dos.e_lfanew=0x100;
    IMAGE_NT_HEADERS64 nt{}; nt.Signature=IMAGE_NT_SIGNATURE; nt.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.TimeDateStamp=cs2_offsets::ClientTimestamp; nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.SizeOfImage=cs2_offsets::ClientImageSize;
    Put(memory,0,dos); Put(memory,0x100,nt);
    for (const auto& window:off::ContractWindows) std::memcpy(memory+window.rva,window.bytes,window.size);
    AimAutowallContext context{};
    Check(AimAutowallPrepare(client,context),"verified native contracts prepare");
    for (const auto& window:off::ContractWindows) {
        memory[window.rva]^=1;
        Check(!AimAutowallPrepare(client,context) && !context.prepared,"changed native windows reject");
        memory[window.rva]^=1;
    }
    Check(AimAutowallPrepare(client,context),"restored contracts prepare");
    AimSource source{}; source.pawn=Address(local.data());
    AimTarget target{}; target.local.pawn=source.pawn; target.local.team=2; target.enemy.pawn=Address(victim.data()); target.eye={10,20,30};
    AimTargetVolume volume{}; volume.valid=true; volume.pawn=target.enemy.pawn; volume.center={100,20,30};
    AimWeaponSnapshot weapon{}; weapon.data=Address(weaponData.data()); weapon.range=5000;
    auto run=[&] { return AimAutowallCheck(context,source,target,volume,weapon); };
    for (int count=0;count<=3;++count) {
        Reset(); walls=count;
        const auto result=run();
        Check(result.valid && result.clear && result.hitgroupKnown && result.hitgroup==2 && result.penetrations==count,
            "native path reaches victim after each allowed cover count");
        float damage=100; for(int i=0;i<count*2+1;++i) damage=(i&1)?damage-loss:damage*.99f;
        Check(std::fabs(result.penetrationDamage-damage)<.001f && result.bulletBaseDamage==100 && result.bulletPenetration==2 &&
            result.bulletRangeModifier==.99f,"copied native residual damage preserves range loss and weapon inputs");
        Check(destroys==1 && traces==1,"one native trace and cleanup per evaluation");
    }
    for (int mode=0;mode<11;++mode) {
        Reset();
        switch(mode) {
        case 0:block=true;break; case 1:fault=true;break; case 2:managerChange=true;break;
        case 3:pawnChange=true;break; case 4:geometryChange=true;break; case 5:destroyFault=true;break;
        case 6:foreignBody=true;break; case 7:unknownGroup=true;break; case 8:noVictim=true;break;
        case 9:loss=100;break; case 10:Put(hitbox.data(),aim_damage_offsets::HitboxGroup,0);break;
        }
        const auto result=run();
        Check(!result.valid && !result.clear && result.penetrationDamage==-1 && destroys==1,
            "blocked faulty stale unknown and foreign paths reject and clean up");
    }
    for(int bad=1;bad<=8;++bad) {
        Reset(); corrupt=bad;
        Check(!run().valid && destroys==1,"malformed native containers and fractions reject with cleanup");
    }
    Reset(); initFail=true; Check(!run().valid && traces==0 && destroys==0,"failed construction never enters trace");
    Reset(); rayScale=2;
    const auto extended=run();
    Check(extended.valid && std::fabs(extended.distance-54)<.001f, "native extended ray supplies the distance domain for segment fractions");
    Reset(); rayScale=100; weapon.range=90;
    Check(!run().valid && destroys==1, "native extension cannot grant damage beyond weapon range"); weapon.range=5000;
    Reset(); rayScale=std::numeric_limits<float>::quiet_NaN();
    Check(!run().valid && destroys==1, "nonfinite native displacement rejects with cleanup");
    Reset(); current=false; Check(!run().valid && traces==0,"stale geometry rejects before engine call");
    Reset(); weapon.range=89; Check(!run().valid && traces==0,"out-of-range target rejects before engine call"); weapon.range=5000;
    for(float bad:{0.0f,-1.0f,std::numeric_limits<float>::quiet_NaN(),101.0f}) {
        Reset(); Put(weaponData.data(),off::Penetration,bad);
        Check(!run().valid && traces==0,"invalid penetration power rejects before engine call");
    }
    VirtualFree(memory,0,MEM_RELEASE);
    std::printf("%u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
