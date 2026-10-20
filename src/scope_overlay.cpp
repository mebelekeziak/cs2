#include "scope_overlay.h"
#include "scope_overlay_offsets.h"
#include "scoped_crosshair.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <MinHook.h>
#include <intrin.h>
#include <atomic>

namespace {
namespace off = scope_overlay_offsets;
using NativeFn = void (*)(void*, void*);
NativeFn original = nullptr;
using IronSightFn = bool (*)(void*);
IronSightFn originalIronSight = nullptr;
std::uintptr_t client = 0;
std::atomic<bool> active{false};
bool prepared = false;
std::atomic<const char*> status{"scope overlay not initialized"};
#ifdef CS2_SCOPE_OVERLAY_CHECKS
bool testing = false;
#endif
bool Current() noexcept {
#ifdef CS2_SCOPE_OVERLAY_CHECKS
    if (testing) return client != 0;
#endif
    return client && client == reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
}
void Produce(void* pawn, void* output, std::uintptr_t caller) noexcept {
    // Native acquisition and any bookkeeping always execute. The render caller
    // owns this temporary stack output; the other (FOV) caller is left intact.
    original(pawn, output);
    if (!active.load(std::memory_order_acquire) || !Current() || !output ||
        caller != client + off::RenderReturn) return;
    const auto byte = reinterpret_cast<std::uintptr_t>(output) + off::DrawScope;
    unsigned char native = 0;
    if (!MovementCopy(byte, &native, sizeof(native)) || native != 1 ||
        !MovementOwnedWritable(byte, sizeof(native)) ||
        !ScopedCrosshairSuppressNativeScope(reinterpret_cast<std::uintptr_t>(pawn)) ||
        !active.load(std::memory_order_acquire)) return;
    // +0 is the independent healthshot boost and must survive. Only the native
    // Legacy Sniper Scope boolean changes, including its mask/lines/blur pass.
    __try { *reinterpret_cast<unsigned char*>(byte) = 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
__declspec(noinline) void Hook(void* pawn, void* output) noexcept {
    Produce(pawn, output, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
bool IronSight(void* controller, std::uintptr_t caller) noexcept {
    const bool native = originalIronSight(controller);
    if (!native || !active.load(std::memory_order_acquire) || !Current() ||
        (caller != client + off::IronSightDrawReturn && caller != client + off::IronSightGraphReturn)) return native;
    const auto embedded = reinterpret_cast<std::uintptr_t>(controller);
    if (embedded < static_cast<std::uintptr_t>(off::IronSightController)) return native;
    // Both verified callers naturally return before resource allocation when
    // this result is false. Other controller consumers receive the unchanged
    // result.
    if (ScopedCrosshairSuppressNativeIronSight(embedded - off::IronSightController) &&
        active.load(std::memory_order_acquire)) return false;
    return native;
}
__declspec(noinline) bool HookIronSight(void* controller) noexcept {
    return IronSight(controller, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
template<std::size_t N> bool Bytes(std::ptrdiff_t rva, const unsigned char (&wanted)[N]) noexcept {
    unsigned char actual[N]{};
    return MovementCopy(client + rva, actual, N) && std::memcmp(actual, wanted, N) == 0;
}
bool Identity() noexcept {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    return Current() && MovementCopy(client, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        dos.e_lfanew > 0 && dos.e_lfanew < 0x100000 &&
        MovementCopy(client + dos.e_lfanew, &nt, sizeof(nt)) && nt.Signature == IMAGE_NT_SIGNATURE &&
        nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 && nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt.FileHeader.TimeDateStamp == cs2_offsets::ClientTimestamp && nt.OptionalHeader.SizeOfImage == cs2_offsets::ClientImageSize &&
        Bytes(off::Producer, off::ProducerBytes) && Bytes(off::RenderCaller, off::RenderCallerBytes) &&
        Bytes(off::Consumer, off::ConsumerBytes) && Bytes(off::ScopeGate, off::ScopeGateBytes) &&
        Bytes(off::IronSightGate, off::IronSightGateBytes) &&
        Bytes(off::IronSightDrawCaller, off::IronSightDrawCallerBytes) &&
        Bytes(off::IronSightGraphCaller, off::IronSightGraphCallerBytes);
}
}
bool ScopeOverlayPrepare(ScopeOverlayLogCallback logger) noexcept {
    if (prepared) return true;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    if (!Identity()) { status.store("scope overlay contracts unavailable"); return false; }
    auto* target = reinterpret_cast<void*>(client + off::Producer);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Hook), reinterpret_cast<void**>(&original)) != MH_OK ||
        !original || MH_QueueEnableHook(target) != MH_OK) {
        status.store("scope overlay hook unavailable"); return false;
    }
    target = reinterpret_cast<void*>(client + off::IronSightGate);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookIronSight), reinterpret_cast<void**>(&originalIronSight)) != MH_OK ||
        !originalIronSight || MH_QueueEnableHook(target) != MH_OK) {
        status.store("iron sight overlay hook unavailable"); return false;
    }
    prepared = true;
    status.store("scope overlay prepared");
    if (logger) logger("SCOPE_OVERLAY_BUILD_VERIFIED native temporary sniper scope parameter and iron sight draw/graph results; health boost/FOV/state callers preserved; replacement requires fresh same local source; stopped hooks remain resident");
    return true;
}
void ScopeOverlayActivate() noexcept {
    active.store(prepared, std::memory_order_release);
    if (prepared) status.store("scope overlay ready");
}
void ScopeOverlayStop() noexcept {
    active.store(false, std::memory_order_release);
    status.store("scope overlay stopped and resident");
}
const char* ScopeOverlayStatus() noexcept { return status.load(std::memory_order_acquire); }
#ifdef CS2_SCOPE_OVERLAY_CHECKS
void ScopeOverlayTestConfigure(std::uintptr_t base, ScopeOverlayNativeFn native, ScopeOverlayIronSightFn ironSight) noexcept {
    active.store(false); testing = true; client = base; original = native; originalIronSight = ironSight;
    prepared = native && ironSight;
}
void ScopeOverlayTestCall(void* pawn, void* output, std::uintptr_t caller) noexcept { Produce(pawn, output, caller); }
bool ScopeOverlayTestIronSight(void* controller, std::uintptr_t caller) noexcept { return IronSight(controller, caller); }
#endif
