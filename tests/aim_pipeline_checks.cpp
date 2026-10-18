#include "silent_aim.h"
#include "silent_aim_command.h"
#include "silent_aim_settings.h"
#include "silent_aim_target.h"
#include "silent_aim_offsets.h"
#include "aim_autofire_offsets.h"
#include "aim_weapon.h"
#include "shot_logging.h"
#include "shot_log_settings.h"
#include "movement.h"
#include "movement_offsets.h"
#include "movement_telemetry.h"
#include "cs2_offsets.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
namespace cmd = silent_aim_offsets;
namespace fire = aim_autofire_offsets;
namespace move = movement_offsets;
unsigned failures = 0, checks = 0, recorderCalls = 0, builderCalls = 0, checksumCalls = 0;
bool selectAvailable = true, targetCurrent = true, volumeAvailable = true, volumeCurrent = true;
bool weaponAvailable = true, weaponCurrent = true, checksumAvailable = true, badHistory = false;
bool recorderSawNativePress = false, builderSawPrimary = false, checksumSawCancelledPrimary = false;
std::vector<std::string> telemetry;
std::vector<ShotLogMetadata> shotMetadata;
unsigned shotPumps = 0;
AimWeaponSnapshot weapon{};
constexpr std::uint64_t UnrelatedDown = 0x1000, UnrelatedChanged = 0x2000, UnrelatedThird = 0x4000;
constexpr std::uint32_t UnrelatedPresence = 0x400000;

template<class T> void Put(std::uintptr_t address, const T& value) noexcept {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}
template<class T> T Get(std::uintptr_t address) noexcept {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}
template<class T> std::uintptr_t Address(T& value) noexcept { return reinterpret_cast<std::uintptr_t>(&value); }
void Expect(bool result, const std::string& label) {
    ++checks;
    std::printf("%s %s\n", result ? "PASS" : "FAIL", label.c_str());
    failures += !result;
}
bool Equal(const AimAngles& left, const AimAngles& right) noexcept {
    return std::abs(left.pitch - right.pitch) < 0.0001f && std::abs(left.yaw - right.yaw) < 0.0001f &&
        std::abs(left.roll - right.roll) < 0.0001f;
}
bool Equal(const MovementButtons& left, const MovementButtons& right) noexcept {
    return left.down == right.down && left.changed == right.changed && left.third == right.third;
}

struct Command {
    std::array<unsigned char, 0x90> command{}, base{};
    std::array<unsigned char, 0x38> baseAngles{}, entry{}, historyAngles{};
    std::array<unsigned char, 0x18> repeated{};
    MovementButtons originalButtons{0x8, 0x200, 0x400};
    static constexpr std::uint32_t OriginalPresence = 0x8000;
    static constexpr AimAngles PhysicalView{};
    Command() {
        Put(Address(command) + move::command::Sequence, 37);
        Put(Address(command) + move::command::Base, Address(base));
        Put(Address(command) + move::command::Buttons, originalButtons);
        Put(Address(command) + cmd::command::Presence, OriginalPresence);
        Put(Address(command) + cmd::command::Attack1Index, -1);
        Put(Address(command) + cmd::command::HistorySize, 1);
        Put(Address(command) + cmd::command::HistoryCapacity, 1);
        Put(Address(command) + cmd::command::HistoryRep, Address(repeated));
        Put(Address(base) + move::base::Angles, Address(baseAngles));
        Put(Address(baseAngles) + cmd::angle::Presence, cmd::angle::AllPresent);
        Put(Address(baseAngles) + cmd::angle::Pitch, PhysicalView);
        Put(Address(repeated) + cmd::repeated::Allocated, 1);
        Put(Address(repeated) + cmd::repeated::Entries, Address(entry));
        Put(Address(entry) + cmd::history::Presence, std::uint32_t{0x8001});
        Put(Address(entry) + cmd::history::Angles, Address(historyAngles));
        Put(Address(historyAngles) + cmd::angle::Presence, std::uint32_t{0x8007});
        Put(Address(historyAngles) + cmd::angle::Pitch, PhysicalView);
    }
    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;
    void* Pointer() noexcept { return command.data(); }
    MovementButtons Buttons() noexcept { return Get<MovementButtons>(Address(command) + move::command::Buttons); }
    int Index() noexcept { return Get<int>(Address(command) + cmd::command::Attack1Index); }
    std::uint32_t Presence() noexcept { return Get<std::uint32_t>(Address(command) + cmd::command::Presence); }
    AimAngles History() noexcept { return Get<AimAngles>(Address(historyAngles) + cmd::angle::Pitch); }
    void AddUnrelatedChanges() noexcept {
        auto buttons = Buttons();
        buttons.down |= UnrelatedDown; buttons.changed |= UnrelatedChanged; buttons.third |= UnrelatedThird;
        Put(Address(command) + move::command::Buttons, buttons);
        Put(Address(command) + cmd::command::Presence, Presence() | UnrelatedPresence);
    }
    MovementButtons ExpectedAfterChanges() const noexcept {
        return {originalButtons.down | UnrelatedDown, originalButtons.changed | UnrelatedChanged,
            originalButtons.third | UnrelatedThird};
    }
};

