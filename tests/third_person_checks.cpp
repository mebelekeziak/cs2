#include "third_person.h"
#include "third_person_settings.h"
#include "third_person_view.h"
#include "third_person_offsets.h"
#include "cs2_offsets.h"
#include "movement_offsets.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>

namespace {
namespace off = third_person_offsets;
namespace ent = cs2_offsets;
using View = std::array<unsigned char, 0x600>;
unsigned failures = 0, checks = 0;
void Check(bool condition, const char* label) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", label); }
}
std::uintptr_t Address(const void* pointer) { return reinterpret_cast<std::uintptr_t>(pointer); }
template<class T> void Put(void* pointer, std::size_t offset, const T& value) {
    std::memcpy(static_cast<unsigned char*>(pointer) + offset, &value, sizeof(value));
}
template<class T> T Get(const void* pointer, std::size_t offset) {
    T value{}; std::memcpy(&value, static_cast<const unsigned char*>(pointer) + offset, sizeof(value)); return value;
}
bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }
bool Near(CameraVector a, CameraVector b) { return Near(a.x,b.x) && Near(a.y,b.y) && Near(a.z,b.z); }

unsigned char* clientMemory = nullptr;
std::uintptr_t client = 0;
std::array<unsigned char, 0x1600> pawn{};
std::array<unsigned char, 0x1000> controller{};
std::array<unsigned char, 0x70> identity{};
std::array<unsigned char, 0x30> mode{};
std::array<unsigned char, 16> manager{};
View view{}, nestedView{};
struct Mocks {
    CameraVector eye{100,200,64}, angles{};
    CameraVector traceEye{}, traceEnd{};
    float fraction = 1;
    bool traceReturn = true, writeFraction = true;
    bool mutateOriginalSource = false, mutateTraceSource = false, mutateOriginalMode = false;
    bool disableDuringOriginal = false, nested = false, inNested = false;
    bool forwarded = true, traceContract = true, filterContract = true, shapeContract = true;
    int viewCalls = 0, queryCalls = 0, shapeCalls = 0, resultCalls = 0, filterCalls = 0, traceCalls = 0;
    int cameraQuery = -1, visibilityQuery = -1, otherCallerQuery = -1, nestedCameraQuery = -1, restoredCameraQuery = -1;
    void* queryInput = nullptr;
    int querySlot = -1;
    void* shape = nullptr; void* filter = nullptr; void* result = nullptr;
    HANDLE entered = nullptr, resume = nullptr;
} mock;

