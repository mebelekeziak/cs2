#include "silent_aim.h"
#include "silent_aim_command.h"
#include "silent_aim_settings.h"
#include "silent_aim_target.h"
#include "silent_aim_offsets.h"
#include "aim_autofire_offsets.h"
#include "aim_weapon.h"
#include "aim_occlusion.h"
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
bool pathAvailable = true, pathClear = true, changeDuringTrace = false;
bool quickstopAvailable = true, wideFallback = false;
std::vector<bool> quickstopBrakes;
std::vector<void*> quickstopCommands;
std::uint32_t selectedPawnHandle = 0x8004, localPawnHandle = 0x8003;
unsigned traceCalls = 0;
bool multipleTargets = false, reverseCandidates = false;
int rejectedCandidate = 0; // 1 cover, 2 range/chance, 3 missing geometry.
unsigned weaponReads = 0;
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
    explicit Command(int sequence = 37) {
        Put(Address(command) + move::command::Sequence, sequence);
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
    std::array<unsigned char, 0x40> weaponIdentity{};
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
        pathAvailable = pathClear = true; changeDuringTrace = false; traceCalls = 0;
        multipleTargets = reverseCandidates = false; rejectedCandidate = 0; weaponReads = 0;
        quickstopAvailable = true; wideFallback = false;
        quickstopBrakes.clear(); quickstopCommands.clear();
        selectedPawnHandle = 0x8004; localPawnHandle = 0x8003;
        telemetry.clear();
        shotMetadata.clear(); shotPumps = 0;
        ShotLogSetSettings({});
        weapon = {};
        weapon.weapon = 0x444000; weapon.identity = 0x445000; weapon.services = 0x446000;
        weapon.identity = Address(weaponIdentity);
        Put(weapon.identity + cs2_offsets::CEntityIdentity::m_designerName, reinterpret_cast<std::uintptr_t>("weapon_ak47"));
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
    Expect(shotMetadata.size() == 1 && shotMetadata[0].automatic && shotMetadata[0].chanceValid && shotMetadata[0].attackPublished &&
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
    Expect(shotMetadata.size() == 1 && !shotMetadata[0].automatic && shotMetadata[0].chanceValid && shotMetadata[0].attackPublished &&
        shotMetadata[0].hitchance == 0, "manual aimed history captures its actual zero-percent estimate");

    world.Reset();
    Command manualStale;
    volumeCurrent = weaponCurrent = false;
    auto staleButtons = manualStale.originalButtons; staleButtons.down |= 1;
    Put(Address(manualStale.command) + move::command::Buttons, staleButtons);
    world.Prepare(manualStale); world.History(manualStale);
    Expect(Equal(manualStale.History(), Desired()) && shotMetadata.size() == 1 &&
        !shotMetadata[0].chanceValid && !shotMetadata[0].capsuleValid && !shotMetadata[0].attackPublished && !*shotMetadata[0].weapon,
        "manual fire remains available when logging revalidation marks stale weapon/geometry metadata unknown");

    world.Reset();
    Command cooldown;
    weapon.ready = false;
    auto cooldownButtons = cooldown.originalButtons; cooldownButtons.down |= 1;
    Put(Address(cooldown.command) + move::command::Buttons, cooldownButtons);
    world.Prepare(cooldown); world.History(cooldown);
    Expect(shotMetadata.size() == 1 && !shotMetadata[0].attackPublished,
        "manual held primary during weapon cooldown cannot create unregistered-shot attempts");

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
        "focus", "stopped", "source", "history", "occlusion", "trace unavailable", "trace invalidates state"};
    for (int invalidation = 0; invalidation < 13; ++invalidation) {
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
        case 10: pathClear = false; break;
        case 11: pathAvailable = false; break;
        case 12: changeDuringTrace = true; break;
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

void OcclusionChecks(World& world) {
    for (int mode = 0; mode < 3; ++mode) {
        world.Reset();
        Command command;
        const auto inputBefore = world.InputBytes();
        const auto commandBefore = command.command;
        if (mode == 0) pathClear = false;
        if (mode == 1) pathAvailable = false;
        if (mode == 2) changeDuringTrace = true;
        world.Prepare(command);
        Expect(traceCalls == 1 && recorderCalls == 0 && command.command == commandBefore &&
            world.InputBytes() == inputBefore, "blocked, unavailable or invalidated trace prevents native fire request");
    }
    world.Reset();
    Command clear;
    world.Prepare(clear); world.History(clear);
    Expect(traceCalls == 3 && recorderCalls == 1 && Equal(clear.History(), Desired()),
        "clear path is traced for eligibility, freshly before request and again before history publication");

    world.Reset();
    Command rebuild;
    world.Prepare(rebuild); world.History(rebuild);
    pathClear = false;
    world.Prepare(rebuild);
    Expect(recorderCalls == 1 && Equal(rebuild.Buttons(), rebuild.originalButtons) && rebuild.Index() == -1,
        "same-command rebuild removes the earlier owned pulse when cover blocks the replacement request");

    world.Reset();
    Command manual;
    auto settings = SilentAimGetSettings(); settings.autofire = false; SilentAimSetSettings(settings);
    pathClear = false;
    world.Prepare(manual);
    Put(Address(manual.command) + move::command::Buttons, MovementButtons{1, 1, 0});
    Put(world.InputAddress() + fire::InputPrimaryIndex, 0);
    world.History(manual);
    Expect(traceCalls == 0 && recorderCalls == 0 && Equal(manual.History(), Desired()) && (manual.Buttons().down & 1),
        "occlusion gate leaves manual primary input and manual aiming available");

    world.Reset();
    Command takeover;
    world.Prepare(takeover);
    Put(world.InputAddress() + fire::InputNativeButtons, MovementButtons{1, 1, 0});
    pathClear = false;
    world.History(takeover);
    Expect((takeover.Buttons().down & 1) && takeover.Index() == 0 && checksumCalls == 0 &&
        Equal(takeover.History(), Command::PhysicalView),
        "new cover cancels automatic aiming without clearing physical primary takeover");
}

void WideFovChecks(World& world) {
    for (int rejection = 1; rejection <= 3; ++rejection) {
        for (bool reversed : {false, true}) {
            world.Reset();
            multipleTargets = true; reverseCandidates = reversed; rejectedCandidate = rejection;
            auto settings = SilentAimGetSettings(); settings.fov = 180; SilentAimSetSettings(settings);
            Command command;
            const auto baseBefore = command.baseAngles;
            world.Prepare(command); world.History(command);
            Expect(recorderCalls == 1 && Equal(command.History(), AimAngles{0, 180, 0}) &&
                shotMetadata.size() == 1 && shotMetadata[0].targetPawn == 0x8005 &&
                command.baseAngles == baseBefore,
                "180-degree autofire skips ineligible crosshair-nearest enemy and aims behind; rejection=" +
                std::to_string(rejection) + " reversed=" + std::to_string(reversed));
            Expect(weaponReads == 1, "candidate scan shares one command-owner weapon snapshot");
        }
    }
    world.Reset(); multipleTargets = true; rejectedCandidate = 1;
    Command narrow;
    world.Prepare(narrow);
    Expect(recorderCalls == 0, "fallback candidates still obey the configured FOV");

    world.Reset(); multipleTargets = true;
    auto settings = SilentAimGetSettings(); settings.fov = 180; SilentAimSetSettings(settings);
    Command nearest;
    world.Prepare(nearest); world.History(nearest);
    Expect(Equal(nearest.History(), Desired()) && shotMetadata.size() == 1 && shotMetadata[0].targetPawn == 0x8004,
        "eligible nearest enemy retains priority over a farther angular candidate");

    world.Reset(); multipleTargets = true; rejectedCandidate = 1;
    settings = SilentAimGetSettings(); settings.fov = 180; SilentAimSetSettings(settings);
    Command lateCover;
    world.Prepare(lateCover); pathClear = false; world.History(lateCover);
    Expect(recorderCalls == 1 && checksumCalls == 1 && !(lateCover.Buttons().down & 1) && shotMetadata.empty(),
        "fallback target gets fresh publication occlusion and owned-attack cancellation");

    world.Reset(); multipleTargets = true; rejectedCandidate = 1;
    settings = SilentAimGetSettings(); settings.fov = 180; SilentAimSetSettings(settings);
    Command manual;
    Put(Address(manual.command) + move::command::Buttons, MovementButtons{1, 1, 0});
    Put(world.InputAddress() + fire::InputPrimaryIndex, 0);
    world.History(manual);
    Expect(Equal(manual.History(), Desired()) && traceCalls == 0 && shotMetadata.size() == 1 && !shotMetadata[0].automatic,
        "manual primary keeps nearest-landmark behavior even while autofire is enabled");
}

void EnableQuickstop() {
    auto settings = SilentAimGetSettings();
    settings.quickstop = true;
    SilentAimSetSettings(settings);
    weapon.inaccuracy = 0.4f;
}
bool LastBrake(bool brake) {
    return !quickstopBrakes.empty() && quickstopBrakes.back() == brake;
}
void QuickstopChecks(World& world) {
    world.Reset(); EnableQuickstop();
    Command moving;
    const auto before = moving.command;
    world.Prepare(moving);
    Expect(LastBrake(true) && quickstopCommands.back() == moving.Pointer() && recorderCalls == 0 && moving.command == before &&
        std::strstr(SilentAimStatus(), "braking below hitchance"),
        "clear ready target below hitchance acquires braking without inventing a shot or modifying its accuracy sample");
    world.History(moving);
    Expect(shotMetadata.empty(), "braking command alone does not publish a shot context");

    weapon.inaccuracy = 0;
    Command threshold(38);
    world.Prepare(threshold);
    Expect(LastBrake(true) && recorderCalls == 1,
        "next command meeting exact hitchance retains the active brake through the requested shot");
    world.History(threshold);
    Expect(Equal(threshold.History(), Desired()) && shotMetadata.size() == 1,
        "stopped command publishes through the existing real autofire history path");

    weapon.ready = false; weapon.reason = "primary cooldown"; weapon.inaccuracy = 0.4f;
    Command cycling(39);
    world.Prepare(cycling);
    Expect(LastBrake(false) && recorderCalls == 1 && Equal(cycling.Buttons(), cycling.originalButtons),
        "weapon recovery releases normal movement even when the target remains below hitchance");
    weapon.ready = true; weapon.reason = "ready";
    Command readyAgain(40);
    world.Prepare(readyAgain);
    Expect(LastBrake(true) && recorderCalls == 1, "ready weapon begins a fresh brake cycle for a still-valid low-chance target");

    constexpr const char* cancellations[]{"quickstop off", "autofire off", "aim off", "unfocused", "no target",
        "missing geometry", "invalid volume", "missing weapon", "stale target", "stale weapon", "covered",
        "trace unavailable", "trace invalidates state", "out of weapon range", "invalid accuracy", "invalid view", "stopped"};
    for (int mode = 0; mode < static_cast<int>(std::size(cancellations)); ++mode) {
        world.Reset(); EnableQuickstop();
        Command initial;
        world.Prepare(initial);
        Expect(LastBrake(true), std::string(cancellations[mode]) + ": initial cycle acquired");
        auto settings = SilentAimGetSettings();
        Command released(38);
        switch (mode) {
        case 0: settings.quickstop = false; SilentAimSetSettings(settings); break;
        case 1: settings.autofire = false; SilentAimSetSettings(settings); break;
        case 2: settings.enabled = false; SilentAimSetSettings(settings); break;
        case 3: SilentAimTestForeground(false); break;
        case 4: selectAvailable = false; break;
        case 5: volumeAvailable = false; break;
        case 6: volumeCurrent = false; break;
        case 7: weaponAvailable = false; break;
        case 8: targetCurrent = false; break;
        case 9: weaponCurrent = false; break;
        case 10: pathClear = false; break;
        case 11: pathAvailable = false; break;
        case 12: changeDuringTrace = true; break;
        case 13: weapon.range = 50; break;
        case 14: weapon.inaccuracy = -1; break;
        case 15:
            Put(Address(released.baseAngles) + cmd::angle::Presence, std::uint32_t{0});
            Put(Address(released.baseAngles) + cmd::angle::Pitch, AimAngles{1, 0, 0}); break;
        case 16: SilentAimStop("quickstop cancellation regression"); break;
        }
        world.Prepare(released);
        Expect(LastBrake(false) && recorderCalls == 0 && Equal(released.Buttons(), released.originalButtons),
            std::string(cancellations[mode]) + ": releases braking and cannot request a shot");
    }

    for (bool activeCycle : {false, true}) {
        world.Reset(); EnableQuickstop();
        Command initial;
        if (activeCycle) world.Prepare(initial);
        quickstopAvailable = false;
        weapon.inaccuracy = activeCycle ? 0.0f : 0.4f;
        Command unavailable(38);
        world.Prepare(unavailable);
        Expect(LastBrake(true) && recorderCalls == 0 && std::strstr(SilentAimStatus(), "movement unavailable"),
            activeCycle ? "failed firing-command brake cancels automatic request for an active stop cycle" :
                "failed first brake cannot create an automatic request");
    }

    world.Reset(); EnableQuickstop(); weapon.inaccuracy = 0; quickstopAvailable = false;
    Command failedRelease;
    world.Prepare(failedRelease);
    Expect(LastBrake(false) && recorderCalls == 0 && Equal(failedRelease.Buttons(), failedRelease.originalButtons) &&
        std::strstr(SilentAimStatus(), "movement unavailable"),
        "failed release handoff suppresses even an immediately eligible automatic shot");

    for (int changed = 0; changed < 10; ++changed) {
        world.Reset(); EnableQuickstop();
        Command initial;
        world.Prepare(initial);
        weapon.inaccuracy = 0;
        auto settings = SilentAimGetSettings();
        switch (changed) {
        case 0: Put(world.Base() + cs2_offsets::globals::dwLocalPlayerPawn, std::uintptr_t{0x334000}); break;
        case 1: settings.fov = 29; SilentAimSetSettings(settings); break;
        case 2: selectedPawnHandle = 0x9004; break;
        case 3: ++weapon.handle; break;
        case 4: ++localPawnHandle; break;
        case 5: Put(world.Base() + cs2_offsets::globals::dwLocalPlayerController, std::uintptr_t{0x223000}); break;
        case 6: ++weapon.services; break;
        case 7: ++weapon.data; break;
        case 8: ++weapon.mode; break;
        case 9: ++weapon.weapon; break;
        }
        Command replacement(38);
        world.Prepare(replacement);
        Expect(LastBrake(false) && recorderCalls == 1,
            "source/settings/target/weapon identity change cannot inherit firing-command brake case " + std::to_string(changed));
    }

    for (bool reversed : {false, true}) {
        world.Reset(); EnableQuickstop(); multipleTargets = true; reverseCandidates = reversed; rejectedCandidate = 1;
        auto settings = SilentAimGetSettings(); settings.fov = 180; SilentAimSetSettings(settings);
        Command clearFallback;
        world.Prepare(clearFallback);
        Expect(LastBrake(true) && recorderCalls == 0 && !telemetry.empty() &&
            telemetry.back().find("target=0x00008005") != std::string::npos && weaponReads == 1,
            "blocked nearest target yields brake ownership to clear below-chance fallback; reversed=" + std::to_string(reversed));

        world.Reset(); EnableQuickstop(); multipleTargets = true; reverseCandidates = reversed; wideFallback = true;
        settings = SilentAimGetSettings(); settings.fov = 180; SilentAimSetSettings(settings);
        Command shootableFallback;
        world.Prepare(shootableFallback); world.History(shootableFallback);
        Expect(LastBrake(false) && recorderCalls == 1 && shotMetadata.size() == 1 && shotMetadata[0].targetPawn == 0x8005 &&
            Equal(shootableFallback.History(), AimAngles{0, 180, 0}),
            "immediately eligible alternative takes priority over below-chance brake target; reversed=" + std::to_string(reversed));
    }

    world.Reset(); EnableQuickstop(); weapon.inaccuracy = 0;
    Command alreadyAccurate;
    world.Prepare(alreadyAccurate);
    Expect(LastBrake(false) && recorderCalls == 1, "already sufficient hitchance fires without starting an unnecessary stop cycle");
}
}

// Replace only engine-dependent snapshots and effects. The integrated suite
// links the real runtime, real history/ownership code, settings and estimator.
bool SilentAimSelectTarget(const AimSource& source, const AimAngles& view, float fov, AimHitbox hitbox,
    const AimBoneContext&, AimTarget& target, AimTargetDiagnostics* diagnostics,
    AimTargetFilter filter, void* filterContext) noexcept {
    if (!selectAvailable) return false;
    target = {};
    for (int i = 0; i < (multipleTargets ? 2 : 1); ++i) {
        const bool fallback = multipleTargets && (reverseCandidates ? i == 0 : i == 1);
        AimTarget candidate{};
        candidate.local.pawn = source.pawn;
        candidate.local.controllerHandle = 0x8002; candidate.local.pawnHandle = localPawnHandle;
        candidate.enemy.pawn = fallback ? 0x556000 : 0x555000;
        candidate.enemy.pawnHandle = fallback ? 0x8005 : selectedPawnHandle;
        candidate.point = fallback ? AimVector{-100, 0, 0} : AimVector{100, 10, 0}; candidate.hitbox = hitbox;
        candidate.solution = SilentAimSolve(candidate.eye, candidate.point, view, fov);
        if (!candidate.solution.valid || (target.solution.valid && candidate.solution.separation >= target.solution.separation)) continue;
        if (filter && !filter(candidate, filterContext)) continue;
        target = candidate;
    }
    if (diagnostics) diagnostics->stage = target.solution.valid ? "selected" : "outside FOV";
    return target.solution.valid;
}
bool SilentAimTargetValid(const AimSource&, const AimTarget&) noexcept { return targetCurrent; }
bool SilentAimReadTargetVolume(const AimSource&, const AimTarget& target, AimTargetVolume& volume,
    const char*& reason) noexcept {
    if (!volumeAvailable || (rejectedCandidate == 3 && target.enemy.pawnHandle == 0x8004)) {
        reason = "fixture geometry unavailable"; return false;
    }
    volume = {};
    volume.center = target.point;
    if (rejectedCandidate == 2 && target.enemy.pawnHandle == 0x8004) volume.center = {2000, 200, 0};
    volume.capsule = {{volume.center.x, volume.center.y, -2}, {volume.center.x, volume.center.y, 2}, 4};
    if (wideFallback && target.enemy.pawnHandle == 0x8005) volume.capsule.radius = 80;
    volume.pawn = target.enemy.pawn; volume.valid = true;
    reason = "fixture authored capsule";
    return true;
}
bool SilentAimTargetVolumeValid(const AimSource&, const AimTarget&, const AimTargetVolume&) noexcept { return volumeCurrent; }
bool AimWeaponPrepare(std::uintptr_t client, AimWeaponContext& context) noexcept { context.client = client; return true; }
bool AimWeaponRead(const AimWeaponContext&, const AimSource&, AimWeaponSnapshot& out) noexcept {
    ++weaponReads;
    out = weapon;
    return weaponAvailable;
}
bool AimWeaponStillCurrent(const AimWeaponContext&, const AimSource&, const AimWeaponSnapshot&) noexcept { return weaponCurrent; }
bool AimOcclusionPrepare(std::uintptr_t client, AimOcclusionContext& context) noexcept { context = {client, true}; return true; }
AimOcclusionResult AimOcclusionCheck(const AimOcclusionContext&, const AimSource&, const AimTarget& target, const AimTargetVolume&) noexcept {
    ++traceCalls;
    if (changeDuringTrace) weaponCurrent = false;
    const bool clear = pathClear && !(rejectedCandidate == 1 && target.enemy.pawnHandle == 0x8004);
    return {pathAvailable, pathAvailable && clear, clear ? 1.0f : 0.25f,
        !pathAvailable ? "occlusion trace unavailable" : clear ? "direct path clear" : "target occluded"};
}
bool MovementRefinalizeCommand(void*, void* command) noexcept {
    ++checksumCalls;
    const auto address = reinterpret_cast<std::uintptr_t>(command);
    const auto buttons = Get<MovementButtons>(address + move::command::Buttons);
    checksumSawCancelledPrimary = ((buttons.down | buttons.changed | buttons.third) & 1) == 0 &&
        Get<int>(address + cmd::command::Attack1Index) == -1 &&
        !(Get<std::uint32_t>(address + cmd::command::Presence) & cmd::command::Attack1Present);
    return checksumAvailable;
}
bool MovementQuickstop(void* command, bool brake) noexcept {
    quickstopBrakes.push_back(brake); quickstopCommands.push_back(command);
    return quickstopAvailable;
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
    OcclusionChecks(world);
    WideFovChecks(world);
    QuickstopChecks(world);
    SilentAimStop("pipeline checks complete");
    std::printf("Aim pipeline checks: %u, failures: %u\n", checks, failures);
    return failures ? 1 : 0;
}