void NativeRecorder(void* input, int slot) {
    ++recorderCalls;
    const auto address = reinterpret_cast<std::uintptr_t>(input);
    const int events = Get<int>(address + fire::InputEventCount);
    const auto event = address + fire::InputEvents + (events - 1) * fire::InputEventStride;
    recorderSawNativePress = slot == 0 && events == 2 &&
        Get<std::uint64_t>(event + fire::EventButton) == fire::PrimaryAttack &&
        Get<std::uint8_t>(event + fire::EventPressed) == 1;
    Put(address + fire::InputPrimaryIndex, 0);
    Put(address + fire::InputHistoryCount, 1);
    // Exercise ownership restoration even when the native event scanner updates
    // its processed and held fields while recording our temporary press.
    Put(address + fire::InputEventsProcessed, events);
    Put(address + fire::InputPrimaryHeld, std::uint8_t{1});
}

void NativeBuilder(void* input, int slot, void* command) {
    ++builderCalls;
    const auto address = reinterpret_cast<std::uintptr_t>(command);
    const auto inputAddress = reinterpret_cast<std::uintptr_t>(input);
    const auto buttons = Get<MovementButtons>(address + move::command::Buttons);
    builderSawPrimary = slot == 0 && (buttons.down & 1) && (buttons.changed & 1) && !(buttons.third & 1);
    const int index = Get<int>(inputAddress + fire::InputPrimaryIndex);
    auto presence = Get<std::uint32_t>(address + cmd::command::Presence);
    presence = index >= 0 ? presence | cmd::command::Attack1Present : presence & ~cmd::command::Attack1Present;
    Put(address + cmd::command::Presence, presence);
    Put(address + cmd::command::Attack1Index, index);
    const auto rep = Get<std::uintptr_t>(address + cmd::command::HistoryRep);
    const auto entry = Get<std::uintptr_t>(rep + cmd::repeated::Entries);
    const auto angles = Get<std::uintptr_t>(entry + cmd::history::Angles);
    const auto base = Get<std::uintptr_t>(address + move::command::Base);
    const auto baseAngles = Get<std::uintptr_t>(base + move::base::Angles);
    Put(angles + cmd::angle::Pitch, baseAngles ? Get<AimAngles>(baseAngles + cmd::angle::Pitch) : AimAngles{});
    Put(angles + cmd::angle::Presence, badHistory ? std::uint32_t{0} : std::uint32_t{0x8007});
}

