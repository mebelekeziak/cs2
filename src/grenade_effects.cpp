#include "grenade_effects.h"
#include "grenade_effect_offsets.h"
#include "grenade_settings.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <MinHook.h>
#include <intrin.h>
#include <atomic>

namespace {
namespace off = grenade_effect_offsets;
using SmokeFn = std::uintptr_t (*)(void*, void*, void*, int, void*, void*, void*);
using FlashFn = std::uintptr_t (*)(void*, std::uint32_t, const float*);
using OpacityFn = float (*)(void*);
SmokeFn originalSmoke = nullptr;
FlashFn originalFlash = nullptr;
OpacityFn originalOpacity = nullptr;
std::uintptr_t client = 0;
std::atomic<bool> active{false};
bool prepared = false;
std::atomic<const char*> status{"grenade removals not initialized"};
#ifdef CS2_GRENADE_EFFECTS_CHECKS
bool testing = false;
#endif
bool Current() noexcept {
#ifdef CS2_GRENADE_EFFECTS_CHECKS
    if (testing) return client != 0;
#endif
    return client && client == reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
}
bool Descriptor(void* object, std::ptrdiff_t expected) noexcept {
    std::uintptr_t table = 0;
    return MovementCopy(reinterpret_cast<std::uintptr_t>(object), &table, sizeof(table)) &&
        table == client + expected;
}
std::uintptr_t HookSmoke(void* descriptor, void* context, void* packets, int count,
    void* arg5, void* view, void* stats) noexcept {
    if (active.load(std::memory_order_acquire) && Current() && GrenadeGetSettings().removeSmoke &&
        Descriptor(descriptor, off::SmokeDescriptor)) return 0;
    return originalSmoke(descriptor, context, packets, count, arg5, view, stats);
}
std::uintptr_t Flash(void* collection, std::uint32_t key, const float* values, std::uintptr_t caller) noexcept {
    const bool matching = (caller == client + off::FlashOverlayReturn && key == off::FlashOverlayKey) ||
        (caller == client + off::FlashScreenshotReturn && key == off::FlashScreenshotKey);
    if (matching && active.load(std::memory_order_acquire) && Current() && GrenadeGetSettings().removeFlash) {
        // The verified writer copies 16 bytes synchronously into its own
        // collection; native screenshot capture and pass lifecycle continue.
        const float zero[4]{};
        return originalFlash(collection, key, zero);
    }
    return originalFlash(collection, key, values);
}
__declspec(noinline) std::uintptr_t HookFlash(void* collection, std::uint32_t key, const float* values) noexcept {
    return Flash(collection, key, values, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
float Opacity(void* pawn, std::uintptr_t caller) noexcept {
    // Preserve the engine's color, age and density bookkeeping. Only the
    // returned visual opacity is replaced; simulation/voxel state is untouched.
    const float native = originalOpacity(pawn);
    if (active.load(std::memory_order_acquire) && Current() && GrenadeGetSettings().removeSmoke &&
        caller == client + off::SmokeOpacityReturn) return 0.0f;
    return native;
}
__declspec(noinline) float HookOpacity(void* pawn) noexcept {
    return Opacity(pawn, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
template<std::size_t N> bool Bytes(std::ptrdiff_t rva, const unsigned char (&wanted)[N]) noexcept {
    unsigned char actual[N]{};
    return MovementCopy(client + rva, actual, N) && std::memcmp(actual, wanted, N) == 0;
}
bool Identity() noexcept {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::uintptr_t smoke = 0;
    return Current() && MovementCopy(client, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        dos.e_lfanew > 0 && dos.e_lfanew < 0x100000 &&
        MovementCopy(client + dos.e_lfanew, &nt, sizeof(nt)) && nt.Signature == IMAGE_NT_SIGNATURE &&
        nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 && nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt.FileHeader.TimeDateStamp == cs2_offsets::ClientTimestamp && nt.OptionalHeader.SizeOfImage == cs2_offsets::ClientImageSize &&
        MovementCopy(client + off::SmokeDescriptor + off::SmokeDrawSlot * sizeof(smoke), &smoke, sizeof(smoke)) &&
        smoke == client + off::SmokeDraw &&
        Bytes(off::SmokeDraw, off::SmokeDrawBytes) &&
        Bytes(off::FlashSetFloat4, off::FlashSetFloat4Bytes) && Bytes(off::FlashProducer, off::FlashProducerBytes) && Bytes(off::SmokeOpacity, off::SmokeOpacityBytes) &&
        Bytes(off::SmokeOpacityCaller, off::SmokeOpacityCallerBytes);
}
}
bool GrenadeEffectsPrepare(GrenadeEffectsLogCallback logger) noexcept {
    if (prepared) return true;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    if (!Identity()) { status.store("grenade removal contracts unavailable"); return false; }
    struct Hook { std::ptrdiff_t rva; void* detour; void** original; };
    Hook hooks[]{
        {off::SmokeDraw, reinterpret_cast<void*>(&HookSmoke), reinterpret_cast<void**>(&originalSmoke)},
        {off::FlashSetFloat4, reinterpret_cast<void*>(&HookFlash), reinterpret_cast<void**>(&originalFlash)},
        {off::SmokeOpacity, reinterpret_cast<void*>(&HookOpacity), reinterpret_cast<void**>(&originalOpacity)}
    };
    // A partial preparation remains disarmed. Any already queued callbacks only
    // forward, and all hook/trampoline code remains resident through exit.
    for (const auto& hook : hooks) {
        auto* target = reinterpret_cast<void*>(client + hook.rva);
        if (MH_CreateHook(target, hook.detour, hook.original) != MH_OK || !*hook.original ||
            MH_QueueEnableHook(target) != MH_OK) {
            status.store("grenade removal hooks unavailable"); return false;
        }
    }
    prepared = true;
    status.store("grenade removals prepared");
    if (logger) logger("GRENADE_EFFECTS_BUILD_VERIFIED native smoke draw, smoke opacity and flash alpha parameters; no entity/voxel writes; stopped hooks remain resident");
    return true;
}
void GrenadeEffectsActivate() noexcept {
    active.store(prepared, std::memory_order_release);
    if (prepared) status.store("grenade removals ready");
}
void GrenadeEffectsStop() noexcept {
    active.store(false, std::memory_order_release);
    status.store("grenade removals stopped and resident");
}
const char* GrenadeEffectsStatus() noexcept { return status.load(std::memory_order_acquire); }
#ifdef CS2_GRENADE_EFFECTS_CHECKS
void GrenadeEffectsTestConfigure(std::uintptr_t base, GrenadeEffectsSmokeFn smoke,
    GrenadeEffectsFlashFn flash, GrenadeEffectsOpacityFn opacity) noexcept {
    active.store(false); testing = true; client = base;
    originalSmoke = smoke; originalFlash = flash; originalOpacity = opacity;
    prepared = smoke && flash && opacity;
}
std::uintptr_t GrenadeEffectsTestSmoke(void* a, void* b, void* c, int d, void* e, void* f, void* g) noexcept {
    return HookSmoke(a,b,c,d,e,f,g);
}
std::uintptr_t GrenadeEffectsTestFlash(void* a, std::uint32_t b, const float* c, std::uintptr_t d) noexcept { return Flash(a,b,c,d); }
float GrenadeEffectsTestOpacity(void* pawn, std::uintptr_t caller) noexcept { return Opacity(pawn, caller); }
#endif
