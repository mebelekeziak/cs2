#include "player_chams_materials.h"
#include "player_chams_material_offsets.h"
#include "client_frame.h"
#include "model_swap_offsets.h"
#include "movement_memory.h"
#include <atomic>
#include <cstring>

namespace {
namespace off = player_chams_material_offsets;
using ConstructFn = void* (*)(void*, unsigned);
using RootFn = void* (*)(void*);
using DestroyFn = void (*)(void*);
using ParseFn = bool (*)(void*, void*, const char*, const void*, const char*, unsigned);
using CreateFn = void* (*)(void*, void**, const char*, void*, unsigned, bool);
using ModeFn = std::uintptr_t (*)(void*, void*);
ConstructFn construct = nullptr;
RootFn root = nullptr;
DestroyFn destroy = nullptr;
ParseFn parse = nullptr;
CreateFn create = nullptr;
std::uintptr_t client = 0, materialModule = 0, tier0 = 0, host = 0, resourceModule = 0;
std::uintptr_t manager = 0;
PlayerChamsMaterialsLogCallback logger = nullptr;
SRWLOCK lifetime = SRWLOCK_INIT;
std::atomic<bool> enabled{false};
std::atomic<unsigned> phase{0}; // Idle, constructing, ready, failed.
std::atomic<const char*> status{"Player materials not initialized."};
struct Retained { std::uintptr_t binding = 0, material = 0; } retained[2];
struct KV3ID { const char* name; std::uint64_t low, high; };
constexpr KV3ID format{"generic", 0x469806E97412167Cull, 0xE73790B53EE6F2AFull};
constexpr const char* textures[]{"materials/dev/primary_white_color_tga_21186c76.vtex",
    "materials/default/default_normal.vtex"};
constexpr const char* names[]{"materials/codex_player_flat.vmat", "materials/codex_player_iridescent.vmat"};
constexpr const char* recipes[]{
R"kv3(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
 shader = "csgo_unlitgeneric.vfx"
 F_BLEND_MODE = 1
 F_VERTEX_COLOR = 0
 g_flModelTintAmount = 1.0
 g_vColorTint = [ 1.0, 1.0, 1.0, 1.0 ]
 g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
}
)kv3",
R"kv3(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
 shader = "csgo_character.vfx"
 F_IRIDESCENCE = 1
 F_TRANSLUCENT = 1
 g_flModelTintAmount = 1.0
 g_vColorTint = [ 1.0, 1.0, 1.0, 1.0 ]
 g_flIridescentStrength = 1.0
 g_flIridescentFresnelStrength = 2.0
 g_flIridescentHueShift = 0.5
 g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
 g_tNormal = resource:"materials/default/default_normal.vtex"
 g_tIridescentThickness_Mask = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
 g_tMetalness = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
}
)kv3"};
#ifdef CS2_PLAYER_CHAMS_MATERIAL_CHECKS
bool testing = false;
PlayerChamsMaterialTestApi testApi{};
#endif
template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
template<std::size_t N> bool Match(std::uintptr_t address, const unsigned char (&expected)[N]) noexcept {
    unsigned char copy[N]{};
    return MovementCopy(address, copy, N) && !std::memcmp(copy, expected, N);
}
bool Image(std::uintptr_t module, unsigned timestamp, unsigned size) noexcept {
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    return Read(module, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        dos.e_lfanew > 0 && dos.e_lfanew < 0x100000 && Read(module + dos.e_lfanew, nt) &&
        nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt.FileHeader.TimeDateStamp == timestamp && nt.OptionalHeader.SizeOfImage == size;
}
void Report(const char* message) noexcept {
    status.store(message, std::memory_order_release);
    if (logger) logger(message);
}
bool Current() noexcept {
#ifdef CS2_PLAYER_CHAMS_MATERIAL_CHECKS
    if (testing) return true;
#endif
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) == client &&
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"materialsystem2.dll")) == materialModule &&
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"tier0.dll")) == tier0 &&
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"host.dll")) == host &&
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"resourcesystem.dll")) == resourceModule;
}
bool Preload(const char* path) noexcept {
#ifdef CS2_PLAYER_CHAMS_MATERIAL_CHECKS
    if (testing) return testApi.preload && testApi.preload(path);
#endif
    namespace mo = model_swap_offsets;
    alignas(16) unsigned char name[mo::ResourceNameSize]{};
    const auto flags = mo::ResourceNameFlags;
    std::memcpy(name + mo::ResourceNameFlagsOffset, &flags, sizeof(flags));
    void* binding = nullptr;
    bool initialized = false, loaded = false;
    __try {
        initialized = reinterpret_cast<bool (*)(void*, const char*)>(host + mo::InitializeResourceName)(name, path);
        std::uintptr_t system = 0, table = 0, type = 0;
        std::memcpy(&type, name + mo::ResourceType, sizeof(type));
        if (initialized && type == 0x78657476 && Read(host + mo::ResourceSystemPointer, system) &&
            Read(system, table) && table == resourceModule + mo::ResourceSystemVtable) {
            binding = reinterpret_cast<void* (*)(void*, const void*, const char*)>(resourceModule + mo::BlockingLoad)(
                reinterpret_cast<void*>(system), name, "CodexPlayerChams");
            std::uintptr_t stateMethod = 0;
            if (binding && Read(table + 0x198, stateMethod) && stateMethod >= resourceModule &&
                stateMethod < resourceModule + mo::ResourceSystemImageSize) {
                loaded = reinterpret_cast<int (*)(void*, const void*)>(stateMethod)(reinterpret_cast<void*>(system), name) == 3;
            }
        }
        reinterpret_cast<void (*)(void*, int)>(tier0 + mo::PurgeResourceName)(name, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    std::uintptr_t resource = 0;
    return initialized && loaded && binding && Read(reinterpret_cast<std::uintptr_t>(binding), resource) && resource;
}
bool Retain(void* output, const char* expected, Retained& result) noexcept {
    const auto binding = reinterpret_cast<std::uintptr_t>(output);
    std::uintptr_t material = 0, table = 0, name = 0;
    if (!Read(binding, material) || !material) return false;
#ifdef CS2_PLAYER_CHAMS_MATERIAL_CHECKS
    if (testing) { result = {binding, material}; return true; }
#endif
    char copied[96]{};
    if (!Read(material, table) || table != materialModule + off::MaterialVtable ||
        !Read(material + 0x10, name) || !MovementCopy(name, copied, std::strlen(expected) + 1) ||
        std::strcmp(copied, expected)) return false;
    // Native resource handles use this same atomic binding reference. Hold one
    // for the process lifetime: queued GPU/native users may outlive our stop.
    __try { InterlockedIncrement(reinterpret_cast<volatile LONG*>(binding + 0x20)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    result = {binding, material};
    return true;
}
bool Build(unsigned index, Retained& output) noexcept {
    alignas(16) unsigned char arena[off::ArenaSize]{};
    void* binding = nullptr;
    bool constructed = false, parsed = false;
    __try {
        construct(arena, 0);
        constructed = true;
        if (void* node = root(arena)) {
            parsed = parse(node, nullptr, recipes[index], &format, "", 0);
            if (parsed) create(reinterpret_cast<void*>(manager), &binding, names[index], node, 1, true);
        }
        constructed = false;
        destroy(arena);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Only guarded native access is recovered. A completed construction
        // still owns native allocations and must take its native destructor.
        if (constructed) {
            __try { destroy(arena); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return false;
    }
    return parsed && Retain(binding, names[index], output);
}
void OwnerCallback(void*, int) noexcept { PlayerChamsMaterialsOwnerFrame(); }
}

bool PlayerChamsMaterialsInitialize(PlayerChamsMaterialsLogCallback log) noexcept {
    logger = log;
#ifdef CS2_PLAYER_CHAMS_MATERIAL_CHECKS
    if (testing) { enabled.store(true); return true; }
#endif
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    materialModule = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"materialsystem2.dll"));
    tier0 = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"tier0.dll"));
    host = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"host.dll"));
    resourceModule = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"resourcesystem.dll"));
    namespace mo = model_swap_offsets;
    if (!Image(client, off::ClientTimestamp, off::ClientImageSize) ||
        !Image(materialModule, off::MaterialTimestamp, off::MaterialImageSize) ||
        !Image(tier0, mo::Tier0Timestamp, mo::Tier0ImageSize) ||
        !Image(host, mo::HostTimestamp, mo::HostImageSize) ||
        !Image(resourceModule, mo::ResourceSystemTimestamp, mo::ResourceSystemImageSize) ||
        !Match(client + off::ArenaConstructor, off::ArenaConstructorBytes) ||
        !Match(client + off::ArenaGetRoot, off::ArenaGetRootBytes) ||
        !Match(client + off::ArenaDestructor, off::ArenaDestructorBytes) ||
        !Match(materialModule + off::CreateMaterial, off::CreateMaterialBytes) ||
        !Match(host + mo::InitializeResourceName, mo::InitializeResourceNameBytes) ||
        !Match(resourceModule + mo::BlockingLoad, mo::BlockingLoadBytes) ||
        !Match(tier0 + mo::PurgeResourceName, mo::PurgeResourceNameBytes)) {
        Report("Player material native contract rejected."); return false;
    }
    const auto parser = GetProcAddress(reinterpret_cast<HMODULE>(tier0),
        "?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2I@Z");
    if (reinterpret_cast<std::uintptr_t>(parser) != tier0 + off::LoadKV3String ||
        !Match(tier0 + off::LoadKV3String, off::LoadKV3StringBytes)) {
        Report("Player material parser contract rejected."); return false;
    }
    construct = reinterpret_cast<ConstructFn>(client + off::ArenaConstructor);
    root = reinterpret_cast<RootFn>(client + off::ArenaGetRoot);
    destroy = reinterpret_cast<DestroyFn>(client + off::ArenaDestructor);
    parse = reinterpret_cast<ParseFn>(parser);
    create = reinterpret_cast<CreateFn>(materialModule + off::CreateMaterial);
    enabled.store(true, std::memory_order_release);
    if (!ClientFrameSubscribe(OwnerCallback, logger)) {
        enabled.store(false); Report("Player material frame owner unavailable."); return false;
    }
    Report("Player materials waiting for enable and native owner.");
    return true;
}