struct World {
    std::vector<unsigned char> client = std::vector<unsigned char>(cs2_offsets::ClientImageSize);
    std::uintptr_t Base() noexcept { return reinterpret_cast<std::uintptr_t>(client.data()); }
    std::uintptr_t InputAddress() noexcept { return Base() + cmd::Input; }
    void* Input() noexcept { return reinterpret_cast<void*>(InputAddress()); }
    std::array<unsigned char, 0xC00> InputBytes() noexcept {
        std::array<unsigned char, 0xC00> value{};
        std::memcpy(value.data(), Input(), value.size());
        return value;
    }
    void Reset() {
        std::memset(Input(), 0, 0xC00);
        Put(Base() + cs2_offsets::globals::dwEntityList, std::uintptr_t{0x111000});
        Put(Base() + cs2_offsets::globals::dwLocalPlayerController, std::uintptr_t{0x222000});
        Put(Base() + cs2_offsets::globals::dwLocalPlayerPawn, std::uintptr_t{0x333000});
        Put(InputAddress() + fire::InputPrimaryIndex, -1);
        Put(InputAddress() + fire::InputEventCount, 1);
        Put(InputAddress() + fire::InputEventsProcessed, 1);
        Put(InputAddress() + fire::InputEvents + fire::EventButton, std::uint64_t{2});
        Put(InputAddress() + fire::InputEvents + fire::EventPressed, std::uint8_t{1});
        std::memset(reinterpret_cast<void*>(InputAddress() + fire::InputEvents + fire::InputEventStride), 0xCD,
            fire::InputEventStride);
        recorderCalls = builderCalls = checksumCalls = 0;
        recorderSawNativePress = builderSawPrimary = checksumSawCancelledPrimary = false;
        selectAvailable = targetCurrent = volumeAvailable = volumeCurrent = true;
        weaponAvailable = weaponCurrent = checksumAvailable = true;
        badHistory = false;
        telemetry.clear();
        shotMetadata.clear(); shotPumps = 0;
        ShotLogSetSettings({});
        weapon = {};
        weapon.weapon = 0x444000; weapon.identity = 0x445000; weapon.services = 0x446000;
        weapon.data = 0x447000; weapon.handle = 0x8008;
        weapon.range = 1000; weapon.clip = 30; weapon.ready = true;
        weapon.name = "fixture rifle"; weapon.reason = "ready";
        SilentAimSettings settings{};
        settings.enabled = settings.autofire = true;
        settings.fov = 30; settings.hitchance = 100; settings.samples = 64;
        SilentAimSetSettings(settings);
        SilentAimTestConfigure(Base(), NativeBuilder, NativeRecorder);
    }
    void Prepare(Command& command) { SilentAimPrepareCommand(Input(), command.Pointer()); }
    void History(Command& command) { SilentAimTestHistory(Input(), command.Pointer()); }
};

AimAngles Desired() noexcept { return SilentAimSolve({}, {100, 10, 0}, {}, 30).angles; }

