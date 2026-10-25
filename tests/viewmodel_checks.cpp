#include "viewmodel.h"
#include "viewmodel_settings.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>

namespace {
namespace ent = cs2_offsets;
unsigned checks = 0, failures = 0;
void Check(bool condition, const char* label) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", label); }
}
template<class T> void Put(void* p, std::size_t offset, T value) {
    std::memcpy(static_cast<unsigned char*>(p) + offset, &value, sizeof(value));
}
std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
unsigned char* clientMemory = nullptr;
std::uintptr_t client = 0;
std::array<unsigned char, 0x2200> pawn{};
std::array<unsigned char, 0x1000> controller{}, alternateController{};
std::array<unsigned char, 0x70> identity{}, alternateIdentity{};
constexpr std::uint32_t pawnHandle = 0x18003;
struct Output {
    std::array<std::uint32_t, 4> before{0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00};
    std::array<float, 3> xyz{-1, -2, -3};
    std::uint32_t between = 0x13579bdf;
    float fov = -4;
    std::array<std::uint32_t, 4> after{0xfedcba98, 0x76543210, 0xabcdef12, 0x34567890};
    bool operator==(const Output&) const = default;
};
Output output{}, nestedOutput{};
struct Call { void* pawn = nullptr; void* xyz = nullptr; float* fov = nullptr; };
struct Mock {
    std::array<float, 3> xyz{2.5f, -1.25f, 4};
    float fov = 68;
    std::array<Call, 8> calls{};
    unsigned count = 0;
    int mutation = 0;
    bool write = true, nested = false, inside = false, nestedFov = false, nestedFovResult = true;
    float nestedFovOutput = -321;
    HANDLE entered = nullptr, resume = nullptr;
} mock;
void ChangeSource(int change) {
    switch (change) {
    case 1: Put(clientMemory, ent::globals::dwLocalPlayerPawn, std::uintptr_t{}); break;
    case 2: Put(clientMemory, ent::globals::dwLocalPlayerController, std::uintptr_t{}); break;
    case 3: Put(pawn.data(), ent::CEntityInstance::m_pEntity, std::uintptr_t{1}); break;
    case 4: Put(identity.data(), ent::entity_layout::HandleOffset, pawnHandle + 0x8000); break;
    case 5: Put(controller.data(), ent::CCSPlayerController::m_hPlayerPawn, pawnHandle + 0x8000); break;
    case 6: Put(pawn.data(), ent::C_BaseEntity::m_lifeState, std::uint8_t{1}); break;
    case 7: Put(pawn.data(), ent::C_BaseEntity::m_iHealth, 0); break;
    case 8:
        Put(clientMemory, ent::globals::dwLocalPlayerController, Address(alternateController.data())); break;
    case 9: Put(pawn.data(), ent::CEntityInstance::m_pEntity, Address(alternateIdentity.data())); break;
    case 10: Put(identity.data(), 0, Address(pawn.data()) + 8); break;
    case 11: Put(identity.data(), ent::entity_layout::HandleOffset, UINT32_MAX); break;
    case 12: Put(clientMemory, ent::globals::dwLocalPlayerPawn, std::uintptr_t{1}); break;
    case 13: Put(clientMemory, ent::globals::dwLocalPlayerController, std::uintptr_t{1}); break;
    default: break;
    }
}
void Native(void* input, void* xyz, float* fov) {
    if (mock.count < mock.calls.size()) mock.calls[mock.count] = {input, xyz, fov};
    ++mock.count;
    if (mock.write) {
        if (xyz) std::memcpy(xyz, mock.xyz.data(), sizeof(mock.xyz));
        if (fov) *fov = mock.fov;
    }
    if (mock.nested && !mock.inside) {
        mock.inside = true;
        ViewmodelTestParameters(input, nestedOutput.xyz.data(), &nestedOutput.fov);
        mock.inside = false;
    }
    if (mock.nestedFov)
        mock.nestedFovResult = ViewmodelUnzoomedFov(Address(input), mock.nestedFovOutput);
    ChangeSource(mock.mutation);
    if (mock.entered) {
        SetEvent(mock.entered);
        WaitForSingleObject(mock.resume, INFINITE);
    }
}
void Reset() {
    pawn.fill(0xa5); controller.fill(0x5a); identity.fill(0x3c);
    Put(clientMemory, ent::globals::dwLocalPlayerPawn, Address(pawn.data()));
    Put(clientMemory, ent::globals::dwLocalPlayerController, Address(controller.data()));
    Put(pawn.data(), ent::CEntityInstance::m_pEntity, Address(identity.data()));
    Put(identity.data(), 0, Address(pawn.data()));
    Put(identity.data(), ent::entity_layout::HandleOffset, pawnHandle);
    Put(controller.data(), ent::CCSPlayerController::m_hPlayerPawn, pawnHandle);
    Put(pawn.data(), ent::C_BaseEntity::m_iHealth, 100);
    Put(pawn.data(), ent::C_BaseEntity::m_lifeState, std::uint8_t{});
    alternateController = controller; alternateIdentity = identity;
    output = Output{}; nestedOutput = Output{}; mock = Mock{};
    ViewmodelSetSettings({true, 1.5f, -2, 3.25f, 99});
    ViewmodelTestConfigure(client, &Native);
}
Output NativeOutput() {
    Output expected;
    expected.xyz = mock.xyz; expected.fov = mock.fov;
    return expected;
}
void Parameters() { ViewmodelTestParameters(pawn.data(), output.xyz.data(), &output.fov); }
struct SourceSnapshot {
    decltype(pawn) pawnBytes;
    decltype(controller) controllerBytes, alternateControllerBytes;
    decltype(identity) identityBytes, alternateIdentityBytes;
    std::uintptr_t localPawn, localController;
    bool operator==(const SourceSnapshot&) const = default;
};
SourceSnapshot Snapshot() {
    std::uintptr_t localPawn = 0, localController = 0;
    std::memcpy(&localPawn, clientMemory + ent::globals::dwLocalPlayerPawn, sizeof(localPawn));
    std::memcpy(&localController, clientMemory + ent::globals::dwLocalPlayerController, sizeof(localController));
    return {pawn, controller, alternateController, identity, alternateIdentity, localPawn, localController};
}
void Runtime() {
    Reset(); const auto source = Snapshot(); Parameters();
    auto expected = NativeOutput(); expected.xyz = {4, -3.25f, 7.25f}; expected.fov = 99;
    Check(output == expected, "position offsets add to native XYZ and selected FOV replaces native base, preserving surrounding canaries");
    Check(mock.count == 1 && mock.calls[0].pawn == pawn.data() && mock.calls[0].xyz == output.xyz.data() && mock.calls[0].fov == &output.fov,
        "native callback receives exact pawn and output addresses once");
    Check(Snapshot() == source, "viewmodel edit leaves all source, identity and controller bytes unchanged");
    Reset(); ViewmodelSetSettings({false, 10, -10, 10, 120, true}); Parameters();
    Check(output == NativeOutput(), "disabled XYZ/FOV override forwards native even when cosmetic camera recoil removal is enabled");
    Reset(); ViewmodelSetSettings({true, 0, 0, 0, 40}); Parameters();
    expected = NativeOutput(); expected.fov = 40;
    Check(output == expected, "zero offsets preserve native position with absolute low FOV");
    Reset(); ViewmodelSetSettings({true, -10, 10, -10, 120}); Parameters();
    expected = NativeOutput(); expected.xyz = {-7.5f, 8.75f, -6}; expected.fov = 120;
    Check(output == expected, "signed offset and upper FOV endpoints reach caller outputs");
    for (int change = 1; change <= 13; ++change) {
        Reset(); ChangeSource(change); const auto invalid = Snapshot(); Parameters();
        // Alternate objects are valid sources before a call; changing to them
        // during the callback is covered separately below.
        if (change != 8 && change != 9)
            Check(output == NativeOutput(), "dead, inaccessible, stale, serial-mismatched or nonlocal source keeps native outputs");
        else {
            expected = NativeOutput(); expected.xyz = {4, -3.25f, 7.25f}; expected.fov = 99;
            Check(output == expected, "fresh reciprocal controller or identity replacement is resolved on the current call");
        }
        Check(Snapshot() == invalid, "rejected source is never repaired or otherwise changed by the hook");
    }
    Reset(); ViewmodelTestParameters(reinterpret_cast<void*>(1), output.xyz.data(), &output.fov);
    Check(output == NativeOutput() && mock.calls[0].pawn == reinterpret_cast<void*>(1), "nonlocal callback forwards exact input and retains native outputs");
    for (int change = 1; change <= 13; ++change) {
        Reset(); mock.mutation = change; Parameters();
        Check(output == NativeOutput(), "source lifecycle change during native callback prevents stale viewmodel edit");
    }
    for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        Reset(); mock.fov = bad; Parameters();
        Check(output.xyz == mock.xyz && std::memcmp(&output.fov, &bad, sizeof(bad)) == 0,
            "nonfinite native FOV prevents partial position or FOV replacement");
        for (unsigned axis = 0; axis < 3; ++axis) {
            Reset(); mock.xyz[axis] = bad; Parameters();
            Check(std::memcmp(output.xyz.data(), mock.xyz.data(), sizeof(mock.xyz)) == 0 && output.fov == mock.fov,
                "nonfinite native XYZ prevents partial output replacement");
        }
    }
}
void InvalidOutputs() {
    for (bool badPosition : {false, true}) {
        Reset(); mock.write = false; const auto before = output;
        ViewmodelTestParameters(pawn.data(), badPosition ? reinterpret_cast<void*>(1) : output.xyz.data(),
            badPosition ? &output.fov : reinterpret_cast<float*>(1));
        Check(output == before && mock.count == 1, "inaccessible destination preserves the other output and still forwards native once");
        Reset(); mock.write = false;
        ViewmodelTestParameters(pawn.data(), badPosition ? nullptr : output.xyz.data(), badPosition ? &output.fov : nullptr);
        Check(output == Output{} && mock.count == 1, "null output prevents partial writes");
    }
    void* readonly = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Check(readonly != nullptr, "read-only destination fixture allocated");
    if (!readonly) return;
    std::array<float, 3> bytes{1, 2, 3};
    std::memcpy(readonly, bytes.data(), sizeof(bytes));
    DWORD old = 0;
    if (VirtualProtect(readonly, 4096, PAGE_READONLY, &old)) {
        Reset(); mock.write = false;
        ViewmodelTestParameters(pawn.data(), readonly, &output.fov);
        Check(output == Output{} && std::memcmp(readonly, bytes.data(), sizeof(bytes)) == 0,
            "read-only XYZ destination leaves FOV and destination bytes intact");
        Reset(); mock.write = false;
        ViewmodelTestParameters(pawn.data(), output.xyz.data(), static_cast<float*>(readonly));
        Check(output == Output{} && std::memcmp(readonly, bytes.data(), sizeof(bytes)) == 0,
            "read-only FOV destination leaves XYZ and destination bytes intact");
    } else Check(false, "read-only fixture protection applied");
    VirtualFree(readonly, 0, MEM_RELEASE);
}
void UnzoomedFov() {
    Reset(); float fov = -123; const auto source = Snapshot();
    ViewmodelSetSettings({}); mock.fov = 73.5f;
    Check(ViewmodelUnzoomedFov(Address(pawn.data()), fov) && fov == 73.5f && mock.count == 1,
        "scope unzoom uses current native viewmodel FOV with custom controls disabled");
    Check(Snapshot() == source, "scope unzoom getter does not persist changes into engine sources");
    Reset(); fov = -123;
    Check(ViewmodelUnzoomedFov(Address(pawn.data()), fov) && fov == 99 && mock.count == 1,
        "scope unzoom uses selected absolute viewmodel FOV when controls enabled");
    for (float bad : {0.0f, -1.0f, 179.0f, 200.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        for (bool enabled : {false, true}) {
            Reset(); ViewmodelSetSettings({enabled}); mock.fov = bad; fov = -123;
            Check(!ViewmodelUnzoomedFov(Address(pawn.data()), fov) && fov == -123,
                "invalid native base FOV rejects unzoom without publishing a replacement");
        }
    }
    for (int change = 1; change <= 13; ++change) {
        Reset(); mock.mutation = change; fov = -123;
        Check(!ViewmodelUnzoomedFov(Address(pawn.data()), fov) && fov == -123,
            "unzoom getter rejects source lifecycle changes inside native helper");
    }
    Reset(); fov = -123;
    Check(!ViewmodelUnzoomedFov(Address(pawn.data()) + 8, fov) && fov == -123 && mock.count == 0,
        "nonlocal unzoom request avoids native helper entirely");
}
void Reentry() {
    Reset(); mock.nested = true; Parameters();
    auto expected = NativeOutput(); expected.xyz = {4, -3.25f, 7.25f}; expected.fov = 99;
    Check(mock.count == 2 && output == expected && nestedOutput == NativeOutput(),
        "nested parameter callback forwards native while outer callback applies exactly once");
    Reset(); mock.nestedFov = true; Parameters();
    Check(mock.count == 1 && !mock.nestedFovResult && mock.nestedFovOutput == -321,
        "unzoom request nested inside parameter callback cannot borrow its source or recurse");
    Reset(); mock.nested = true; float fov = -123;
    Check(ViewmodelUnzoomedFov(Address(pawn.data()), fov) && fov == 99 && mock.count == 2 && nestedOutput == NativeOutput(),
        "parameter callback nested inside unzoom getter forwards native without edits");
}
void StopBarrier() {
    for (bool getter : {false, true}) {
        Reset(); mock.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        mock.resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!mock.entered || !mock.resume) { Check(false, "stop fixture events allocated"); return; }
        std::atomic<bool> stopStarted{false}, stopReturned{false};
        float fov = -123;
        std::thread callback([&] {
            if (getter) ViewmodelUnzoomedFov(Address(pawn.data()), fov);
            else Parameters();
        });
        Check(WaitForSingleObject(mock.entered, 3000) == WAIT_OBJECT_0, "native callback owns outputs before concurrent stop");
        std::thread stopper([&] { stopStarted.store(true); ViewmodelStop(); stopReturned.store(true); });
        while (!stopStarted.load()) SwitchToThread();
        Sleep(20);
        Check(!stopReturned.load(), "cooperative stop waits for in-flight parameter or unzoom callback");
        SetEvent(mock.resume); callback.join(); stopper.join();
        Check(stopReturned.load(), "stop completes once native callback and any output edit have drained");
        CloseHandle(mock.entered); CloseHandle(mock.resume); mock.entered = mock.resume = nullptr;
        output = Output{}; Parameters();
        Check(output == NativeOutput(), "callbacks arriving after stop still forward native without edits");
        ViewmodelActivate(); output = Output{}; Parameters();
        Check(output == NativeOutput(), "activation cannot restart a stopped resident runtime");
        const auto count = mock.count; fov = -123;
        Check(!ViewmodelUnzoomedFov(Address(pawn.data()), fov) && fov == -123 && mock.count == count,
            "stopped unzoom getter does not call native or publish output");
    }
}
}
int main() {
    clientMemory = static_cast<unsigned char*>(VirtualAlloc(nullptr, ent::ClientImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!clientMemory) return 2;
    client = Address(clientMemory);
    Runtime(); InvalidOutputs(); UnzoomedFov(); Reentry(); StopBarrier();
    VirtualFree(clientMemory, 0, MEM_RELEASE);
    std::printf("viewmodel checks: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
