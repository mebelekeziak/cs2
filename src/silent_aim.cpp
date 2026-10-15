#include "silent_aim.h"
#include "silent_aim_command.h"
#include "silent_aim_target.h"
#include "silent_aim_settings.h"
#include "silent_aim_offsets.h"
#include "silent_aim_hitbox_offsets.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include "movement_telemetry.h"
#include <MinHook.h>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstring>

namespace {
namespace off = silent_aim_offsets;
using HistoryFn = void (*)(void*, int, void*);
HistoryFn original = nullptr;
std::uintptr_t client = 0;
AimBoneContext bones{};
SilentAimLogCallback logger = nullptr;
std::atomic<bool> active{false};
std::atomic<const char*> state{"silent aim not initialized"};
SRWLOCK mutationLock = SRWLOCK_INIT;
bool stopped = false, prepared = false;
std::uint64_t callbacks = 0, edited = 0;
ULONGLONG lastReport = 0;

template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
void Status(const char* value) noexcept { state.store(value, std::memory_order_release); }
bool Source(AimSource& source) noexcept {
    return Read(client + cs2_offsets::globals::dwEntityList, source.system) && source.system &&
        Read(client + cs2_offsets::globals::dwLocalPlayerController, source.controller) && source.controller &&
        Read(client + cs2_offsets::globals::dwLocalPlayerPawn, source.pawn) && source.pawn;
}
void Process(void* input, void* command) noexcept {
    ++callbacks;
    if (reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) != client ||
        reinterpret_cast<std::uintptr_t>(input) != client + off::Input) {
        active.store(false, std::memory_order_release);
        stopped = true; Status("client/input changed; silent aim stopped"); return;
    }
    DWORD foreground = 0;
    const auto window = GetForegroundWindow();
    GetWindowThreadProcessId(window, &foreground);
    if (!window || foreground != GetCurrentProcessId() || IsIconic(window)) {
        Status("game unfocused"); return;
    }
    if (GetAsyncKeyState(VK_END) & 0x8000) {
        active.store(false, std::memory_order_release);
        stopped = true; Status("stopped and resident"); return;
    }
    const auto settings = SilentAimGetSettings();
    if (!settings.enabled) { Status("disabled"); return; }
    AimShot shot{};
    if (!SilentAimReadShot(reinterpret_cast<std::uintptr_t>(command), shot)) {
        Status("awaiting primary-fire history"); return;
    }
    AimSource source{}, current{};
    AimTarget target{};
    if (!Source(source) || !SilentAimSelectTarget(source, shot.view, settings.fov, settings.hitbox, bones, target)) {
        Status("no enemy in aim FOV"); return;
    }
    if (!Source(current) || current.system != source.system || current.controller != source.controller ||
        current.pawn != source.pawn || !SilentAimTargetValid(source, target) ||
        !SilentAimWriteShot(shot, target.solution.angles)) {
        Status("source/history changed; shot unchanged"); return;
    }
    ++edited;
    Status("primary-fire history updated");
    const auto now = GetTickCount64();
    if (!lastReport || now - lastReport >= 2000) {
        lastReport = now;
        char message[512]{};
        std::snprintf(message, sizeof(message),
            "AIM_HISTORY callbacks=%llu edited=%llu target=0x%08x history_index=%d entries=%d hitbox=%d separation=%.3f pitch=%.3f yaw=%.3f owner=%lu; base/camera/buttons unchanged; history edit is not observed hit registration",
            callbacks, edited, target.enemy.pawnHandle, shot.index, shot.count, static_cast<int>(settings.hitbox), target.solution.separation,
            target.solution.angles.pitch, target.solution.angles.yaw, GetCurrentThreadId());
        MovementTelemetryPush(message);
    }
}
void HookHistory(void* input, int slot, void* command) noexcept {
    original(input, slot, command);
    if (!active.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&mutationLock);
    if (active.load(std::memory_order_acquire)) Process(input, command);
    ReleaseSRWLockExclusive(&mutationLock);
}
}

bool SilentAimPrepare(SilentAimLogCallback callback) noexcept {
    logger = callback;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::array<unsigned char, sizeof(off::HistoryBuilderBytes)> bytes{};
    namespace bone = silent_aim_hitbox_offsets;
    std::array<unsigned char, sizeof(bone::LookupBoneBytes)> lookupBytes{};
    std::array<unsigned char, sizeof(bone::SkeletonQueryBytes)> queryBytes{};
    std::uintptr_t vtable = 0, method = 0;
    if (!client || !Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !Read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != cs2_offsets::ClientTimestamp || nt.OptionalHeader.SizeOfImage != cs2_offsets::ClientImageSize ||
        !Read(client + off::Input, vtable) || vtable != client + off::InputVtable ||
        !Read(vtable + off::HistorySlot * sizeof(std::uintptr_t), method) || method != client + off::HistoryBuilder ||
        !MovementCopy(method, bytes.data(), bytes.size()) || std::memcmp(bytes.data(), off::HistoryBuilderBytes, bytes.size()) ||
        !MovementCopy(client + bone::LookupBone, lookupBytes.data(), lookupBytes.size()) ||
        std::memcmp(lookupBytes.data(), bone::LookupBoneBytes, lookupBytes.size()) ||
        !MovementCopy(client + bone::SkeletonQuery, queryBytes.data(), queryBytes.size()) ||
        std::memcmp(queryBytes.data(), bone::SkeletonQueryBytes, queryBytes.size())) {
        Status("installed history build verification failed"); return false;
    }
    if (MH_CreateHook(reinterpret_cast<void*>(method), reinterpret_cast<void*>(&HookHistory),
        reinterpret_cast<void**>(&original)) != MH_OK || !original ||
        MH_QueueEnableHook(reinterpret_cast<void*>(method)) != MH_OK) {
        Status("history hook preparation failed"); return false;
    }
    prepared = true;
    bones = {client, reinterpret_cast<AimBoneLookup>(client + bone::LookupBone)};
    Status("awaiting history hook activation");
    if (logger) logger("AIM_BUILD_VERIFIED native history slot=25; primary-fire history edited after original; dynamic named bones from copied pose; no camera/base/movement writes");
    return true;
}
void SilentAimActivate() noexcept {
    AcquireSRWLockExclusive(&mutationLock);
    if (prepared && !stopped) active.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&mutationLock);
}
void SilentAimStop(const char* reason) noexcept {
    active.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&mutationLock);
    stopped = true;
    active.store(false, std::memory_order_release);
    Status("stopped and resident");
    ReleaseSRWLockExclusive(&mutationLock);
    if (logger) {
        char message[512]{};
        std::snprintf(message, sizeof(message), "AIM_STOPPED_RESIDENT reason=%s; history mutations drained; forwarding hook/trampoline retained until process exit", reason ? reason : "stop");
        logger(message);
    }
}
const char* SilentAimStatus() noexcept { return state.load(std::memory_order_acquire); }