void GateChecks(World& world) {
    world.Reset();
    Command below;
    weapon.range = 50; // Target is more than 100 units away: exactly zero hits.
    const auto inputBefore = world.InputBytes();
    const auto commandBefore = below.command;
    world.Prepare(below);
    Expect(recorderCalls == 0 && below.command == commandBefore && world.InputBytes() == inputBefore,
        "below-threshold estimate leaves native attack/input state untouched");
    Expect(!telemetry.empty() && telemetry[0].find("estimated=0.00%") != std::string::npos &&
        telemetry[0].find("below hitchance") != std::string::npos,
        "bounded hook telemetry includes the real zero-percent estimate and rejection reason");
    world.History(below);
    Expect(Equal(below.History(), Command::PhysicalView) && checksumCalls == 0,
        "rejected automatic candidate does not acquire manual-fire history or regenerate checksum");
    Expect(shotMetadata.empty() && shotPumps == 1, "rejected fire has no shot context; native logger still receives its owner pump");

    world.Reset();
    Command accepted;
    const auto baseBefore = accepted.base;
    const auto baseAnglesBefore = accepted.baseAngles;
    auto expectedInput = world.InputBytes();
    world.Prepare(accepted);
    Put(Address(expectedInput) + fire::InputPrimaryIndex, 0);
    Put(Address(expectedInput) + fire::InputHistoryCount, 1);
    Expect(recorderCalls == 1 && recorderSawNativePress && world.InputBytes() == expectedInput,
        "threshold equality records one native press then restores temporary event/count/scan/held state");
    Expect((accepted.Buttons().down & 1) && (accepted.Buttons().changed & 1) && !(accepted.Buttons().third & 1) &&
        accepted.Index() == -1 && !(accepted.Presence() & cmd::command::Attack1Present),
        "automatic primary buttons exist before the builder; native builder retains index ownership");
    world.History(accepted);
    Expect(shotMetadata.size() == 1 && shotMetadata[0].automatic && shotMetadata[0].chanceValid &&
        shotMetadata[0].commandSequence == 37 && shotMetadata[0].hitchance == 100 &&
        shotMetadata[0].source.pawn == 0x8003 && shotMetadata[0].targetPawn == 0x8004,
        "successful automatic history publishes copied aim context without claiming an actual shot");
    Put(Address(expectedInput) + fire::InputPrimaryIndex, -1);
    Expect(builderCalls == 1 && builderSawPrimary && accepted.Index() == 0 &&
        (accepted.Presence() & cmd::command::Attack1Present) && Equal(accepted.History(), Desired()),
        "builder sees the requested primary and its real history is aimed before publication");
    Expect(world.InputBytes() == expectedInput && accepted.base == baseBefore && accepted.baseAngles == baseAnglesBefore &&
        checksumCalls == 0, "successful publication restores input index, preserves camera/base and needs no cancellation checksum");
    Expect(!telemetry.empty() && telemetry[0].find("estimated=100.00%") != std::string::npos &&
        telemetry[0].find("automatic primary requested") != std::string::npos,
        "eligible estimate is queued with its native-fire decision");

    world.Reset();
    Command disabled;
    auto settings = SilentAimGetSettings(); settings.autofire = false; SilentAimSetSettings(settings);
    world.Prepare(disabled);
    Expect(recorderCalls == 0 && Equal(disabled.Buttons(), disabled.originalButtons),
        "autofire toggle off permits estimates without synthesizing primary");

    world.Reset();
    Command manual;
    weapon.range = 50;
    auto manualButtons = manual.originalButtons; manualButtons.down |= 1;
    Put(Address(manual.command) + move::command::Buttons, manualButtons);
    world.Prepare(manual); world.History(manual);
    Expect(recorderCalls == 0 && Equal(manual.Buttons(), manualButtons) && Equal(manual.History(), Desired()),
        "physical/manual command primary remains available to existing silent aim below the automatic threshold");
    Expect(shotMetadata.size() == 1 && !shotMetadata[0].automatic && shotMetadata[0].chanceValid &&
        shotMetadata[0].hitchance == 0, "manual aimed history captures its actual zero-percent estimate");

    world.Reset();
    Command manualStale;
    volumeCurrent = weaponCurrent = false;
    auto staleButtons = manualStale.originalButtons; staleButtons.down |= 1;
    Put(Address(manualStale.command) + move::command::Buttons, staleButtons);
    world.Prepare(manualStale); world.History(manualStale);
    Expect(Equal(manualStale.History(), Desired()) && shotMetadata.size() == 1 &&
        !shotMetadata[0].chanceValid && !shotMetadata[0].capsuleValid && !*shotMetadata[0].weapon,
        "manual fire remains available when logging revalidation marks stale weapon/geometry metadata unknown");

    world.Reset();
    Command loggingChanged;
    world.Prepare(loggingChanged);
    ShotLogSetSettings({false, true});
    world.History(loggingChanged);
    Expect(Equal(loggingChanged.History(), Desired()) && loggingChanged.Index() == 0 && checksumCalls == 0 && shotMetadata.empty(),
        "logging preferences can change between request and publication without cancelling working autofire");

    for (int unavailable = 0; unavailable < 4; ++unavailable) {
        world.Reset();
        Command missing;
        if (unavailable == 0) selectAvailable = false;
        if (unavailable == 1) volumeAvailable = false;
        if (unavailable == 2) weaponAvailable = false;
        if (unavailable == 3) { weapon.ready = false; weapon.reason = "cooldown"; }
        world.Prepare(missing);
        Expect(recorderCalls == 0 && Equal(missing.Buttons(), missing.originalButtons),
            "missing target/geometry/weapon or cooldown rejects automatic request case " + std::to_string(unavailable));
    }
}

