#include "aim_shoot_origin.h"
#include "aim_shoot_origin_offsets.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
namespace off = aim_shoot_origin_offsets;
namespace ent = cs2_offsets;
unsigned checks = 0, failures = 0;
unsigned char* memory = nullptr;
std::uintptr_t client = 0;
std::array<unsigned char, 0x2200> pawn{};
std::array<unsigned char, 0x1000> controller{};
std::array<unsigned char, 0x100> services{}, system{}, camera{}, observer{};
std::array<unsigned char, 0x200> scene{};
std::array<unsigned char, 0x70> identity{}, controllerIdentity{};
AimSource source{};
AimShootOriginContext context{};
struct Mock { AimVector position{30, -40, 62}; int calls = 0, mutation = 0; bool arguments = true; } mock;
template<class T> void Put(void* p, std::size_t at, T value) {
    std::memcpy(static_cast<unsigned char*>(p) + at, &value, sizeof(value));
}
std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
void Check(bool condition, const char* label) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", label); }
}
AimVector* Getter(void* service, AimVector* out) {
    ++mock.calls;
    mock.arguments &= service == services.data() && out != nullptr;
    *out = mock.position;
    switch (mock.mutation) {
    case 1: Put(pawn.data(), off::WeaponServices, std::uintptr_t{0}); break;
    case 2: Put(identity.data(), ent::entity_layout::HandleOffset, std::uint32_t{0x18003}); break;
    case 3: Put(services.data(), off::ServicePawn, std::uintptr_t{0}); break;
    case 4: Put(pawn.data(), ent::C_BaseEntity::m_pGameSceneNode, std::uintptr_t{0}); break;
    case 5: Put(pawn.data(), off::CameraServices, std::uintptr_t{0}); break;
    case 6: Put(pawn.data(), off::ObserverServices, std::uintptr_t{0}); break;
    case 7: Put(pawn.data(), ent::C_BaseEntity::m_iHealth, 0); break;
    case 8: return nullptr;
    case 9: return reinterpret_cast<AimVector*>(services.data());
    }
    return out;
}
AimVector* PartialGetter(void*, AimVector* out) { out->x = 1; return out; }
AimVector* FaultGetter(void*, AimVector*) { RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr); return nullptr; }
void Reset() {
    pawn.fill(0); controller.fill(0); services.fill(0); scene.fill(0); system.fill(0);
    identity.fill(0); controllerIdentity.fill(0); camera.fill(0); observer.fill(0); mock = {};
    Put(memory, ent::globals::dwLocalPlayerPawn, Address(pawn.data()));
    Put(memory, ent::globals::dwLocalPlayerController, Address(controller.data()));
    Put(memory, ent::globals::dwGameEntitySystem, Address(system.data()));
    Put(memory, off::ServicesVtable + off::GetterSlot, client + off::Getter);
    Put(memory, off::PawnVtable + off::PawnGetterSlot, client + off::PawnGetter);
    Put(memory, off::PawnVtable + off::ViewOffsetSlot, client + off::ViewOffsetGetter);
    Put(pawn.data(), 0, client + off::PawnVtable);
    Put(pawn.data(), ent::CEntityInstance::m_pEntity, Address(identity.data()));
    Put(identity.data(), 0, Address(pawn.data()));
    Put(identity.data(), ent::entity_layout::HandleOffset, std::uint32_t{0x10003});
    Put(controller.data(), ent::CEntityInstance::m_pEntity, Address(controllerIdentity.data()));
    Put(controllerIdentity.data(), 0, Address(controller.data()));
    Put(controllerIdentity.data(), ent::entity_layout::HandleOffset, std::uint32_t{0x10002});
    Put(controller.data(), ent::CCSPlayerController::m_hPlayerPawn, std::uint32_t{0x10003});
    Put(pawn.data(), ent::C_BasePlayerPawn::m_hController, std::uint32_t{0x10002});
    Put(pawn.data(), ent::C_BaseEntity::m_iHealth, 100);
    Put(pawn.data(), ent::C_BaseEntity::m_pGameSceneNode, Address(scene.data()));
    Put(pawn.data(), off::WeaponServices, Address(services.data()));
    Put(pawn.data(), off::CameraServices, Address(camera.data()));
    Put(pawn.data(), off::ObserverServices, Address(observer.data()));
    Put(services.data(), 0, client + off::ServicesVtable);
    Put(services.data(), off::ServicePawn, Address(pawn.data()));
    source = {Address(system.data()), Address(controller.data()), Address(pawn.data())};
    context = {client, &Getter};
}
void Positive() {
    Reset();
    const auto beforePawn = pawn;
    const auto beforeServices = services;
    const auto beforeScene = scene;
    AimShootOriginSnapshot out{};
    Check(AimShootOriginRead(context, source, out) && out.valid, "current native origin copied");
    Check(out.position.x == 30 && out.position.y == -40 && out.position.z == 62, "native coordinates preserved");
    Check(out.services == Address(services.data()) && out.identity == Address(identity.data()) &&
        out.scene == Address(scene.data()) && out.handle == 0x10003 && out.controllerHandle == 0x10002,
        "full current source ownership copied");
    Check(mock.calls == 1 && mock.arguments, "services this-pointer and caller-owned float3 output ABI");
    Check(pawn == beforePawn && services == beforeServices && scene == beforeScene, "adapter makes no direct source writes");
    Check(AimShootOriginStillCurrent(context, source, out) && mock.calls == 2, "fresh native position query on revalidation");
    mock.position.x += .001f;
    Check(!AimShootOriginStillCurrent(context, source, out), "sub-unit movement invalidates staged origin");
    Reset(); mock.position = {};
    Check(AimShootOriginRead(context, source, out), "world zero is a valid native position");
    Reset(); Put(pawn.data(), off::CameraServices, std::uintptr_t{0}); Put(pawn.data(), off::ObserverServices, std::uintptr_t{0});
    Check(AimShootOriginRead(context, source, out), "native fallback accepts absent optional services");
}
void Reject() {
    for (int test = 0; test < 26; ++test) {
        Reset(); AimShootOriginSnapshot out{};
        switch (test) {
        case 0: source.pawn = 0; break;
        case 1: context.client = 0; break;
        case 2: context.getter = nullptr; break;
        case 3: Put(memory, ent::globals::dwLocalPlayerPawn, std::uintptr_t{0}); break;
        case 4: Put(memory, ent::globals::dwLocalPlayerController, std::uintptr_t{0}); break;
        case 5: Put(memory, ent::globals::dwGameEntitySystem, std::uintptr_t{0}); break;
        case 6: Put(identity.data(), 0, std::uintptr_t{0}); break;
        case 7: Put(identity.data(), ent::entity_layout::HandleOffset, UINT32_MAX); break;
        case 8: Put(controllerIdentity.data(), 0, std::uintptr_t{0}); break;
        case 9: Put(controllerIdentity.data(), ent::entity_layout::HandleOffset, UINT32_MAX); break;
        case 10: Put(controller.data(), ent::CCSPlayerController::m_hPlayerPawn, std::uint32_t{0x18003}); break;
        case 11: Put(pawn.data(), ent::C_BasePlayerPawn::m_hController, std::uint32_t{0x18002}); break;
        case 12: Put(pawn.data(), ent::C_BaseEntity::m_iHealth, 0); break;
        case 13: Put(pawn.data(), ent::C_BaseEntity::m_lifeState, std::uint8_t{1}); break;
        case 14: Put(scene.data(), ent::CGameSceneNode::m_bDormant, std::uint8_t{1}); break;
        case 15: Put(pawn.data(), 0, std::uintptr_t{0}); break;
        case 16: Put(memory, off::PawnVtable + off::PawnGetterSlot, std::uintptr_t{0}); break;
        case 17: Put(memory, off::PawnVtable + off::ViewOffsetSlot, std::uintptr_t{0}); break;
        case 18: Put(memory, off::ServicesVtable + off::GetterSlot, std::uintptr_t{0}); break;
        case 19: Put(services.data(), 0, std::uintptr_t{0}); break;
        case 20: Put(services.data(), off::ServicePawn, std::uintptr_t{0}); break;
        case 21: mock.position.x = std::numeric_limits<float>::quiet_NaN(); break;
        case 22: mock.position.y = std::numeric_limits<float>::infinity(); break;
        case 23: mock.position.z = 1000001; break;
        case 24: context.getter = &PartialGetter; break;
        case 25: context.getter = &FaultGetter; break;
        }
        Check(!AimShootOriginRead(context, source, out) && !out.valid, "invalid source or native output rejected");
    }
    for (int mutation = 1; mutation <= 9; ++mutation) {
        Reset(); mock.mutation = mutation; AimShootOriginSnapshot out{};
        Check(!AimShootOriginRead(context, source, out) && !out.valid, "mutation during getter or foreign return pointer rejected");
    }
    for (int change = 0; change < 5; ++change) {
        Reset(); AimShootOriginSnapshot out{};
        Check(AimShootOriginRead(context, source, out), "baseline before late source mutation");
        if (change == 0) Put(pawn.data(), off::WeaponServices, std::uintptr_t{0});
        if (change == 1) Put(pawn.data(), off::CameraServices, std::uintptr_t{0});
        if (change == 2) Put(pawn.data(), off::ObserverServices, std::uintptr_t{0});
        if (change == 3) Put(pawn.data(), ent::C_BaseEntity::m_pGameSceneNode, std::uintptr_t{0});
        if (change == 4) out.valid = false;
        Check(!AimShootOriginStillCurrent(context, source, out), "late lifecycle change cancels staged origin");
    }
    Reset(); AimShootOriginTestRuntime(false); AimShootOriginSnapshot out{};
    Check(!AimShootOriginRead(context, source, out), "unloaded or replaced module rejected");
    AimShootOriginTestRuntime(true);
}
void Prepare() {
    Reset();
    IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 0x100;
    IMAGE_NT_HEADERS64 nt{}; nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64; nt.FileHeader.TimeDateStamp = ent::ClientTimestamp;
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC; nt.OptionalHeader.SizeOfImage = ent::ClientImageSize;
    Put(memory, 0, dos); Put(memory, 0x100, nt);
    std::memcpy(memory + off::Getter, off::GetterBytes, sizeof(off::GetterBytes));
    std::memcpy(memory + off::PawnGetter, off::PawnGetterBytes, sizeof(off::PawnGetterBytes));
    std::memcpy(memory + off::ViewOffsetGetter, off::ViewOffsetGetterBytes, sizeof(off::ViewOffsetGetterBytes));
    std::memcpy(memory + off::BaseEye, off::BaseEyeBytes, sizeof(off::BaseEyeBytes));
    AimShootOriginContext prepared{};
    Check(AimShootOriginPrepare(client, prepared) && prepared.client == client &&
        prepared.getter == reinterpret_cast<AimShootOriginFn>(client + off::Getter), "matching installed PE and ABI fingerprints prepare");
    for (auto at : {off::Getter, off::PawnGetter, off::ViewOffsetGetter, off::BaseEye}) {
        memory[at] ^= 1;
        Check(!AimShootOriginPrepare(client, prepared) && !prepared.client && !prepared.getter, "changed native function prefix rejects and clears context");
        memory[at] ^= 1;
    }
    nt.FileHeader.TimeDateStamp ^= 1; Put(memory, 0x100, nt);
    Check(!AimShootOriginPrepare(client, prepared), "changed client identity rejected");
}
}

int main() {
    memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, ent::ClientImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory) return 2;
    client = Address(memory); AimShootOriginTestRuntime(true);
    Positive(); Reject(); Prepare();
    AimShootOriginTestRuntime(false); VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("aim_shoot_origin_checks: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
