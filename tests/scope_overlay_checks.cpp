#include "scope_overlay.h"
#include "scope_overlay_offsets.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>

namespace {
constexpr std::uintptr_t base=0x10000000;
int checks=0, failures=0, nativeCalls=0, predicateCalls=0;
int ironCalls=0, ironPredicateCalls=0;
bool ironNative=true;
bool replacement=true, nativeWrites=true, stopInside=false;
std::uintptr_t selectedPawn=0x12345678;
std::uintptr_t selectedWeapon=0x23456789;
void* seenController=nullptr;
void* seenPawn=nullptr;
void* seenOutput=nullptr;
std::array<unsigned char,28> nativeValues{};
void Check(bool value, const char* label) {
    ++checks; if (!value) { ++failures; std::printf("FAIL %s\n",label); }
}
void Native(void* pawn, void* output) {
    ++nativeCalls; seenPawn=pawn; seenOutput=output;
    if (nativeWrites && output) std::memcpy(output,nativeValues.data(),nativeValues.size());
    if (stopInside) ScopeOverlayStop();
}
bool NativeIronSight(void* controller) {
    ++ironCalls; seenController=controller;
    if (stopInside) ScopeOverlayStop();
    return ironNative;
}
}
// The native adapter must pass the producer's pawn, and must not ask for a
// replacement on unrelated callers, absent native scope or unreadable output.
bool ScopedCrosshairSuppressNativeScope(std::uintptr_t pawn) noexcept {
    ++predicateCalls;
    return replacement && pawn==selectedPawn;
}
bool ScopedCrosshairSuppressNativeIronSight(std::uintptr_t weapon) noexcept {
    ++ironPredicateCalls;
    return replacement && weapon==selectedWeapon;
}
int main() {
    nativeValues.fill(0xAB); nativeValues[4]=1;
    std::array<unsigned char,28> output{};
    const auto invoke=[&](std::uintptr_t caller=base+scope_overlay_offsets::RenderReturn,
                          std::uintptr_t pawn=0x12345678, void* bytes=nullptr) {
        ScopeOverlayTestCall(reinterpret_cast<void*>(pawn),bytes?bytes:output.data(),caller);
    };
    const auto original=[&] { return output==nativeValues; };
    ScopeOverlayTestConfigure(base,Native,NativeIronSight);
    invoke(); Check(original() && predicateCalls==0,"unactivated native parameter forwarding");
    ScopeOverlayActivate();
    replacement=false; invoke(); Check(original(),"disabled or unavailable replacement preserves native scope");
    replacement=true;
    invoke();
    auto expected=nativeValues; expected[4]=0;
    Check(output==expected,"only sniper scope byte cleared; health boost and other bytes preserved");
    Check(seenPawn==reinterpret_cast<void*>(selectedPawn) && seenOutput==output.data() && nativeCalls==3,
        "native producer always executes with identical arguments");
    invoke(base+scope_overlay_offsets::OtherReturn);
    Check(original(),"native FOV consumer remains unchanged");
    invoke(base+1); Check(original(),"foreign caller remains unchanged");
    invoke(base+scope_overlay_offsets::RenderReturn,selectedPawn+1);
    Check(original(),"other observed pawn cannot borrow local replacement");
    for (unsigned char bit:std::array<unsigned char,3>{0,2,255}) {
        nativeValues[4]=bit; const int before=predicateCalls; invoke();
        Check(original() && predicateCalls==before,"absent or invalid native scope does not mark replacement");
    }
    nativeValues[4]=1;
    nativeWrites=false;
    const int before=predicateCalls;
    invoke(base+scope_overlay_offsets::RenderReturn,selectedPawn,reinterpret_cast<void*>(1));
    ScopeOverlayTestCall(reinterpret_cast<void*>(selectedPawn),nullptr,base+scope_overlay_offsets::RenderReturn);
    Check(predicateCalls==before,"invalid and null output do not publish replacement");
    auto* readonly=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(readonly!=nullptr,"read-only fixture allocation");
    if (readonly) {
        std::memcpy(readonly,nativeValues.data(),nativeValues.size()); DWORD previous=0;
        const bool protectedPage=VirtualProtect(readonly,4096,PAGE_READONLY,&previous)!=FALSE;
        Check(protectedPage,"read-only fixture protection");
        if (protectedPage) {
            invoke(base+scope_overlay_offsets::RenderReturn,selectedPawn,readonly);
            Check(predicateCalls==before && std::memcmp(readonly,nativeValues.data(),nativeValues.size())==0,
                "unwritable output preserves bytes and does not publish replacement");
        }
        VirtualFree(readonly,0,MEM_RELEASE);
    }
    nativeWrites=true; stopInside=true; invoke();
    Check(original(),"reentrant native cooperative stop preserves native output");
    stopInside=false; invoke(); Check(original(),"stopped callback continues native forwarding");
    Check(std::strstr(ScopeOverlayStatus(),"resident")!=nullptr,"stop accurately describes retained code");
    ScopeOverlayTestConfigure(0,Native,NativeIronSight); ScopeOverlayActivate(); invoke();
    Check(original(),"missing module identity disables suppression");
    ScopeOverlayTestConfigure(base,Native,NativeIronSight);
    const auto controller=reinterpret_cast<void*>(selectedWeapon+scope_overlay_offsets::IronSightController);
    const auto iron=[&](std::uintptr_t caller=base+scope_overlay_offsets::IronSightDrawReturn, void* object=nullptr) {
        return ScopeOverlayTestIronSight(object?object:controller,caller);
    };
    Check(iron() && ironCalls==1 && ironPredicateCalls==0,"inactive iron sight forwards native result");
    ScopeOverlayActivate(); replacement=false;
    Check(iron() && seenController==controller,"disabled replacement preserves original controller argument/result");
    replacement=true;
    Check(!iron() && seenController==controller && ironCalls==3,"local iron sight geometry uses native false early exit");
    Check(!iron(base+scope_overlay_offsets::IronSightGraphReturn),"local iron sight resolve/blur/filter graph uses native false early exit");
    for (std::uintptr_t caller:std::array<std::uintptr_t,4>{base+0x11DEBFC,base+0xCEFcf3,base+0x11F0742,base+1}) {
        const int predicates=ironPredicateCalls;
        Check(iron(caller) && ironPredicateCalls==predicates,"non-target state, FOV and stencil consumers unchanged");
    }
    Check(iron(base+scope_overlay_offsets::IronSightDrawReturn,reinterpret_cast<void*>(selectedWeapon+scope_overlay_offsets::IronSightController+1)),
        "foreign controller cannot borrow local active weapon replacement");
    const int predicates=ironPredicateCalls;
    Check(iron(base+scope_overlay_offsets::IronSightDrawReturn,reinterpret_cast<void*>(1)) && ironPredicateCalls==predicates,
        "invalid embedded controller cannot underflow weapon ownership");
    ironNative=false;
    Check(!iron() && ironPredicateCalls==predicates,"native false result never publishes replacement");
    ironNative=true; stopInside=true;
    Check(iron(),"native reentrant stop takes priority over iron sight replacement");
    stopInside=false;
    Check(iron(),"stopped iron sight callback forwards original");
    ScopeOverlayTestConfigure(0,Native,NativeIronSight); ScopeOverlayActivate();
    Check(iron(),"stale module identity preserves native iron sight");
    ScopeOverlayStop();
    std::printf("scope overlay: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