void SparseViewChecks(World& world) {
    // Native base angles omit zero components. The reported run has scalar
    // presence 0x3 (pitch/yaw, zero roll); do not manufacture all-present data.
    for (std::uint32_t presence = 0; presence < 8; ++presence) {
        world.Reset();
        Command command;
        auto settings = SilentAimGetSettings();
        settings.hitchance = presence == 3 ? 0.0f : 100.0f;
        SilentAimSetSettings(settings);
        Put(Address(command.baseAngles) + cmd::angle::Presence, presence);
        Put(Address(command.base) + move::base::Presence, presence ? fire::BaseAnglesPresent : 0u);
        Put(Address(command.baseAngles) + cmd::angle::Pitch,
            AimAngles{presence & 1 ? 1.531f : 0.0f, presence & 2 ? 10.0f : 0.0f, 0});
        const auto baseBefore = command.baseAngles;
        world.Prepare(command);
        Expect(recorderCalls == 1 && (command.Buttons().down & 1),
            "sparse base view presence=" + std::to_string(presence) + " can request a shot without manual attack");
        world.History(command);
        Expect(Equal(command.History(), Desired()) && command.Index() == 0 && checksumCalls == 0 &&
            command.baseAngles == baseBefore && Get<int>(world.InputAddress() + fire::InputPrimaryIndex) == -1,
            "sparse base view presence=" + std::to_string(presence) + " publishes aimed history and preserves native base bytes");
    }
    world.Reset();
    Command nativeDefault;
    Put(Address(nativeDefault.base) + move::base::Angles, std::uintptr_t{0});
    const auto defaultBaseBefore = nativeDefault.base;
    world.Prepare(nativeDefault); world.History(nativeDefault);
    Expect(recorderCalls == 1 && nativeDefault.Index() == 0 && Equal(nativeDefault.History(), Desired()) &&
        nativeDefault.base == defaultBaseBefore && checksumCalls == 0,
        "native null default view can publish an automatic shot without allocating or editing base angles");

    world.Reset();
    Command inconsistent;
    Put(Address(inconsistent.baseAngles) + cmd::angle::Presence, std::uint32_t{2});
    Put(Address(inconsistent.baseAngles) + cmd::angle::Pitch, AimAngles{5, 10, 0});
    const auto inconsistentBefore = inconsistent.command;
    world.Prepare(inconsistent);
    Expect(recorderCalls == 0 && inconsistent.command == inconsistentBefore,
        "nonzero payload hidden behind an absent scalar flag prevents an automatic request");

    world.Reset();
    Command zeroThreshold;
    auto settings = SilentAimGetSettings(); settings.hitchance = 0; SilentAimSetSettings(settings);
    weapon.range = 50; // Valid geometric estimate with exactly zero intersections.
    Put(Address(zeroThreshold.baseAngles) + cmd::angle::Presence, std::uint32_t{3});
    world.Prepare(zeroThreshold);
    Expect(recorderCalls == 1 && (zeroThreshold.Buttons().down & 1) && !telemetry.empty() &&
        telemetry[0].find("estimate_valid=1 estimated=0.00%") != std::string::npos,
        "zero threshold accepts an exactly zero-percent valid estimate from a native sparse view");
    world.History(zeroThreshold);
    Expect(zeroThreshold.Index() == 0 && Equal(zeroThreshold.History(), Desired()) && checksumCalls == 0,
        "zero-threshold request publishes without any physical primary input");
}

void CaptureLog(const char* message) { if (message) telemetry.emplace_back(message); }
void PreparationDiagnosticsChecks(World& world) {
    world.Reset();
    Command invalid;
    SilentAimTestConfigure(world.Base(), NativeBuilder, NativeRecorder, &CaptureLog);
    Put(Address(invalid.baseAngles) + cmd::angle::Presence, std::uint32_t{3});
    Put(Address(invalid.baseAngles) + cmd::angle::Pitch, AimAngles{5, 10, 1});
    world.Prepare(invalid);
    world.History(invalid); // This overwrites the shared status with no-primary history.
    SilentAimFlushTelemetry();
    bool found = false;
    for (const auto& message : telemetry) {
        if (message.find("AIM_PREPARE_DIAGNOSTICS calls=1 evaluated=0 view_rejected=1") != std::string::npos &&
            message.find("stage=\"angle_values_invalid\"") != std::string::npos &&
            message.find("angle_presence=0x3") != std::string::npos) found = true;
    }
    Expect(found && recorderCalls == 0, "preparation rejection and scalar flags remain logged after the history callback");

    world.Reset();
    Command blocked;
    SilentAimTestConfigure(world.Base(), NativeBuilder, NativeRecorder, &CaptureLog);
    SilentAimPrepareCommand(world.Input(), blocked.Pointer(), false);
    SilentAimFlushTelemetry();
    found = false;
    for (const auto& message : telemetry) {
        if (message.find("AIM_PREPARE_DIAGNOSTICS calls=1 evaluated=0 view_rejected=0 blocked=1") != std::string::npos &&
            message.find("anti_aim_restore_unresolved") != std::string::npos) found = true;
    }
    Expect(found && recorderCalls == 0, "unresolved prior anti-aim restoration is counted before acquisition");
}