int OriginalQuery(void* input, int slot) {
    ++mock.queryCalls; mock.queryInput = input; mock.querySlot = slot;
    return 37 + slot;
}
void OriginalView(void* nativeMode, void* nativeView) {
    ++mock.viewCalls;
    mock.forwarded &= nativeMode == mode.data() && (nativeView == view.data() || nativeView == nestedView.data() || !nativeView);
    if (nativeView) {
        std::memset(nativeView, 0xA5, view.size());
        Put(nativeView, off::ViewOrigin, mock.eye);
        Put(nativeView, off::ViewAngles, mock.angles);
    }
    void* input = clientMemory + movement_offsets::Input;
    const int camera = ThirdPersonTestQuery(input, 0, client + off::IsThirdPersonCameraReturn);
    if (mock.inNested) mock.nestedCameraQuery = camera;
    else {
        mock.cameraQuery = camera;
        mock.visibilityQuery = ThirdPersonTestQuery(input, 0);
        mock.otherCallerQuery = ThirdPersonTestQuery(input, 0, client + off::IsThirdPersonCameraReturn + 1);
    }
    if (mock.nested && !mock.inNested) {
        mock.inNested = true;
        ThirdPersonTestView(mode.data(), nestedView.data());
        mock.inNested = false;
        mock.restoredCameraQuery = ThirdPersonTestQuery(input,0,client + off::IsThirdPersonCameraReturn);
    }
    if (mock.mutateOriginalSource) Put(clientMemory, ent::globals::dwLocalPlayerPawn, std::uintptr_t{0});
    if (mock.mutateOriginalMode) Put(mode.data(), off::ModeSlot, int{1});
    if (mock.disableDuringOriginal) ThirdPersonResetSettings();
    if (mock.entered) {
        SetEvent(mock.entered);
        WaitForSingleObject(mock.resume, INFINITE);
    }
}
void Shape(void* output, const float* hull) {
    ++mock.shapeCalls; mock.shape = output;
    mock.shapeContract &= Address(output) % 16 == 0;
    for (unsigned index = 0; index < 6; ++index)
        mock.shapeContract &= hull[index] == (index < 3 ? -9.0f : 9.0f);
    std::memset(output, 0x53, off::ShapeSize);
}
void Result(void* output) {
    ++mock.resultCalls; mock.result = output;
    mock.traceContract &= Address(output) % 16 == 0;
    std::memset(output, 0x54, off::TraceSize);
    Put(output, off::TraceFraction, 1.0f);
}
void Filter(void* output, void* skipPawn, std::uint64_t mask, std::uint8_t layer, std::uint8_t flags) {
    ++mock.filterCalls; mock.filter = output;
    mock.filterContract &= Address(output) % 8 == 0 && skipPawn == pawn.data() && mask == 0x80 && layer == 3 && flags == 0x0F;
    std::array<unsigned char, off::FilterSize> expected{};
    Put(expected.data(), 0, client + off::FilterVtable);
    for (const auto offset : {0x20,0x24,0x28,0x2C}) Put(expected.data(), offset, UINT32_MAX);
    Put(expected.data(), 0x34, static_cast<std::uint16_t>(UINT16_MAX));
    expected[0x37] = 0x0F; expected[0x38] = 3; expected[0x39] = 0x49;
    mock.filterContract &= std::memcmp(output, expected.data(), expected.size()) == 0;
    Put(output, 0x08, mask);
    Put(output, 0x10, std::uint64_t{0x20});
    Put(output, 0x20, std::uint32_t{0xCAFE});
}
bool Trace(void* nativeManager, void* shape, const void* start, const void* end, void* filter, void* result) {
    ++mock.traceCalls;
    mock.traceContract &= nativeManager == manager.data() && shape == mock.shape && filter == mock.filter && result == mock.result;
    mock.traceEye = Get<CameraVector>(start, 0); mock.traceEnd = Get<CameraVector>(end, 0);
    mock.traceContract &= Get<std::uint64_t>(filter,0x08) == 0x80 && Get<std::uint64_t>(filter,0x10) == 0xC0020 &&
        Get<std::uint32_t>(filter,0x20) == 0xCAFE && Get<float>(result,off::TraceFraction) == 1;
    for (std::size_t index = 0; index < off::ShapeSize; ++index)
        mock.traceContract &= static_cast<unsigned char*>(shape)[index] == 0x53;
    if (mock.writeFraction) Put(result, off::TraceFraction, mock.fraction);
    if (mock.mutateTraceSource) Put(clientMemory, ent::globals::dwLocalPlayerPawn, std::uintptr_t{0});
    return mock.traceReturn;
}
void Reset() {
    mock = Mocks{};
    pawn.fill(0xD1); controller.fill(0xC2); identity.fill(0xB3); mode.fill(0xA4);
    view.fill(0xEE); nestedView.fill(0xEF);
    Put(clientMemory, ent::globals::dwLocalPlayerPawn, Address(pawn.data()));
    Put(clientMemory, ent::globals::dwLocalPlayerController, Address(controller.data()));
    Put(clientMemory, off::TraceManager, Address(manager.data()));
    Put(clientMemory, off::InputPointer, client + movement_offsets::Input);
    Put(pawn.data(), ent::CEntityInstance::m_pEntity, Address(identity.data()));
    Put(pawn.data(), ent::C_BaseEntity::m_iHealth, int{100});
    Put(pawn.data(), ent::C_BaseEntity::m_lifeState, std::uint8_t{0});
    Put(identity.data(),0,Address(pawn.data()));
    Put(identity.data(),ent::entity_layout::HandleOffset,std::uint32_t{0x10003});
    Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,std::uint32_t{0x10003});
    Put(mode.data(),0,client + off::ModeNormalVtable);
    Put(mode.data(),off::ModeSlot,int{0});
    ThirdPersonSetSettings({true,120,0,0,true});
}
View NativeView() {
    View native{}; native.fill(0xA5);
    Put(native.data(),off::ViewOrigin,mock.eye); Put(native.data(),off::ViewAngles,mock.angles);
    return native;
}
void CheckOrigin(CameraVector expected, const char* label) {
    Check(Near(Get<CameraVector>(view.data(),off::ViewOrigin), expected),label);
    View native = NativeView(), observed = view;
    Put(observed.data(),off::ViewOrigin,mock.eye);
    Check(observed == native,"only the current view origin bytes change; angles and opaque fields remain native");
}
void CheckUnchanged(const char* label) { Check(view == NativeView(),label); }
void Draw() { ThirdPersonTestView(mode.data(),view.data()); }

