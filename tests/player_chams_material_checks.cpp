#include "player_chams_materials.h"
#include "client_frame.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
int checks=0,failures=0,constructs=0,destroys=0,parses=0,creates=0,preloads=0,modes=0;
bool parseSuccess=true,preloadSuccess=true,createSuccess=true,rootSuccess=true;
bool alive=false,blockCreate=false,reenter=false;
HANDLE entered=nullptr,releaseCreate=nullptr;
std::uintptr_t bindings[]{0x100001,0x100002};
void* expectedArena=nullptr;
void Check(bool result,const char* label) {
    ++checks;
    if (!result) { ++failures; std::fprintf(stderr,"FAIL %s\n",label); }
}
void* Construct(void* arena,unsigned mode) {
    ++constructs; Check(!alive && mode==0 && !(reinterpret_cast<std::uintptr_t>(arena)&15),"native arena lifetime/alignment");
    expectedArena=arena; alive=true; return arena;
}
void* Root(void* arena) { Check(alive && arena==expectedArena,"root requires constructed arena"); return rootSuccess ? static_cast<unsigned char*>(arena)+0x78 : nullptr; }
void Destroy(void* arena) { ++destroys; Check(alive && arena==expectedArena,"destroy constructed arena once"); alive=false; }
bool Parse(void* node,void* error,const char* text,const void* format,const char* name,unsigned flags) {
    ++parses; Check(alive && node==static_cast<unsigned char*>(expectedArena)+0x78 && !error && format && name && !*name && !flags,"parser ABI and owner");
    Check(text && std::strstr(text,"format:generic:version{") && std::strstr(text,"shader ="),"native material source schema");
    return parseSuccess;
}
void* Create(void* manager,void** output,const char* name,void* node,unsigned flags,bool finalFlag) {
    ++creates; Check(manager && alive && node && flags==1 && finalFlag && name,"material create ABI consumes live root");
    if (reenter) PlayerChamsMaterialsOwnerFrame();
    if (blockCreate) { SetEvent(entered); WaitForSingleObject(releaseCreate,INFINITE); }
    const unsigned style=std::strstr(name,"iridescent") ? 1u : 0u;
    *output=createSuccess ? &bindings[style] : nullptr; return output;
}
std::uintptr_t Mode(void* material,void* view) {
    ++modes; Check(material && view==reinterpret_cast<void*>(0x12345030),"per-view native mode argument"); return 0x444444;
}
bool Preload(const char* path) { ++preloads; Check(path && std::strstr(path,".vtex"),"load dependency before create"); return preloadSuccess; }
void Reset() {
    constructs=destroys=parses=creates=preloads=modes=0;
    parseSuccess=preloadSuccess=createSuccess=rootSuccess=true;
    alive=blockCreate=reenter=false;
    PlayerChamsMaterialsTestConfigure({Construct,Root,Destroy,Parse,Create,Mode,Preload});
    PlayerChamsResetSettings();
    Check(PlayerChamsMaterialsInitialize(),"mock initialize");
}
void Enable() { auto settings=PlayerChamsGetSettings(); settings.enabled=true; PlayerChamsSetSettings(settings); }
bool Resolve(PlayerChamsStyle style=PlayerChamsStyle::Flat) {
    std::uintptr_t material=99,mode=99;
    const bool result=PlayerChamsMaterialsResolve(style,0x12345000,material,mode);
    Check(result ? (material && mode) : (!material && !mode),"resolver clears failed output"); return result;
}
}
bool ClientFrameSubscribe(ClientFrameCallback,ClientFrameLogCallback) noexcept { return true; }

int main() {
    Reset(); PlayerChamsMaterialsOwnerFrame();
    Check(!constructs && !preloads,"disabled defaults never load resources");
    Check(!Resolve(),"resolve unavailable before prepare");
    Enable(); reenter=true; PlayerChamsMaterialsOwnerFrame();
    Check(constructs==2 && destroys==2 && creates==2 && preloads==2 && !alive,"two immutable materials and dependency loads");
    Check(Resolve() && Resolve(PlayerChamsStyle::Iridescent),"both styles published");
    for (int i=0;i<40;++i) {
        auto settings=PlayerChamsGetSettings(); settings.color[0]=i/40.0f; settings.iridescentColor[1]=i/40.0f;
        PlayerChamsSetSettings(settings); PlayerChamsMaterialsOwnerFrame();
    }
    Check(constructs==2 && creates==2 && preloads==2,"palette edits do not recreate or leak native resources");
    Check(!Resolve(static_cast<PlayerChamsStyle>(44)),"invalid style rejected");
    PlayerChamsMaterialsStop(); Check(!Resolve(),"stop disarms publication");
    PlayerChamsMaterialsOwnerFrame(); Check(creates==2,"stop forbids new creation");

    Reset(); Enable(); parseSuccess=false; PlayerChamsMaterialsOwnerFrame();
    Check(constructs==1 && destroys==1 && !creates && !alive,"parse failure destroys native arena");
    Check(!Resolve(),"parse failure publishes nothing");
    PlayerChamsMaterialsOwnerFrame(); Check(parses==1,"failed prepare does not retry every frame");
    Reset(); Enable(); rootSuccess=false; PlayerChamsMaterialsOwnerFrame();
    Check(constructs==1 && destroys==1 && !parses && !creates && !alive,"missing root cleanup");
    Reset(); Enable(); preloadSuccess=false; PlayerChamsMaterialsOwnerFrame();
    Check(!constructs && !creates && preloads==1,"dependency failure leaves draw unchanged");
    Reset(); Enable(); createSuccess=false; PlayerChamsMaterialsOwnerFrame();
    Check(constructs==1 && destroys==1 && creates==1 && !Resolve(),"null native binding cleanup");

    Reset(); Enable(); blockCreate=true;
    entered=CreateEventW(nullptr,TRUE,FALSE,nullptr); releaseCreate=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::atomic<bool> stopped{false};
    HANDLE stopComplete=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread owner([]{PlayerChamsMaterialsOwnerFrame();});
    Check(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0,"owner entered native material create");
    std::thread stopper([&]{PlayerChamsMaterialsStop();stopped.store(true);SetEvent(stopComplete);});
    Check(WaitForSingleObject(stopComplete,5000)==WAIT_OBJECT_0,"stop returns while native create is blocked");
    SetEvent(releaseCreate); owner.join(); stopper.join();
    Check(stopped.load() && !Resolve() && !alive && destroys==1,"in-flight creation cleanup stays resident and never publishes after stop");
    Check(std::strstr(PlayerChamsMaterialsStatus(),"stopped")!=nullptr,"late preparation preserves stop status");
    CloseHandle(entered); CloseHandle(releaseCreate); CloseHandle(stopComplete);
    std::printf("%d player material checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
