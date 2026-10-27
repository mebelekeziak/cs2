#include "client_frame.h"
#include "model_swap_offsets.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <MinHook.h>
#include <array>

namespace {
using FrameFn = void (*)(void*, int);
FrameFn original = nullptr;
std::uintptr_t client = 0;
DWORD scopeTls = TLS_OUT_OF_INDEXES;
std::array<ClientFrameCallback, 8> subscribers{};
std::size_t subscriberCount = 0;
bool prepared = false;
#ifdef CS2_CLIENT_FRAME_CHECKS
bool testRuntime = false;
#endif
bool CurrentClient() noexcept {
#ifdef CS2_CLIENT_FRAME_CHECKS
    if (testRuntime) return client != 0;
#endif
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) == client;
}
// Registration occurs during OverlayStart, before MH_ApplyQueued. There are no
// callback-list mutations after activation and no engine call under a lock.
void HookFrame(void* owner, int stage) noexcept {
    if (scopeTls == TLS_OUT_OF_INDEXES || TlsGetValue(scopeTls)) {
        original(owner, stage);
        return;
    }
    bool scoped = true;
    if (!TlsSetValue(scopeTls, &scoped)) { original(owner, stage); return; }
    original(owner, stage);
    std::uintptr_t table = 0;
    if (stage == model_swap_offsets::ApplyStage &&
        CurrentClient() &&
        MovementCopy(reinterpret_cast<std::uintptr_t>(owner), &table, sizeof(table)) &&
        table == client + model_swap_offsets::FrameVtable) {
        for (std::size_t i = 0; i < subscriberCount; ++i) subscribers[i](owner, stage);
    }
    TlsSetValue(scopeTls, nullptr);
}
bool Prepare(ClientFrameLogCallback logger) noexcept {
    if (prepared) return true;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::uintptr_t target = 0;
    unsigned char bytes[sizeof(model_swap_offsets::FrameStageBytes)]{};
    if (!client || !MovementCopy(client, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !MovementCopy(client + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != cs2_offsets::ClientTimestamp ||
        nt.OptionalHeader.SizeOfImage != cs2_offsets::ClientImageSize ||
        !MovementCopy(client + model_swap_offsets::FrameStage, bytes, sizeof(bytes)) ||
        std::memcmp(bytes, model_swap_offsets::FrameStageBytes, sizeof(bytes)) ||
        !MovementCopy(client + model_swap_offsets::FrameVtable + model_swap_offsets::FrameSlot * sizeof(target),
            &target, sizeof(target)) || target != client + model_swap_offsets::FrameStage) return false;
    scopeTls = TlsAlloc();
    if (scopeTls == TLS_OUT_OF_INDEXES) return false;
    if (MH_CreateHook(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&HookFrame),
        reinterpret_cast<void**>(&original)) != MH_OK || !original ||
        MH_QueueEnableHook(reinterpret_cast<void*>(target)) != MH_OK) return false;
    prepared = true;
    if (logger) logger("CLIENT_FRAME_BUILD_VERIFIED shared stage-8 owner; original-first, nested callbacks forwarded; subscribers retained until process exit");
    return true;
}

}
bool ClientFrameSubscribe(ClientFrameCallback callback, ClientFrameLogCallback logger) noexcept {
    if (!callback) return false;
    for (std::size_t i = 0; i < subscriberCount; ++i) if (subscribers[i] == callback) return true;
    if (subscriberCount == subscribers.size() || !Prepare(logger)) return false;
    subscribers[subscriberCount++] = callback;
    return true;
}
bool ClientFrameInCallback() noexcept {
    return scopeTls != TLS_OUT_OF_INDEXES && TlsGetValue(scopeTls) != nullptr;
}

#ifdef CS2_CLIENT_FRAME_CHECKS
void ClientFrameTestConfigure(std::uintptr_t base, void (*fn)(void*, int)) noexcept {
    if (scopeTls == TLS_OUT_OF_INDEXES) scopeTls = TlsAlloc();
    client = base; original = fn; prepared = true; testRuntime = true;
    subscriberCount = 0; subscribers = {};
}
void ClientFrameTestDispatch(void* owner, int stage) noexcept { HookFrame(owner, stage); }
#endif