void CancellationChecks(World& world) {
    constexpr const char* names[]{"target", "volume", "weapon", "settings", "disabled", "autofire off",
        "focus", "stopped", "source", "history"};
    for (int invalidation = 0; invalidation < 10; ++invalidation) {
        world.Reset();
        Command command;
        world.Prepare(command);
        Expect(recorderCalls == 1, std::string(names[invalidation]) + ": eligible request exists before state change");
        command.AddUnrelatedChanges();
        auto settings = SilentAimGetSettings();
        switch (invalidation) {
        case 0: targetCurrent = false; break;
        case 1: volumeCurrent = false; break;
        case 2: weaponCurrent = false; break;
        case 3: settings.fov = 29; SilentAimSetSettings(settings); break;
        case 4: settings.enabled = false; SilentAimSetSettings(settings); break;
        case 5: settings.autofire = false; SilentAimSetSettings(settings); break;
        case 6: SilentAimTestForeground(false); break;
        case 7: SilentAimStop("pipeline regression"); break;
        case 8: Put(world.Base() + cs2_offsets::globals::dwLocalPlayerPawn, std::uintptr_t{0x334000}); break;
        case 9: badHistory = true; break;
        }
        world.History(command);
        Expect(Equal(command.Buttons(), command.ExpectedAfterChanges()) && command.Index() == -1 &&
            command.Presence() == (Command::OriginalPresence | UnrelatedPresence) &&
            Get<int>(world.InputAddress() + fire::InputPrimaryIndex) == -1 && Equal(command.History(), Command::PhysicalView),
            std::string(names[invalidation]) + ": cancellation restores only owned primary and preserves unrelated mutations");
        Expect(checksumCalls == 1 && checksumSawCancelledPrimary,
            std::string(names[invalidation]) + ": cancelled native primary is removed before checksum regeneration");
    }

    world.Reset();
    Command checksumFailure;
    world.Prepare(checksumFailure);
    targetCurrent = false; checksumAvailable = false;
    world.History(checksumFailure);
    Expect(checksumCalls == 1 && std::strstr(SilentAimStatus(), "checksum unavailable"),
        "failed cancellation checksum disarms automatic edits and reports its concrete failure");
    targetCurrent = true;
    Command following;
    Put(Address(following.command) + move::command::Sequence, 38);
    world.Prepare(following);
    Expect(recorderCalls == 1 && Equal(following.Buttons(), following.originalButtons),
        "checksum failure prevents later automatic requests");
}

void OwnershipChecks(World& world) {
    world.Reset();
    Command takeover;
    world.Prepare(takeover);
    Put(world.InputAddress() + fire::InputNativeButtons, MovementButtons{1, 1, 0});
    const auto requestedButtons = takeover.Buttons();
    world.History(takeover);
    Expect(Equal(takeover.Buttons(), requestedButtons) && takeover.Index() == 0 && checksumCalls == 0 &&
        Get<int>(world.InputAddress() + fire::InputPrimaryIndex) == 0,
        "physical primary takeover preserves native input and command attack instead of clearing player input");

    world.Reset();
    Command duplicate;
    world.Prepare(duplicate);
    world.Prepare(duplicate);
    Expect(recorderCalls == 2 && Get<int>(world.InputAddress() + fire::InputEventCount) == 1 &&
        Get<int>(world.InputAddress() + fire::InputEventsProcessed) == 1,
        "same-command rebuild retires its earlier owned request before one replacement native recording");
    world.History(duplicate);
    Expect(Equal(duplicate.History(), Desired()) && Get<int>(world.InputAddress() + fire::InputPrimaryIndex) == -1 &&
        checksumCalls == 0, "replacement request publishes once with no retained temporary input state");
    duplicate.AddUnrelatedChanges();
    auto settings = SilentAimGetSettings(); settings.autofire = false; SilentAimSetSettings(settings);
    world.Prepare(duplicate);
    Expect(recorderCalls == 2 && Equal(duplicate.Buttons(), duplicate.ExpectedAfterChanges()) && duplicate.Index() == -1 &&
        duplicate.Presence() == (Command::OriginalPresence | UnrelatedPresence),
        "turning autofire off during a successful command rebuild removes only its owned primary pulse");

    world.Reset();
    Command physicalRebuild;
    world.Prepare(physicalRebuild); world.History(physicalRebuild);
    Put(world.InputAddress() + fire::InputNativeButtons, MovementButtons{1, 0, 0});
    auto physicalBefore = physicalRebuild.Buttons();
    settings = SilentAimGetSettings(); settings.enabled = false; SilentAimSetSettings(settings);
    world.Prepare(physicalRebuild);
    Expect(Equal(physicalRebuild.Buttons(), physicalBefore) && physicalRebuild.Index() == 0 && recorderCalls == 1,
        "rebuild abandons copied synthetic ownership after physical primary takeover");

    world.Reset();
    Command reused;
    world.Prepare(reused); world.History(reused);
    Put(Address(reused.command) + move::command::Sequence, 37 + 256);
    const auto reusedBefore = reused.command;
    settings = SilentAimGetSettings(); settings.enabled = false; SilentAimSetSettings(settings);
    world.Prepare(reused);
    Expect(reused.command == reusedBefore && recorderCalls == 1,
        "reused command address/sequence bucket abandons stale ownership without editing current native bytes");

    world.Reset();
    Command beforeCommit;
    weaponCurrent = false;
    world.Prepare(beforeCommit);
    Expect(recorderCalls == 0 && Equal(beforeCommit.Buttons(), beforeCommit.originalButtons),
        "weapon identity is revalidated immediately before the native request");
}
}

