#include "anti_aim.h"
#include "menu_input.h"
#include "anti_aim_command.h"
#include "anti_aim_math.h"
#include "anti_aim_view.h"
#include "anti_aim_boundary.h"
#include "anti_aim_history.h"
#include "anti_aim_round.h"
#include "movement_command.h"
#include "anti_aim_settings.h"
#include "anti_aim_offsets.h"
#include "movement_offsets.h"
#include "silent_aim_offsets.h"
#include "cs2_offsets.h"
#include "movement_memory.h"
#include <MinHook.h>
#include <atomic>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
namespace off = anti_aim_offsets;
using ReplayFn = void (*)(void*, int);
ReplayFn originalReplay = nullptr;
std::uintptr_t client = 0;
SRWLOCK lock = SRWLOCK_INIT;
std::atomic<bool> active{false};
std::atomic<const char*> status{"anti aim not initialized"};
bool prepared = false, stopped = false, protectReplay = false;
AntiAimLogCallback logger = nullptr;
DWORD owner = 0;
struct Source {
    std::uintptr_t pawn = 0, identity = 0, controller = 0;
    std::uint32_t handle = UINT32_MAX;
    bool operator==(const Source&) const = default;
};
Source source{};
std::uint64_t sourceEpoch = 1;
AntiAimHistoryState publications{};
bool ingestedBasisKnown = false;
struct Cached {
    bool valid = false, angleEdit = false;
    std::uintptr_t command = 0;
    int sequence = 0;
    AntiAimCommandPatch patch{};
    AntiAimBoundaryPatch boundary{};
};
std::array<Cached, 256> cache{};
bool commandRestoreRejected = false;
struct MovementOrientation {
    std::uintptr_t input = 0, command = 0, base = 0, transition = 0;
    int sequence = 0;
    std::uint64_t epoch = 0;
    DWORD thread = 0;
    float yaw = 0;
    bool requested = false;
};
MovementOrientation finalizeScope{}, movementOrientation{};
std::uint64_t edited = 0, rejected = 0, replayRestored = 0;
std::uint64_t movementBasisEdits = 0, movementBasisFallbacks = 0;
std::uint64_t movementAntiAimEdits = 0;
std::uint64_t beginCalls = 0, finishCalls = 0, replayCalls = 0;
std::uint64_t localIngests = 0, editedIngests = 0, physicalIngests = 0, unknownIngests = 0;
std::uint64_t publishedCommands = 0;
std::uint64_t boundaryEdits = 0;
std::uint64_t previousHistoryReads = 0, previousHistoryMisses = 0;
int lastEditedSequence = 0, lastIngestSequence = 0;
AimAngles lastOutgoingView{}, lastIngestView{};
std::uint32_t lastOutgoingPresence = 0, lastIngestPresence = 0;
AntiAimCommandDiagnostics commandDiagnostics{};
ULONGLONG lastReport = 0;
template<class T> bool Read(std::uintptr_t address, T& value) noexcept {
    return MovementCopy(address, &value, sizeof(value));
}
void State(const char* value) noexcept { status.store(value, std::memory_order_release); }
void Invalidate() noexcept {
    for (auto& entry : cache) { entry.valid = false; entry.boundary.valid = false; entry.command = 0; }
    source = {}; protectReplay = false;
    finalizeScope = {}; movementOrientation = {};
    ingestedBasisKnown = false;
    if (!++sourceEpoch) ++sourceEpoch;
    AntiAimHistoryReset(publications);
}
bool Identity(void* input) noexcept {
    return client && reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) == client &&
        reinterpret_cast<std::uintptr_t>(input) == client + off::Input;
}
bool ReadSource(Source& current, bool walking) noexcept {
    namespace ent = cs2_offsets;
    std::uintptr_t resolved = 0;
    std::uint32_t linked = UINT32_MAX;
    int health = 0; std::uint8_t life = 1, type = 0;
    float water = 0; std::uint32_t flags = 0;
    if (!Read(client + ent::globals::dwLocalPlayerPawn, current.pawn) || !current.pawn ||
        !Read(current.pawn + ent::CEntityInstance::m_pEntity, current.identity) || !current.identity ||
        !Read(current.identity, resolved) || resolved != current.pawn ||
        !Read(current.identity + ent::entity_layout::HandleOffset, current.handle) || current.handle == UINT32_MAX ||
        !Read(client + ent::globals::dwLocalPlayerController, current.controller) || !current.controller ||
        !Read(current.controller + ent::CCSPlayerController::m_hPlayerPawn, linked) || linked != current.handle ||
        !Read(current.pawn + ent::C_BaseEntity::m_iHealth, health) || health <= 0 ||
        !Read(current.pawn + ent::C_BaseEntity::m_lifeState, life) || life) return false;
    return !walking || (Read(current.pawn + movement_offsets::pawn::ActualMoveType, type) && type == movement_offsets::Walk &&
        Read(current.pawn + movement_offsets::pawn::Flags, flags) && !(flags & (movement_offsets::Frozen | movement_offsets::WaterJump)) &&
        Read(current.pawn + movement_offsets::pawn::WaterLevel, water) && std::isfinite(water) && water >= 0 && water < 0.5f);
}
bool Foreground() noexcept {
    if (MenuInputCapturing()) return false;
    DWORD pid = 0; const auto window = GetForegroundWindow();
    GetWindowThreadProcessId(window, &pid);
    return window && pid == GetCurrentProcessId() && !IsIconic(window);
}
bool Action(std::uintptr_t command, bool movement = false) noexcept {
    MovementButtons buttons{};
    int attack1 = -1;
    std::uint32_t presence = 0;
    if (!Read(command + movement_offsets::command::Buttons, buttons) ||
        !Read(command + movement_offsets::command::Presence, presence) ||
        !Read(command + silent_aim_offsets::command::Attack1Index, attack1)) return true;
    // Also cover released intra-command taps, before the history builder runs.
    // Secondary attack keeps anti aim active, including short subtick taps.
    const auto actions = off::Attack | off::Use | (movement ? off::Attack2 : 0);
    if ((buttons.down | buttons.changed | buttons.third) & actions) return true;
    if ((presence & 0x20) && attack1 >= 0) return true;
    std::uintptr_t base = 0, rep = 0; int size = 0, capacity = 0, allocated = 0;
    if (!Read(command + movement_offsets::command::Base, base) || !base ||
        !Read(base + movement_offsets::base::SubtickSize, size) || size < 0 || size > 128) return true;
    if (!size) return false;
    if (!Read(base + movement_offsets::base::SubtickCapacity, capacity) || capacity < size ||
        !Read(base + movement_offsets::base::SubtickRep, rep) || !rep ||
        !Read(rep, allocated) || allocated < size || allocated > capacity) return true;
    for (int i = 0; i < size; ++i) {
        std::uintptr_t step = 0; MovementSubtick value{};
        if (!Read(rep + 8 + static_cast<std::uintptr_t>(i) * 8, step) || !step || !Read(step, value)) return true;
        if ((value.presence & 1) && (value.button & actions)) return true;
    }
    return false;
}
bool MovementScope(void* input, void* command) noexcept {
    int sequence = 0;
    std::uintptr_t base = 0;
    return finalizeScope.input == reinterpret_cast<std::uintptr_t>(input) &&
        finalizeScope.command == reinterpret_cast<std::uintptr_t>(command) &&
        finalizeScope.epoch == sourceEpoch && finalizeScope.thread == GetCurrentThreadId() &&
        Read(finalizeScope.command + movement_offsets::command::Sequence, sequence) && sequence == finalizeScope.sequence &&
        sequence > 0 && Read(finalizeScope.command + movement_offsets::command::Base, base) && base == finalizeScope.base;
}
bool MovementReady(void* input, void* command) noexcept {
    Source current{};
    return prepared && active.load(std::memory_order_acquire) && !stopped && !commandRestoreRejected &&
        MovementScope(input, command) && Identity(input) && Foreground() && !(GetAsyncKeyState(VK_END) & 0x8000) &&
        AntiAimReadRoundState(client) == AntiAimRoundState::Playable &&
        ReadSource(current, true) && current == source && !Action(reinterpret_cast<std::uintptr_t>(command), true);
}
bool CommandHistory(std::uintptr_t command, std::uintptr_t services,
    float& historicalForward, float& historicalLeft, float& physicalForward, float& physicalLeft) noexcept {
    int sequence = 0;
    if (!Read(command + movement_offsets::command::Sequence, sequence) || sequence <= 0) return false;
    AntiAimHistoryCommand previous{}, matched{};
    if (AntiAimHistoryReadPrevious(reinterpret_cast<AntiAimCommandLookup>(client + off::CommandLookup),
        reinterpret_cast<void*>(source.controller), sequence, sourceEpoch, previous)) {
        ++previousHistoryReads;
        historicalForward = previous.directions & (movement_offsets::Forward | movement_offsets::Back) ? previous.forward : 0;
        historicalLeft = previous.directions & (movement_offsets::Left | movement_offsets::Right) ? previous.left : 0;
        physicalForward = historicalForward; physicalLeft = historicalLeft;
        if (AntiAimHistoryMatch(publications, previous, matched)) {
            if (!matched.edited) return true;
            const auto physical = AntiAimCorrectWalkMovement(matched.outgoing, matched.physical, historicalForward, historicalLeft);
            if (!physical.valid) return false;
            physicalForward = physical.forward; physicalLeft = physical.left;
            return true;
        }
        // Before the first edit, unpublished older commands are physical.
        // Afterwards an unknown predecessor must never be guessed from the
        // most recently simulated command, even if its services are readable.
        if (!protectReplay) return true;
        ++previousHistoryMisses;
        return false;
    }
    if (protectReplay) { ++previousHistoryMisses; return false; }
    // Initial activation can precede a readable predecessor. No command from
    // this source has been edited yet, so native history is still physical.
    MovementButtons buttons{};
    if (!Read(services + movement_offsets::services::Buttons, buttons) ||
        !Read(services + movement_offsets::services::CommandForward, historicalForward) ||
        !Read(services + movement_offsets::services::CommandLeft, historicalLeft) ||
        !std::isfinite(historicalForward) || !std::isfinite(historicalLeft)) return false;
    if (!(buttons.down & (movement_offsets::Forward | movement_offsets::Back))) historicalForward = 0;
    if (!(buttons.down & (movement_offsets::Left | movement_offsets::Right))) historicalLeft = 0;
    physicalForward = historicalForward; physicalLeft = historicalLeft;
    return true;
}
bool BoundaryStillActive(const AntiAimBoundaryPatch& patch) noexcept {
    std::uintptr_t base = 0, rep = 0; int count = 0;
    if (!Read(patch.command + movement_offsets::command::Base, base) || base != patch.base ||
        !Read(base + movement_offsets::base::SubtickSize, count) || count < 0 || count > 128 ||
        !Read(base + movement_offsets::base::SubtickRep, rep)) return true;
    for (int i = 0; i < count; ++i) {
        std::uintptr_t address = 0;
        if (!Read(rep + 8 + static_cast<std::uintptr_t>(i) * 8, address)) return true;
        if (address != patch.generated) continue;
        MovementSubtick value{};
        if (!Read(address, value)) return true;
        const auto& expected = patch.editedGenerated;
        return value.vtable == expected.vtable && value.arena == expected.arena && value.presence == expected.presence &&
            value.button == expected.button && value.pressed == expected.pressed &&
            !std::memcmp(&value.when, &expected.when, sizeof(float) * 5);
    }
    return false;
}
struct ReplaySource { void* input; Source source; };
bool ReplaySourceValid(void* context) noexcept {
    const auto& before = *static_cast<ReplaySource*>(context);
    Source after{};
    return Identity(before.input) && ReadSource(after, false) && after == before.source;
}
void HookReplay(void* input, int slot) noexcept {
    AcquireSRWLockExclusive(&lock);
    ++replayCalls;
    Source current{};
    const auto address = reinterpret_cast<std::uintptr_t>(input) + off::InputAngles;
    const bool preserve = slot == 0 && protectReplay && Identity(input) &&
        ReadSource(current, false) && current == source;
    // Native replay resets both the previous-down mask and the camera from
    // the outgoing command. Rotated directions must not become physical input
    // edges on the next rebuild. Advance previous directions from physical
    // current input, rather than freezing the old previous-down mask;
    // native action-button state, edge resets and timestamps still advance.
    ReplaySource before{input, current};
    if (AntiAimReplayPreservingInput(address, reinterpret_cast<std::uintptr_t>(input) + off::InputCurrentButtons,
        reinterpret_cast<std::uintptr_t>(input) + off::InputPreviousButtons,
        preserve, originalReplay, input, slot, &ReplaySourceValid, &before)) ++replayRestored;
    ReleaseSRWLockExclusive(&lock);
}
}

