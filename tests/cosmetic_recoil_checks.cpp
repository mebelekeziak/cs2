#include "cosmetic_recoil.h"
#include "cosmetic_recoil_offsets.h"
#include "viewmodel_settings.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <thread>

namespace {
namespace off = cosmetic_recoil_offsets;
namespace ent = cs2_offsets;
int checks = 0, failures = 0;
void Check(bool condition, const char* label) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", label); }
}
template<class T> void Put(void* memory, std::size_t offset, T value) {
    std::memcpy(static_cast<unsigned char*>(memory) + offset, &value, sizeof(value));
}
std::uintptr_t Address(const void* memory) { return reinterpret_cast<std::uintptr_t>(memory); }
unsigned char* clientMemory = nullptr;
std::uintptr_t client = 0;
constexpr std::uint32_t pawnHandle = 0x18003, controllerHandle = 0x20004;
std::array<unsigned char, 0x2200> pawn{};
std::array<unsigned char, 0x1000> controller{};
std::array<unsigned char, 0x100> service{}, system{};
std::array<unsigned char, 0x300> cameraService{};
std::array<unsigned char, 0x70 * 512> bucket{};
struct Output {
    std::uint32_t prefix = 0x12345678;
    std::array<float, 3> angles{9, 9, 9};
    std::uint32_t suffix = 0x89abcdef;
} output, nested;
struct Mock {
    int calls = 0, mutation = 0;
    bool forwarded = true, writeOutput = true, recurse = false, inside = false;
    void* result = nullptr;
    std::array<float, 3> angles{3, -5, 7};
    HANDLE entered = nullptr, resume = nullptr;
} mock;
struct CameraMock {
    bool base = false, nested = false, inside = false, forwarded = true;
    int calls = 0;
    std::array<float, 3> origin{}, angles{}, nestedOrigin{}, nestedAngles{};
    float fov = 0, nestedFov = 0;
    HANDLE entered = nullptr, resume = nullptr;
} cameraMock;
std::string telemetry;
void Log(const char* line) { telemetry += line; telemetry += '\n'; }
std::string Telemetry() {
    telemetry.clear();
    CosmeticRecoilTestSetLogger(Log);
    CosmeticRecoilFlushTelemetry();
    return telemetry;
}
void* Native(void* input, void* target) {
    ++mock.calls;
    mock.forwarded &= input == service.data();
    if (mock.writeOutput && target) std::memcpy(target, mock.angles.data(), sizeof(mock.angles));
    if (mock.recurse && !mock.inside) {
        mock.inside = true;
        CosmeticRecoilTestCall(input, nested.angles.data(), client + std::ptrdiff_t{0x10ca496});
        mock.inside = false;
    }
    switch (mock.mutation) {
    case 1: Put(clientMemory, ent::globals::dwLocalPlayerPawn, std::uintptr_t{0}); break;
    case 2: Put(service.data(), off::ServiceOwner, Address(pawn.data()) + 1); break;
    case 3: Put(pawn.data(), off::PunchServices, Address(service.data()) + 1); break;
    case 4: Put(controller.data(), ent::CCSPlayerController::m_hPlayerPawn, pawnHandle + 1); break;
    case 5: Put(bucket.data() + 3 * 0x70, ent::entity_layout::HandleOffset, pawnHandle + 0x8000); break;
    case 6: Put(pawn.data(), ent::C_BaseEntity::m_iHealth, 0); break;
    case 7: Put(service.data(), 0, client + off::PunchVtable + 8); break;
    case 8: Put(clientMemory, ent::globals::dwLocalPlayerController, std::uintptr_t{0}); break;
    case 9: Put(bucket.data() + 4 * 0x70, ent::entity_layout::HandleOffset, controllerHandle + 0x8000); break;
    }
    if (mock.entered) { SetEvent(mock.entered); WaitForSingleObject(mock.resume, INFINITE); }
    return mock.result ? mock.result : target;
}
void NativeAdd(const float* left, const float* right, float* target) {
    for (int axis = 0; axis < 3; ++axis) target[axis] = left[axis] + right[axis];
}
void NativeCamera(void* input, void* rawOrigin, void* rawAngles, float* fov) {
    ++cameraMock.calls;
    cameraMock.forwarded &= input == cameraService.data();
    auto* origin = static_cast<float*>(rawOrigin);
    auto* angles = static_cast<float*>(rawAngles);
    for (int axis = 0; axis < 3; ++axis) { origin[axis] = static_cast<float>(axis + 1); angles[axis] = static_cast<float>((axis + 1) * 10); }
    *fov = 90;
    if (cameraMock.nested && !cameraMock.inside) {
        cameraMock.inside = true;
        CosmeticRecoilTestCamera(input, cameraMock.nestedOrigin.data(), cameraMock.nestedAngles.data(), &cameraMock.nestedFov, cameraMock.base);
        cameraMock.inside = false;
    }
    const float viewPunch[]{8, -4, 2};
    CosmeticRecoilTestAngleAdd(angles, viewPunch, angles,
        client + (cameraMock.base ? off::ViewPunchBaseReturn : off::ViewPunchReturn));
    if (cameraMock.entered) { SetEvent(cameraMock.entered); WaitForSingleObject(cameraMock.resume, INFINITE); }
    float punch[3]{};
    CosmeticRecoilTestCall(service.data(), punch, client + (cameraMock.base ? off::CameraBaseReturn : off::CameraReturn));
    for (float& value : punch) value *= .45f;
    CosmeticRecoilTestAngleAdd(angles, punch, angles, client + off::CameraReturn + 0x23);
    angles[2] += 3; // Independent native roll/shake work must survive.
}
void Reset() {
    mock = {};
    cameraMock = {};
    output = {}; nested = {};
    pawn.fill(0); controller.fill(0); service.fill(0); system.fill(0); bucket.fill(0); cameraService.fill(0);
    Put(clientMemory, ent::globals::dwLocalPlayerPawn, Address(pawn.data()));
    Put(clientMemory, ent::globals::dwLocalPlayerController, Address(controller.data()));
    Put(clientMemory, ent::globals::dwGameEntitySystem, Address(system.data()));
    Put(system.data(), ent::entity_layout::TableOffset, Address(bucket.data()));
    Put(pawn.data(), ent::CEntityInstance::m_pEntity, Address(bucket.data() + 3 * 0x70));
    Put(bucket.data() + 3 * 0x70, 0, Address(pawn.data()));
    Put(bucket.data() + 3 * 0x70, ent::entity_layout::HandleOffset, pawnHandle);
    Put(controller.data(), ent::CEntityInstance::m_pEntity, Address(bucket.data() + 4 * 0x70));
    Put(bucket.data() + 4 * 0x70, 0, Address(controller.data()));
    Put(bucket.data() + 4 * 0x70, ent::entity_layout::HandleOffset, controllerHandle);
    Put(controller.data(), ent::CCSPlayerController::m_hPlayerPawn, pawnHandle);
    Put(pawn.data(), ent::C_BaseEntity::m_iHealth, 100);
    Put(pawn.data(), off::PunchServices, Address(service.data()));
    Put(service.data(), 0, client + off::PunchVtable);
    Put(service.data(), off::ServiceOwner, Address(pawn.data()));
    Put(pawn.data(), off::CameraServices, Address(cameraService.data()));
    Put(cameraService.data(), 0, client + off::CameraVtable);
    Put(cameraService.data(), off::ServiceOwner, Address(pawn.data()));
    ViewmodelSettings settings;
    settings.removeCameraRecoil = true;
    ViewmodelSetSettings(settings);
    CosmeticRecoilTestConfigure(client, Native);
    CosmeticRecoilTestConfigureCamera(NativeCamera, NativeCamera, NativeAdd);
}
void* Invoke(std::ptrdiff_t caller = off::CameraReturn) {
    return CosmeticRecoilTestCall(service.data(), output.angles.data(), client + caller);
}
bool NativeOutput() { return std::memcmp(output.angles.data(), mock.angles.data(), sizeof(mock.angles)) == 0; }
bool ZeroOutput() { return output.angles == std::array<float, 3>{}; }
void IndependentToggles() {
    for (int camera = 0; camera < 2; ++camera) for (int enabled = 0; enabled < 2; ++enabled) {
        for (const auto caller : {off::CameraBaseReturn, off::CameraReturn, std::ptrdiff_t{0x10ca496}, std::ptrdiff_t{0x8cd865}}) {
            Reset();
            ViewmodelSettings settings;
            settings.removeCameraRecoil = camera != 0; settings.enabled = enabled != 0;
            ViewmodelSetSettings(settings);
            const auto pawnBefore = pawn;
            const auto controllerBefore = controller;
            const auto serviceBefore = service, systemBefore = system;
            const auto bucketBefore = bucket;
            const bool selected = (caller == off::CameraBaseReturn || caller == off::CameraReturn) && camera;
            Check(Invoke(caller) == output.angles.data(), "original output pointer return and ABI preserved");
            Check(selected ? ZeroOutput() : NativeOutput(), "camera toggle independently selects exact camera callers and preserves weapon/other consumers");
            Check(mock.calls == 1 && mock.forwarded, "native executes exactly once with unchanged service/output arguments");
            Check(output.prefix == 0x12345678 && output.suffix == 0x89abcdef, "only twelve temporary angle bytes change");
            Check(pawnBefore == pawn && controllerBefore == controller && serviceBefore == service &&
                systemBefore == system && bucketBefore == bucket, "all persistent engine and gameplay source bytes remain unchanged");
        }
    }
    Reset();
    ViewmodelSettings settings; settings.enabled = true; settings.fov = 120;
    ViewmodelSetSettings(settings);
    Invoke(); Check(NativeOutput(), "position/FOV enable does not enable cosmetic recoil removal");
}
void Rejections() {
    for (int mutation = 1; mutation <= 9; ++mutation) {
        Reset(); mock.mutation = mutation;
        Invoke(); Check(NativeOutput(), "source changes inside native invalidate current edit");
    }
    for (int invalid = 0; invalid < 14; ++invalid) {
        Reset();
        switch (invalid) {
        case 0: Put(clientMemory, ent::globals::dwLocalPlayerPawn, std::uintptr_t{0}); break;
        case 1: Put(pawn.data(), ent::CEntityInstance::m_pEntity, std::uintptr_t{0}); break;
        case 2: Put(bucket.data() + 3 * 0x70, 0, Address(pawn.data()) + 1); break;
        case 3: Put(bucket.data() + 3 * 0x70, ent::entity_layout::HandleOffset, UINT32_MAX); break;
        case 4: Put(controller.data(), ent::CCSPlayerController::m_hPlayerPawn, pawnHandle + 1); break;
        case 5: Put(pawn.data(), ent::C_BaseEntity::m_iHealth, 0); break;
        case 6: Put(pawn.data(), ent::C_BaseEntity::m_lifeState, static_cast<unsigned char>(1)); break;
        case 7: Put(service.data(), off::ServiceOwner, Address(pawn.data()) + 1); break;
        case 8: Put(service.data(), 0, client + off::PunchVtable + 8); break;
        case 9: Put(pawn.data(), off::PunchServices, Address(service.data()) + 1); break;
        case 10: Put(system.data(), ent::entity_layout::TableOffset, std::uintptr_t{0}); break;
        case 11: Put(clientMemory, ent::globals::dwLocalPlayerController, std::uintptr_t{0}); break;
        case 12: Put(bucket.data() + 4 * 0x70, 0, Address(controller.data()) + 1); break;
        case 13: Put(controller.data(), ent::CEntityInstance::m_pEntity, Address(bucket.data() + 5 * 0x70)); break;
        }
        Invoke(); Check(NativeOutput(), "invalid/dead/nonlocal/stale source retains native recoil");
    }
    Reset(); mock.result = nested.angles.data();
    Check(Invoke() == nested.angles.data() && NativeOutput(), "unusual native returned storage is forwarded without touching output");
    for (float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        for (int axis = 0; axis < 3; ++axis) {
            Reset(); mock.angles[axis] = value; Invoke();
            Check(NativeOutput(), "nonfinite native angle vector remains unchanged");
        }
    }
    Reset(); mock.writeOutput = false;
    Check(CosmeticRecoilTestCall(service.data(), nullptr, client + off::CameraReturn) == nullptr,
        "null output returns native result safely");
    Check(CosmeticRecoilTestCall(service.data(), reinterpret_cast<void*>(1), client + off::CameraBaseReturn) == reinterpret_cast<void*>(1),
        "inaccessible output returns native result safely");
    auto* readonly = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Check(readonly != nullptr, "read-only output fixture allocated");
    if (readonly) {
        std::memcpy(readonly, mock.angles.data(), sizeof(mock.angles));
        DWORD previous = 0;
        Check(VirtualProtect(readonly, 4096, PAGE_READONLY, &previous) != FALSE, "read-only output fixture protected");
        CosmeticRecoilTestCall(service.data(), readonly, client + off::CameraReturn);
        Check(std::memcmp(readonly, mock.angles.data(), sizeof(mock.angles)) == 0, "read-only output retains all native bytes");
        VirtualFree(readonly, 0, MEM_RELEASE);
    }
}
void NestingAndStop() {
    Reset(); mock.recurse = true;
    Invoke();
    Check(ZeroOutput() && nested.angles == mock.angles && mock.calls == 2,
        "nested visual callback forwards native without borrowing outer source/settings");
    CosmeticRecoilStop();
    Invoke(); Check(NativeOutput(), "stopped resident hook keeps forwarding native");
    CosmeticRecoilActivate();
    Invoke(); Check(NativeOutput(), "stopped runtime cannot reactivate");
    Reset();
    mock.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    mock.resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<bool> stopReturned{false}, stopEntered{false};
    std::thread callback([] { Invoke(); });
    Check(WaitForSingleObject(mock.entered, 2000) == WAIT_OBJECT_0, "blocking native callback entered");
    std::thread stopper([&] { stopEntered.store(true); CosmeticRecoilStop(); stopReturned.store(true); });
    while (!stopEntered.load()) SwitchToThread();
    Sleep(20);
    Check(!stopReturned.load(), "cooperative stop waits for in-flight native callback and any output edit");
    SetEvent(mock.resume);
    callback.join(); stopper.join();
    Check(stopReturned.load(), "cooperative stop returns after callback barrier");
    CloseHandle(mock.entered); CloseHandle(mock.resume); mock.entered = mock.resume = nullptr;
    Invoke(); Check(NativeOutput(), "callbacks arriving after stop retain native output");
}
void Diagnostics() {
    Reset();
    Invoke(off::CameraBaseReturn); Invoke(); Invoke(std::ptrdiff_t{0x10ca496}); Invoke(0x8cd865);
    auto text = Telemetry();
    Check(text.find("calls=4 camera_base=1 camera=1 other=2") != std::string::npos &&
        text.find("selected=2 camera_edits=2 nonzero=2") != std::string::npos,
        "telemetry distinguishes exact visual callers, requested settings, edits and nonzero native punch");
    Check(text.find("remove_camera=1 active=1") != std::string::npos &&
        text.find("last_native_punch=(3.0000,-5.0000,7.0000)") != std::string::npos,
        "telemetry reports effective camera toggle and removed native vector");
    const auto first = telemetry;
    CosmeticRecoilFlushTelemetry();
    Check(telemetry == first, "telemetry flush throttles duplicate reports without callback-side file I/O");
    Reset();
    Put(service.data(), 0, client + off::PunchVtable + 8);
    Invoke(); text = Telemetry();
    Check(text.find("before_rejected=1") != std::string::npos && text.find("last_failure=service_table") != std::string::npos &&
        text.find("service_table=1 service_owner=0") != std::string::npos,
        "source diagnostics identify rejected service RTTI stage without claiming an edit");
    Reset();
    mock.mutation = 2; Invoke(); text = Telemetry();
    Check(text.find("after_rejected=1") != std::string::npos && text.find("last_failure=service_owner") != std::string::npos,
        "source diagnostics distinguish changes after native evaluation");
    Reset();
    mock.mutation = 9; Invoke(); text = Telemetry();
    Check(text.find("changed=1") != std::string::npos && text.find("camera_edits=0") != std::string::npos,
        "valid but different controller incarnation is diagnosed as a changed source");
    Reset();
    Put(system.data(), ent::entity_layout::TableOffset, std::uintptr_t{0});
    Invoke(); text = Telemetry();
    Check(text.find("last_failure=pawn_bucket") != std::string::npos && text.find("pawn_bucket=1") != std::string::npos,
        "source diagnostics identify bucket resolution failure separately from pawn identity");
    Reset();
    ViewmodelResetSettings(); Invoke(); text = Telemetry();
    Check(text.find("camera=1") != std::string::npos && text.find("selected=0") != std::string::npos &&
        text.find("remove_camera=0") != std::string::npos,
        "diagnostics distinguish disabled toggles from missing native callbacks");
    Reset();
    mock.result = nested.angles.data(); Invoke(); text = Telemetry();
    Check(text.find("return_rejected=1 output_rejected=0") != std::string::npos,
        "diagnostics distinguish native return mismatch from output accessibility");
    Reset();
    mock.angles[1] = std::numeric_limits<float>::quiet_NaN(); Invoke(); text = Telemetry();
    Check(text.find("return_rejected=0 output_rejected=1") != std::string::npos,
        "diagnostics expose rejected nonfinite native output");
}
bool Near(float a, float b) { return std::abs(a - b) < .0001f; }
void CameraComposition() {
    const std::array<float, 3> rawAngles{19.35f, 13.75f, 38.15f};
    const std::array<float, 3> cleanAngles{10, 20, 33};
    for (bool base : {false, true}) for (bool camera : {false, true}) for (bool enabled : {false, true}) {
        Reset(); cameraMock.base = base;
        if (base) Put(cameraService.data(), 0, client + off::CameraBaseVtable);
        ViewmodelSettings settings; settings.removeCameraRecoil = camera; settings.enabled = enabled;
        ViewmodelSetSettings(settings);
        const auto cameraBefore = cameraService;
        const auto pawnBefore = pawn;
        CosmeticRecoilTestCamera(cameraService.data(), cameraMock.origin.data(), cameraMock.angles.data(), &cameraMock.fov, base);
        for (int axis = 0; axis < 3; ++axis)
            Check(Near(cameraMock.angles[axis], (camera ? cleanAngles : rawAngles)[axis]),
                "native camera composition independently removes both copied view-punch and .45 visual aim-punch");
        Check(cameraMock.calls == 1 && cameraMock.forwarded && mock.calls == 1,
            "camera core and expected inner visual getter execute once with unchanged native ABI");
        Check(cameraMock.origin == std::array<float, 3>{1, 2, 3} && cameraMock.fov == 90,
            "camera punch removal preserves native origin/FOV and unrelated roll/shake");
        Check(cameraBefore == cameraService && pawnBefore == pawn,
            "camera service view-punch and all pawn state remain unchanged");
        const auto text = Telemetry();
        Check(text.find(camera ? "viewpunch_edits=1 viewpunch_nonzero=1" : "viewpunch_edits=0 viewpunch_nonzero=0") != std::string::npos,
            "camera diagnostics distinguish cleared nonzero additional view-punch from existing aim-punch");
    }
    Reset(); cameraMock.nested = true;
    CosmeticRecoilTestCamera(cameraService.data(), cameraMock.origin.data(), cameraMock.angles.data(), &cameraMock.fov);
    for (int axis = 0; axis < 3; ++axis) {
        Check(Near(cameraMock.angles[axis], cleanAngles[axis]), "outer local camera clears both visual contributions");
        Check(Near(cameraMock.nestedAngles[axis], rawAngles[axis]), "nested view cannot borrow outer camera scope for either contribution");
    }
    Reset();
    Put(cameraService.data(), off::ServiceOwner, Address(pawn.data()) + 1);
    CosmeticRecoilTestCamera(cameraService.data(), cameraMock.origin.data(), cameraMock.angles.data(), &cameraMock.fov);
    Check(Near(cameraMock.angles[0], 18), "foreign camera source retains additional view-punch; independently valid visual getter still handles its own local source");
    Reset();
    std::array<float, 3> angles{1, 2, 3}, punch{4, 5, 6}, outputAngles{};
    CosmeticRecoilTestAngleAdd(angles.data(), punch.data(), outputAngles.data(), client + off::ViewPunchReturn);
    Check(outputAngles == std::array<float, 3>{5, 7, 9}, "matching math caller outside a validated camera core remains native");
    Reset();
    cameraMock.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    cameraMock.resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<bool> returned{false};
    std::thread callback([] { CosmeticRecoilTestCamera(cameraService.data(), cameraMock.origin.data(), cameraMock.angles.data(), &cameraMock.fov); });
    Check(WaitForSingleObject(cameraMock.entered, 2000) == WAIT_OBJECT_0, "blocking camera core reaches native visual contribution");
    std::thread stopper([&] { CosmeticRecoilStop(); returned.store(true); });
    Sleep(20); Check(!returned.load(), "stop waits for outer camera scope without recursively locking expected inner getter");
    SetEvent(cameraMock.resume);
    callback.join(); stopper.join();
    Check(returned.load(), "expected getter can finish while stop waits for outer camera barrier");
    CloseHandle(cameraMock.entered); CloseHandle(cameraMock.resume); cameraMock.entered = cameraMock.resume = nullptr;
    CosmeticRecoilTestCamera(cameraService.data(), cameraMock.origin.data(), cameraMock.angles.data(), &cameraMock.fov);
    Check(Near(cameraMock.angles[0], rawAngles[0]), "stopped camera core and helper keep forwarding complete native recoil");
}

}
int main() {
    clientMemory = static_cast<unsigned char*>(VirtualAlloc(nullptr, ent::ClientImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!clientMemory) return 2;
    client = Address(clientMemory);
    IndependentToggles(); Rejections(); NestingAndStop(); Diagnostics(); CameraComposition();
    CosmeticRecoilStop(); VirtualFree(clientMemory, 0, MEM_RELEASE);
    std::printf("cosmetic recoil: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
