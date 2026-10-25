#include "world_sky.h"
#include "world_offsets.h"
#include "world_settings.h"
#include "world_skybox_offsets.h"
#include "model_swap_offsets.h"
#include "player_chams_material_offsets.h"
#include "client_frame.h"
#include "skybox_catalog.h"
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <array>

namespace {
using RenderFn = std::uintptr_t (*)(void*, void*, void*, int, void*, void*, void*);
using SetterFn = std::uintptr_t (*)(void*, std::uint32_t, const float*);
RenderFn originalRender = nullptr;
SetterFn originalSetter = nullptr;
std::uintptr_t sceneBase = 0;
DWORD scopeTls = TLS_OUT_OF_INDEXES;
struct DrawScope { std::uint32_t kind; };
WorldSkyLogCallback logger = nullptr;
std::atomic<bool> enabled{false};
std::atomic<const char*> status{"sky controls not initialized"};
std::atomic<unsigned long long> draws{0}, modified{0}, rejected{0};
std::atomic<DWORD> lastOwner{0};
SRWLOCK mutationLock = SRWLOCK_INIT;
bool stopped = false;
unsigned long long reportedDraws = 0, reportedModified = 0, reportedRejected = 0;
ULONGLONG reportTime = 0;
namespace skybox = world_skybox_offsets;
namespace resource = model_swap_offsets;
namespace material = player_chams_material_offsets;
std::atomic<bool> resourcesEnabled{false};
std::atomic<const char*> resourceStatus{"custom skybox not initialized"};
SRWLOCK resourceLock = SRWLOCK_INIT;
std::uintptr_t hostBase = 0, resourcesBase = 0, tierBase = 0, materialBase = 0, fsBase = 0;
std::uintptr_t clientBase = 0, filesystem = 0, materialManager = 0;
bool mounted = false, resourcesPrepared = false;
struct SkyResource {
    std::array<char,256> path{};
    std::uintptr_t binding = 0;
};
// Native resource references and the workspace search path remain until exit.
// Bounded immutable resources also keep in-flight native/GPU users valid.
std::array<SkyResource,64> skyResources{};
unsigned skyResourceCount = 0;
unsigned materialAttempts = 0;
std::array<char,256> requestedResource{};
std::uintptr_t publishedBinding = 0;
#ifdef CS2_WORLD_SKY_CHECKS
WorldSkyTestLoadResource testLoadResource = nullptr;
bool testResources = false;
#endif
void ResourceStatus(const char* message) noexcept {
    if (resourceStatus.exchange(message,std::memory_order_acq_rel)==message) return;
    WorldSkyboxSetRuntimeStatus(message);
    if (logger) {
        char text[320]{};
        std::snprintf(text,sizeof(text),"WORLD_SKYBOX status=%s",message);
        logger(text);
    }
}

bool Copy(std::uintptr_t address, void* output, std::size_t bytes) noexcept {
    if (!output || address < 0x10000 || address > 0x00007fffffffffffULL - bytes) return false;
    __try { std::memcpy(output, reinterpret_cast<const void*>(address), bytes); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool Read(std::uintptr_t address, T& output) noexcept {
    return Copy(address, &output, sizeof(output));
}

template<std::size_t N> bool Match(std::uintptr_t address, const unsigned char (&bytes)[N]) noexcept {
    unsigned char actual[N]{};
    return Copy(address, actual, N) && !std::memcmp(actual, bytes, N);
}
bool Image(std::uintptr_t address, unsigned timestamp, unsigned size) noexcept {
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    return Read(address,dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<0x100000 &&
        Read(address+dos.e_lfanew,nt) && nt.Signature==IMAGE_NT_SIGNATURE && nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 &&
        nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC && nt.FileHeader.TimeDateStamp==timestamp && nt.OptionalHeader.SizeOfImage==size;
}
bool CurrentResources() noexcept {
#ifdef CS2_WORLD_SKY_CHECKS
    if (testResources) return true;
#endif
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"host.dll"))==hostBase &&
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"resourcesystem.dll"))==resourcesBase &&
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"tier0.dll"))==tierBase &&
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"materialsystem2.dll"))==materialBase &&
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"filesystem_stdio.dll"))==fsBase &&
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"))==clientBase;
}
struct ResourceDiagnostic {
    const char* stage = "catalog";
    const char* failure = "Skybox is absent from the catalog. Refresh skyboxes, then select it again.";
    std::uint64_t type = 0;
    int state = -1;
    std::uintptr_t binding = 0, value = 0;
    bool textureReady = false, parsed = false;
    char expectedName[256]{}, actualName[256]{};
};
void ReportResourceAttempt(const char* path, bool valid, const ResourceDiagnostic& diagnostic) noexcept {
    if (!logger) return;
    char message[1400]{};
    std::snprintf(message,sizeof(message),
        "WORLD_SKYBOX_LOAD path=\"%s\" result=%s stage=%s type=0x%llx state=%d binding=0x%llx value=0x%llx texture_ready=%d parsed=%d expected=\"%s\" actual=\"%s\" failure=\"%s\"",
        path,valid ? "ready" : "failed",diagnostic.stage,
        static_cast<unsigned long long>(diagnostic.type),diagnostic.state,
        static_cast<unsigned long long>(diagnostic.binding),static_cast<unsigned long long>(diagnostic.value),
        diagnostic.textureReady,diagnostic.parsed,diagnostic.expectedName,diagnostic.actualName,valid ? "none" : diagnostic.failure);
    logger(message);
}
bool MountResources(ResourceDiagnostic& diagnostic) noexcept {
    diagnostic.stage="mount";
    diagnostic.failure="Workspace skybox mount unavailable. See WORLD_SKYBOX_LOAD in the session log.";
    if (mounted) return true;
    std::uintptr_t table = 0, method = 0;
    if (!Read(filesystem,table) || table!=fsBase+skybox::FilesystemVtable ||
        !Read(table+skybox::AddSearchPathSlot,method) || method!=fsBase+skybox::AddSearchPath) return false;
#ifdef CS2_WORKSPACE_ROOT
    constexpr wchar_t directory[] = CS2_WORKSPACE_ROOT L"/skyboxes";
    char path[2048]{};
    // The native filesystem consumes UTF-8 paths. Never derive this from the
    // host process's working directory or write into the game installation.
    if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,directory,-1,path,sizeof(path),nullptr,nullptr)) return false;
    __try {
        // Same six-argument ABI and tail/default arguments as engine2's
        // USRLOCAL mount at 0x20E0D2..0x20E0FB, with a GAME content path ID.
        reinterpret_cast<void (*)(void*,const char*,const char*,int,int,int)>(method)(
            reinterpret_cast<void*>(filesystem),path,"GAME",1,0,0);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    mounted = true;
    return true;
#else
    return false;
#endif
}
std::uintptr_t LoadResource(const char* path, std::uint64_t expectedType, ResourceDiagnostic& diagnostic) noexcept {
    alignas(16) unsigned char name[resource::ResourceNameSize]{};
    const auto flags = resource::ResourceNameFlags;
    std::memcpy(name+resource::ResourceNameFlagsOffset,&flags,sizeof(flags));
    bool initialized = false, loaded = false;
    std::uintptr_t binding = 0;
    diagnostic.stage="name_initialization";
    diagnostic.failure="Native skybox resource name initialization failed.";
    __try {
        initialized = reinterpret_cast<bool (*)(void*,const char*)>(hostBase+resource::InitializeResourceName)(name,path);
        std::uint64_t type = 0;
        std::memcpy(&type,name+resource::ResourceType,sizeof(type));
        diagnostic.type=type;
        if (initialized) {
            diagnostic.stage="resource_type";
            diagnostic.failure="Native skybox resource type does not match the selected asset.";
        }
        if (initialized && type==expectedType) {
            diagnostic.stage="resource_system";
            diagnostic.failure="Native skybox resource-system interface is unavailable.";
        }
        if (initialized && type==expectedType) {
            diagnostic.stage="resource_system";
            diagnostic.failure="Native skybox resource system is unavailable or changed.";
        }
        std::uintptr_t manager = 0, table = 0, method = 0;
        if (initialized && type==expectedType && Read(hostBase+resource::ResourceSystemPointer,manager) &&
            Read(manager,table) && table==resourcesBase+resource::ResourceSystemVtable &&
            Read(table+resource::BlockingLoadSlot,method) && method==resourcesBase+resource::BlockingLoad) {
            diagnostic.stage="native_load_binding";
            diagnostic.failure="Native skybox resource load returned no usable binding.";
            binding = reinterpret_cast<std::uintptr_t (*)(void*,const void*,const char*)>(method)(
                reinterpret_cast<void*>(manager),name,"CodexCustomSkybox");
            diagnostic.binding=binding;
            std::uintptr_t state = 0;
            if (binding) {
                diagnostic.stage="native_load_state_query";
                diagnostic.failure="Native skybox loaded-state query is unavailable.";
            }
            if (Read(table+0x198,state) && state>=resourcesBase && state<resourcesBase+resource::ResourceSystemImageSize) {
                diagnostic.state=reinterpret_cast<int (*)(void*,const void*)>(state)(reinterpret_cast<void*>(manager),name);
                loaded=diagnostic.state==3;
                diagnostic.stage="native_load_state";
                diagnostic.failure="Native skybox resource did not reach loaded state. See the session log for its state.";
            }
        }
        reinterpret_cast<void (*)(void*,int)>(tierBase+resource::PurgeResourceName)(name,0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        diagnostic.failure="Native skybox loading raised an exception. See the session log for its stage."; return 0;
    }
    std::uintptr_t value = 0;
    if (!initialized || !loaded || !binding) {
        if (initialized && !std::strcmp(diagnostic.stage,"native_load_state") && !binding) {
            diagnostic.stage="native_load_binding";
            diagnostic.failure="Native skybox resource load returned no binding.";
        }
        return 0;
    }
    diagnostic.stage="resource_value";
    diagnostic.failure="Native skybox binding has no loaded resource value.";
    if (!Read(binding,value) || !value) return 0;
    diagnostic.value=value;
    diagnostic.stage="resource_ready";
    return binding;
}
std::uintptr_t BuildSkyMaterial(const char* texture, unsigned index, ResourceDiagnostic& diagnostic) noexcept {
    if (!LoadResource(texture,skybox::TextureResourceType,diagnostic)) return 0;
    diagnostic.textureReady=true;
    diagnostic.stage="texture_ready";
    char recipe[1024]{}, name[96]{};
    if (std::snprintf(recipe,sizeof(recipe),
        "<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->\n"
        "{ shader=\"sky.vfx\" g_tSkyTexture=resource:\"%s\" g_flBrightnessExposureBias=0.0 g_flRenderOnlyExposureBias=0.0 }",texture)>=static_cast<int>(sizeof(recipe))) return 0;
    std::snprintf(name,sizeof(name),"materials/codex_custom_sky_%u.vmat",index);
    std::snprintf(diagnostic.expectedName,sizeof(diagnostic.expectedName),"%s",name);
    struct KV3ID { const char* name; std::uint64_t low,high; };
    constexpr KV3ID format{"generic",0x469806E97412167Cull,0xE73790B53EE6F2AFull};
    alignas(16) unsigned char arena[material::ArenaSize]{};
    void* binding = nullptr;
    bool constructed = false, parsed = false;
    __try {
        diagnostic.stage="material_arena";
        diagnostic.failure="Skybox material arena initialization failed.";
        reinterpret_cast<void* (*)(void*,unsigned)>(clientBase+material::ArenaConstructor)(arena,0); constructed=true;
        if (void* node = reinterpret_cast<void* (*)(void*)>(clientBase+material::ArenaGetRoot)(arena)) {
            diagnostic.stage="material_parse";
            diagnostic.failure="Skybox material recipe parsing failed.";
            parsed = reinterpret_cast<bool (*)(void*,void*,const char*,const void*,const char*,unsigned)>(tierBase+material::LoadKV3String)(
                node,nullptr,recipe,&format,"",0);
            diagnostic.parsed=parsed;
            if (parsed) {
                diagnostic.stage="material_create";
                diagnostic.failure="Native skybox material creation failed.";
                reinterpret_cast<void* (*)(void*,void**,const char*,void*,unsigned,bool)>(materialBase+material::CreateMaterial)(
                    reinterpret_cast<void*>(materialManager),&binding,name,node,1,true);
                diagnostic.binding=reinterpret_cast<std::uintptr_t>(binding);
            }
        }
        constructed=false;
        reinterpret_cast<void (*)(void*)>(clientBase+material::ArenaDestructor)(arena);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (constructed) { __try { reinterpret_cast<void (*)(void*)>(clientBase+material::ArenaDestructor)(arena); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
        return 0;
    }
    std::uintptr_t value=0, actualName=0;
    if (!parsed || !binding || !Read(reinterpret_cast<std::uintptr_t>(binding),value) || !value) return 0;
    diagnostic.value=value;
    diagnostic.stage="material_name";
    diagnostic.failure="Created skybox material is a placeholder or has an unexpected name.";
    if (!Read(value+0x10,actualName) || !Copy(actualName,diagnostic.actualName,std::strlen(name)+1) ||
        std::strcmp(diagnostic.actualName,name)) return 0;
    diagnostic.stage="material_ready";
    return reinterpret_cast<std::uintptr_t>(binding);
}
bool ValidBinding(std::uintptr_t binding, bool newlyCreated = false) noexcept {
    std::uintptr_t value = 0, table = 0;
    int references = 0;
    if (!Read(binding,value) || !value || !Read(binding+skybox::BindingReferences,references) ||
        references<0 || (!newlyCreated && references==0)) return false;
    return Read(value,table) && table==materialBase+material::MaterialVtable;
}
bool RetainBinding(std::uintptr_t binding, bool newlyCreated) noexcept {
    if (!ValidBinding(binding,newlyCreated)) return false;
    __try {
        auto* counter=reinterpret_cast<volatile LONG*>(binding+skybox::BindingReferences);
        // Creation returns a registered binding with zero strong references.
        // Acquire our first reference before requiring draw-ready ownership.
        // A loaded/borrowed binding must already have a native owner.
        LONG references=InterlockedCompareExchange(counter,0,0);
        for (;;) {
            if (references<0 || (!newlyCreated && references==0) || references==MAXLONG) return false;
            const LONG observed=InterlockedCompareExchange(counter,references+1,references);
            if (observed==references) break;
            references=observed;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    // Publication and every subsequent draw still require a positive count
    // and the exact native material type. Retain stopped resources until exit.
    return ValidBinding(binding);
}
void ResourceOwner(void*,int) noexcept {
    if (!resourcesEnabled.load(std::memory_order_acquire)) return;
    const auto settings = WorldGetSettings();
    AcquireSRWLockExclusive(&resourceLock);
    if (!resourcesEnabled.load(std::memory_order_acquire)) { ReleaseSRWLockExclusive(&resourceLock); return; }
    if (!settings.customSkyboxEnabled || !settings.skyboxMaterial[0]) {
        requestedResource={}; publishedBinding=0;
        ResourceStatus("Custom skybox disabled.");
        ReleaseSRWLockExclusive(&resourceLock); return;
    }
    if (requestedResource==settings.skyboxMaterial) { ReleaseSRWLockExclusive(&resourceLock); return; }
    requestedResource=settings.skyboxMaterial; publishedBinding=0;
    for (unsigned i=0;i<skyResourceCount;++i) if (skyResources[i].path==requestedResource) {
        publishedBinding=skyResources[i].binding;
        ResourceStatus("Custom skybox ready.");
        ReleaseSRWLockExclusive(&resourceLock); return;
    }
    const unsigned index = skyResourceCount;
    ResourceStatus("Loading selected skybox...");
    ReleaseSRWLockExclusive(&resourceLock);
    if (index==skyResources.size() || !CurrentResources()) {
        if (resourcesEnabled.load(std::memory_order_acquire)) ResourceStatus("Custom skybox resource limit or runtime changed; restart to retry."); return;
    }
    std::uintptr_t binding = 0;
    ResourceDiagnostic diagnostic;
    if (logger) {
        char message[384]{};
        std::snprintf(message,sizeof(message),"WORLD_SKYBOX_LOAD_BEGIN path=\"%s\"",settings.skyboxMaterial.data());
        logger(message);
    }
#ifdef CS2_WORLD_SKY_CHECKS
    diagnostic.stage="test_resource_boundary";
    diagnostic.failure="Native skybox resource unavailable at the test loading boundary.";
    binding=testLoadResource ? testLoadResource(settings.skyboxMaterial.data()) : 0;
    diagnostic.binding=binding;
#else
    {
        // Catalog validation establishes a single-cubemap texture or a known
        // shipped sky material. Loading/parsing stays on the frame owner.
        if (SkyboxCatalogContains(settings.skyboxMaterial.data()) && MountResources(diagnostic)) {
            const auto* path=settings.skyboxMaterial.data();
            const auto length=std::strlen(path);
            const bool texture=length>=5 && !std::strcmp(path+length-5,".vtex");
            if (texture) {
                if (materialAttempts<skyResources.size()) binding=BuildSkyMaterial(path,materialAttempts++,diagnostic);
                else { diagnostic.stage="material_attempt_limit"; diagnostic.failure="Skybox material attempt limit reached. Restart normally to try more."; }
            } else {
                binding=LoadResource(path,skybox::MaterialResourceType,diagnostic);
                if (binding) {
                    diagnostic.stage="material_name";
                    diagnostic.failure="Loaded skybox material is a placeholder or has an unexpected name.";
                    std::snprintf(diagnostic.expectedName,sizeof(diagnostic.expectedName),"%s",path);
                    std::uintptr_t value=0, actualName=0;
                    if (!Read(binding,value) || !Read(value+0x10,actualName) ||
                        !Copy(actualName,diagnostic.actualName,length+1) || std::strcmp(diagnostic.actualName,path)) binding=0;
                    else diagnostic.stage="material_ready";
                }
            }
        }
    }
#endif
    const bool newlyCreated=std::string_view(settings.skyboxMaterial.data()).ends_with(".vtex");
    bool valid = ValidBinding(binding,newlyCreated);
    if (binding && !valid) {
        diagnostic.stage="material_binding_validation";
        diagnostic.failure="Skybox material binding failed native type or reference validation.";
    }
    if (valid) {
        if (!RetainBinding(binding,newlyCreated)) {
            diagnostic.stage="material_retain";
            diagnostic.failure="Skybox material could not retain its native reference."; valid=false;
        }
    }
    ReportResourceAttempt(settings.skyboxMaterial.data(),valid,diagnostic);
    // Blocking native loads may wait for render jobs. Only publication owns
    // this lock; never hold it while invoking the engine or the loader.
    AcquireSRWLockExclusive(&resourceLock);
    if (valid) { skyResources[skyResourceCount++]={settings.skyboxMaterial,binding}; }
    const auto current = WorldGetSettings();
    if (resourcesEnabled.load(std::memory_order_acquire) && CurrentResources() && current.customSkyboxEnabled &&
        current.skyboxMaterial==settings.skyboxMaterial && requestedResource==settings.skyboxMaterial) {
        publishedBinding=valid ? binding : 0;
        ResourceStatus(valid ? "Custom skybox ready." : diagnostic.failure);
    }
    ReleaseSRWLockExclusive(&resourceLock);
}
bool PrepareResources() noexcept {
#ifdef CS2_WORLD_SKY_CHECKS
    return false;
#else
    clientBase=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    hostBase=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"host.dll"));
    resourcesBase=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"resourcesystem.dll"));
    tierBase=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"tier0.dll"));
    materialBase=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"materialsystem2.dll"));
    fsBase=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"filesystem_stdio.dll"));
    if (!Image(clientBase,material::ClientTimestamp,material::ClientImageSize) || !Image(hostBase,resource::HostTimestamp,resource::HostImageSize) ||
        !Image(resourcesBase,resource::ResourceSystemTimestamp,resource::ResourceSystemImageSize) || !Image(tierBase,resource::Tier0Timestamp,resource::Tier0ImageSize) ||
        !Image(materialBase,material::MaterialTimestamp,material::MaterialImageSize) || !Image(fsBase,skybox::FilesystemTimestamp,skybox::FilesystemImageSize) ||
        !Match(hostBase+resource::InitializeResourceName,resource::InitializeResourceNameBytes) || !Match(resourcesBase+resource::BlockingLoad,resource::BlockingLoadBytes) ||
        !Match(tierBase+resource::PurgeResourceName,resource::PurgeResourceNameBytes) || !Match(fsBase+skybox::AddSearchPath,skybox::AddSearchPathBytes) ||
        !Match(clientBase+material::ArenaConstructor,material::ArenaConstructorBytes) || !Match(clientBase+material::ArenaGetRoot,material::ArenaGetRootBytes) ||
        !Match(clientBase+material::ArenaDestructor,material::ArenaDestructorBytes) || !Match(materialBase+material::CreateMaterial,material::CreateMaterialBytes) ||
        !Match(tierBase+material::LoadKV3String,material::LoadKV3StringBytes) ||
        reinterpret_cast<std::uintptr_t>(GetProcAddress(reinterpret_cast<HMODULE>(tierBase),"?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2I@Z"))!=tierBase+material::LoadKV3String) return false;
    const auto fsFactory=GetProcAddress(reinterpret_cast<HMODULE>(fsBase),"CreateInterface");
    const auto materialFactory=GetProcAddress(reinterpret_cast<HMODULE>(materialBase),"CreateInterface");
    __try {
        filesystem=fsFactory ? reinterpret_cast<std::uintptr_t (*)(const char*,int*)>(fsFactory)("VFileSystem017",nullptr) : 0;
        materialManager=materialFactory ? reinterpret_cast<std::uintptr_t (*)(const char*,int*)>(materialFactory)("VMaterialSystem2_001",nullptr) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    std::uintptr_t table=0, method=0;
    if (!Read(filesystem,table) || table!=fsBase+skybox::FilesystemVtable || !Read(table+skybox::AddSearchPathSlot,method) || method!=fsBase+skybox::AddSearchPath ||
        !Read(materialManager,table) || table!=materialBase+material::MaterialSystemVtable || !Read(table+29*sizeof(void*),method) || method!=materialBase+material::CreateMaterial) return false;
    resourcesPrepared=ClientFrameSubscribe(ResourceOwner,logger);
    return resourcesPrepared;
#endif
}
void DiscoverResources() noexcept {
#ifndef CS2_WORLD_SKY_CHECKS
    try {
        std::string error;
        if (!SkyboxCatalogDiscover(SkyboxCatalogDirectory(),error) && logger) logger(error.c_str());
    } catch (...) {
        if (logger) logger("WORLD_SKYBOX initial workspace catalog discovery failed; use Refresh skyboxes to retry");
    }
#endif
}
std::uintptr_t SelectedBinding(const WorldSettings& settings) noexcept {
    if (!settings.customSkyboxEnabled || !resourcesEnabled.load(std::memory_order_acquire)) return 0;
    AcquireSRWLockShared(&resourceLock);
    const auto binding=requestedResource==settings.skyboxMaterial ? publishedBinding : 0;
    ReleaseSRWLockShared(&resourceLock);
    return resourcesEnabled.load(std::memory_order_acquire) && CurrentResources() && ValidBinding(binding) ? binding : 0;
}