bool AntiAimPrepare(AntiAimLogCallback callback) noexcept {
    logger = callback;
    client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    std::uintptr_t vtable = 0, method = 0;
    std::array<unsigned char, sizeof(off::ReplayBytes)> bytes{};
    std::array<unsigned char, sizeof(off::CommandLookupBytes)> lookupBytes{};
    if (!client || !Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !Read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != cs2_offsets::ClientTimestamp || nt.OptionalHeader.SizeOfImage != cs2_offsets::ClientImageSize ||
        !Read(client + off::Input, vtable) || vtable != client + off::InputVtable ||
        !Read(vtable + off::ReplaySlot * sizeof(std::uintptr_t), method) || method != client + off::Replay ||
        !MovementCopy(method, bytes.data(), bytes.size()) || std::memcmp(bytes.data(), off::ReplayBytes, bytes.size()) ||
        !MovementCopy(client + off::CommandLookup, lookupBytes.data(), lookupBytes.size()) ||
        std::memcmp(lookupBytes.data(), off::CommandLookupBytes, lookupBytes.size())) {
        State("installed anti aim build verification failed"); return false;
    }
    if (MH_CreateHook(reinterpret_cast<void*>(method), reinterpret_cast<void*>(&HookReplay),
        reinterpret_cast<void**>(&originalReplay)) != MH_OK || !originalReplay ||
        MH_QueueEnableHook(reinterpret_cast<void*>(method)) != MH_OK) {
        State("anti aim replay hook preparation failed"); return false;
    }
    prepared = true; State("awaiting anti aim activation");
    if (logger) logger("ANTI_AIM_BUILD_VERIFIED revision=8 sequence_previous_history; quantized movement composes with configured anti aim; input replay slot=8; physical view/direction input retained");
    return true;
}
void AntiAimActivate(bool commandHookReady) noexcept {
    AcquireSRWLockExclusive(&lock);
    if (prepared && commandHookReady && !stopped) { active.store(true, std::memory_order_release); State("disabled"); }
    else State("anti aim command/replay hooks unavailable");
    ReleaseSRWLockExclusive(&lock);
}
bool AntiAimBeginCommand(void* input, void* command) noexcept {
    AcquireSRWLockExclusive(&lock);
    ++beginCalls;
    finalizeScope = {}; movementOrientation = {};
    commandRestoreRejected = false;
    if (!Identity(input)) {
        active.store(false, std::memory_order_release); Invalidate(); State("client/input changed; anti aim stopped");
    } else {
        const DWORD thread = GetCurrentThreadId();
        Source current{};
        if (!ReadSource(current, false) || !(current == source)) {
            Invalidate(); owner = thread; source = current;
        } else if (owner != thread) {
            // Discard callback-owned command snapshots on owner migration.
            // Source-validated copied ingestion basis and the requirement to
            // protect retained replay views still belong to this same pawn.
            for (auto& entry : cache) { entry.valid = false; entry.boundary.valid = false; entry.command = 0; }
            owner = thread;
        }
        int sequence = 0;
        if (Read(reinterpret_cast<std::uintptr_t>(command) + movement_offsets::command::Sequence, sequence) && sequence > 0) {
            auto& entry = cache[static_cast<unsigned>(sequence) % cache.size()];
            if (entry.command == reinterpret_cast<std::uintptr_t>(command) && entry.sequence == sequence) {
                if (entry.boundary.valid && !AntiAimRestoreBoundary(entry.boundary) && BoundaryStillActive(entry.boundary)) {
                    // A rebuilt command may have reused its storage. Never
                    // append another delta over an unrecognized old event.
                    commandRestoreRejected = true;
                }
                // If native code rebuilt the payload, the exact snapshot check
                // refuses the restore and the newly built input is used.
                if (entry.valid && !commandRestoreRejected && !AntiAimRestoreCommand(entry.patch)) {
                    std::uintptr_t base = 0, angles = 0; AimAngles value{};
                    // A fresh physical payload can proceed. A partially
                    // rebuilt outgoing payload must never rotate twice.
                    if (entry.angleEdit && Read(entry.command + movement_offsets::command::Base, base) && base == entry.patch.base &&
                        Read(base + movement_offsets::base::Angles, angles) && angles == entry.patch.angles &&
                        Read(angles + silent_aim_offsets::angle::Pitch, value) &&
                        !std::memcmp(&value, &entry.patch.editedView, sizeof(value))) commandRestoreRejected = true;
                }
            }
            if (!commandRestoreRejected) { entry.valid = false; entry.boundary.valid = false; entry.command = 0; }
        }
    }
    const bool ready = !commandRestoreRejected;
    int sequence = 0;
    std::uintptr_t base = 0;
    if (ready && Identity(input) && Read(reinterpret_cast<std::uintptr_t>(command) + movement_offsets::command::Sequence, sequence) &&
        sequence > 0 && Read(reinterpret_cast<std::uintptr_t>(command) + movement_offsets::command::Base, base) && base) {
        finalizeScope.input = reinterpret_cast<std::uintptr_t>(input);
        finalizeScope.command = reinterpret_cast<std::uintptr_t>(command); finalizeScope.base = base;
        finalizeScope.sequence = sequence; finalizeScope.epoch = sourceEpoch; finalizeScope.thread = GetCurrentThreadId();
    }
    ReleaseSRWLockExclusive(&lock);
    return ready;
}
void AntiAimFinishCommand(void* input, void* command) noexcept {
    if (!prepared) return;
    AcquireSRWLockExclusive(&lock);
    ++finishCalls;
    const auto requested = movementOrientation;
    int requestSequence = 0;
    const bool movementRequested = requested.requested &&
        requested.input == reinterpret_cast<std::uintptr_t>(input) && requested.command == reinterpret_cast<std::uintptr_t>(command) &&
        Read(requested.command + movement_offsets::command::Sequence, requestSequence) && requestSequence == requested.sequence &&
        requested.input == finalizeScope.input && requested.command == finalizeScope.command &&
        requested.sequence == finalizeScope.sequence && requested.base == finalizeScope.base &&
        requested.epoch == sourceEpoch && requested.thread == GetCurrentThreadId();
    movementOrientation = {}; // A request is consumed even when preparation fails.
    bool publishedEdit = false;
    AimAngles physicalView{};
    if (Identity(input) && !commandRestoreRejected) {
        if (Foreground() && (GetAsyncKeyState(VK_END) & 0x8000)) { stopped = true; active.store(false, std::memory_order_release); }
        const auto options = AntiAimGetSettings();
        const bool movementEnabled = movementRequested && MovementReady(input, command);
        const bool enabled = active.load(std::memory_order_acquire) && options.enabled && Foreground();
        const auto round = (enabled || movementEnabled) ? AntiAimReadRoundState(client) : AntiAimRoundState::Unavailable;
        const bool roundReady = round == AntiAimRoundState::Playable;
        const char* roundReason = round == AntiAimRoundState::FreezePeriod ?
            "round freeze period; awaiting round start" : "round state unavailable";
        // Retained outgoing commands still need physical movement handoff and
        // replay protection while new angle edits wait for the round to start.
        if (movementRequested || ((enabled || movementEnabled) && roundReady) || protectReplay) {
            Source current{};
            const auto address = reinterpret_cast<std::uintptr_t>(command);
            int sequence = 0;
            if (!ReadSource(current, false) || !(current == source)) State("awaiting live pawn");
            else if (!Read(address + movement_offsets::command::Sequence, sequence) || sequence <= 0) State("invalid command sequence");
            else {
                auto& entry = cache[static_cast<unsigned>(sequence) % cache.size()];
                entry.command = address; entry.sequence = sequence; entry.valid = false; entry.angleEdit = false; entry.boundary.valid = false;
                const char* reason = nullptr;
                std::uintptr_t services = 0;
                float historicalForward = 0, historicalLeft = 0;
                float physicalForward = 0, physicalLeft = 0;
                if (!Read(current.pawn + movement_offsets::pawn::MovementServices, services) || !services ||
                    !CommandHistory(address, services, historicalForward, historicalLeft, physicalForward, physicalLeft)) {
                    ++rejected; State("previous command movement unavailable");
                } else {
                    Source walking{};
                    // A requested quantized command already contains the safe
                    // digital fallback. If its upgrade is unavailable, ordinary
                    // arbitrary anti-aim rotation must not spoil that fallback.
                    const bool transform = movementRequested ? movementEnabled :
                        enabled && roundReady && ReadSource(walking, true) && walking == current && !Action(address);
                    const AntiAimMovementHistory nativeHistory{historicalForward, historicalLeft};
                    const bool combinedMovement = movementRequested && enabled && roundReady;
                    if (transform) entry.valid = movementRequested ?
                        AntiAimApplyMovementCommand(address, requested.yaw, requested.transition, entry.patch, &reason, &commandDiagnostics, &nativeHistory,
                            combinedMovement ? &options : nullptr) :
                        AntiAimApplyCommand(address, entry.patch, options, &reason, &commandDiagnostics);
                    const bool angleApplied = entry.valid;
                    entry.angleEdit = angleApplied;
                    if (movementRequested && !entry.valid) {
                        // The digital fallback also starts from native N-1,
                        // which may still have another outgoing angle basis.
                        // Coalesce its handoff in the existing owned event;
                        // never depend on allocating another boundary event.
                        entry.valid = AntiAimApplyMovementHandoff(address, requested.transition, nativeHistory, entry.patch, &reason);
                    }
                    float targetForward = physicalForward, targetLeft = physicalLeft;
                    if (entry.valid && !movementRequested) {
                        const auto outgoing = AntiAimCorrectWalkMovement(entry.patch.originalView, entry.patch.editedView, physicalForward, physicalLeft);
                        targetForward = outgoing.forward; targetLeft = outgoing.left;
                    }
                    // Native simulation starts this sequence from sequence-1,
                    // even when the latest ingestion was this same command.
                    // Serialize that predecessor's change of basis at time 0.
                    const float deltaForward = targetForward - historicalForward, deltaLeft = targetLeft - historicalLeft;
                    if (!movementRequested && (deltaForward != 0 || deltaLeft != 0) && !AntiAimAppendBoundary(address, deltaForward, deltaLeft,
                        client + movement_offsets::AllocateSubtick, client + movement_offsets::AddAllocatedSubtick, entry.boundary, &reason)) {
                        if (entry.valid) {
                            AntiAimRebindCommandContainer(entry.boundary, entry.patch);
                            AntiAimRestoreCommand(entry.patch);
                        }
                        entry.valid = false; ++rejected; State(reason ? reason : "movement handoff unavailable");
                    } else if (entry.valid && angleApplied) {
                        if (entry.boundary.valid) {
                            // Native append can grow/reallocate the pointer
                            // representation. Removal retains that arena pool.
                            AntiAimRebindCommandContainer(entry.boundary, entry.patch);
                        }
                        ++edited; protectReplay = true;
                        if (movementRequested) ++movementBasisEdits;
                        if (combinedMovement) ++movementAntiAimEdits;
                        publishedEdit = true; physicalView = entry.patch.originalView;
                        lastEditedSequence = sequence; lastOutgoingView = entry.patch.editedView;
                        lastOutgoingPresence = entry.patch.editedAnglePresence;
                        State(combinedMovement ? "anti aim + quantized steering; local view preserved" :
                            movementRequested ? "quantized steering basis; local view preserved" : "configured angles; local view preserved");
                    } else if (movementRequested && entry.valid) {
                        State("quantized digital fallback; physical view retained");
                    } else if (transform || movementRequested) { ++rejected; State(reason ? reason : "unsupported command; unchanged"); }
                    else State(stopped ? "stopped; physical movement handoff" : !enabled ? "disabled; physical movement handoff" :
                        !roundReady ? roundReason : "attack/movement passthrough");
                    if (entry.boundary.valid && entry.boundary.appended) ++boundaryEdits;
                }
            }
        } else State(stopped ? "stopped and resident" : enabled ? roundReason : "disabled");
        if (movementRequested && !publishedEdit) ++movementBasisFallbacks;
        // Prediction can deep-copy this command and later replay an older
        // rebuild. Publish copied values separately from restoration pointers.
        Source current{};
        AntiAimHistoryCommand published{};
        if (ReadSource(current, false) && current == source &&
            AntiAimHistoryReadCommand(reinterpret_cast<std::uintptr_t>(command), sourceEpoch, published)) {
            published.edited = publishedEdit;
            if (publishedEdit) published.physical = physicalView;
            if (AntiAimHistoryPublish(publications, published)) ++publishedCommands;
        }
    }
    finalizeScope = {};
    ReleaseSRWLockExclusive(&lock);
}
void AntiAimStop(const char* reason) noexcept {
    active.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&lock);
    stopped = true; active.store(false, std::memory_order_release); State("stopped and resident");
    finalizeScope = {}; movementOrientation = {};
    // Retained edited commands may still replay after stopping. Keep their
    // physical-view compensation resident until source change/process exit.
    ReleaseSRWLockExclusive(&lock);
    if (logger) {
        char message[512]{};
        std::snprintf(message, sizeof(message), "ANTI_AIM_STOPPED_RESIDENT reason=%s; new angle edits drained; movement handoff and retained-command camera compensation remain resident", reason ? reason : "stop");
        logger(message);
    }
}
const char* AntiAimStatus() noexcept { return status.load(std::memory_order_acquire); }
void AntiAimFlushTelemetry() noexcept {
    if (!logger) return;
    const auto now = GetTickCount64();
    if (lastReport && now - lastReport < 2000) return;
    lastReport = now;
    char message[1664]{};
    AcquireSRWLockShared(&lock);
    std::snprintf(message, sizeof(message),
        "ANTI_AIM revision=8 begin=%llu finish=%llu replay_calls=%llu active=%u enabled=%u edited=%llu rejected=%llu replay_input_restores=%llu state=\"%s\" command_reason=%s command_presence=0x%x base_presence=0x%x angle_presence=0x%x subticks=%d/%d allocated=%d last_edit_sequence=%d outgoing=%.3f,%.3f,%.3f outgoing_presence=0x%x publications=%llu local_ingests=%llu edited_ingests=%llu physical_ingests=%llu unknown_ingests=%llu history_known=%u boundary_edits=%llu previous_history_reads=%llu previous_history_misses=%llu last_ingest_sequence=%d ingested=%.3f,%.3f,%.3f ingested_presence=0x%x movement_basis_edits=%llu movement_basis_fallbacks=%llu movement_anti_aim_edits=%llu; mutation/ingestion evidence is not observed model facing or server receipt",
        beginCalls, finishCalls, replayCalls, active.load(std::memory_order_acquire), AntiAimGetSettings().enabled,
        edited, rejected, replayRestored, AntiAimStatus(), commandDiagnostics.reason,
        commandDiagnostics.commandPresence, commandDiagnostics.basePresence, commandDiagnostics.anglePresence,
        commandDiagnostics.count, commandDiagnostics.capacity, commandDiagnostics.allocated,
        lastEditedSequence, lastOutgoingView.pitch, lastOutgoingView.yaw, lastOutgoingView.roll, lastOutgoingPresence,
        publishedCommands, localIngests, editedIngests, physicalIngests, unknownIngests, ingestedBasisKnown, boundaryEdits,
        previousHistoryReads, previousHistoryMisses, lastIngestSequence,
        lastIngestView.pitch, lastIngestView.yaw, lastIngestView.roll, lastIngestPresence, movementBasisEdits, movementBasisFallbacks, movementAntiAimEdits);
    ReleaseSRWLockShared(&lock);
    logger(message);
}
void AntiAimObserveIngest(void* services, void* command) noexcept {
    AcquireSRWLockExclusive(&lock);
    Source current{};
    std::uintptr_t localServices = 0;
    if (!ReadSource(current, false) || !(current == source) ||
        !Read(current.pawn + movement_offsets::pawn::MovementServices, localServices) ||
        localServices != reinterpret_cast<std::uintptr_t>(services)) {
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    ingestedBasisKnown = false;
    ++localIngests;
    AntiAimHistoryCommand observed{}, matched{};
    if (AntiAimHistoryReadCommand(reinterpret_cast<std::uintptr_t>(command), sourceEpoch, observed)) {
        lastIngestSequence = observed.sequence; lastIngestView = observed.outgoing; lastIngestPresence = observed.anglePresence;
        if (AntiAimHistoryMatch(publications, observed, matched)) {
            ingestedBasisKnown = true;
            if (matched.edited) ++editedIngests;
            else ++physicalIngests;
        }
    }
    if (!ingestedBasisKnown) ++unknownIngests;
    ReleaseSRWLockExclusive(&lock);
}
bool AntiAimPhysicalHistory(std::uintptr_t services, std::uintptr_t command, float& forward, float& left) noexcept {
    AcquireSRWLockExclusive(&lock);
    bool valid = true;
    if (protectReplay) {
        float historicalForward = 0, historicalLeft = 0, physicalForward = 0, physicalLeft = 0;
        valid = CommandHistory(command, services, historicalForward, historicalLeft, physicalForward, physicalLeft);
        if (valid) { forward = physicalForward; left = physicalLeft; }
    }
    ReleaseSRWLockExclusive(&lock);
    return valid;
}
bool AntiAimCanOrientMovement(void* input, void* command) noexcept {
    AcquireSRWLockExclusive(&lock);
    const bool ready = MovementReady(input, command);
    ReleaseSRWLockExclusive(&lock);
    return ready;
}
bool AntiAimRequestMovementYaw(void* input, void* command, float worldYaw, std::uintptr_t analogTransition) noexcept {
    AcquireSRWLockExclusive(&lock);
    bool accepted = false;
    if (MovementScope(input, command)) {
        movementOrientation = finalizeScope;
        movementOrientation.requested = true;
        movementOrientation.yaw = worldYaw; movementOrientation.transition = analogTransition;
        // Record the marker even on rejection. Finish must preserve this
        // command's digital fallback when actions or lifecycle checks reject.
        accepted = std::isfinite(worldYaw) && analogTransition && MovementReady(input, command);
    }
    ReleaseSRWLockExclusive(&lock);
    return accepted;
}
