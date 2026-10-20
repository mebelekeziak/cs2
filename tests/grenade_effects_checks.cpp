#include "grenade_effects.h"
#include "grenade_effect_offsets.h"
#include "grenade_settings.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace {
int checks=0, failures=0, smokeCalls=0, flashCalls=0, opacityCalls=0;
bool stopInsideOpacity=false;
void Check(bool value, const char* label) {
    ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", label); }
}
std::array<void*,6> smokeArgs{};
void* flashCollection=nullptr;
std::uint32_t flashKey=0;
std::array<float,4> flashValues{};
int smokeCount=0;
std::uintptr_t Smoke(void* a,void* b,void* c,int n,void* d,void* e,void* f) {
    ++smokeCalls; smokeArgs={a,b,c,d,e,f}; smokeCount=n; return 0x12345678;
}
std::uintptr_t Flash(void* a,std::uint32_t b,const float* c) {
    ++flashCalls; flashCollection=a; flashKey=b;
    if (c) std::memcpy(flashValues.data(),c,sizeof(flashValues));
    return 0xABCDEF;
}
float Opacity(void* pawn) {
    ++opacityCalls;
    ++*static_cast<int*>(pawn); // Native bookkeeping must still execute.
    if (stopInsideOpacity) GrenadeEffectsStop();
    return 0.75f;
}
}
int main() {
    constexpr std::uintptr_t base=0x10000000;
    std::uintptr_t smokeTable=base+grenade_effect_offsets::SmokeDescriptor;
    int flashTable=123;
    const float alphas[4]{0.1f,0.2f,0.3f,0.4f};
    std::uintptr_t otherTable=base+123;
    int a=1,b=2,c=3,d=4,e=5,bookkeeping=0;
    const auto smoke=[&](void* descriptor) {
        return GrenadeEffectsTestSmoke(descriptor,&a,&b,-7,&c,&d,&e);
    };
    const auto flash=[&](void* collection, std::uint32_t key=grenade_effect_offsets::FlashOverlayKey, std::uintptr_t caller=0) {
        if (!caller) caller=base+grenade_effect_offsets::FlashOverlayReturn;
        return GrenadeEffectsTestFlash(collection,key,alphas,caller);
    };
    const auto opacity=[&](std::uintptr_t caller=0) {
        if (!caller) caller=base+grenade_effect_offsets::SmokeOpacityReturn;
        return GrenadeEffectsTestOpacity(&bookkeeping,caller);
    };
    GrenadeResetSettings();
    GrenadeEffectsTestConfigure(base,Smoke,Flash,Opacity);
    Check(smoke(&smokeTable)==0x12345678,"unactivated smoke forwarding");
    Check(flash(&flashTable)==0xABCDEF,"unactivated flash forwarding");
    Check(opacity()==0.75f && bookkeeping==1,"unactivated native opacity");
    GrenadeEffectsActivate();
    Check(smoke(&smokeTable)==0x12345678,"default smoke forwarding");
    Check(smokeArgs==std::array<void*,6>{&smokeTable,&a,&b,&c,&d,&e} && smokeCount==-7,
        "all seven native smoke args preserved");
    Check(flash(&flashTable)==0xABCDEF,"default flash forwarding");
    Check(flashCollection==&flashTable && flashKey==grenade_effect_offsets::FlashOverlayKey && flashValues==std::array<float,4>{0.1f,0.2f,0.3f,0.4f},"all native flash arguments and values preserved");
    GrenadeSettings settings{}; settings.removeSmoke=true; GrenadeSetSettings(settings);
    const int smokeBefore=smokeCalls;
    Check(smoke(&smokeTable)==0 && smokeCalls==smokeBefore,"native smoke volume skipped");
    Check(flash(&flashTable)==0xABCDEF,"smoke toggle independent of flash");
    Check(opacity()==0.0f && bookkeeping==2 && opacityCalls==2,"zero smoke opacity preserves bookkeeping");
    Check(opacity(base+1)==0.75f,"unrelated caller preserves opacity");
    Check(smoke(&otherTable)==0x12345678,"foreign smoke descriptor forwarded");
    Check(smoke(reinterpret_cast<void*>(1))==0x12345678,"invalid smoke descriptor forwarded");
    settings.removeSmoke=false; settings.removeFlash=true; GrenadeSetSettings(settings);
    Check(smoke(&smokeTable)==0x12345678 && opacity()==0.75f,"smoke disable resumes native state immediately");
    const int flashBefore=flashCalls;
    Check(flash(&flashTable)==0xABCDEF && flashCalls==flashBefore+1 && flashValues==std::array<float,4>{},"flash overlay alpha replaced; writer still executes");
    Check(flash(&flashTable,grenade_effect_offsets::FlashScreenshotKey,base+grenade_effect_offsets::FlashScreenshotReturn)==0xABCDEF && flashValues==std::array<float,4>{},"flash screenshot alpha also replaced");
    Check(flash(&flashTable,grenade_effect_offsets::FlashOverlayKey,base+1)==0xABCDEF && flashValues[0]==0.1f,"unrelated native writer caller forwarded");
    Check(flash(&flashTable,123)==0xABCDEF && flashValues[0]==0.1f,"unrelated shader key forwarded");
    settings.removeSmoke=true; GrenadeSetSettings(settings);
    Check(smoke(&smokeTable)==0 && flash(&flashTable)==0xABCDEF && flashValues[0]==0 && opacity()==0,"both removals independent and active");
    stopInsideOpacity=true;
    Check(opacity()==0.75f,"reentrant stop takes precedence over pending opacity replacement");
    Check(smoke(&smokeTable)==0x12345678 && flash(&flashTable)==0xABCDEF && flashValues[0]==0.1f,"cooperative stop forwards native draws");
    Check(std::strstr(GrenadeEffectsStatus(),"resident")!=nullptr,"stop accurately reports residency");
    stopInsideOpacity=false;
    GrenadeEffectsTestConfigure(0,Smoke,Flash,Opacity); GrenadeEffectsActivate();
    Check(smoke(&smokeTable)==0x12345678 && flash(&flashTable)==0xABCDEF && opacity()==0.75f,
        "stale client rejects suppression");
    Check(a==1&&b==2&&c==3&&d==4&&e==5&&smokeTable==base+grenade_effect_offsets::SmokeDescriptor&&
        flashTable==123 && alphas[0]==0.1f && alphas[3]==0.4f,"caller data and descriptor bytes unchanged");
    GrenadeEffectsStop(); GrenadeResetSettings();
    std::printf("grenade effects: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
