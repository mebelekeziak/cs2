#include "movement.h"
#include "movement_math.h"
#include "movement_command.h"
#include "movement_offsets.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <algorithm>

namespace {
namespace off = movement_offsets;
namespace entity = cs2_offsets;
using FinalizeFn = void (*)(void*, void*);
FinalizeFn original = nullptr;
std::uintptr_t client = 0;
MovementLogCallback logger = nullptr;
std::atomic<bool> enabled{false};
std::atomic<const char*> status{"movement not initialized"};
SRWLOCK commandLock = SRWLOCK_INIT;
bool stopEverRequested = false; // Protected by commandLock.
DWORD owner = 0;
std::uintptr_t sourcePawn = 0, sourceServices = 0;
std::uint32_t sourceHandle = UINT32_MAX;
int lastSequence = -1, lastTick = -1, lastPressTick = -1;
bool jumpDown = false, wasGrounded = false, haveGround = false;
bool strafeControlled = false;
std::uint64_t previousDirections = 0;
float lastSpeed = 0;
ULONGLONG lastReport = 0;
std::uint64_t callbacks = 0, modified = 0, presses = 0, strafes = 0, rejected = 0;

void Log(const char* format, ...) noexcept {
    if (!logger) return;
    char buffer[1400]{};
    va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    logger(buffer);
}
void State(const char* next) noexcept {
    const char* old = status.exchange(next, std::memory_order_acq_rel);
    if (strcmp(old, next) != 0) Log("MOVE_STATE status=%s", next);
}
bool Copy(std::uintptr_t address, void* output, std::size_t bytes) noexcept {
    if (address < 0x10000 || address > 0x00007fffffffffffULL - bytes) return false;
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), output, bytes, &copied) && copied == bytes;
}
template<class T> bool Read(std::uintptr_t address, T& output) noexcept { return Copy(address, &output, sizeof(output)); }
bool Writable(std::uintptr_t address, std::size_t bytes) noexcept {
    if (address < 0x10000 || address > 0x00007fffffffffffULL - bytes) return false;
    const auto end = address + bytes;
    while (address < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD protection = memory.Protect & 0xff;
        if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
        address = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    }
    return true;
}
template<class T> bool Cvar(std::ptrdiff_t rva, T& value) noexcept {
    std::uintptr_t pointer = 0;
    return Read(client + rva + off::CvarValuePointer, pointer) && pointer && Read(pointer + off::CvarValue, value);
}
bool EffectiveSpeed(std::uintptr_t pawn, float& speed) noexcept {
    // Locally verified engine helper, RCX=pawn and XMM0=float result. Invoke
    // only on the engine command owner; it includes server and weapon limits.
    __try {
        speed = reinterpret_cast<float (*)(void*)>(client + off::EffectiveMaxSpeed)(reinterpret_cast<void*>(pawn));
        return std::isfinite(speed) && speed > 0 && speed < 100000;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void Reset() noexcept {
    sourcePawn = sourceServices = 0; sourceHandle = UINT32_MAX;
    lastSequence = lastTick = lastPressTick = -1;
    jumpDown = wasGrounded = haveGround = false; lastSpeed = 0;
    strafeControlled = false; previousDirections = 0;
}

struct Pawn {
    std::uintptr_t address = 0, services = 0, identity = 0, system = 0;
    std::uint32_t handle = UINT32_MAX, flags = 0;
    std::array<float, 3> velocity{};
    float friction = 0, maxSpeed = 0;
};
bool SamePawn(const Pawn& pawn) noexcept {
    std::uintptr_t current = 0, back = 0, bucket = 0, resolved = 0, services = 0, system = 0;
    std::uint32_t handle = UINT32_MAX;
    const auto index = pawn.handle & entity::entity_layout::IndexMask;
    return Read(client + entity::globals::dwLocalPlayerPawn, current) && current == pawn.address &&
        Read(client + entity::globals::dwGameEntitySystem, system) && system == pawn.system &&
        Read(pawn.address + entity::CEntityInstance::m_pEntity, back) && back == pawn.identity &&
        Read(system + entity::entity_layout::TableOffset + (index >> 9) * sizeof(std::uintptr_t), bucket) && bucket &&
        pawn.identity == bucket + (index & 511) * entity::entity_layout::IdentityStride &&
        Read(pawn.identity, resolved) && resolved == pawn.address &&
        Read(pawn.identity + entity::entity_layout::HandleOffset, handle) && handle == pawn.handle &&
        Read(pawn.address + off::pawn::MovementServices, services) && services == pawn.services;
}
bool ReadPawn(Pawn& pawn) noexcept {
    int health = 0;
    std::uint8_t life = 1, moveType = 0;
    float water = 0;
    if (!Read(client + entity::globals::dwLocalPlayerPawn, pawn.address) || !pawn.address ||
        !Read(client + entity::globals::dwGameEntitySystem, pawn.system) || !pawn.system ||
        !Read(pawn.address + entity::CEntityInstance::m_pEntity, pawn.identity) || !pawn.identity ||
        !Read(pawn.identity + entity::entity_layout::HandleOffset, pawn.handle) || pawn.handle == UINT32_MAX ||
        !Read(pawn.address + off::pawn::MovementServices, pawn.services) || !pawn.services ||
        !Read(pawn.address + entity::C_BaseEntity::m_iHealth, health) || health <= 0 || health > 1000 ||
        !Read(pawn.address + entity::C_BaseEntity::m_lifeState, life) || life != 0 ||
        !Read(pawn.address + off::pawn::ActualMoveType, moveType) || moveType != off::Walk ||
        !Read(pawn.address + off::pawn::Flags, pawn.flags) || (pawn.flags & (off::Frozen | off::WaterJump)) ||
        !Read(pawn.address + off::pawn::WaterLevel, water) || !std::isfinite(water) || water < 0 || water >= 0.5f ||
        !Read(pawn.address + off::pawn::Velocity, pawn.velocity) ||
        !Read(pawn.services + off::services::Friction, pawn.friction) ||
        !Read(pawn.services + off::services::MaxSpeed, pawn.maxSpeed)) return false;
    for (float component : pawn.velocity) if (!std::isfinite(component) || std::abs(component) > 100000) return false;
    return SamePawn(pawn);
}
bool AirSpeed(const Pawn& pawn, float& speed) noexcept {
    if (!EffectiveSpeed(pawn.address, speed)) return false;
    float stamina = 0, staminaScale = 0, surfaceFactor = 1;
    std::uintptr_t surface = 0;
    if (!Read(pawn.services + off::services::Stamina, stamina) || !std::isfinite(stamina) ||
        !Read(client + off::StaminaScaleConstant, staminaScale) || !std::isfinite(staminaScale) || staminaScale <= 0 ||
        !Read(pawn.services + off::services::SurfaceData, surface)) return false;
    // Parameter checking uses factor 1 when no surface data exists. A present
    // but unreadable surface does not receive that fallback.
    if (surface) {
        int index = -1;
        std::uintptr_t manager = 0, rows = 0;
        if (!Read(surface + 0x10, index) || index < 0 ||
            !Read(client + off::SurfaceManager, manager) || !manager ||
            !Read(manager + 0x28, rows) || !rows ||
            !Read(rows + static_cast<std::uintptr_t>(index) * 32, surfaceFactor) ||
            !std::isfinite(surfaceFactor) || surfaceFactor < 0) return false;
    }
    if (stamina > 0) {
        const float factor = std::clamp(1.0f - stamina / staminaScale, 0.0f, 1.0f);
        speed *= factor * factor;
    }
    speed *= surfaceFactor;
    return std::isfinite(speed) && speed > 0 && speed < 100000;
}

// Only borrowed messages owned by this command are changed. No protobuf arena,
// repeated-array ownership, timestamps, camera history or game entity is written.
struct Edit {
    std::uintptr_t address = 0;
    MovementSubtick value{};
};
struct Patch {
    std::uintptr_t command = 0, base = 0;
    MovementButtons buttons{};
    std::uint32_t commandPresence = 0, basePresence = 0;
    float forward = 0, left = 0;
    bool jump = false, strafe = false;
    std::array<Edit, 128> steps{};
    int size = 0;
};
bool Apply(const Patch& patch) noexcept {
    // Keep SEH in a function without C++ unwinding objects. The command callback
    // owns these allocations; preflight checks reject malformed/stale layouts.
    __try {
        if (patch.jump || patch.strafe) std::memcpy(reinterpret_cast<void*>(patch.command + off::command::Buttons), &patch.buttons, sizeof(patch.buttons));
        if (patch.strafe) {
            std::memcpy(reinterpret_cast<void*>(patch.base + off::base::Forward), &patch.forward, sizeof(float));
            std::memcpy(reinterpret_cast<void*>(patch.base + off::base::Left), &patch.left, sizeof(float));
        }
        std::memcpy(reinterpret_cast<void*>(patch.command + off::command::Presence), &patch.commandPresence, sizeof(std::uint32_t));
        std::memcpy(reinterpret_cast<void*>(patch.base + off::base::Presence), &patch.basePresence, sizeof(std::uint32_t));
        for (int i = 0; i < patch.size; ++i) {
            // Do not rewrite vtable, arena or the protobuf's cached size.
            const auto& step = patch.steps[static_cast<std::size_t>(i)];
            std::memcpy(reinterpret_cast<void*>(step.address + 0x10), &step.value.presence, sizeof(std::uint32_t));
            std::memcpy(reinterpret_cast<void*>(step.address + 0x18), &step.value.button, sizeof(MovementSubtick) - 0x18);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool PrepareSteps(Patch& patch) noexcept {
    int capacity = 0, allocated = 0;
    std::uintptr_t rep = 0;
    if (!Read(patch.base + off::base::SubtickSize, patch.size) ||
        !Read(patch.base + off::base::SubtickCapacity, capacity) ||
        !Read(patch.base + off::base::SubtickRep, rep) || patch.size < 0 ||
        patch.size > static_cast<int>(patch.steps.size()) || capacity < patch.size || capacity > 4096) return false;
    if (!patch.size) return true;
    if (!rep || !Read(rep, allocated) || allocated < patch.size || allocated > capacity) return false;
    for (int i = 0; i < patch.size; ++i) {
        auto& step = patch.steps[static_cast<std::size_t>(i)];
        if (!Read(rep + 8 + static_cast<std::uintptr_t>(i) * 8, step.address) || !step.address ||
            !Read(step.address, step.value) || !Writable(step.address + 0x10, sizeof(MovementSubtick) - 0x10) ||
            (step.value.presence & ~0x7fu) || !std::isfinite(step.value.when) || step.value.when < 0 || step.value.when > 1)
            return false;
        MovementSanitizeSubtick(step.value, patch.jump, patch.strafe);
    }
    return true;
}

void Process(void* input, void* commandPointer) noexcept {
    ++callbacks;
    if (reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) != client ||
        reinterpret_cast<std::uintptr_t>(input) != client + off::Input) {
        enabled.store(false, std::memory_order_release); Reset(); State("client/input identity changed; movement stopped"); return;
    }
    const DWORD thread = GetCurrentThreadId();
    if (owner != thread) {
        Log("MOVE_OWNER old=%lu current=%lu; discard command state", owner, thread);
        owner = thread; Reset();
    }
    DWORD foregroundPid = 0;
    const HWND window = GetForegroundWindow();
    GetWindowThreadProcessId(window, &foregroundPid);
    if (!window || foregroundPid != GetCurrentProcessId() || IsIconic(window)) {
        Reset(); State("game unfocused"); return;
    }
    // End disarms input even when rendering has stalled. Present completes its
    // own resource cleanup; mapped hooks continue forwarding original calls.
    if (GetAsyncKeyState(VK_END) & 0x8000) {
        enabled.store(false, std::memory_order_release); Reset(); State("movement stopped by End"); return;
    }
    Pawn pawn{};
    if (!ReadPawn(pawn)) { Reset(); State("awaiting live walking pawn"); return; }
    if (pawn.address != sourcePawn || pawn.services != sourceServices || pawn.handle != sourceHandle) {
        Reset(); sourcePawn = pawn.address; sourceServices = pawn.services; sourceHandle = pawn.handle;
        Log("MOVE_SOURCE pawn=%p services=%p handle=0x%x", reinterpret_cast<void*>(sourcePawn), reinterpret_cast<void*>(sourceServices), sourceHandle);
    }
    const auto settings = MovementGetSettings();
    const bool held = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (!held || (!settings.bunnyhop && !settings.autostrafe)) {
        jumpDown = false; haveGround = false; lastPressTick = -1;
        strafeControlled = false;
        State(held ? "movement disabled in settings" : "ready; hold Space"); return;
    }
    Patch patch{}; patch.command = reinterpret_cast<std::uintptr_t>(commandPointer);
    int sequence = 0, tick = 0;
    std::uintptr_t angles = 0;
    float yaw = 0;
    if (!Read(patch.command + off::command::Base, patch.base) || !patch.base ||
        !Read(patch.command + off::command::Sequence, sequence) || sequence <= 0 ||
        !Read(patch.base + off::base::Tick, tick) || tick < 0 ||
        !Read(patch.command + off::command::Presence, patch.commandPresence) ||
        !Read(patch.base + off::base::Presence, patch.basePresence) ||
        !Read(patch.command + off::command::Buttons, patch.buttons) ||
        !Read(patch.base + off::base::Forward, patch.forward) ||
        !Read(patch.base + off::base::Left, patch.left) ||
        !Writable(patch.command + off::command::Buttons, sizeof(patch.buttons)) ||
        !Writable(patch.command + off::command::Presence, sizeof(patch.commandPresence)) ||
        !Writable(patch.base + off::base::Presence, sizeof(patch.basePresence)) ||
        !Writable(patch.base + off::base::Forward, 2 * sizeof(float))) {
        ++rejected; State("command layout unavailable"); return;
    }
    if (sequence == lastSequence) return; // Never edit an already finalized/replayed command twice.
    const float desiredForward = patch.forward, desiredLeft = patch.left;
    if (sequence < lastSequence || tick < lastTick) { jumpDown = haveGround = false; lastPressTick = -1; strafeControlled = false; }
    const bool grounded = (pawn.flags & off::OnGround) != 0;
    const float speed = std::hypot(pawn.velocity[0], pawn.velocity[1]);
    if (haveGround && grounded != wasGrounded) {
        Log("MOVE_%s sequence=%d tick=%d speed=%.3f previous_speed=%.3f vz=%.3f", grounded ? "LAND" : "TAKEOFF",
            sequence, tick, speed, lastSpeed, pawn.velocity[2]);
    }
    haveGround = true; wasGrounded = grounded; lastSpeed = speed;
    float interval = 0, penalty = 0;
    bool nextJump = false, newPress = false;
    if (settings.bunnyhop && Read(client + off::TickIntervalConstant, interval) &&
        std::isfinite(interval) && interval > 0 && interval < 0.1f &&
        Cvar(off::JumpPenalty, penalty) && std::isfinite(penalty) && penalty >= 0 && penalty < 10) {
        // A press on the first grounded command, followed by airborne release.
        // If a press failed to produce takeoff, release before retrying and wait
        // beyond the engine's current cooldown. Airborne commands never spam.
        nextJump = grounded && !jumpDown && (lastPressTick < 0 ||
            static_cast<double>(tick - lastPressTick) * interval > penalty);
        newPress = nextJump;
        patch.jump = true;
        MovementEncodeJump(patch.buttons, nextJump, jumpDown);
    }
    StrafeOutput strafe{};
    AirPhysics physics{};
    physics.surfaceFriction = pawn.friction;
    if (settings.autostrafe && !grounded &&
        AirSpeed(pawn, physics.wishSpeed) &&
        Read(patch.base + off::base::Angles, angles) && angles && Read(angles + 0x1C, yaw) &&
        Read(client + off::TickIntervalConstant, physics.interval) &&
        Cvar(off::AirCap, physics.wishSpeedCap) && Cvar(off::AirAccelerate, physics.acceleration)) {
        strafe = OptimizeAirStrafe({pawn.velocity[0], pawn.velocity[1], yaw, patch.forward, patch.left,
                                  static_cast<std::uint32_t>(sequence)}, physics);
        if (strafe.valid) {
            patch.strafe = true; patch.forward = strafe.forward; patch.left = strafe.left;
            patch.basePresence |= off::base::ForwardPresent | off::base::LeftPresent;
            const auto prior = strafeControlled ? previousDirections : (patch.buttons.down ^ patch.buttons.changed);
            MovementEncodeStrafe(patch.buttons, patch.forward, patch.left, prior);
        }
    }
    if (!patch.jump && !patch.strafe) { State("movement physics unavailable"); return; }
    patch.commandPresence |= 1;
    if (!PrepareSteps(patch) || !SamePawn(pawn)) { ++rejected; State("stale pawn or unsupported subtick layout"); return; }
    if (!Apply(patch)) {
        ++rejected; enabled.store(false, std::memory_order_release); State("command write failed; movement stopped"); return;
    }
    lastSequence = sequence; lastTick = tick;
    strafeControlled = patch.strafe;
    previousDirections = patch.buttons.down & off::Directions;
    if (patch.jump) jumpDown = nextJump;
    else { jumpDown = false; lastPressTick = -1; }
    if (newPress) { lastPressTick = tick; ++presses; }
    ++modified; strafes += patch.strafe;
    State(patch.strafe ? "air strafing" : grounded ? "ground jump timing" : "airborne jump released");
    const auto now = GetTickCount64();
    if (!lastReport || now - lastReport >= 2000) {
        lastReport = now;
        std::uint8_t autoBhop = 0, precision = 0;
        const bool rules = Cvar(off::AutoBunnyhop, autoBhop) && Cvar(off::JumpPrecision, precision);
        Log("MOVE_CMD sequence=%d tick=%d ground=%u speed=%.3f velocity=%.3f,%.3f,%.3f jump_down=%u fresh_press=%u strafe=%u analog=%.5f,%.5f intent=%.5f,%.5f yaw=%.3f wish_yaw=%.3f gain=%.4f buttons=0x%llx/0x%llx/0x%llx subticks=%d callbacks=%llu modified=%llu presses=%llu strafes=%llu rejected=%llu cap=%.4f accel=%.4f maxspeed=%.4f friction=%.4f interval=%.6f penalty=%.6f server_autobhop=%u precision=%u rules_valid=%u",
            sequence, tick, grounded, speed, pawn.velocity[0], pawn.velocity[1], pawn.velocity[2], nextJump, newPress,
            patch.strafe, patch.forward, patch.left, desiredForward, desiredLeft, yaw, strafe.wishYaw, strafe.accelerationStep,
            patch.buttons.down, patch.buttons.changed, patch.buttons.third, patch.size, callbacks, modified, presses, strafes, rejected,
            physics.wishSpeedCap, physics.acceleration, physics.wishSpeed, physics.surfaceFriction, physics.interval,
            penalty, autoBhop, precision, rules);
    }
}
void HookFinalize(void* input, void* command) noexcept {
    if (enabled.load(std::memory_order_acquire)) {
        AcquireSRWLockExclusive(&commandLock);
        if (enabled.load(std::memory_order_acquire)) Process(input, command);
        ReleaseSRWLockExclusive(&commandLock);
    }
    // Original rebuilds move_crc using the modified native buttons, then normal
    // command production copies those buttons to the network protobuf itself.
    original(input, command);
}
}

bool MovementPrepare(MovementLogCallback callback) noexcept {
    logger = callback;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    std::array<unsigned char, sizeof(off::FinalizeBytes)> bytes{};
    std::array<unsigned char, sizeof(off::MaxSpeedBytes)> maxSpeedBytes{};
    std::uintptr_t vtable = 0, method = 0;
    if (!client || !Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
        dos.e_lfanew > 0x100000 || !Read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != entity::ClientTimestamp || nt.OptionalHeader.SizeOfImage != entity::ClientImageSize ||
        !Copy(client + off::Finalize, bytes.data(), bytes.size()) || memcmp(bytes.data(), off::FinalizeBytes, bytes.size()) ||
        !Copy(client + off::EffectiveMaxSpeed, maxSpeedBytes.data(), maxSpeedBytes.size()) ||
        memcmp(maxSpeedBytes.data(), off::MaxSpeedBytes, maxSpeedBytes.size()) ||
        !Read(client + off::Input, vtable) || vtable != client + off::InputVtable ||
        !Read(vtable + 6 * sizeof(std::uintptr_t), method) || method != client + off::Finalize) {
        State("installed input build verification failed"); return false;
    }
    const auto result = MH_CreateHook(reinterpret_cast<void*>(method), reinterpret_cast<void*>(&HookFinalize), reinterpret_cast<void**>(&original));
    if (result != MH_OK) { Log("MOVE_HOOK_FAILED create=%s", MH_StatusToString(result)); State("command hook creation failed"); return false; }
    const auto queue = MH_QueueEnableHook(reinterpret_cast<void*>(method));
    if (queue != MH_OK) { Log("MOVE_HOOK_FAILED queue=%s", MH_StatusToString(queue)); State("command hook queue failed"); return false; }
    Log("MOVE_BUILD_VERIFIED client=%p input=%p vtable=%p finalize=%p slot=6 timestamp=0x%lx image_size=0x%lx; match values read at command time",
        reinterpret_cast<void*>(client), reinterpret_cast<void*>(client + off::Input), reinterpret_cast<void*>(vtable),
        reinterpret_cast<void*>(method), nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage);
    State("awaiting command hook activation"); return true;
}
void MovementActivate() noexcept {
    AcquireSRWLockExclusive(&commandLock);
    if (!stopEverRequested) enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&commandLock);
}
void MovementStop(const char* reason) noexcept {
    enabled.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&commandLock);
    stopEverRequested = true;
    enabled.store(false, std::memory_order_release);
    Reset(); State("stopped and resident");
    Log("MOVE_STOPPED_RESIDENT reason=%s callbacks=%llu modified=%llu presses=%llu strafes=%llu rejected=%llu; command mutations drained, hook/trampoline retained",
        reason, callbacks, modified, presses, strafes, rejected);
    ReleaseSRWLockExclusive(&commandLock);
}
const char* MovementStatus() noexcept { return status.load(std::memory_order_acquire); }