void MathChecks() {
    const CameraVector eye{100,200,64}; CameraVector destination{};
    ThirdPersonSettings settings{true,120,0,0,true};
    struct Direction { CameraVector angles, expected; };
    for (const auto& direction : std::array<Direction,6>{{
        {{0,0,0},{-20,200,64}}, {{0,90,0},{100,80,64}},
        {{0,180,0},{220,200,64}}, {{0,270,0},{100,320,64}},
        {{90,0,0},{100,200,184}}, {{-90,0,0},{100,200,-56}} }}) {
        Check(ThirdPersonDestination(eye,direction.angles,settings,destination) && Near(destination,direction.expected),
              "cardinal yaw and pitch put camera behind native view");
    }
    settings.shoulder = 20; settings.height = 10;
    Check(ThirdPersonDestination(eye,{0,0,0},settings,destination) && Near(destination,{-20,180,74}),
          "positive shoulder is camera right and height follows world Z");
    Check(ThirdPersonDestination(eye,{0,90,0},settings,destination) && Near(destination,{120,80,74}),
          "shoulder rotates with native yaw");
    Check(ThirdPersonDestination(eye,{0,0,90},settings,destination) && Near(destination,{-20,200,54}),
          "native roll rotates shoulder axis while height stays in world Z");
    settings.shoulder = -20; settings.height = -10;
    Check(ThirdPersonDestination(eye,{0,360,0},settings,destination) && Near(destination,{-20,220,54}),
          "negative offsets and wrapped angles remain finite");
    const CameraVector untouched{7,8,9}; destination = untouched;
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    Check(!ThirdPersonDestination({nan,0,0},{},settings,destination) && destination == untouched,
          "nonfinite eye rejects without modifying output");
    Check(!ThirdPersonDestination(eye,{0,nan,0},settings,destination) && destination == untouched,
          "nonfinite angles reject without modifying output");
    Check(!ThirdPersonDestination({1000000,0,0},{},settings,destination) && destination == untouched,
          "implausible native coordinates reject without modifying output");
    const CameraVector end{-20,160,84};
    for (const float fraction : {0.0f,0.5f,1.0f}) {
        CameraVector result{};
        const CameraVector expected{100-120*fraction,200-40*fraction,64+20*fraction};
        Check(ThirdPersonClip(eye,end,fraction,result) && Near(result,expected),"trace fraction clips full displacement including offsets");
    }
    for (const float fraction : {-0.01f,1.01f,nan,std::numeric_limits<float>::infinity()}) {
        CameraVector result = untouched;
        Check(!ThirdPersonClip(eye,end,fraction,result) && result == untouched,"invalid trace fraction rejects without modifying output");
    }
    CameraVector result = untouched;
    Check(!ThirdPersonClip(eye,{nan,0,0},1,result) && result == untouched,"invalid clip endpoint leaves output untouched");
}

