#include "world_materials.h"
#include "world_material_offsets.h"
#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <array>

namespace {
using namespace world_material_offsets;
using GenerateFn = void (*)(void*, void*, void*, void*);
using MaterialModeFn = std::uintptr_t (*)(void*, void*);
GenerateFn original = nullptr;
std::uintptr_t scene = 0, materialModule = 0;
std::size_t materialModuleSize = 0;
WorldMaterialsLogCallback logger = nullptr;
SRWLOCK mutationLock = SRWLOCK_INIT;
std::atomic<bool> enabled{false};
bool stopped = false;
std::atomic<const char*> status{"world materials not initialized"};
std::atomic<unsigned long long> callbacks{0}, modified{0}, rejected{0};
std::atomic<DWORD> owner{0};
enum class Reject : unsigned { Disabled, BeforeUnreadable, BeforeLayout, Descriptor, View,
    AfterUnreadable, NoPackets, FlatBinding, FlatMethod, FlatMode, PacketLayout, Neutral, Count };
std::array<std::atomic<unsigned long long>, static_cast<unsigned>(Reject::Count)> reasons{};
std::atomic<std::uintptr_t> lastDescriptor{0};
std::atomic<std::uint64_t> lastViewFlags{0};
std::atomic<int> lastFixedCapacity{0}, lastFixedCount{0}, lastOverflowCapacity{0}, lastOverflowCount{0};
unsigned long long reportedCalls = 0, reportedChanges = 0, reportedRejected = 0;
ULONGLONG reportTime = 0;
#ifdef CS2_WORLD_MATERIAL_CHECKS
WorldFlatMaterial testFlat{};
#endif

bool Copy(std::uintptr_t address, void* output, std::size_t bytes) noexcept {
    if (!output || address < 0x10000 || bytes > 0x00007fffffffffffULL ||
        address > 0x00007fffffffffffULL - bytes) return false;
    __try { std::memcpy(output, reinterpret_cast<const void*>(address), bytes); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool Read(std::uintptr_t address, T& output) noexcept {
    return Copy(address, &output, sizeof(output));
}
bool Writable(std::uintptr_t address, std::size_t bytes) noexcept {
    if (address < 0x10000 || address > 0x00007fffffffffffULL - bytes) return false;
    __try {
        auto* destination = reinterpret_cast<volatile unsigned char*>(address);
        for (std::size_t n = 0; n < bytes; ++n) {
            const auto value = destination[n]; destination[n] = value;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ValidList(const WorldDrawList& list) noexcept {
    constexpr int bound = 65536;
    constexpr std::uintptr_t max = 0x00007fffffffffffULL;
    return list.fixedCapacity >= 0 && list.fixedCapacity <= bound &&
        list.fixedCount >= 0 && list.fixedCount <= list.fixedCapacity &&
        list.overflowCapacity >= 0 && list.overflowCapacity <= bound &&
        list.overflowCount >= 0 && list.overflowCount <= list.overflowCapacity &&
        (!list.fixedCapacity || (list.fixed >= 0x10000 &&
            list.fixed <= max - static_cast<std::uintptr_t>(list.fixedCapacity) * PacketSize)) &&
        (!list.overflowCapacity || (list.overflow >= 0x10000 &&
            list.overflow <= max - static_cast<std::uintptr_t>(list.overflowCapacity) * PacketSize));
}
bool Image(std::uintptr_t base, IMAGE_NT_HEADERS64& nt) noexcept {
    IMAGE_DOS_HEADER dos{};
    return base && Read(base, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        dos.e_lfanew > 0 && dos.e_lfanew < 0x100000 && Read(base + dos.e_lfanew, nt) &&
        nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
}
bool CallMode(std::uintptr_t method, std::uintptr_t material, std::uintptr_t view,
    std::uintptr_t& result) noexcept {
    __try {
        result = reinterpret_cast<MaterialModeFn>(method)(reinterpret_cast<void*>(material),
            reinterpret_cast<void*>(view + ModeInVtable));
        return result != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void Count(Reject reason, const char* message = nullptr) noexcept {
    reasons[static_cast<unsigned>(reason)].fetch_add(1, std::memory_order_relaxed);
    if (message) status.store(message, std::memory_order_release);
}
void HookGenerate(void* desc, void* objectPointer, void* argsPointer, void* listPointer) noexcept {
    // Call the game outside our mutation lock; it can dispatch worker jobs.
    WorldDrawList before{};
    const bool readable = enabled.load(std::memory_order_acquire) &&
        Read(reinterpret_cast<std::uintptr_t>(listPointer), before);
    const bool captured = readable && ValidList(before);
    original(desc, objectPointer, argsPointer, listPointer);
    callbacks.fetch_add(1, std::memory_order_relaxed);
    if (!enabled.load(std::memory_order_acquire)) return;
    const auto settings = WorldGetSettings();
    if (!settings.worldMaterialEnabled) { Count(Reject::Disabled, "world material controls disabled"); return; }
    if (!settings.flatTextures) { Count(Reject::Disabled, "flat world textures disabled; tint has separate shader consumer"); return; }
    if (!captured) {
        lastFixedCapacity.store(before.fixedCapacity, std::memory_order_relaxed);
        lastFixedCount.store(before.fixedCount, std::memory_order_relaxed);
        lastOverflowCapacity.store(before.overflowCapacity, std::memory_order_relaxed);
        lastOverflowCount.store(before.overflowCount, std::memory_order_relaxed);
        Count(readable ? Reject::BeforeLayout : Reject::BeforeUnreadable,
            readable ? "world draw list layout rejected" : "world draw list unreadable");
        rejected.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    std::uintptr_t vtable = 0, view = 0;
    std::uint64_t flags = 0;
    WorldDrawList after{};
    const auto object = reinterpret_cast<std::uintptr_t>(objectPointer);
    if (!Read(reinterpret_cast<std::uintptr_t>(desc), vtable) || vtable != scene + AggregateVtable) {
        lastDescriptor.store(vtable >= scene ? vtable - scene : vtable, std::memory_order_relaxed);
        Count(Reject::Descriptor, "world descriptor rejected");
        rejected.fetch_add(1, std::memory_order_relaxed); return;
    }
    if (!Read(reinterpret_cast<std::uintptr_t>(argsPointer) + ViewInArgs, view) ||
        !Read(view + ViewFlags, flags)) {
        Count(Reject::View, "world material view unreadable");
        rejected.fetch_add(1, std::memory_order_relaxed); return;
    }
    // Native bit24/bit32 select a debug fallback material branch. They are not
    // a contract classifying camera versus auxiliary views. Resolve the chosen
    // material against this actual view below instead of inventing a view gate.
    lastViewFlags.store(flags, std::memory_order_relaxed);
    if (!Read(reinterpret_cast<std::uintptr_t>(listPointer), after)) {
        Count(Reject::AfterUnreadable, "world emitted draw list unreadable");
        rejected.fetch_add(1, std::memory_order_relaxed); return;
    }
    if (after.fixedCount == before.fixedCount && after.overflowCount == before.overflowCount) {
        Count(Reject::NoPackets, "world callback emitted no draw packets"); return;
    }
    WorldFlatMaterial flat{};
    if (settings.flatTextures) {
#ifdef CS2_WORLD_MATERIAL_CHECKS
        flat = testFlat;
#else
        std::uintptr_t binding = 0, materialVtable = 0, method = 0;
        if (!Read(scene + FlatBinding, binding) || !Read(binding, flat.material) || !flat.material) {
            Count(Reject::FlatBinding, "native flat material unavailable");
            rejected.fetch_add(1, std::memory_order_relaxed);
        } else if (!Read(flat.material, materialVtable) || !Read(materialVtable + ModeInVtable, method) ||
            method < materialModule || method >= materialModule + materialModuleSize) {
            Count(Reject::FlatMethod, "native flat material method rejected");
            rejected.fetch_add(1, std::memory_order_relaxed); flat = {};
        } else if (CallMode(method, flat.material, view, flat.mode)) {
            Read(scene + Type1Stream, flat.type1Stream);
            Read(scene + Type2Stream, flat.type2Stream);
        } else {
            flat = {};
            Count(Reject::FlatMode, "native flat material has no mode for this view");
            rejected.fetch_add(1, std::memory_order_relaxed);
        }
#endif
    }
    AcquireSRWLockShared(&mutationLock);
    if (!enabled.load(std::memory_order_acquire)) { ReleaseSRWLockShared(&mutationLock); return; }
    std::size_t changed = 0;
    if (!WorldMaterialsApplyNewDraws(before, after, object, settings, flat, changed)) {
        Count(Reject::PacketLayout, "world emitted packet layout rejected");
        rejected.fetch_add(1, std::memory_order_relaxed);
    }
    if (changed) {
        modified.fetch_add(changed, std::memory_order_relaxed);
        owner.store(GetCurrentThreadId(), std::memory_order_relaxed);
        status.store("world draw materials active", std::memory_order_release);
    } else Count(Reject::Neutral);
    ReleaseSRWLockShared(&mutationLock);
}
}

bool WorldMaterialsApplyNewDraws(const WorldDrawList& before, const WorldDrawList& after,
    std::uintptr_t object, const WorldSettings& settings, const WorldFlatMaterial& flat,
    std::size_t& changed) noexcept {
    changed = 0;
    if (!settings.worldMaterialEnabled) return true;
    // The aggregate renderer consumes its own instance/vertex tint streams.
    // Packet+0x50 is not a tint input on this path. Only change material tuples
    // here; surface tint belongs to the verified shader consumer.
    const bool flatten = settings.flatTextures && flat.material && flat.mode;
    if (!flatten) return true;
    if (!ValidList(before) || !ValidList(after) || object < 0x10000 ||
        before.fixed != after.fixed || before.fixedCapacity != after.fixedCapacity ||
        after.fixedCount < before.fixedCount || after.overflowCount < before.overflowCount ||
        (after.overflowCount > before.overflowCount && after.fixedCount != after.fixedCapacity)) return false;
    for (unsigned list = 0; list < 2; ++list) {
        const auto storage = list ? after.overflow : after.fixed;
        const int begin = list ? before.overflowCount : before.fixedCount;
        const int end = list ? after.overflowCount : after.fixedCount;
        for (int index = begin; index < end; ++index) {
            const auto address = storage + static_cast<std::uintptr_t>(index) * PacketSize;
            unsigned char packet[PacketSize]{};
            if (!Copy(address, packet, sizeof(packet))) return false;
            std::uintptr_t source = 0, material = 0, mode = 0, mesh = 0;
            std::memcpy(&source, packet + 0x18, sizeof(source));
            std::memcpy(&material, packet + Material, sizeof(material));
            std::memcpy(&mode, packet + Mode, sizeof(mode));
            if (source != object || !material || !mode) continue;
            if (flatten) {
                std::memcpy(&mesh, packet + 8, sizeof(mesh));
                int type = 0;
                if (!Read(mesh + 0x80, type)) return false;
                const auto replacement = type == 1 ? flat.type1Stream : type == 2 ? flat.type2Stream : 0;
                if ((type == 1 || type == 2) && !replacement) continue;
                std::memcpy(packet + Material, &flat.material, sizeof(flat.material));
                std::memcpy(packet + Mode, &flat.mode, sizeof(flat.mode));
                if (replacement) std::memcpy(packet + 0x48, &replacement, sizeof(replacement));
                packet[0x62] |= 0x10;
            }
            unsigned char baseline[PacketSize]{};
            if (!Copy(address, baseline, sizeof(baseline))) return false;
            if (std::memcmp(baseline, packet, sizeof(packet)) == 0) continue;
            if (!Writable(address, sizeof(packet))) return false;
            __try { std::memcpy(reinterpret_cast<void*>(address), packet, sizeof(packet)); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
            ++changed;
        }
    }
    return true;
}

bool WorldMaterialsPrepare(WorldMaterialsLogCallback callback) noexcept {
    logger = callback;
    scene = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"scenesystem.dll"));
    materialModule = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"materialsystem2.dll"));
    IMAGE_NT_HEADERS64 nt{}, materialNt{};
    unsigned char bytes[sizeof(CallbackBytes)]{};
    std::uintptr_t target = 0;
    if (!Image(scene, nt) || nt.FileHeader.TimeDateStamp != Timestamp ||
        nt.OptionalHeader.SizeOfImage != ImageSize || !Image(materialModule, materialNt) ||
        materialNt.FileHeader.TimeDateStamp != MaterialTimestamp || materialNt.OptionalHeader.SizeOfImage != MaterialImageSize ||
        !Copy(scene + AggregateCallback, bytes, sizeof(bytes)) || std::memcmp(bytes, CallbackBytes, sizeof(bytes)) ||
        !Read(scene + AggregateVtable + 0x20, target) || target != scene + AggregateCallback) {
        status.store("installed world material build verification failed", std::memory_order_release); return false;
    }
    materialModuleSize = materialNt.OptionalHeader.SizeOfImage;
    const auto hook = reinterpret_cast<void*>(scene + AggregateCallback);
    if (MH_CreateHook(hook, reinterpret_cast<void*>(&HookGenerate), reinterpret_cast<void**>(&original)) != MH_OK ||
        !original || MH_QueueEnableHook(hook) != MH_OK) {
        status.store("world material hook setup failed", std::memory_order_release); return false;
    }
    status.store("awaiting world draw callback", std::memory_order_release);
    if (logger) logger("WORLD_MATERIAL_BUILD_VERIFIED callback=0x41F30 class=CAggregateSceneObjectDesc stride=0x70 material=0x20 mode=0x28 rgba=0x50; draw packets owned by engine callback; native reflectivity_90 binding reused");
    return true;
}
void WorldMaterialsActivate() noexcept {
    AcquireSRWLockExclusive(&mutationLock);
    if (!stopped && original) enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
}
void WorldMaterialsStop(const char* reason) noexcept {
    enabled.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&mutationLock);
    stopped = true; enabled.store(false, std::memory_order_release);
    status.store("world materials stopped and resident", std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
    if (logger) {
        char message[384]{};
        std::snprintf(message, sizeof(message), "WORLD_MATERIAL_STOPPED_RESIDENT reason=%s; mutations drained; original callbacks retained; baseline packets regenerate normally; no physical unload", reason ? reason : "stop");
        logger(message);
    }
}
const char* WorldMaterialsStatus() noexcept { return status.load(std::memory_order_acquire); }
#ifdef CS2_WORLD_MATERIAL_CHECKS
void WorldMaterialsTestConfigure(std::uintptr_t sceneBase, WorldMaterialsTestGenerateFn fn,
    const WorldFlatMaterial& flat) noexcept {
    scene = sceneBase; original = fn; stopped = false;
    testFlat = flat;
    enabled.store(true, std::memory_order_release);
}
void WorldMaterialsTestGenerate(void* desc, void* object, void* args, void* list) noexcept {
    HookGenerate(desc, object, args, list);
}
#endif
void WorldMaterialsFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (reportTime && now - reportTime < 2000) return;
    reportTime = now;
    const auto callCount = callbacks.load(std::memory_order_relaxed);
    const auto changeCount = modified.load(std::memory_order_relaxed);
    const auto failCount = rejected.load(std::memory_order_relaxed);
    if (callCount == reportedCalls && changeCount == reportedChanges && failCount == reportedRejected) return;
    reportedCalls = callCount; reportedChanges = changeCount; reportedRejected = failCount;
    char message[1200]{};
    std::snprintf(message, sizeof(message), "WORLD_MATERIAL callbacks=%llu packets=%llu rejected=%llu last_owner=%lu status=%s disabled=%llu before_read=%llu before_layout=%llu descriptor=%llu view_read=%llu after_read=%llu empty=%llu flat_binding=%llu flat_method=%llu flat_mode=%llu packet_layout=%llu neutral=%llu desc_rva=0x%llx view_flags=0x%llx fixed=%d/%d overflow=%d/%d; packet mutation does not prove visible output", callCount, changeCount, failCount, owner.load(std::memory_order_relaxed), WorldMaterialsStatus(),
        reasons[0].load(), reasons[1].load(), reasons[2].load(), reasons[3].load(), reasons[4].load(),
        reasons[5].load(), reasons[6].load(), reasons[7].load(), reasons[8].load(), reasons[9].load(),
        reasons[10].load(), reasons[11].load(), static_cast<unsigned long long>(lastDescriptor.load()),
        static_cast<unsigned long long>(lastViewFlags.load()),lastFixedCount.load(),lastFixedCapacity.load(),lastOverflowCount.load(),lastOverflowCapacity.load());
    logger(message);
}
