#include "model_swap.h"
#include "client_frame.h"
#include "model_swap_offsets.h"
#include "model_swap_settings.h"
#include "cs2_offsets.h"
#include "silent_aim_hitbox_offsets.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {
namespace off=model_swap_offsets;
namespace ent=cs2_offsets;
unsigned checks=0, failures=0;
const char* preloadRequest=nullptr;
bool preloadResult=false;
void PreloadOnOwner(void*, int) noexcept {
    if (preloadRequest) preloadResult=ModelSwapPreloadResource(preloadRequest);
}
void Check(bool value,const char* label) { ++checks; if (!value) { ++failures; std::printf("FAIL %s\n",label); } }
template<class T> void Put(void* p,std::size_t at,T value) { std::memcpy(static_cast<unsigned char*>(p)+at,&value,sizeof(value)); }
std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
unsigned char* image=nullptr;
std::uintptr_t client=0;
std::array<unsigned char,0x2200> pawn{};
std::array<unsigned char,0x1000> controller{};
std::array<unsigned char,0x500> scene{};
std::array<unsigned char,0x220> system{};
std::array<unsigned char,0x70*512> bucket{};
std::array<unsigned char,0x30> model{},owner{};
std::uintptr_t binding=0;
std::array<char,260> modelName{};
constexpr std::uint32_t pawnHandle=0x10003, controllerHandle=0x18002;
constexpr const char* nativePath="agents/models/ctm_sas/ctm_sas.vmdl";
constexpr const char* pathA="agents/models/hmn_ag2/zenless_zone_zero/miyabi/miyabi.vmdl";
constexpr const char* pathB="agents/models/hmn_ag2/zenless_zone_zero/ellen/ellen.vmdl";
constexpr const char* nativeB="agents/models/tm_phoenix/tm_phoenix.vmdl";
struct Mock {
    unsigned frames=0, sets=0;
    bool correct=true, nested=false, inNested=false;
    int mutation=0, fail=0;
    unsigned loads=0;
    bool loadReady=true;
    int loadMutation=0;
    std::string last;
    DWORD setterThread=0;
    HANDLE entered=nullptr, resume=nullptr;
} mock;
void Name(const char* path) {
    modelName.fill(0); std::snprintf(modelName.data(),modelName.size(),"%s",path);
    binding=Address(model.data());
    Put(scene.data(),off::ModelBinding,Address(&binding));
    Put(model.data(),off::ResourceName,Address(modelName.data()));
}
void Native(void* input,int stage) {
    ++mock.frames; mock.correct &= input==owner.data();
    if (mock.nested && !mock.inNested) { mock.inNested=true; ModelSwapTestFrame(input,stage); mock.inNested=false; }
    if (mock.mutation==1) Put(image,ent::globals::dwLocalPlayerPawn,std::uintptr_t{0});
    if (mock.mutation==2) Put(bucket.data()+3*0x70,0x10,std::uint32_t{0x20003});
}
void Setter(void* input,const char* path) {
    ++mock.sets; mock.correct &= input==pawn.data(); mock.last=path; mock.setterThread=GetCurrentThreadId();
    if (mock.entered) { SetEvent(mock.entered); WaitForSingleObject(mock.resume,INFINITE); }
    if (mock.fail==1) return;
    if (mock.fail==2 && (std::strcmp(path,pathA)==0 || std::strcmp(path,pathB)==0)) { Name("models/error.vmdl"); return; }
    if (mock.fail==3) { Name("models/error.vmdl"); return; }
    Name(path);
    if (mock.mutation==3) Put(image,ent::globals::dwLocalPlayerPawn,std::uintptr_t{0});
}
bool Load(const char*) {
    ++mock.loads;
    if (mock.loadMutation==1) Put(image,ent::globals::dwLocalPlayerPawn,std::uintptr_t{0});
    if (mock.loadMutation==2) Name(nativeB);
    if (mock.loadMutation==3) ModelSwapResetSettings();
    if (mock.loadMutation==4) ModelSwapStop("reentrant native loader stop");
    if (mock.loadMutation==5) Put(pawn.data(),ent::C_BaseEntity::m_iHealth,0);
    return mock.loadReady;
}
void Select(const char* path,bool enabled=true) {
    ModelSwapSettings value; value.enabled=enabled;
    Check(ModelSwapSetModel(value,path),"test path accepted"); ModelSwapSetSettings(value);
}
void Frame(int stage=off::ApplyStage) { ModelSwapTestFrame(owner.data(),stage); }
bool Current(const char* path) { return !std::strcmp(modelName.data(),path); }
void Reset() {
    pawn.fill(0);controller.fill(0);scene.fill(0);system.fill(0);bucket.fill(0);owner.fill(0);model.fill(0);mock={};
    Put(image,ent::globals::dwLocalPlayerPawn,Address(pawn.data()));
    Put(image,ent::globals::dwLocalPlayerController,Address(controller.data()));
    Put(image,ent::globals::dwGameEntitySystem,Address(system.data()));
    Put(system.data(),ent::entity_layout::TableOffset,Address(bucket.data()));
    Put(bucket.data()+3*0x70,0,Address(pawn.data()));
    Put(bucket.data()+3*0x70,0x10,pawnHandle);
    Put(bucket.data()+2*0x70,0,Address(controller.data()));
    Put(bucket.data()+2*0x70,0x10,controllerHandle);
    Put(pawn.data(),ent::CEntityInstance::m_pEntity,Address(bucket.data()+3*0x70));
    Put(controller.data(),ent::CEntityInstance::m_pEntity,Address(bucket.data()+2*0x70));
    Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,pawnHandle);
    Put(controller.data(),ent::CBasePlayerController::m_bIsLocalPlayerController,std::uint8_t{1});
    Put(pawn.data(),ent::C_BasePlayerPawn::m_hController,controllerHandle);
    Put(pawn.data(),ent::C_BasePlayerPawn::m_hDefaultController,UINT32_MAX);
    Put(pawn.data(),ent::C_BaseEntity::m_iHealth,100);
    Put(pawn.data(),ent::C_BaseEntity::m_iTeamNum,std::uint8_t{3});
    Put(pawn.data(),ent::C_BaseEntity::m_pGameSceneNode,Address(scene.data()));
    Put(scene.data(),0,client+silent_aim_hitbox_offsets::PlayerSkeletonVtable);
    Put(owner.data(),0,client+off::FrameVtable);
    Name(nativePath);
    ModelSwapTestConfigure(client,&Native,&Setter,&Load);ModelSwapActivate();
}
}
int main() {
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,ent::ClientImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if (!image) return 2;
    client=Address(image);
    Reset();
    Check(!ModelSwapPreloadResource("weapons/models/knife/knife_karam.vmdl"), "shared resource loader rejects calls outside native frame owner");
    Check(ClientFrameSubscribe(&PreloadOnOwner), "cosmetics can share the verified model loader");
    for (const char* resource : {"weapons/models/knife/knife_karam.vmdl", nativePath}) {
        preloadRequest=resource; preloadResult=false; Frame();
        Check(preloadResult, "shared loader accepts installed weapon and character resource roots on the owner");
    }
    const auto loaded=mock.loads;
    for (const char* resource : {"weapons/models/../bad.vmdl", "weapons/models//bad.vmdl", "weapons/models/bad.vmdl_c", "C:/bad.vmdl"}) {
        preloadRequest=resource; preloadResult=true; Frame();
        Check(!preloadResult && mock.loads==loaded, "shared loader rejects noncanonical resource paths without native calls");
    }
    preloadRequest=nullptr;
    Reset(); Frame(); Check(mock.frames==1 && !mock.sets && Current(nativePath),"disabled callback forwards without model writes");
    Select(pathA); Frame(off::ApplyStage-1); Check(!mock.sets,"other frame stages never assign");
    Frame(); Check(mock.sets==1 && Current(pathA) && mock.correct,"native setter applies selected model after frame original");
    for (int i=0;i<100;++i) Frame();
    Check(mock.sets==1 && mock.loads==1,"stable model is not loaded or assigned every frame");
    Select(pathB); Frame(); Check(Current(pathB) && mock.sets==2,"selection changes replace the local model");
    ModelSwapResetSettings(); Frame(); Check(Current(nativePath) && mock.sets==3,"disable restores original across multiple selections");
    Frame(); Check(mock.sets==3,"restoration occurs once");

    Reset(); Select(pathA); Frame(); Name(nativeB); Frame();
    Check(Current(pathA),"new native model baseline is replaced while enabled");
    ModelSwapResetSettings(); Frame(); Check(Current(nativeB),"disable restores latest native baseline");
    Reset(); Select(pathA); Frame(); Name(nativeB); ModelSwapResetSettings(); Frame();
    Check(Current(nativeB) && mock.sets==1,"disable preserves native replacement already made by engine");

    Reset(); Select(pathA); Frame(); ModelSwapStop("test");
    Check(Current(pathA) && mock.sets==1,"stop never calls entity setter from requesting thread");
    Frame(); Check(Current(nativePath) && mock.sets==2,"stopped resident callback restores on native owner");
    Select(pathB);ModelSwapActivate();Frame();Check(Current(nativePath) && mock.sets==2,"stopped module cannot reactivate swaps");

    Reset(); Select(pathA); Frame();
    Put(image,ent::globals::dwLocalPlayerPawn,std::uintptr_t{0});ModelSwapStop("disconnect");Frame();
    Check(mock.sets==1,"disconnect drops stale ownership without calling old pawn");
    Reset();Select(pathA);Frame();
    const std::uint32_t newHandle=0x20003;
    Put(bucket.data()+3*0x70,0x10,newHandle);Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,newHandle);
    Name(nativeB); Frame();ModelSwapResetSettings();Frame();
    Check(Current(nativeB),"reused address with new serialized handle gets fresh baseline");
    Reset();Select(pathA);Frame();Put(pawn.data(),ent::C_BaseEntity::m_iHealth,0);Frame();
    Check(Current(nativePath),"death restores while exact local identity remains available");

    for (int invalid=0;invalid<11;++invalid) {
        Reset();Select(pathA);
        if (invalid==0) Put(controller.data(),ent::CCSPlayerController::m_hPlayerPawn,std::uint32_t{0x20003});
        if (invalid==1) Put(bucket.data()+3*0x70,0x10,std::uint32_t{0x20003});
        if (invalid==2) Put(bucket.data()+2*0x70,0x10,std::uint32_t{0x20002});
        if (invalid==3) Put(controller.data(),ent::CBasePlayerController::m_bIsLocalPlayerController,std::uint8_t{0});
        if (invalid==4) Put(pawn.data(),ent::C_BasePlayerPawn::m_hController,UINT32_MAX);
        if (invalid==5) Put(pawn.data(),ent::C_BaseEntity::m_iTeamNum,std::uint8_t{1});
        if (invalid==6) Put(scene.data(),ent::CGameSceneNode::m_bDormant,std::uint8_t{1});
        if (invalid==7) Put(scene.data(),0,std::uintptr_t{0});
        if (invalid==8) Put(scene.data(),off::ModelBinding,std::uintptr_t{0});
        if (invalid==9) modelName.fill('a');
        if (invalid==10) Put(owner.data(),0,std::uintptr_t{0});
        Frame();Check(!mock.sets,"invalid identity/owner/resource never invokes setter");
    }
    Reset();Select(pathA);ModelSwapTestSetAvailable(false);Frame();Check(!mock.sets,"preset selection absent from validated catalog cannot load");
    Reset();Select(pathA);mock.mutation=1;Frame();Check(!mock.sets,"source removed during original frame is rejected");
    Reset();Select(pathA);mock.mutation=2;Frame();Check(!mock.sets,"serial changed during original frame is rejected");
    Reset();Select(pathA);mock.mutation=3;Frame();ModelSwapStop("changed in setter");Frame();Check(mock.sets==1,"source invalidated in native setter is never restored through stale pointer");
    Reset();Select(pathA);mock.nested=true;Frame();Check(mock.frames==2 && mock.sets==1,"reentrant native frames forward and only outer frame assigns");

    Reset();Select(pathA);mock.fail=1;Frame();Frame();Check(mock.sets==1 && Current(nativePath),"failed assignment remains native and is not retried every frame");
    Select(pathA,false);Frame();mock.fail=0;Select(pathA);Frame();Check(Current(pathA),"toggle retries previously failed selection");
    Reset();Select(pathA);mock.fail=2;Frame();
    Check(Current(nativePath) && mock.sets==2,"unexpected error model rolls back to native baseline on the same owner");
    Reset();Select(pathA);mock.fail=3;Frame();mock.fail=0;Frame();
    Check(Current(nativePath) && mock.sets==3,"failed rollback keeps restoration ownership for a subsequent native frame");
    Frame();Check(mock.sets==3,"failed selection remains suppressed after deferred rollback succeeds");
    Reset();Select(pathA);Frame();ModelSwapResetSettings();mock.fail=3;Frame();mock.fail=0;Frame();
    Check(Current(nativePath),"failed disable restoration retries original rather than retaining an error resource");
    Reset();Select(pathA);mock.loadReady=false;Frame();Frame();
    Check(!mock.sets && mock.loads==1 && Current(nativePath),"failed preload never passes placeholder to native setter and does not retry each frame");
    Select(pathB);Frame();Select(pathA);mock.loadReady=true;Frame();
    Check(Current(pathA),"changing selection clears failed-load cache even when returning to a prior selection");
    for (int mutation=1;mutation<=5;++mutation) {
        Reset();Select(pathA);mock.loadMutation=mutation;Frame();
        Check(!mock.sets,"source, native model or selected setting changed during blocking load prevents assignment");
    }

    Reset();Select(pathA);
    const auto entered=CreateEventW(nullptr,TRUE,FALSE,nullptr), resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    mock.entered=entered;mock.resume=resume;
    std::atomic<bool> done=false;
    std::thread nativeThread([]{ Frame(); });
    const bool enteredOk=WaitForSingleObject(entered,5000)==WAIT_OBJECT_0;
    Check(enteredOk,"native setter entered controlled shutdown test");
    std::thread stopThread([&]{ ModelSwapStop("drain");done.store(true); });
    Check(!done.load(),"stop cannot finish while native assignment is in flight");
    SetEvent(resume);nativeThread.join();stopThread.join();
    mock.entered=nullptr;mock.resume=nullptr;
    Check(done.load() && Current(pathA),"stop drains assignment but leaves owner-thread restoration queued");
    Frame();Check(Current(nativePath) && mock.setterThread==GetCurrentThreadId(),"restoration executes on next native callback thread");
    CloseHandle(entered);CloseHandle(resume);
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("%u model swap checks; %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