void ViewChecks() {
    Reset(); ThirdPersonResetSettings(); Draw();
    CheckUnchanged("disabled camera preserves entire native view");
    Check(mock.viewCalls == 1 && mock.forwarded && mock.cameraQuery == 37 && mock.visibilityQuery == 37 && mock.traceCalls == 0,
          "disabled camera forwards native view and query without tracing");
    Reset();
    const auto originalPawn = pawn;
    const auto originalController = controller;
    const auto originalIdentity = identity;
    const auto originalMode = mode;
    std::array<unsigned char,0x1000> inputBefore{};
    std::memcpy(inputBefore.data(),clientMemory + movement_offsets::Input,inputBefore.size());
    Check(ThirdPersonTestQuery(clientMemory + movement_offsets::Input,0) == 37,
          "activation alone leaves native visibility until a view is successfully translated");
    Draw(); CheckOrigin({-20,200,64},"enabled camera translates native eye by configured distance");
    Check(mock.viewCalls == 1 && mock.forwarded && mock.cameraQuery == 0 && mock.visibilityQuery == 1 && mock.otherCallerQuery == 1,
          "only exact native camera caller is suppressed inside this view; visibility queries remain third person");
    Check(ThirdPersonTestQuery(clientMemory + movement_offsets::Input,0,client + off::IsThirdPersonCameraReturn) == 1,
          "native camera query outside scoped view keeps third-person visibility");
    Check(mock.shapeCalls == 1 && mock.resultCalls == 1 && mock.filterCalls == 1 && mock.traceCalls == 1 &&
          mock.shapeContract && mock.filterContract && mock.traceContract && Near(mock.traceEye,mock.eye) && Near(mock.traceEnd,{-20,200,64}),
          "native hull, filter baseline, skip pawn, mask, flags, exclusions and trace arguments are preserved");
    Check(pawn == originalPawn && controller == originalController && identity == originalIdentity && mode == originalMode &&
          std::memcmp(inputBefore.data(),clientMemory + movement_offsets::Input,inputBefore.size()) == 0,
          "camera leaves input, entity, controller, identity and mode storage untouched");
    void* foreign = controller.data();
    Check(ThirdPersonTestQuery(foreign,0) == 37 && mock.queryInput == foreign && mock.querySlot == 0,
          "foreign input query forwards exact arguments and return value");
    void* input = clientMemory + movement_offsets::Input;
    Check(ThirdPersonTestQuery(input,1) == 38 && mock.queryInput == input && mock.querySlot == 1,
          "other player-slot query forwards exact arguments and return value");
    Reset(); mock.nested = true; Draw();
    CheckOrigin({-20,200,64},"outer view resumes third-person edit after nested native callback");
    Check(mock.viewCalls == 2,"nested view forwards its own native callback once");
    Check(mock.traceCalls == 1,"nested native view does not invoke another override sweep");
    Check(mock.nestedCameraQuery == 37,"nested view forwards original camera query instead of inheriting outer suppression");
    Check(mock.restoredCameraQuery == 0,"nested view restores outer callback's query suppression");
    Check(nestedView == NativeView(),"nested native view bytes do not receive third-person translation");
    Reset(); mock.fraction = 0.5f; ThirdPersonSetSettings({true,120,20,10,true}); Draw();
    CheckOrigin({40,190,69},"collision clips distance, shoulder and height together");
    for (const float fraction : {0.0f,1.0f}) {
        Reset(); mock.fraction = fraction; Draw();
        CheckOrigin({100-120*fraction,200,64},"blocked and clear trace endpoints retain valid native fraction semantics");
    }
    Reset(); mock.traceReturn = false; mock.fraction = 0.5f; Draw();
    CheckOrigin({40,200,64},"false trace bool still consumes a valid collision fraction");
    Reset(); mock.traceReturn = false; mock.writeFraction = false; Draw();
    CheckOrigin({-20,200,64},"unobstructed false trace retains native initializer's clear fraction");
    for (const float fraction : {-1.0f,1.1f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        Reset(); Draw(); mock.fraction = fraction; Draw();
        CheckUnchanged("invalid native trace fraction preserves original native view");
        Check(ThirdPersonTestQuery(clientMemory + movement_offsets::Input,0) == 37,
              "rejected trace clears previously published model visibility");
    }
    Reset(); ThirdPersonSetSettings({true,200,-15,25,false}); Put(clientMemory,off::TraceManager,std::uintptr_t{0}); Draw();
    CheckOrigin({-100,215,89},"disabled collision applies new distance and signed offsets without trace manager");
    Check(mock.traceCalls == 0,"collision switch bypasses native sweep");
    ThirdPersonResetSettings(); Draw(); CheckUnchanged("settings reset disables next camera view without stale displacement");
    Reset(); mock.disableDuringOriginal = true; Draw();
    CheckOrigin({-20,200,64},"an in-flight view uses one coherent settings snapshot");
    Draw(); CheckUnchanged("settings changed during callback take effect on next view");
    Reset(); ThirdPersonTestView(mode.data(),nullptr);
    Check(mock.viewCalls == 1 && mock.forwarded && mock.traceCalls == 0,"unreadable view forwards safely and cannot be edited");
}

void SourceChecks() {
    const auto rejected = [](const char* label) {
        Draw(); CheckUnchanged(label); Check(mock.traceCalls == 0,"invalid initial source never invokes native sweep");
    };
    Reset(); Put(mode.data(),0,client + off::ModeNormalVtable + 8); rejected("foreign view mode keeps native view");
    Reset(); Put(clientMemory,off::InputPointer,std::uintptr_t{0}); rejected("unavailable native input singleton keeps native view");
    Reset(); Put(clientMemory,off::InputPointer,Address(controller.data())); rejected("unexpected native input singleton keeps native view");
    Reset(); Put(mode.data(),off::ModeSlot,int{1}); rejected("secondary view slot keeps native view");
    Reset(); Put(mode.data(),0,client + off::ModeSharedVtable); Draw();
    CheckOrigin({-20,200,64},"verified shared base mode accepts primary slot");
    Put(identity.data(),ent::entity_layout::HandleOffset,std::uint32_t{0x20003});
    Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,std::uint32_t{0x20003});
    Check(ThirdPersonTestQuery(clientMemory + movement_offsets::Input,0) == 37,
          "matching but newly serialized entity cannot reuse previous visibility publication");
    Draw();
    Check(ThirdPersonTestQuery(clientMemory + movement_offsets::Input,0) == 1,
          "successfully translated new entity serial publishes its own visibility");
    Reset(); Put(pawn.data(),ent::C_BaseEntity::m_iHealth,int{0}); rejected("dead pawn health keeps native view");
    Check(ThirdPersonTestQuery(clientMemory + movement_offsets::Input,0) == 37,"dead pawn query preserves native visibility result");
    Reset(); Put(pawn.data(),ent::C_BaseEntity::m_lifeState,std::uint8_t{1}); rejected("non-alive life state keeps native view");
    Reset(); Put(identity.data(),0,Address(controller.data())); rejected("identity resolving to different entity keeps native view");
    Reset(); Put(identity.data(),ent::entity_layout::HandleOffset,UINT32_MAX); rejected("invalid entity handle keeps native view");
    Reset(); Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,std::uint32_t{0x20003}); rejected("stale controller handle serial keeps native view");
    Reset(); Put(clientMemory,ent::globals::dwLocalPlayerPawn,std::uintptr_t{0}); rejected("missing local pawn keeps native view");
    Reset(); Put(clientMemory,ent::globals::dwLocalPlayerController,std::uintptr_t{1}); rejected("unreadable controller keeps native view");
    Reset(); mock.mutateOriginalSource = true; Draw(); CheckUnchanged("source changed in original callback cannot edit old view");
    Check(mock.traceCalls == 0,"source invalidation before sweep prevents native trace");
    Reset(); mock.mutateOriginalMode = true; Draw(); CheckUnchanged("mode slot changed in original callback cannot edit old view");
    Reset(); mock.mutateTraceSource = true; Draw(); CheckUnchanged("source changed during native trace cannot edit old view");
    Check(mock.traceCalls == 1,"source revalidation occurs after trace callback");
    Reset(); Draw(); Put(clientMemory,off::TraceManager,std::uintptr_t{0}); Draw(); CheckUnchanged("missing trace manager leaves native view");
    Check(ThirdPersonTestQuery(clientMemory + movement_offsets::Input,0) == 37,
          "missing trace manager clears previous model visibility");
    for (const CameraVector eye : {CameraVector{std::numeric_limits<float>::quiet_NaN(),0,0},CameraVector{1000000,0,0}}) {
        Reset(); mock.eye = eye; Draw(); CheckUnchanged("invalid native eye leaves all native view bytes untouched");
        Check(mock.traceCalls == 0,"invalid coordinates never reach native trace");
    }
    Reset(); mock.angles.y = std::numeric_limits<float>::infinity(); Draw(); CheckUnchanged("nonfinite native angles preserve original view");
}