void PlayerChamsMaterialsOwnerFrame() noexcept {
    if (!enabled.load(std::memory_order_acquire) || !PlayerChamsGetSettings().enabled) return;
    unsigned idle = 0;
    if (!phase.compare_exchange_strong(idle, 1)) return;
    bool success = enabled.load(std::memory_order_acquire) && Current();
    Retained prepared[2]{};
#ifdef CS2_PLAYER_CHAMS_MATERIAL_CHECKS
    if (testing) manager = 1;
    else
#endif
    if (success) {
        // Obtain the already initialized native singleton on the engine owner.
        const auto factory = GetProcAddress(reinterpret_cast<HMODULE>(materialModule), "CreateInterface");
        __try {
            manager = factory ? reinterpret_cast<std::uintptr_t>(reinterpret_cast<void* (*)(const char*, int*)>(factory)(
                "VMaterialSystem2_001", nullptr)) : 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) { manager = 0; }
        std::uintptr_t table = 0, method = 0;
        success = Read(manager, table) && table == materialModule + off::MaterialSystemVtable &&
            Read(table + 29 * sizeof(void*), method) && method == materialModule + off::CreateMaterial;
    }
    if (success) for (const auto* path : textures) if (!Preload(path)) { success = false; break; }
    if (success) for (unsigned index = 0; index < 2; ++index) {
        if (!enabled.load(std::memory_order_acquire) || !Build(index, prepared[index])) { success = false; break; }
    }
    // Blocking resource work can dispatch render jobs. Never hold our
    // publication lock across native loading, parsing, creation, or mode calls.
    AcquireSRWLockExclusive(&lifetime);
    if (enabled.load(std::memory_order_acquire) && Current()) {
        if (success) { retained[0] = prepared[0]; retained[1] = prepared[1]; }
        phase.store(success ? 2u : 3u, std::memory_order_release);
        Report(success ? "Player materials ready (native flat and iridescent)." : "Player material preparation failed; restart required to retry.");
    } else phase.store(3, std::memory_order_release);
    ReleaseSRWLockExclusive(&lifetime);
}