std::uintptr_t SetParameter(void* collection, std::uint32_t key, const float* value, std::uintptr_t caller) noexcept {
    if (!enabled.load(std::memory_order_acquire) || key != world_offsets::SkyTintKey || caller != sceneBase + world_offsets::SkyTintReturn)
        return originalSetter(collection, key, value);
    const auto* scope = scopeTls == TLS_OUT_OF_INDEXES ? nullptr : static_cast<const DrawScope*>(TlsGetValue(scopeTls));
    if (!scope || (scope->kind != world_offsets::VisibleKind && scope->kind != world_offsets::LightingKind))
        return originalSetter(collection, key, value);
    AcquireSRWLockExclusive(&mutationLock);
    // Stop disarms before taking this barrier. The full native Render never
    // holds it, so engine/job waits cannot deadlock the stop barrier.
    const auto settings = WorldGetSettings();
    float output[4]{};
    bool apply = enabled.load(std::memory_order_acquire) && !stopped && settings.skyEnabled;
    if (enabled.load(std::memory_order_acquire) && !stopped && !settings.skyEnabled)
        status.store("sky controls disabled", std::memory_order_release);
    if (apply) {
        apply = Copy(reinterpret_cast<std::uintptr_t>(value), output, sizeof(output));
        const auto* tint = scope->kind == world_offsets::VisibleKind ? settings.skyTint : settings.skyLightingTint;
        for (unsigned i = 0; apply && i < 3; ++i) {
            apply = std::isfinite(output[i]) && output[i] >= 0 && output[i] <= 65536;
            if (apply) output[i] *= tint[i] * settings.skyBrightness;
        }
        if (!apply) rejected.fetch_add(1, std::memory_order_relaxed);
    }
    if (!apply) {
        ReleaseSRWLockExclusive(&mutationLock);
        return originalSetter(collection, key, value);
    }
    // Verified native writer copies all 16 bytes into its own collection. Its
    // possible allocator calls do not contain render/job synchronization.
    const auto result = originalSetter(collection, key, output);
    modified.fetch_add(1, std::memory_order_relaxed);
    status.store("sky draw tint active", std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
    return result;
}
__declspec(noinline) std::uintptr_t HookSetter(void* collection, std::uint32_t key, const float* value) noexcept {
    return SetParameter(collection, key, value, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
std::uintptr_t HookRender(void* descriptor, void* context, void* packets, int count,
                          void* arg5, void* view, void* stats) noexcept {
    draws.fetch_add(1, std::memory_order_relaxed);
    lastOwner.store(GetCurrentThreadId(), std::memory_order_relaxed);
    if (!enabled.load(std::memory_order_acquire))
        return originalRender(descriptor, context, packets, count, arg5, view, stats);
    DrawScope scope{};
    const auto packetsAddress = reinterpret_cast<std::uintptr_t>(packets);
    std::uintptr_t source = 0;
    // The native renderer selects the final packet's source for the batch.
    // Never cache a source across draws or change any source field.
    std::uintptr_t finalPacket = 0;
    if (count > 0 && count <= 1048576 && packetsAddress >= 0x10000 &&
        packetsAddress <= 0x00007fffffffffffULL - static_cast<std::uintptr_t>(count) * world_offsets::DrawStride &&
        Read(packetsAddress + static_cast<std::uintptr_t>(count - 1) * world_offsets::DrawStride + world_offsets::DrawSource, source) &&
        source <= 0x00007fffffffffffULL - world_offsets::SceneKind)
        Read(source + world_offsets::SceneKind, scope.kind);
    if (source && scope.kind==world_offsets::VisibleKind)
        finalPacket=packetsAddress+static_cast<std::uintptr_t>(count-1)*world_offsets::DrawStride;
    alignas(16) unsigned char localScene[skybox::SceneBytes]{}, localPacket[world_offsets::DrawStride]{};
    void* renderPackets=packets;
    int renderCount=count;
    if (finalPacket) {
        const auto settings=WorldGetSettings();
        const auto binding=SelectedBinding(settings);
        if (binding && Copy(source,localScene,sizeof(localScene)) && Copy(finalPacket,localPacket,sizeof(localPacket))) {
            std::memcpy(localScene+skybox::SceneMaterial,&binding,sizeof(binding));
            const auto localSource=reinterpret_cast<std::uintptr_t>(localScene);
            std::memcpy(localPacket+world_offsets::DrawSource,&localSource,sizeof(localSource));
            // This renderer reads its packet array only once, to select the
            // final source. Count otherwise only guards an empty call. Native
            // transformation and parameter writers synchronously copy values.
            renderPackets=localPacket; renderCount=1;
        }
    }
    void* previous = scopeTls == TLS_OUT_OF_INDEXES ? nullptr : TlsGetValue(scopeTls);
    const bool scoped = scopeTls != TLS_OUT_OF_INDEXES && TlsSetValue(scopeTls, &scope);
    if (!scoped) {
        enabled.store(false, std::memory_order_release);
        status.store("sky draw TLS failed; controls disarmed", std::memory_order_release);
    }
    const auto result = originalRender(descriptor, context, renderPackets, renderCount, arg5, view, stats);
    if (scoped && !TlsSetValue(scopeTls, previous)) {
        enabled.store(false, std::memory_order_release);
        status.store("sky draw TLS restoration failed; controls disarmed", std::memory_order_release);
    }
    return result;
}
}

bool WorldSkyPrepare(WorldSkyLogCallback callback) noexcept {
    logger = callback;
    sceneBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"scenesystem.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    unsigned char renderBytes[sizeof(world_offsets::SkyRenderBytes)]{}, setterBytes[sizeof(world_offsets::SetFloat4Bytes)]{};
    std::uintptr_t renderSlot = 0;
    const auto client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER clientDos{}; IMAGE_NT_HEADERS64 clientNt{};
    unsigned char producerBytes[sizeof(world_offsets::LightingProducerBytes)]{};
    if (!sceneBase || !Read(sceneBase, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !Read(sceneBase + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.TimeDateStamp != world_offsets::SceneTimestamp ||
        nt.OptionalHeader.SizeOfImage != world_offsets::SceneImageSize ||
        !Copy(sceneBase + world_offsets::SkyRender, renderBytes, sizeof(renderBytes)) || std::memcmp(renderBytes, world_offsets::SkyRenderBytes, sizeof(renderBytes)) ||
        !Copy(sceneBase + world_offsets::SetFloat4, setterBytes, sizeof(setterBytes)) || std::memcmp(setterBytes, world_offsets::SetFloat4Bytes, sizeof(setterBytes)) ||
        !Read(sceneBase + world_offsets::SkyDescriptorVtable + 8, renderSlot) || renderSlot != sceneBase + world_offsets::SkyRender ||
        !client || !Read(client, clientDos) || clientDos.e_magic != IMAGE_DOS_SIGNATURE || clientDos.e_lfanew <= 0 || clientDos.e_lfanew > 0x100000 ||
        !Read(client + clientDos.e_lfanew, clientNt) || clientNt.Signature != IMAGE_NT_SIGNATURE || clientNt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        clientNt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || clientNt.FileHeader.TimeDateStamp != world_offsets::ClientTimestamp ||
        clientNt.OptionalHeader.SizeOfImage != world_offsets::ClientImageSize ||
        !Copy(client + world_offsets::LightingProducer, producerBytes, sizeof(producerBytes)) || std::memcmp(producerBytes, world_offsets::LightingProducerBytes, sizeof(producerBytes))) {
        status.store("installed sky draw build verification failed", std::memory_order_release); return false;
    }
    if (scopeTls == TLS_OUT_OF_INDEXES) scopeTls = TlsAlloc();
    if (scopeTls == TLS_OUT_OF_INDEXES) { status.store("sky draw TLS allocation failed", std::memory_order_release); return false; }
    const auto renderTarget = reinterpret_cast<void*>(sceneBase + world_offsets::SkyRender);
    const auto setterTarget = reinterpret_cast<void*>(sceneBase + world_offsets::SetFloat4);
    if (MH_CreateHook(renderTarget, reinterpret_cast<void*>(&HookRender), reinterpret_cast<void**>(&originalRender)) != MH_OK || !originalRender ||
        MH_CreateHook(setterTarget, reinterpret_cast<void*>(&HookSetter), reinterpret_cast<void**>(&originalSetter)) != MH_OK || !originalSetter ||
        MH_QueueEnableHook(renderTarget) != MH_OK || MH_QueueEnableHook(setterTarget) != MH_OK) {
        status.store("sky draw hook preparation failed", std::memory_order_release); return false;
    }
    status.store("awaiting sky draw", std::memory_order_release);
    DiscoverResources();
    ResourceStatus(PrepareResources() ? "Choose a skybox to replace the map sky." : "Custom skybox native resource contract unavailable.");
    if (logger) logger("WORLD_SKY_BUILD_VERIFIED scenesystem Render=0x154FB0 args=7 SetFloat4=0x4DA70 caller=0x1551A9 key=0x99E6002A; per-draw local parameters, scene fields untouched");
    return true;
}
void WorldSkyActivate() noexcept {
    AcquireSRWLockExclusive(&mutationLock);
    if (!stopped && originalRender && originalSetter && scopeTls != TLS_OUT_OF_INDEXES) enabled.store(true, std::memory_order_release);
    if (!stopped && resourcesPrepared) resourcesEnabled.store(true,std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
}
void WorldSkyStop(const char* reason) noexcept {
    resourcesEnabled.store(false,std::memory_order_release);
    enabled.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&mutationLock);
    stopped = true;
    enabled.store(false, std::memory_order_release);
    status.store("sky stopped and resident; future draws use native parameters", std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
    AcquireSRWLockExclusive(&resourceLock);
    publishedBinding=0;
    ResourceStatus("Custom skybox stopped; resources and pending draws retained until exit.");
    ReleaseSRWLockExclusive(&resourceLock);
    if (logger) {
        char message[512]{};
        std::snprintf(message, sizeof(message), "WORLD_SKY_STOPPED_RESIDENT reason=%s; modified parameter writers drained, pending GPU draws may finish; forwarding callbacks and TLS retained, no physical unload", reason ? reason : "stop");
        logger(message);
    }
}
const char* WorldSkyStatus() noexcept { return status.load(std::memory_order_acquire); }
void WorldSkyFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (reportTime && now - reportTime < 2000) return;
    reportTime = now;
    const auto called = draws.load(std::memory_order_relaxed), applied = modified.load(std::memory_order_relaxed), failed = rejected.load(std::memory_order_relaxed);
    if (called == reportedDraws && applied == reportedModified && failed == reportedRejected) return;
    reportedDraws = called; reportedModified = applied; reportedRejected = failed;
    char message[512]{};
    std::snprintf(message, sizeof(message), "WORLD_SKY draws=%llu parameter_updates=%llu rejected=%llu last_owner=%lu status=%s; callback activity does not prove visible output", called, applied, failed, lastOwner.load(std::memory_order_relaxed), WorldSkyStatus());
    logger(message);
}
#ifdef CS2_WORLD_SKY_CHECKS
bool WorldSkyTestConfigure(std::uintptr_t base, WorldSkyTestRender render, WorldSkyTestSetter setter) noexcept {
    if (scopeTls == TLS_OUT_OF_INDEXES) scopeTls = TlsAlloc();
    sceneBase = base; originalRender = render; originalSetter = setter; stopped = false;
    enabled.store(scopeTls != TLS_OUT_OF_INDEXES, std::memory_order_release);
    testResources=true; materialBase=base; resourcesPrepared=true; resourcesEnabled.store(true);
    skyResourceCount=0; materialAttempts=0; skyResources={}; requestedResource={}; publishedBinding=0; testLoadResource=nullptr;
    return scopeTls != TLS_OUT_OF_INDEXES;
}
std::uintptr_t WorldSkyTestDraw(void* packets, int count) noexcept { return HookRender(nullptr, nullptr, packets, count, nullptr, nullptr, nullptr); }
std::uintptr_t WorldSkyTestSet(void* collection, std::uint32_t key, const float* value, std::uintptr_t caller) noexcept { return SetParameter(collection, key, value, caller); }
void WorldSkyTestResourceLoader(WorldSkyTestLoadResource loader) noexcept { testLoadResource=loader; }
void WorldSkyTestOwnerFrame() noexcept { ResourceOwner(nullptr,0); }
void WorldSkyTestLogger(WorldSkyLogCallback callback) noexcept { logger=callback; }
#endif