void StopChecks() {
    Reset();
    Draw();
    Check(ThirdPersonTestQuery(clientMemory + movement_offsets::Input,0) == 1,
          "successful view publishes model visibility before shutdown disarm test");
    mock.entered = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    mock.resume = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE stopEntered = CreateEventW(nullptr,TRUE,FALSE,nullptr), stopDone = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if (!mock.entered || !mock.resume || !stopEntered || !stopDone) {
        Check(false,"shutdown test synchronization created"); return;
    }
    std::thread render([] { Draw(); });
    Check(WaitForSingleObject(mock.entered,5000) == WAIT_OBJECT_0,"native original callback blocks inside active view scope");
    std::thread stopper([&] { SetEvent(stopEntered); ThirdPersonStop("camera checks"); SetEvent(stopDone); });
    Check(WaitForSingleObject(stopEntered,5000) == WAIT_OBJECT_0,"cooperative stop thread started");
    // Observe the stop's release store before testing that the exclusive barrier waits.
    bool stoppedQuery = false;
    for (unsigned attempt = 0; attempt < 2000; ++attempt) {
        if (ThirdPersonTestQuery(clientMemory + movement_offsets::Input,0) == 37) { stoppedQuery = true; break; }
        Sleep(1);
    }
    Check(stoppedQuery && WaitForSingleObject(stopDone,30) == WAIT_TIMEOUT,"stop disables new queries but waits for active view callback to drain");
    SetEvent(mock.resume); render.join(); stopper.join();
    Check(WaitForSingleObject(stopDone,0) == WAIT_OBJECT_0 && std::strcmp(ThirdPersonStatus(),"third person stopped and resident") == 0,
          "stop completes after view drains and reports resident code");
    CloseHandle(mock.entered); CloseHandle(mock.resume); CloseHandle(stopEntered); CloseHandle(stopDone);
    mock.entered = mock.resume = nullptr;
    const int traceCalls = mock.traceCalls, viewCalls = mock.viewCalls;
    Draw(); CheckUnchanged("stopped hook still forwards the original view without edits");
    Check(mock.viewCalls == viewCalls + 1 && mock.traceCalls == traceCalls && mock.cameraQuery == 37,
          "stopped callbacks preserve original forwarding and do not trace");
    ThirdPersonActivate(); Draw(); CheckUnchanged("stopped resident hooks cannot reactivate");
    Check(ThirdPersonTestQuery(clientMemory + movement_offsets::Input,0) == 37,"query remains native after attempted reactivation");
}
void PublishedViewChecks() {
    const auto published = [] {
        return ThirdPersonViewActive(Address(pawn.data()), Address(identity.data()),
            Address(controller.data()), 0x10003);
    };
    ThirdPersonTestConfigure(client,OriginalView,OriginalQuery,Shape,Result,Filter,Trace);
    Reset();
    Check(!published(),"third-person switch alone does not publish an active view");
    Draw();
    Check(published(),"successfully translated current primary view is published");
    Sleep(270);
    Check(!published(),"inactive camera callbacks expire the translated-view publication");
    Draw();
    Check(!ThirdPersonViewActive(Address(pawn.data()),Address(identity.data()),Address(controller.data()),0x20003),
        "active-view query rejects a different serialized pawn handle");
    Put(pawn.data(),ent::C_BaseEntity::m_iHealth,0);
    Check(!published(),"active-view query immediately rejects death without another view callback");
    Put(pawn.data(),ent::C_BaseEntity::m_iHealth,100);
    Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,std::uint32_t{0x20003});
    Check(!published(),"active-view query rejects stale controller ownership");
    Reset(); Draw(); ThirdPersonResetSettings();
    Check(!published(),"disabling third person immediately invalidates active-view query");
    Reset(); Draw(); Put(mode.data(),off::ModeSlot,1); Draw();
    Check(!published(),"native secondary view clears translated-view publication");
    Reset(); Draw(); ThirdPersonStop("published-view checks");
    Check(!published(),"cooperative stop clears active-view publication");
    ThirdPersonTestConfigure(client,OriginalView,OriginalQuery,Shape,Result,Filter,Trace);
}
}

int main() {
    MathChecks();
    clientMemory = static_cast<unsigned char*>(VirtualAlloc(nullptr,ent::ClientImageSize,MEM_COMMIT | MEM_RESERVE,PAGE_READWRITE));
    Check(clientMemory != nullptr,"fake verified-client address space allocated");
    if (!clientMemory) return 1;
    client = Address(clientMemory);
    ThirdPersonTestConfigure(client,OriginalView,OriginalQuery,Shape,Result,Filter,Trace);
    ViewChecks(); SourceChecks(); PublishedViewChecks(); StopChecks();
    ThirdPersonResetSettings();
    VirtualFree(clientMemory,0,MEM_RELEASE);
    std::printf("%u/%u third-person checks passed\n",checks-failures,checks);
    return failures ? 1 : 0;
}