bool PlayerChamsMaterialsResolve(PlayerChamsStyle style, std::uintptr_t view,
    std::uintptr_t& material, std::uintptr_t& mode) noexcept {
    material = mode = 0;
    if (!enabled.load(std::memory_order_acquire) || phase.load(std::memory_order_acquire) != 2 ||
        (style != PlayerChamsStyle::Flat && style != PlayerChamsStyle::Iridescent) || !view) return false;
    AcquireSRWLockShared(&lifetime);
    bool success = enabled.load(std::memory_order_acquire) && Current();
    const auto snapshot = retained[static_cast<unsigned>(style)];
    ReleaseSRWLockShared(&lifetime);
    const auto candidate = snapshot.material;
    std::uintptr_t table = 0, method = 0, bindingMaterial = 0;
#ifdef CS2_PLAYER_CHAMS_MATERIAL_CHECKS
    if (testing) method = reinterpret_cast<std::uintptr_t>(testApi.mode);
    else
#endif
    success = success && Read(snapshot.binding, bindingMaterial) &&
        bindingMaterial == candidate && Read(candidate, table) && table == materialModule + off::MaterialVtable &&
        Read(table + 0x30, method) && method == materialModule + off::MaterialMode;
    if (success && candidate && method) {
        __try {
            mode = reinterpret_cast<ModeFn>(method)(reinterpret_cast<void*>(candidate), reinterpret_cast<void*>(view + 0x30));
            if (mode) material = candidate;
        } __except (EXCEPTION_EXECUTE_HANDLER) { mode = 0; }
    }
    if (!enabled.load(std::memory_order_acquire)) material = mode = 0;
    return material && mode;
}
void PlayerChamsMaterialsStop() noexcept {
    enabled.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&lifetime);
    Report("Player materials stopped and retained until process exit.");
    ReleaseSRWLockExclusive(&lifetime);
}
const char* PlayerChamsMaterialsStatus() noexcept { return status.load(std::memory_order_acquire); }

#ifdef CS2_PLAYER_CHAMS_MATERIAL_CHECKS
void PlayerChamsMaterialsTestConfigure(const PlayerChamsMaterialTestApi& api) noexcept {
    testing = true; testApi = api;
    construct = api.construct; root = api.root; destroy = api.destroy; parse = api.parse; create = api.create;
    retained[0] = retained[1] = {};
    enabled.store(false); phase.store(0);
}
#endif