// Replace only engine-dependent snapshots and effects. The integrated suite
// links the real runtime, real history/ownership code, settings and estimator.
bool SilentAimSelectTarget(const AimSource& source, const AimAngles& view, float fov, AimHitbox hitbox,
    const AimBoneContext&, AimTarget& target, AimTargetDiagnostics* diagnostics) noexcept {
    if (!selectAvailable) return false;
    target = {};
    target.local.pawn = source.pawn;
    target.local.controllerHandle = 0x8002; target.local.pawnHandle = 0x8003;
    target.enemy.pawn = 0x555000; target.enemy.pawnHandle = 0x8004;
    target.eye = {}; target.point = {100, 10, 0}; target.hitbox = hitbox;
    target.solution = SilentAimSolve(target.eye, target.point, view, fov);
    if (diagnostics) diagnostics->stage = target.solution.valid ? "selected" : "outside FOV";
    return target.solution.valid;
}
bool SilentAimTargetValid(const AimSource&, const AimTarget&) noexcept { return targetCurrent; }
bool SilentAimReadTargetVolume(const AimSource&, const AimTarget& target, AimTargetVolume& volume,
    const char*& reason) noexcept {
    if (!volumeAvailable) { reason = "fixture geometry unavailable"; return false; }
    volume = {};
    volume.capsule = {{100, 10, -2}, {100, 10, 2}, 4};
    volume.center = {100, 10, 0}; volume.pawn = target.enemy.pawn; volume.valid = true;
    reason = "fixture authored capsule";
    return true;
}
bool SilentAimTargetVolumeValid(const AimSource&, const AimTarget&, const AimTargetVolume&) noexcept { return volumeCurrent; }
bool AimWeaponPrepare(std::uintptr_t client, AimWeaponContext& context) noexcept { context.client = client; return true; }
bool AimWeaponRead(const AimWeaponContext&, const AimSource&, AimWeaponSnapshot& out) noexcept {
    out = weapon;
    return weaponAvailable;
}
bool AimWeaponStillCurrent(const AimWeaponContext&, const AimSource&, const AimWeaponSnapshot&) noexcept { return weaponCurrent; }
bool MovementRefinalizeCommand(void*, void* command) noexcept {
    ++checksumCalls;
    const auto address = reinterpret_cast<std::uintptr_t>(command);
    const auto buttons = Get<MovementButtons>(address + move::command::Buttons);
    checksumSawCancelledPrimary = ((buttons.down | buttons.changed | buttons.third) & 1) == 0 &&
        Get<int>(address + cmd::command::Attack1Index) == -1 &&
        !(Get<std::uint32_t>(address + cmd::command::Presence) & cmd::command::Attack1Present);
    return checksumAvailable;
}
void MovementTelemetryPush(const char* message) noexcept { if (message) telemetry.emplace_back(message); }
void ShotLoggingPump() noexcept { ++shotPumps; }
void ShotLoggingCapture(const ShotLogMetadata& metadata) noexcept { shotMetadata.push_back(metadata); }

int main() {
    World world;
    GateChecks(world);
    SparseViewChecks(world);
    PreparationDiagnosticsChecks(world);
    CancellationChecks(world);
    OwnershipChecks(world);
    SilentAimStop("pipeline checks complete");
    std::printf("Aim pipeline checks: %u, failures: %u\n", checks, failures);
    return failures ? 1 : 0;
}
