#include "silent_aim.h"
#include "aim_visualization.h"
#include "menu_input.h"
#include "silent_aim_command.h"
#include "silent_aim_settings.h"
#include "silent_aim_target.h"
#include "silent_aim_offsets.h"
#include "aim_autofire_offsets.h"
#include "aim_weapon.h"
#include "aim_spread.h"
#include "aim_native_hitchance.h"
#include "aim_spread_offsets.h"
#include "aim_weapon_offsets.h"
#include "aim_shoot_origin.h"
#include <limits>
#include "aim_occlusion.h"
#include "aim_damage.h"
#include "aim_autowall.h"
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
bool builderKeepsHistory = false;
bool builderKeepsAttackIndex = false;
bool pathAvailable = true, pathClear = true, changeDuringTrace = false;
bool penetrationClear = true, penetrationChange = false;
unsigned penetrationCalls = 0;
bool quickstopAvailable = true, wideFallback = false;
bool damageAvailable = true, damageCurrent = true, damageChangeDuringTrace = false;
int predictedHp = 34;
std::vector<bool> quickstopBrakes;
std::vector<void*> quickstopCommands;
std::uint32_t selectedPawnHandle = 0x8004, localPawnHandle = 0x8003;
unsigned traceCalls = 0;
unsigned changeOnTraceCall = 0;
bool multipleTargets = false, reverseCandidates = false;
int rejectedCandidate = 0; // 1 cover, 2 range/chance, 3 missing geometry.
unsigned weaponReads = 0;
unsigned selectionCalls = 0, volumeReads = 0;
unsigned performanceEnemies = 0;
bool multiHitgroups = false, reverseHitgroups = false;
std::array<int, 2> groupDamage{90, 40}; // Head, chest.
std::array<float, 2> groupRadius{4, 4};
int coveredGroup = -1, unavailableGroup = -1;
int targetHealth = 100;
bool healthChangeDuringTrace = false;
bool recorderSawNativePress = false, builderSawPrimary = false, checksumSawCancelledPrimary = false;
std::vector<std::string> telemetry;
std::vector<ShotLogMetadata> shotMetadata;
struct WithdrawnCommand { ShotLogSource source; int sequence; std::uint32_t weapon; };
std::vector<WithdrawnCommand> withdrawnCommands;
unsigned shotPumps = 0;
AimWeaponSnapshot weapon{};
AimVector nativeOrigin{};
bool originAvailable = true;
int builderMutation = 0, traceModelMutation = 0, modelReadMutation = 0;
unsigned modelMutationTrace = 3;
unsigned spreadReads = 0, spreadSolves = 0;
unsigned nativePatternCalls = 0;
bool nativePatternAvailable = true;
double spreadTolerance = 0;
unsigned spreadBudget = 0;
bool spreadAvailable = true, spreadCurrent = true, spreadMiss = false;
bool spreadAlternateMiss = false;
bool spreadRawAlternative = false;
bool spreadPunchOverride = false;
AimAngles spreadSelectedPunch{}, spreadRequestedAim{};
int spreadMutation = 0;
void MutateModel(int mode) {
    if (mode == 1) weapon.inaccuracy = .5f;
    if (mode == 2) weapon.spread = .5f;
    if (mode == 3) weapon.aimPunch = {5, -7, .25f};
    if (mode == 4) { weapon.ready = false; weapon.reason = "fixture cooldown"; }
    if (mode == 5) weaponAvailable = false;
    if (mode == 6) nativeOrigin.y = 6;
    if (mode == 7) originAvailable = false;
    if (mode == 8) weapon.aimPunch.pitch = 100;
    if (mode == 9) weapon.currentTime += .01f;
    if (mode == 10) { auto settings = SilentAimGetSettings(); settings.enabled = false; SilentAimSetSettings(settings); }
    if (mode == 11) SilentAimTestForeground(false);
    if (mode == 12) { --weapon.clip; weapon.recoilIndex += 1; }
}
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
    std::array<unsigned char, 0x38> baseAngles{}, historyAngles{};
    std::array<unsigned char, 0x70> entry{};
    std::array<unsigned char, 0x18> repeated{};
    std::array<unsigned char, 0x18> subticks{};
    std::array<MovementSubtick, 2> steps{};
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
        Put(Address(entry) + cmd::history::Presence, std::uint32_t{0x8001 | aim_spread_offsets::HistoryTimePresent});
        Put(Address(entry) + cmd::history::Angles, Address(historyAngles));
        Put(Address(entry) + aim_spread_offsets::HistoryTick, 100);
        Put(Address(entry) + aim_spread_offsets::HistoryFraction, .5f);
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
    void JumpTap() noexcept {
        steps[0].presence = steps[1].presence = 7;
        steps[0].button = steps[1].button = move::Jump;
        steps[0].pressed = true; steps[0].when = .2f;
        steps[1].pressed = false; steps[1].when = .6f;
        Put(Address(subticks), 2);
        Put(Address(subticks) + 8, Address(steps[0]));
        Put(Address(subticks) + 16, Address(steps[1]));
        Put(Address(base) + move::base::SubtickSize, 2);
        Put(Address(base) + move::base::SubtickCapacity, 2);
        Put(Address(base) + move::base::SubtickRep, Address(subticks));
    }
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
    int index = Get<int>(inputAddress + fire::InputPrimaryIndex);
    auto presence = Get<std::uint32_t>(address + cmd::command::Presence);
    if (builderKeepsAttackIndex && index < 0 && (presence & cmd::command::Attack1Present))
        index = Get<int>(address + cmd::command::Attack1Index);
    presence = index >= 0 ? presence | cmd::command::Attack1Present : presence & ~cmd::command::Attack1Present;
    Put(address + cmd::command::Presence, presence);
    Put(address + cmd::command::Attack1Index, index);
    const auto rep = Get<std::uintptr_t>(address + cmd::command::HistoryRep);
    const auto entry = Get<std::uintptr_t>(rep + cmd::repeated::Entries);
    const auto angles = Get<std::uintptr_t>(entry + cmd::history::Angles);
    const auto base = Get<std::uintptr_t>(address + move::command::Base);
    const auto baseAngles = Get<std::uintptr_t>(base + move::base::Angles);
    if (!builderKeepsHistory)
        Put(angles + cmd::angle::Pitch, baseAngles ? Get<AimAngles>(baseAngles + cmd::angle::Pitch) : AimAngles{});
    Put(angles + cmd::angle::Presence, badHistory ? std::uint32_t{0} : std::uint32_t{0x8007});
    MutateModel(builderMutation);
}

struct World {
    std::vector<unsigned char> client = std::vector<unsigned char>(cs2_offsets::ClientImageSize);
    std::array<unsigned char, 0x500> pawn{};
    std::array<unsigned char, 0x70> pawnIdentity{};
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
        pawn.fill(0); // Stable airborne state unless the scenario requests ground.
        pawnIdentity.fill(0); weaponIdentity.fill(0);
        Put(Base() + cs2_offsets::globals::dwEntityList, std::uintptr_t{0x111000});
        Put(Base() + cs2_offsets::globals::dwLocalPlayerController, std::uintptr_t{0x222000});
        Put(Base() + cs2_offsets::globals::dwLocalPlayerPawn, Address(pawn));
        Put(Address(pawn) + cs2_offsets::CEntityInstance::m_pEntity, Address(pawnIdentity));
        Put(Address(pawnIdentity), Address(pawn));
        Put(Address(pawnIdentity) + cs2_offsets::entity_layout::HandleOffset, std::uint32_t{0x8003});
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
        badHistory = builderKeepsHistory = builderKeepsAttackIndex = false;
        pathAvailable = pathClear = true; changeDuringTrace = false; traceCalls = 0;
        changeOnTraceCall = 0;
        penetrationClear = true; penetrationChange = false; penetrationCalls = 0;
        multipleTargets = reverseCandidates = false; rejectedCandidate = 0; weaponReads = 0;
        selectionCalls = volumeReads = 0;
        performanceEnemies = 0;
        multiHitgroups = reverseHitgroups = false; groupDamage = {90, 40}; groupRadius = {4, 4};
        coveredGroup = unavailableGroup = -1;
        targetHealth = 100; healthChangeDuringTrace = false;
        quickstopAvailable = true; wideFallback = false;
        damageAvailable = damageCurrent = true; damageChangeDuringTrace = false; predictedHp = 34;
        quickstopBrakes.clear(); quickstopCommands.clear();
        selectedPawnHandle = 0x8004; localPawnHandle = 0x8003;
        telemetry.clear();
        shotMetadata.clear(); withdrawnCommands.clear(); shotPumps = 0;
        ShotLogSetSettings({});
        nativeOrigin = {}; originAvailable = true; modelMutationTrace = 3; builderMutation = traceModelMutation = modelReadMutation = 0;
        spreadReads = spreadSolves = 0; spreadAvailable = spreadCurrent = true; spreadMiss = false; spreadMutation = 0;
        nativePatternCalls = 0; nativePatternAvailable = true;
        spreadTolerance = 0; spreadBudget = 0;
        spreadAlternateMiss = false;
        spreadRawAlternative = false;
        spreadPunchOverride = false; spreadSelectedPunch = spreadRequestedAim = {};
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
    Expect(Equal(manualStale.History(), Command::PhysicalView) && Equal(manualStale.Buttons(), staleButtons) && shotMetadata.empty(),
        "stale shooting model preserves physical manual fire and original aim without guessed compensation");

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
    Expect(recorderCalls == 1 && shotMetadata.size() == 1,
        "completed normal publication is retained instead of issuing a replacement with a newer path/pose");
    world.History(rebuild); // Native drops the original primary index.
    world.Prepare(rebuild);
    Expect(recorderCalls == 1 && Equal(rebuild.Buttons(), rebuild.originalButtons) && rebuild.Index() == -1,
        "native reset retires the normal publication and blocked cover prevents its replacement request");

    world.Reset();
    Command manual;
    auto settings = SilentAimGetSettings(); settings.autofire = false; SilentAimSetSettings(settings);
    pathClear = false;
    world.Prepare(manual);
    Put(Address(manual.command) + move::command::Buttons, MovementButtons{1, 1, 0});
    Put(world.InputAddress() + fire::InputPrimaryIndex, 0);
    world.History(manual);
    Expect(traceCalls == 2 && recorderCalls == 0 && Equal(manual.History(), Desired()) && (manual.Buttons().down & 1) &&
        shotMetadata.size() == 1 && !shotMetadata[0].damageValid,
        "manual prediction observes cover without gating physical primary input or manual aiming");

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
            Expect(weaponReads == 3, "candidate scan shares one snapshot plus publication and post-trace model samples");
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
    Expect(Equal(manual.History(), AimAngles{0, 180, 0}) && traceCalls == 3 && shotMetadata.size() == 1 && !shotMetadata[0].automatic &&
        shotMetadata[0].damageValid && shotMetadata[0].targetPawn == 0x8005,
        "manual primary ranks clear modeled shots while preserving physical input");
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
bool SilentAimSelectTarget(const AimSource& source, const AimAngles& view, float fov, std::uint32_t hitgroups,
    const AimBoneContext&, AimTarget& target, AimTargetDiagnostics* diagnostics,
    AimTargetFilter filter, void* filterContext, const AimVector* shootingEye) noexcept {
    ++selectionCalls;
    if (!selectAvailable || !hitgroups) return false;
    const int capturedHealth = targetHealth;
    target = {};
    constexpr AimHitbox torso[]{AimHitbox::Head, AimHitbox::Chest, AimHitbox::Stomach,
        AimHitbox::Pelvis, AimHitbox::LowerChest, AimHitbox::MiddleChest};
    const int enemyCount = performanceEnemies ? static_cast<int>(performanceEnemies) : multipleTargets ? 2 : 1;
    for (int i = 0; i < enemyCount; ++i) {
        const bool fallback = multipleTargets && (reverseCandidates ? i == 0 : i == 1);
        for (int group = 0; group < (performanceEnemies ? 6 : multiHitgroups ? 2 : 1); ++group) {
        const int index = multiHitgroups ? (reverseHitgroups ? 1 - group : group) : 1;
        const auto hitbox = performanceEnemies ? torso[group] : index == 0 ? AimHitbox::Head : AimHitbox::Chest;
        if (!(hitgroups & AimHitboxGroup(hitbox))) continue;
        AimTarget candidate{};
        if (shootingEye) candidate.eye = *shootingEye;
        candidate.local.pawn = source.pawn;
        candidate.local.controllerHandle = 0x8002; candidate.local.pawnHandle = localPawnHandle;
        candidate.enemy.pawn = fallback ? 0x556000 : 0x555000;
        candidate.enemy.pawnHandle = fallback ? 0x8005 : selectedPawnHandle;
        candidate.enemy.health = capturedHealth;
        candidate.point = fallback ? AimVector{-100, 0, 0} : AimVector{100, 10, 0}; candidate.hitbox = hitbox;
        if (multiHitgroups && index == 1) candidate.point.y = 20;
        if (performanceEnemies) {
            candidate.enemy.pawn = 0x555000 + i * 0x1000;
            candidate.enemy.pawnHandle = 0x8004 + i;
            candidate.point = {100, 10.0f + i * 10.0f + group * 2.0f, 0};
        }
        candidate.solution = SilentAimSolve(candidate.eye, candidate.point, view, fov);
        if (!candidate.solution.valid || (!filter && target.solution.valid && candidate.solution.separation >= target.solution.separation)) continue;
        if (filter && !filter(candidate, filterContext)) continue;
        target = candidate;
        }
    }
    if (diagnostics) diagnostics->stage = target.solution.valid ? "selected" : "outside FOV";
    return target.solution.valid && SilentAimTargetValid(source, target);
}
bool SilentAimTargetValid(const AimSource&, const AimTarget& target) noexcept {
    return targetCurrent && target.enemy.health == targetHealth;
}
bool SilentAimReadTargetVolume(const AimSource&, const AimTarget& target, AimTargetVolume& volume,
    const char*& reason) noexcept {
    ++volumeReads;
    const int group = target.hitbox == AimHitbox::Head ? 0 : 1;
    if (!volumeAvailable || (multiHitgroups && unavailableGroup == group) ||
        (rejectedCandidate == 3 && target.enemy.pawnHandle == 0x8004)) {
        reason = "fixture geometry unavailable"; return false;
    }
    volume = {};
    volume.center = target.point;
    if (rejectedCandidate == 2 && target.enemy.pawnHandle == 0x8004) volume.center = {2000, 200, 0};
    volume.capsule = {{volume.center.x, volume.center.y, -2}, {volume.center.x, volume.center.y, 2}, 4};
    if (multiHitgroups) volume.capsule.radius = groupRadius[group];
    if (wideFallback && target.enemy.pawnHandle == 0x8005) volume.capsule.radius = 80;
    volume.pawn = target.enemy.pawn; volume.valid = true;
    reason = "fixture authored capsule";
    return true;
}
bool SilentAimTargetVolumeValid(const AimSource& source, const AimTarget& target, const AimTargetVolume&) noexcept {
    return volumeCurrent && SilentAimTargetValid(source, target);
}
bool AimShootOriginPrepare(std::uintptr_t client, AimShootOriginContext& context) noexcept { context.client = client; return true; }
bool AimShootOriginRead(const AimShootOriginContext&, const AimSource&, AimShootOriginSnapshot& out) noexcept {
    out = {}; out.position = nativeOrigin; out.valid = originAvailable;
    out.reason = originAvailable ? "fixture native origin" : "fixture native origin unavailable";
    return originAvailable;
}
bool AimShootOriginStillCurrent(const AimShootOriginContext&, const AimSource&, const AimShootOriginSnapshot& snapshot) noexcept {
    return originAvailable && snapshot.valid && !std::memcmp(&snapshot.position, &nativeOrigin, sizeof(nativeOrigin));
}
bool AimWeaponPrepare(std::uintptr_t client, AimWeaponContext& context) noexcept { context.client = client; return true; }
bool AimWeaponRead(const AimWeaponContext&, const AimSource&, AimWeaponSnapshot& out) noexcept {
    ++weaponReads;
    if (weaponReads == 3) MutateModel(modelReadMutation);
    out = weapon;
    return weaponAvailable;
}
bool AimWeaponStillCurrent(const AimWeaponContext&, const AimSource&, const AimWeaponSnapshot&) noexcept { return weaponCurrent; }
bool AimSpreadPrepare(std::uintptr_t, AimSpreadContext&) noexcept { return false; }
bool AimSpreadReadPattern(const AimSpreadContext&, bool& pattern) noexcept { pattern = false; return nativePatternAvailable; }
bool AimPrepareNativeSpreadPattern(const AimSpreadContext&, const AimHitchanceInput& input, bool,
    AimSpreadPattern& pattern, const char** reason) noexcept {
    ++nativePatternCalls;
    if (!nativePatternAvailable) { pattern.valid = false; if (reason) *reason = "fixture native failure"; return false; }
    if (!AimPrepareSpreadPattern(input, pattern)) return false;
    // Deliberately different fixture distribution proves routing and gating.
    for (auto& offset : pattern.offsets) offset = {0, 0};
    if (reason) *reason = "fixture native samples";
    return true;
}
bool AimSpreadRead(const AimSpreadContext&, const AimSource& source, const AimWeaponSnapshot& value, const AimShot& shot,
    AimSpreadSnapshot& out) noexcept {
    ++spreadReads;
    out = {}; out.weapon = value; out.punch = value.aimPunch; out.time = {100, .5f};
    out.movementStateKnown = true;
    out.movementFlags = Get<std::uint32_t>(source.pawn + move::pawn::Flags);
    out.velocity = Get<AimVector>(source.pawn + move::pawn::Velocity);
    out.alternateTime = {101, .5f}; out.alternatePunch = out.punch;
    if (spreadRawAlternative) {
        out.rawClockAlternative = true; out.converted = out.alternateTime = {104, .25f};
        out.punch.pitch = 1.25f;
        out.selectedVariant = 1;
        if (spreadPunchOverride) out.alternatePunch = spreadSelectedPunch;
    }
    out.seedVariants = 1;
    out.valid = spreadAvailable && shot.index >= 0;
    out.reason = out.valid ? "fixture indexed shot model" : "fixture timing unavailable";
    return out.valid;
}
bool AimSpreadStillCurrent(const AimSpreadContext&, const AimSource&, const AimSpreadSnapshot&) noexcept { return spreadCurrent; }
AimSpreadSearchResult AimSpreadSolve(const AimSpreadContext&, const AimSpreadSnapshot& model, const AimAngles& desired,
    double tolerance, unsigned budget) noexcept {
    ++spreadSolves;
    spreadRequestedAim = desired;
    spreadTolerance = tolerance; spreadBudget = budget;
    const auto corrected = AimSpreadCompensate(desired, model.selectedVariant ? model.alternatePunch : model.punch, .15, -.08);
    AimSpreadSearchResult result{};
    result.command = corrected.command; result.effective = corrected.effective;
    result.predicted = desired; if (spreadMiss) result.predicted.yaw += 90;
    result.variants = model.seedVariants;
    result.alternatePredicted = desired; if (spreadAlternateMiss) result.alternatePredicted.yaw += 90;
    result.alternateSeed = 0x12345679;
    result.valid = corrected.valid; result.residualDegrees = spreadMiss ? 90.0f : 0.0f;
    result.seed = 0x12345678; result.evaluations = 10;
    result.reason = "fixture indexed seed correction";
    MutateModel(spreadMutation);
    return result;
}
bool AimDamagePrepare(std::uintptr_t client, AimDamageContext& context) noexcept { context = {client, true}; return true; }
bool AimAutowallPrepare(std::uintptr_t client, AimAutowallContext& context) noexcept { context = {client, true}; return true; }
AimOcclusionResult AimAutowallCheck(const AimAutowallContext&, const AimSource&, const AimTarget&,
    const AimTargetVolume&, const AimWeaponSnapshot&) noexcept {
    ++penetrationCalls;
    if (penetrationChange) weaponCurrent = false;
    AimOcclusionResult result{};
    result.valid = result.clear = penetrationClear;
    result.reason = penetrationClear ? "native bullet path reached target" : "penetration blocked";
    result.penetrationDamage = penetrationClear ? 34.75f : -1;
    result.penetrations = 1;
    return result;
}
AimDamageResult AimDamageRead(const AimDamageContext&, const AimSource&, const AimTarget& target,
    const AimTargetVolume&, const AimWeaponSnapshot&, const AimOcclusionResult&) noexcept {
    AimDamageResult result{};
    result.valid = damageAvailable;
    result.healthDamage = rejectedCandidate == 4 && target.enemy.pawnHandle == 0x8004 ? 10 : predictedHp;
    if (multiHitgroups) result.healthDamage = groupDamage[target.hitbox == AimHitbox::Head ? 0 : 1];
    result.damage = static_cast<float>(result.healthDamage) + .75f;
    result.input.hitgroup = target.hitbox == AimHitbox::Head ? 1 : 2;
    result.reason = damageAvailable ? "direct-bullet damage estimate" : "fixture damage unavailable";
    return result;
}
bool AimDamageStillCurrent(const AimDamageContext&, const AimSource&, const AimTarget&,
    const AimTargetVolume&, const AimWeaponSnapshot&, const AimDamageResult&) noexcept { return damageCurrent; }
bool AimOcclusionPrepare(std::uintptr_t client, AimOcclusionContext& context) noexcept { context = {client, true}; return true; }
AimOcclusionResult AimOcclusionCheck(const AimOcclusionContext&, const AimSource&, const AimTarget& target, const AimTargetVolume&) noexcept {
    ++traceCalls;
    if (traceCalls == modelMutationTrace) MutateModel(traceModelMutation);
    if (changeDuringTrace || (changeOnTraceCall && traceCalls == changeOnTraceCall)) weaponCurrent = false;
    if (damageChangeDuringTrace) damageCurrent = false;
    if (healthChangeDuringTrace) targetHealth = 40;
    const int group = target.hitbox == AimHitbox::Head ? 0 : 1;
    const bool clear = pathClear && !(multiHitgroups && coveredGroup == group) &&
        !(rejectedCandidate == 1 && target.enemy.pawnHandle == 0x8004);
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
void ShotLoggingWithdrawCommand(const ShotLogSource& source, int sequence, std::uint32_t weaponHandle) noexcept {
    withdrawnCommands.push_back({source, sequence, weaponHandle});
}

void DamageGateChecks(World& world) {
    world.Reset(); pathClear = false;
    Command coveredDisabled; world.Prepare(coveredDisabled);
    Expect(recorderCalls == 0 && penetrationCalls == 0, "autowall disabled keeps covered targets blocked");
    world.Reset(); pathClear = false;
    auto autowallSettings = SilentAimGetSettings(); autowallSettings.autowall = true;
    SilentAimSetSettings(autowallSettings);
    Command throughCover; world.Prepare(throughCover); world.History(throughCover);
    Expect(recorderCalls == 1 && penetrationCalls >= 2 && shotMetadata.size() == 1,
        "enabled autowall evaluates covered target before request and again at publication");
    for (int mode = 0; mode < 5; ++mode) {
        world.Reset(); pathClear = false;
        autowallSettings = SilentAimGetSettings(); autowallSettings.autowall = true;
        SilentAimSetSettings(autowallSettings);
        Command pending; world.Prepare(pending);
        if (mode == 0) penetrationClear = false;
        if (mode == 1) penetrationChange = true;
        if (mode == 2) { autowallSettings.autowall = false; SilentAimSetSettings(autowallSettings); }
        if (mode == 3) predictedHp = 0;
        if (mode == 4) pathAvailable = false;
        world.History(pending);
        Expect(checksumCalls == 1 && Equal(pending.Buttons(), pending.originalButtons) && shotMetadata.empty(),
            "changed penetration damage settings or source cancels owned shot with checksum repair " + std::to_string(mode));
    }
    world.Reset(); pathClear = false;
    autowallSettings = SilentAimGetSettings(); autowallSettings.autowall = true; autowallSettings.minDamage = 35;
    SilentAimSetSettings(autowallSettings);
    Command weakPenetration; world.Prepare(weakPenetration);
    Expect(recorderCalls == 0 && penetrationCalls > 0 && LastBrake(false), "penetrated damage below minimum cannot fire or quickstop");
    world.Reset();
    autowallSettings = SilentAimGetSettings(); autowallSettings.autowall = true;
    SilentAimSetSettings(autowallSettings);
    Command clearWithAutowall; world.Prepare(clearWithAutowall); world.History(clearWithAutowall);
    Expect(recorderCalls == 1 && penetrationCalls == 0, "direct shots keep the existing fast path with autowall enabled");
    for (bool quickstop : {false, true}) {
        world.Reset();
        auto settings = SilentAimGetSettings(); settings.minDamage = 35; settings.quickstop = quickstop;
        SilentAimSetSettings(settings);
        if (quickstop) weapon.inaccuracy = .4f;
        Command low;
        const auto before = low.command;
        world.Prepare(low);
        Expect(recorderCalls == 0 && low.command == before && LastBrake(false) &&
            std::strstr(SilentAimStatus(), "below minimum damage"),
            "below damage threshold cannot fire or own quickstop; quickstop=" + std::to_string(quickstop));
    }
    world.Reset();
    auto settings = SilentAimGetSettings(); settings.minDamage = 34; SilentAimSetSettings(settings);
    Command equal;
    world.Prepare(equal); world.History(equal);
    Expect(recorderCalls == 1 && shotMetadata.size() == 1 && shotMetadata[0].damageValid &&
        shotMetadata[0].predictedDamage == 34.75f && shotMetadata[0].minDamage == 34 && shotMetadata[0].predictedHitgroup == 2,
        "minimum damage equality publishes copied damage estimate and configured minimum");

    for (int mode = 0; mode < 3; ++mode) {
        world.Reset();
        Command changed;
        world.Prepare(changed);
        if (mode == 0) damageCurrent = false;
        else if (mode == 1) { predictedHp = 0; }
        else damageChangeDuringTrace = true;
        world.History(changed);
        Expect(checksumCalls == 1 && Equal(changed.Buttons(), changed.originalButtons) && shotMetadata.empty(),
            "damage change before/during publication cancels owned attack and regenerates checksum case " + std::to_string(mode));
    }
    world.Reset(); damageAvailable = false;
    Command unavailable;
    world.Prepare(unavailable);
    Expect(recorderCalls == 0 && shotMetadata.empty() && std::strstr(SilentAimStatus(), "damage unavailable"),
        "unknown damage fails automatic eligibility closed");

    for (bool reversed : {false, true}) {
        world.Reset(); multipleTargets = true; reverseCandidates = reversed; rejectedCandidate = 4;
        settings = SilentAimGetSettings(); settings.fov = 180; settings.minDamage = 34; SilentAimSetSettings(settings);
        Command fallback;
        world.Prepare(fallback); world.History(fallback);
        Expect(recorderCalls == 1 && shotMetadata.size() == 1 && shotMetadata[0].targetPawn == 0x8005,
            "low-damage nearest enemy cannot hide eligible alternative; reversed=" + std::to_string(reversed));
    }

    world.Reset();
    settings = SilentAimGetSettings(); settings.minDamage = 100; settings.autofire = false; SilentAimSetSettings(settings);
    Command manual;
    Put(world.InputAddress() + fire::InputPrimaryIndex, 0);
    world.History(manual);
    Expect(shotMetadata.size() == 1 && !shotMetadata[0].automatic && shotMetadata[0].damageValid &&
        Equal(manual.History(), Desired()), "minimum damage does not gate manual shot aiming or prediction capture");

    world.Reset();
    settings = SilentAimGetSettings(); settings.autofire = false; SilentAimSetSettings(settings); damageAvailable = false;
    Command manualUnknown;
    Put(world.InputAddress() + fire::InputPrimaryIndex, 0);
    world.History(manualUnknown);
    Expect(shotMetadata.size() == 1 && !shotMetadata[0].damageValid && Equal(manualUnknown.History(), Desired()),
        "unknown damage remains unknown in manual metadata without suppressing manual aim");
}

void MultihitgroupChecks(World& world) {
    const auto configure = [&] {
        world.Reset(); multiHitgroups = true;
        auto settings = SilentAimGetSettings();
        settings.hitgroups = AimHitgroupBit(AimHitgroup::Head) | AimHitgroupBit(AimHitgroup::Chest);
        settings.hitchance = 0; settings.samples = 256;
        SilentAimSetSettings(settings);
    };
    for (bool reversed : {false, true}) {
        for (bool quickstop : {false, true}) {
            configure(); reverseHitgroups = reversed; weapon.inaccuracy = .1f;
            groupRadius = {7, 20}; groupDamage = {300, 90};
            auto lethalSettings = SilentAimGetSettings();
            lethalSettings.minDamage = 70; lethalSettings.hitchance = 70; lethalSettings.quickstop = quickstop;
            SilentAimSetSettings(lethalSettings);
            Command kill;
            world.Prepare(kill); world.History(kill);
            Expect(shotMetadata.size() == 1 && shotMetadata[0].predictedHitgroup == 1 &&
                shotMetadata[0].hitchance >= 70 && shotMetadata[0].hitchance < 90 && LastBrake(false),
                "full-HP lethal head passing 70/70 outranks 90-damage certain body; reversed=" +
                std::to_string(reversed) + " quickstop=" + std::to_string(quickstop));

            configure(); reverseHitgroups = reversed; weapon.inaccuracy = .1f;
            groupRadius = {7, 20}; groupDamage = {300, 90};
            lethalSettings = SilentAimGetSettings();
            lethalSettings.minDamage = 70; lethalSettings.hitchance = 100;
            SilentAimSetSettings(lethalSettings);
            Command belowThreshold;
            world.Prepare(belowThreshold); world.History(belowThreshold);
            Expect(shotMetadata.size() == 1 && shotMetadata[0].predictedHitgroup == 2,
                "lethal head below hitchance cannot displace eligible nonlethal body");
        }

        configure(); reverseHitgroups = reversed;
        Command powerful;
        world.Prepare(powerful); world.History(powerful);
        Expect(shotMetadata.size() == 1 && shotMetadata[0].predictedHitgroup == 1 && weaponReads == 3,
            "higher expected head damage wins with one weapon sample; reversed=" + std::to_string(reversed));

        configure(); reverseHitgroups = reversed; weapon.inaccuracy = .1f; groupRadius = {1, 8};
        Command reliable;
        world.Prepare(reliable); world.History(reliable);
        Expect(shotMetadata.size() == 1 && shotMetadata[0].predictedHitgroup == 2,
            "reliable chest outranks smaller high-damage head even at greater angle; reversed=" + std::to_string(reversed));

        configure(); reverseHitgroups = reversed; groupDamage = {90, 10};
        auto settings = SilentAimGetSettings(); settings.minDamage = 40; SilentAimSetSettings(settings);
        Command minimum;
        world.Prepare(minimum); world.History(minimum);
        Expect(shotMetadata.size() == 1 && shotMetadata[0].predictedHitgroup == 1,
            "minimum damage excludes otherwise selected chest independently of order");

        configure(); reverseHitgroups = reversed; weapon.inaccuracy = .1f; groupRadius = {1, 8};
        settings = SilentAimGetSettings(); settings.hitchance = 50; settings.quickstop = true;
        SilentAimSetSettings(settings);
        Command ready;
        world.Prepare(ready); world.History(ready);
        Expect(shotMetadata.size() == 1 && shotMetadata[0].predictedHitgroup == 2 && LastBrake(false),
            "shootable chest takes priority over head needing quickstop");

        for (int missing = 0; missing < 2; ++missing) {
            configure(); reverseHitgroups = reversed;
            if (missing == 0) coveredGroup = 0; else unavailableGroup = 0;
            Command fallback;
            world.Prepare(fallback); world.History(fallback);
            Expect(shotMetadata.size() == 1 && shotMetadata[0].predictedHitgroup == 2,
                "covered or unsupported head leaves chest selectable; case=" + std::to_string(missing));
        }
    }

    configure(); weapon.inaccuracy = .1f; groupRadius = {5, 20}; groupDamage = {400, 100};
    Command overkill;
    world.Prepare(overkill); world.History(overkill);
    Expect(shotMetadata.size() == 1 && shotMetadata[0].predictedHitgroup == 2,
        "expected effective damage caps overkill at current health and favors reliable lethal chest");

    configure();
    auto settings = SilentAimGetSettings(); settings.hitgroups = AimHitgroupBit(AimHitgroup::Chest);
    SilentAimSetSettings(settings);
    Command disabledHead;
    world.Prepare(disabledHead); world.History(disabledHead);
    Expect(shotMetadata.size() == 1 && shotMetadata[0].predictedHitgroup == 2,
        "disabled head never enters selection even when damage would rank higher");

    configure();
    Command changed;
    world.Prepare(changed);
    settings = SilentAimGetSettings(); settings.hitgroups = AimHitgroupBit(AimHitgroup::Chest);
    SilentAimSetSettings(settings); world.History(changed);
    Expect(checksumCalls == 1 && shotMetadata.empty() && Equal(changed.Buttons(), changed.originalButtons),
        "mask change cancels pending automatic shot through owned checksum path");

    configure();
    settings = SilentAimGetSettings(); settings.hitgroups = 0; settings.quickstop = true;
    SilentAimSetSettings(settings);
    Command empty;
    world.Prepare(empty);
    Expect(recorderCalls == 0 && LastBrake(false) && std::strstr(SilentAimStatus(), "no hitgroups selected"),
        "explicit empty mask neither aims nor starts quickstop or autofire");

    configure();
    settings = SilentAimGetSettings(); settings.autofire = false; settings.minDamage = 100;
    settings.hitchance = 100; SilentAimSetSettings(settings);
    groupRadius = {1, 8}; weapon.inaccuracy = .1f;
    Command manual;
    Put(world.InputAddress() + fire::InputPrimaryIndex, 0);
    world.History(manual);
    Expect(shotMetadata.size() == 1 && !shotMetadata[0].automatic && shotMetadata[0].predictedHitgroup == 2,
        "manual multigroup aiming ranks expected damage without imposing automatic thresholds");

    configure();
    Command healthChanged;
    world.Prepare(healthChanged); targetHealth = 40; world.History(healthChanged);
    Expect(checksumCalls == 1 && shotMetadata.empty() && Equal(healthChanged.Buttons(), healthChanged.originalButtons),
        "remaining health change invalidates ranked pending shot before publication");

    configure(); healthChangeDuringTrace = true;
    Command healthCallback;
    world.Prepare(healthCallback);
    Expect(recorderCalls == 0 && LastBrake(false),
        "health change during native trace cannot publish an old health-cap ranking");

    configure(); weapon.inaccuracy = .1f; groupRadius = {1, 8};
    settings = SilentAimGetSettings(); settings.hitchance = 100; settings.quickstop = true;
    SilentAimSetSettings(settings);
    Command braking;
    world.Prepare(braking);
    Expect(recorderCalls == 0 && LastBrake(true) && !telemetry.empty() &&
        telemetry.back().find("hitbox=1 ") != std::string::npos,
        "all-below-hitchance candidates choose the best expected-damage clear quickstop fallback");
}

void PerformanceChecks(World& world) {
    for (bool quickstop : {false, true}) {
        world.Reset(); multipleTargets = multiHitgroups = true;
        auto settings = SilentAimGetSettings(); settings.quickstop = quickstop;
        settings.hitgroups = AimHitgroupBit(AimHitgroup::Head) | AimHitgroupBit(AimHitgroup::Chest);
        SilentAimSetSettings(settings); weapon.ready = false; weapon.reason = "primary cooldown";
        Command cooldown;
        world.Prepare(cooldown);
        Expect(weaponReads == 1 && selectionCalls == 0 && volumeReads == 0 && traceCalls == 0 &&
            recorderCalls == 0 && LastBrake(false),
            "cooldown reads weapon once and skips all target/capsule/trace work, releasing quickstop");
        weapon.ready = true; weapon.reason = "ready";
        Command resumed(38);
        world.Prepare(resumed); world.History(resumed);
        Expect(selectionCalls == 1 && volumeReads > 0 && recorderCalls == 1 && shotMetadata.size() == 1,
            "weapon readiness resumes fresh selection immediately without a polling delay");
    }
    world.Reset();
    auto settings = SilentAimGetSettings(); settings.autofire = false; SilentAimSetSettings(settings);
    Command quiet;
    world.Prepare(quiet);
    Expect(selectionCalls == 0 && volumeReads == 0 && traceCalls == 0 && weaponReads == 0 && LastBrake(false),
        "manual mode performs no scans or weapon-model work until real shot history arrives");
    Put(world.InputAddress() + fire::InputPrimaryIndex, 0);
    world.History(quiet);
    Expect(selectionCalls == 1 && weaponReads == 2 && shotMetadata.size() == 1 &&
        Equal(quiet.History(), Desired()), "manual history still receives fresh target ranking and aim");

    for (bool reversed : {false, true}) {
        world.Reset(); multiHitgroups = true; reverseHitgroups = reversed;
        groupDamage = {300, 120};
        settings = SilentAimGetSettings();
        settings.hitgroups = AimHitgroupBit(AimHitgroup::Head) | AimHitgroupBit(AimHitgroup::Chest);
        SilentAimSetSettings(settings);
        Command exactBound;
        world.Prepare(exactBound); world.History(exactBound);
        Expect(shotMetadata.size() == 1 && shotMetadata[0].predictedHitgroup == 1 &&
            traceCalls == (reversed ? 4u : 3u),
            "optimistic lethal bound skips only candidates unable to win, preserving equal-chance angle ties");
    }

    world.Reset(); multiHitgroups = true; groupDamage = {90, 95};
    settings = SilentAimGetSettings(); settings.hitgroups = AimHitgroupBit(AimHitgroup::Head) | AimHitgroupBit(AimHitgroup::Chest);
    SilentAimSetSettings(settings);
    Command nonlethal;
    world.Prepare(nonlethal); world.History(nonlethal);
    Expect(traceCalls == 4 && shotMetadata.size() == 1 && shotMetadata[0].predictedHitgroup == 2,
        "nonlethal winner cannot prune a potentially lethal or higher-damage alternative");

    world.Reset();
    SilentAimTestConfigure(world.Base(), NativeBuilder, NativeRecorder, &CaptureLog);
    Command measured;
    world.Prepare(measured);
    SilentAimFlushTelemetry();
    bool measuredPerformance = false;
    for (const auto& message : telemetry) if (message.find("AIM_PERFORMANCE") != std::string::npos &&
        message.find("spread_patterns=1") != std::string::npos && message.find("last_us=") != std::string::npos)
        measuredPerformance = true;
    Expect(measuredPerformance, "bounded diagnostic flush exposes decision time and spread reuse counters");

    for (bool perfect : {false, true}) {
        world.Reset(); performanceEnemies = 5; multiHitgroups = true;
        groupDamage = {300, 120};
        settings = SilentAimGetSettings(); settings.hitgroups = 0x0f; settings.samples = 1024; settings.fov = 180;
        settings.hitchance = 0; SilentAimSetSettings(settings);
        if (!perfect) weapon.inaccuracy = .1f;
        SilentAimTestConfigure(world.Base(), NativeBuilder, NativeRecorder, &CaptureLog);
        Command fiveEnemies;
        world.Prepare(fiveEnemies); world.History(fiveEnemies); SilentAimFlushTelemetry();
        bool shared = false, bounded = false;
        for (const auto& message : telemetry) if (message.find("AIM_PERFORMANCE") != std::string::npos) {
            shared = message.find("candidates=30 ") != std::string::npos &&
                message.find("spread_patterns=2 ") != std::string::npos;
            bounded = message.find("estimates=2 ") != std::string::npos &&
                message.find("bounded_estimates=29 ") != std::string::npos;
        }
        Expect(shared && shotMetadata.size() == 1 && weaponReads == 3 && volumeReads == 30,
            "candidate scan shares one pattern and model; publication independently refreshes the selected shot");
        if (perfect) Expect(bounded && traceCalls == 3,
            "perfect lethal winner samples one candidate plus its full publication recheck and fresh traces");
    }
}

void MenuCaptureChecks(World& world) {
    world.Reset();
    menuInputCapturing.store(true, std::memory_order_release);
    Command menuOpen;
    world.Prepare(menuOpen); world.History(menuOpen);
    Expect(recorderCalls == 0 && shotMetadata.empty() && LastBrake(false) &&
        Equal(menuOpen.Buttons(), menuOpen.originalButtons),
        "menu capture prevents new automatic attacks, quickstop and aimed publication");
    menuInputCapturing.store(false, std::memory_order_release);
    world.Reset();
    Command pending;
    world.Prepare(pending);
    menuInputCapturing.store(true, std::memory_order_release);
    world.History(pending);
    Expect(checksumCalls == 1 && shotMetadata.empty() && pending.Index() == -1 &&
        Equal(pending.Buttons(), pending.originalButtons) && Equal(pending.History(), Command::PhysicalView),
        "opening menu cancels an already requested automatic attack through native checksum restoration");
    menuInputCapturing.store(false, std::memory_order_release);
    world.Reset();
    Command resumed;
    world.Prepare(resumed); world.History(resumed);
    Expect(recorderCalls == 1 && shotMetadata.size() == 1,
        "closing menu restores automatic command processing without changing feature settings");
}

void VisualizationChecks(World& world) {
    for (int rejection = 0; rejection < 15; ++rejection) {
        world.Reset();
        AimVisualizationSetSettings({true, {.2f, .7f, 1, .9f}});
        if (rejection == 0) weapon.range = 50;
        if (rejection == 1) pathClear = false;
        if (rejection == 2) predictedHp = 0;
        if (rejection == 3) changeDuringTrace = true;
        if (rejection == 4) {
            performanceEnemies = 5; targetHealth = 1;
            auto settings = SilentAimGetSettings(); settings.hitgroups = AimAllHitgroups; settings.fov = 180;
            SilentAimSetSettings(settings);
        }
        if (rejection == 5) weapon.ready = false;
        if (rejection == 6) {
            auto settings = SilentAimGetSettings(); settings.hitgroups = 0; SilentAimSetSettings(settings);
        }
        if (rejection == 7) selectAvailable = false;
        if (rejection == 8) volumeAvailable = false;
        if (rejection == 9) weaponAvailable = false;
        if (rejection == 10) damageAvailable = false;
        if (rejection == 11) pathAvailable = false;
        if (rejection == 12) SilentAimActivate(false);
        if (rejection == 13) weapon.clip = 0;
        if (rejection == 14) weapon.inaccuracy = .2f;
        Command command;
        world.Prepare(command);
        const auto frame = AimVisualizationCapture(GetTickCount64());
        Expect(frame.count == (rejection == 4 ? 30u : rejection == 5 || rejection == 12 || rejection == 14 ? 1u : 0u),
            "damage-path markers ignore cooldown/hitchance/fire contract but respect path/data/range; case " + std::to_string(rejection));
        if (frame.count) Expect(frame.points[0].center.x == 100,
            "eligible visualization uses authored capsule centers even for lower-ranked alternatives");
    }
    for (bool reversed : {false, true}) for (int mixed = 0; mixed < 10; ++mixed) {
        world.Reset(); multiHitgroups = true; reverseHitgroups = reversed;
        AimVisualizationSetSettings({true});
        auto settings = SilentAimGetSettings();
        settings.hitgroups = AimHitgroupBit(AimHitgroup::Head) | AimHitgroupBit(AimHitgroup::Chest);
        if (mixed == 0 || mixed == 5 || mixed == 6) coveredGroup = 0;
        if (mixed == 1) settings.minDamage = 70;
        if (mixed == 2) { weapon.inaccuracy = .1f; groupRadius = {1, 20}; settings.hitchance = 70; }
        if (mixed == 3) unavailableGroup = 0;
        if (mixed == 5 || mixed == 6) settings.autowall = true;
        if (mixed == 6) penetrationClear = false;
        if (mixed == 7) settings.minDamage = 100;
        if (mixed == 8) settings.fov = 7;
        if (mixed == 9) settings.hitgroups = AimHitgroupBit(AimHitgroup::Head);
        SilentAimSetSettings(settings);
        Command command; world.Prepare(command);
        const auto frame = AimVisualizationCapture(GetTickCount64(), &settings);
        const std::size_t expected = mixed == 1 || mixed == 2 || mixed == 4 || mixed == 5 || mixed == 7 ? 2 : 1;
        Expect(frame.count == expected, "only damage-path hitboxes on a mixed target appear before hitchance; case " +
            std::to_string(mixed) + " reversed=" + std::to_string(reversed));
        if (frame.count == 1) Expect(frame.points[0].center.y ==
            (mixed == 8 || mixed == 9 ? 10.0f : 20.0f),
            "marker belongs to the eligible hitbox, not every hitbox on its target");
    }
    for (bool penetrated : {false, true}) for (bool ready : {false, true})
        for (bool automatic : {false, true}) for (bool quickstop : {false, true}) {
        world.Reset(); AimVisualizationSetSettings({true});
        auto settings = SilentAimGetSettings();
        settings.minDamage = 40; settings.autowall = penetrated;
        settings.autofire = automatic; settings.quickstop = quickstop;
        SilentAimSetSettings(settings);
        pathClear = !penetrated; predictedHp = 20; weapon.ready = ready;
        Command command;
        const auto beforeCommand = command.command;
        const auto beforeInput = world.InputBytes();
        world.Prepare(command);
        Expect(AimVisualizationCapture(GetTickCount64(), &settings).count == 1 &&
            (!penetrated || penetrationCalls > 0),
            "20 HP path is visible with minimum damage 40 regardless of penetration/readiness/trigger/quickstop");
        Expect(recorderCalls == 0 && checksumCalls == 0 && shotMetadata.empty() && LastBrake(false) &&
            command.command == beforeCommand && world.InputBytes() == beforeInput,
            "below-minimum preview cannot request an automatic shot, acquire braking or modify input");
    }
    world.Reset(); AimVisualizationSetSettings({true});
    auto brakingSettings = SilentAimGetSettings(); brakingSettings.quickstop = true;
    SilentAimSetSettings(brakingSettings); weapon.inaccuracy = .2f;
    Command braking; world.Prepare(braking);
    Expect(LastBrake(true) && recorderCalls == 0 && AimVisualizationCapture(GetTickCount64()).count == 1,
        "reachable quickstop point is visible while braking before accuracy permits a shot");
    world.Reset(); multiHitgroups = true; AimVisualizationSetSettings({true});
    auto changedSettings = SilentAimGetSettings(); changedSettings.hitgroups = AimAllHitgroups;
    SilentAimSetSettings(changedSettings); changeOnTraceCall = 2;
    Command changedDuringAlternatives; world.Prepare(changedDuringAlternatives);
    Expect(AimVisualizationCapture(GetTickCount64()).count == 0,
        "weapon change while checking alternatives invalidates earlier eligible points");

    // Collecting alternatives must preserve the shot the ordinary scan chooses.
    int baselineGroup = 0; float baselineChance = 0;
    for (bool enabled : {false, true}) {
        world.Reset(); multiHitgroups = true;
        AimVisualizationSetSettings({enabled});
        auto settings = SilentAimGetSettings(); settings.hitgroups = AimAllHitgroups;
        SilentAimSetSettings(settings);
        Command command; world.Prepare(command); world.History(command);
        Expect(shotMetadata.size() == 1, "visualization preserves automatic publication");
        if (shotMetadata.size() == 1) {
            if (!enabled) { baselineGroup = shotMetadata[0].predictedHitgroup; baselineChance = shotMetadata[0].hitchance; }
            else Expect(shotMetadata[0].predictedHitgroup == baselineGroup && shotMetadata[0].hitchance == baselineChance,
                "collecting eligible alternative points preserves winner and hitchance");
        }
    }
    world.Reset(); AimVisualizationSetSettings({true});
    Command first; world.Prepare(first);
    Expect(AimVisualizationCapture(GetTickCount64()).count == 1, "eligible pre-fire scan publishes a center");
    weapon.ready = false;
    Command cooldown(38); world.Prepare(cooldown);
    Expect(AimVisualizationCapture(GetTickCount64()).count == 1 && recorderCalls == 1 && LastBrake(false),
        "damage-path point remains visible during cooldown without another attack or brake");
    for (bool ready : {false, true}) {
        world.Reset(); AimVisualizationSetSettings({true});
        auto settings = SilentAimGetSettings(); settings.autofire = false;
        SilentAimSetSettings(settings); weapon.ready = ready; weapon.inaccuracy = .2f;
        SilentAimTestConfigure(world.Base(), NativeBuilder, NativeRecorder, &CaptureLog);
        Command preview;
        const auto beforeCommand = preview.command;
        const auto beforeInput = world.InputBytes();
        world.Prepare(preview);
        SilentAimFlushTelemetry();
        bool noChanceEstimates = false;
        for (const auto& message : telemetry) if (message.find("AIM_PERFORMANCE") != std::string::npos &&
            message.find("estimates=0 ") != std::string::npos && message.find("spread_patterns=0 ") != std::string::npos)
            noChanceEstimates = true;
        Expect(AimVisualizationCapture(GetTickCount64()).count == 1 && recorderCalls == 0 &&
            checksumCalls == 0 && shotMetadata.empty() && preview.command == beforeCommand && world.InputBytes() == beforeInput &&
            LastBrake(false) && noChanceEstimates,
            "manual-mode preview appears before any shot with no command writes, braking or hitchance work; ready=" + std::to_string(ready));
    }
    world.Reset(); AimVisualizationSetSettings({true});
    Command visible; world.Prepare(visible);
    pathClear = false;
    Command newlyCovered(38); world.Prepare(newlyCovered);
    Expect(AimVisualizationCapture(GetTickCount64()).count == 0, "losing the bullet path removes previous eligible markers");
    world.Reset(); AimVisualizationSetSettings({true});
    Command next; world.Prepare(next);
    SilentAimTestForeground(false);
    Command unfocused(38); world.Prepare(unfocused);
    Expect(AimVisualizationCapture(GetTickCount64()).count == 0, "focus loss clears previous points");
    SilentAimTestForeground(true);
    world.Reset(); AimVisualizationSetSettings({true});
    Command pending; world.Prepare(pending);
    AimVisualizationSetSettings({true, {1, .2f, .3f, .7f}});
    world.History(pending);
    Expect(shotMetadata.size() == 1 && pending.Index() == 0,
        "changing visualization color does not cancel a pending shot");
    world.Reset(); AimVisualizationSetSettings({true});
    Command active; world.Prepare(active);
    SilentAimStop("visualization check");
    Expect(AimVisualizationCapture(GetTickCount64()).count == 0, "cooperative stop clears previous points");
    AimVisualizationSetSettings({});
}

void FreshShotChecks(World& world) {
    world.Reset(); weapon.aimPunch = {5, -7, .25f};
    Command compensated; world.Prepare(compensated); world.History(compensated);
    const auto desired = Desired();
    Expect(compensated.Index() == 0 && Equal(compensated.History(), {desired.pitch - 5, desired.yaw + 7, 0}) &&
        shotMetadata.size() == 1 && Equal(shotMetadata[0].aim, {desired.pitch, desired.yaw, .25f}),
        "outgoing angles compensate native recoil while full hitchance and metadata use the effective bullet basis");
    Expect(shotMetadata.size() == 1 && shotMetadata[0].hits == 64 && shotMetadata[0].samples == 64,
        "compensated zero-spread shot retains the full configured sample count");
    world.Reset(); weapon.aimPunch = {5, -7, .25f};
    auto manualSettings = SilentAimGetSettings(); manualSettings.autofire = false; SilentAimSetSettings(manualSettings);
    Command manualCompensated;
    auto physicalButtons = manualCompensated.originalButtons; physicalButtons.down |= 1;
    Put(Address(manualCompensated.command) + move::command::Buttons, physicalButtons);
    world.Prepare(manualCompensated); world.History(manualCompensated);
    Expect(recorderCalls == 0 && Equal(manualCompensated.Buttons(), physicalButtons) &&
        Equal(manualCompensated.History(), {desired.pitch - 5, desired.yaw + 7, 0}) &&
        shotMetadata.size() == 1 && !shotMetadata[0].automatic,
        "manual aim compensates recoil without owning or suppressing physical fire");
    world.Reset(); manualSettings = SilentAimGetSettings(); manualSettings.autofire = false; SilentAimSetSettings(manualSettings);
    Command manualChanged;
    physicalButtons = manualChanged.originalButtons; physicalButtons.down |= 1;
    Put(Address(manualChanged.command) + move::command::Buttons, physicalButtons);
    modelMutationTrace = 2; traceModelMutation = 3;
    world.Prepare(manualChanged); world.History(manualChanged);
    Expect(Equal(manualChanged.Buttons(), physicalButtons) && Equal(manualChanged.History(), Command::PhysicalView) && shotMetadata.empty(),
        "manual model changed during trace preserves physical fire and original history");
    for (int mode : {1, 2, 4, 5, 7, 8}) {
        world.Reset(); Command changed; world.Prepare(changed); builderMutation = mode;
        world.History(changed);
        Expect(checksumCalls == 1 && Equal(changed.Buttons(), changed.originalButtons) && shotMetadata.empty(),
            "native builder model/origin change cancels formerly eligible shot case " + std::to_string(mode));
    }
    world.Reset(); Command recoilChanged; world.Prepare(recoilChanged); builderMutation = 3;
    world.History(recoilChanged);
    Expect(shotMetadata.size() == 1 && Equal(recoilChanged.History(), {desired.pitch - 5, desired.yaw + 7, 0}),
        "punch changed during native history is refreshed and compensated before publication");
    world.Reset(); Command originChanged; world.Prepare(originChanged); builderMutation = 6;
    world.History(originChanged);
    const auto moved = SilentAimSolve({0,6,0}, {100,10,0}, {}, 30).angles;
    Expect(shotMetadata.size() == 1 && Equal(originChanged.History(), moved) && shotMetadata[0].eye.y == 6,
        "publication re-solves selected capsule from the fresh native shooting origin");
    world.Reset(); nativeOrigin = {0,6,0};
    Command nativeInitial; world.Prepare(nativeInitial); world.History(nativeInitial);
    Expect(shotMetadata.size() == 1 && Equal(nativeInitial.History(), moved) && shotMetadata[0].eye.y == 6,
        "acquisition FOV estimation tracing and logging share the native shooting origin");
    for (int mode : {1, 2, 3, 4, 5, 6, 7, 9}) {
        world.Reset(); Command changed; world.Prepare(changed); traceModelMutation = mode;
        world.History(changed);
        Expect(checksumCalls == 1 && Equal(changed.Buttons(), changed.originalButtons) && shotMetadata.empty(),
            "native model/origin mutation inside final trace cancels owned attack case " + std::to_string(mode));
    }
    for (bool manual : {false, true}) {
        world.Reset(); weapon.aimPunch.pitch = 100;
        auto settings = SilentAimGetSettings(); settings.autofire = !manual; SilentAimSetSettings(settings);
        Command impossible;
        if (manual) Put(world.InputAddress() + fire::InputPrimaryIndex, 0);
        world.Prepare(impossible); world.History(impossible);
        Expect(recorderCalls == 0 && Equal(impossible.History(), Command::PhysicalView) && shotMetadata.empty(),
            "unrepresentable compensation never clamps into a different aim; manual=" + std::to_string(manual));
    }
    for (int mode : {10, 11}) {
        world.Reset(); Command lateInvalidation; world.Prepare(lateInvalidation); modelReadMutation = mode;
        world.History(lateInvalidation);
        Expect(checksumCalls == 1 && Equal(lateInvalidation.Buttons(), lateInvalidation.originalButtons) && shotMetadata.empty(),
            "settings or focus changed inside final native model read prevents publication case " + std::to_string(mode));
    }
    world.Reset(); originAvailable = false;
    Command missing; world.Prepare(missing);
    Expect(recorderCalls == 0 && LastBrake(false), "unavailable native shooting origin cannot acquire autofire or quickstop");
}

void SpreadJumpTransitionChecks(World& world) {
    const auto enable = [&]() {
        auto settings = SilentAimGetSettings(); settings.removeSpread = true;
        SilentAimSetSettings(settings); SilentAimTestSpread(true);
    };
    for (int intent = 0; intent < 3; ++intent) {
        world.Reset(); enable();
        Put(Address(world.pawn) + move::pawn::Flags, move::OnGround);
        Command jump;
        if (intent == 1) jump.JumpTap();
        else if (intent == 0) {
            jump.originalButtons.down |= move::Jump;
            Put(Address(jump.command) + move::command::Buttons, jump.originalButtons);
        }
        Put(Address(jump.base) + move::base::Forward, 180.0f);
        Put(Address(jump.base) + move::base::Left, -35.0f);
        const auto nativeSteps = jump.steps;
        const auto nativePointers = jump.subticks;
        const auto nativeBase = jump.base;
        world.Prepare(jump);
        Expect(recorderCalls == 1, "jump deferral still validates the final publication after an owned request");
        if (intent == 2) {
            auto late = jump.Buttons(); late.down |= move::Jump;
            Put(Address(jump.command) + move::command::Buttons, late);
        }
        world.History(jump);
        auto expectedButtons = jump.originalButtons;
        if (intent == 2) expectedButtons.down |= move::Jump;
        Expect(checksumCalls == 1 && Equal(jump.Buttons(), expectedButtons) && jump.Index() == -1 &&
            Equal(jump.History(), Command::PhysicalView) && spreadSolves == 0 && spreadReads == 0 && shotMetadata.empty(),
            "grounded held/tapped/late jump cancels only owned autofire before spread solving; case=" + std::to_string(intent));
        Expect(jump.base == nativeBase && jump.subticks == nativePointers &&
            !std::memcmp(jump.steps.data(), nativeSteps.data(), sizeof(nativeSteps)),
            "jump deferral preserves native movement values and press/release history exactly");
        bool diagnostic = false;
        for (const auto& message : telemetry) diagnostic |=
            message.find("AIM_SPREAD_TRANSITION revision=4 ") != std::string::npos &&
            message.find("automatic_defer=1 readable=1 flags=0x1 jump_intent=1") != std::string::npos &&
            message.find("inaccuracy=0.0000000") != std::string::npos &&
            message.find("grounded jump transition: awaiting stable shot accuracy") != std::string::npos;
        Expect(diagnostic, "transition telemetry records the exact deferral reason and copied model accuracy");

        Put(Address(world.pawn) + move::pawn::Flags, std::uint32_t{0});
        Command airborne(38);
        airborne.originalButtons.down |= move::Jump;
        Put(Address(airborne.command) + move::command::Buttons, airborne.originalButtons);
        airborne.JumpTap();
        world.Prepare(airborne); world.History(airborne);
        Expect(spreadSolves == 1 && shotMetadata.size() == 1 && checksumCalls == 1 &&
            (airborne.Buttons().down & (move::Jump | 1)) == (move::Jump | 1),
            "following stable airborne command can publish with jump held and has no sticky deferral");
    }
    world.Reset(); enable();
    Put(Address(world.pawn) + move::pawn::Flags, move::OnGround);
    Command grounded; world.Prepare(grounded); world.History(grounded);
    Expect(spreadSolves == 1 && shotMetadata.size() == 1 && checksumCalls == 0,
        "stable grounded command without jump retains automatic spread correction");
    world.Reset(); enable();
    Put(Address(world.pawn) + move::pawn::Flags, move::OnGround);
    Command released; released.JumpTap(); released.steps[0].pressed = false;
    world.Prepare(released); world.History(released);
    Expect(spreadSolves == 1 && shotMetadata.size() == 1 && checksumCalls == 0,
        "a grounded release-only history cannot be mistaken for a fresh jump press");

    for (bool aim : {false, true}) {
        world.Reset(); enable();
        auto settings = SilentAimGetSettings(); settings.enabled = aim; settings.autofire = false; SilentAimSetSettings(settings);
        Put(Address(world.pawn) + move::pawn::Flags, move::OnGround);
        Command manual; manual.JumpTap();
        auto physical = manual.Buttons(); physical.down |= 1 | move::Jump;
        Put(Address(manual.command) + move::command::Buttons, physical);
        Put(world.InputAddress() + fire::InputPrimaryIndex, 0);
        const auto nativeSteps = manual.steps;
        world.History(manual);
        Expect(recorderCalls == 0 && checksumCalls == 0 && spreadSolves == 1 && Equal(manual.Buttons(), physical) &&
            !std::memcmp(manual.steps.data(), nativeSteps.data(), sizeof(nativeSteps)),
            "grounded jump guard leaves manual primary ownership and correction available; aim=" + std::to_string(aim));
    }
    world.Reset();
    Put(Address(world.pawn) + move::pawn::Flags, move::OnGround);
    Command ordinary; ordinary.JumpTap();
    ordinary.originalButtons.down |= move::Jump;
    Put(Address(ordinary.command) + move::command::Buttons, ordinary.originalButtons);
    world.Prepare(ordinary); world.History(ordinary);
    Expect(spreadSolves == 0 && checksumCalls == 0 && shotMetadata.size() == 1 &&
        (ordinary.Buttons().down & (move::Jump | 1)) == (move::Jump | 1),
        "remove spread off preserves existing grounded-jump autofire behavior");

    for (int invalid = 0; invalid < 6; ++invalid) {
        world.Reset(); enable();
        Put(Address(world.pawn) + move::pawn::Flags, move::OnGround);
        Command malformed; malformed.JumpTap();
        if (invalid == 0) Put(Address(malformed.base) + move::base::SubtickSize, -1);
        if (invalid == 1) Put(Address(malformed.base) + move::base::SubtickCapacity, 1);
        if (invalid == 2) Put(Address(malformed.subticks), 3);
        if (invalid == 3) Put(Address(malformed.subticks) + 8, std::uintptr_t{0});
        if (invalid == 4) malformed.steps[0].when = std::numeric_limits<float>::quiet_NaN();
        if (invalid == 5) malformed.steps[0].presence |= 0x8000;
        const auto nativeBase = malformed.base;
        const auto nativeSteps = malformed.steps;
        world.Prepare(malformed); world.History(malformed);
        bool unavailable = false;
        for (const auto& message : telemetry) unavailable |=
            message.find("jump intent unavailable: automatic spread model unavailable") != std::string::npos;
        Expect(checksumCalls == 1 && shotMetadata.empty() && spreadSolves == 0 &&
            Equal(malformed.Buttons(), malformed.originalButtons) && malformed.base == nativeBase &&
            !std::memcmp(malformed.steps.data(), nativeSteps.data(), sizeof(nativeSteps)) && unavailable,
            "unreadable jump-intent layout cancels only owned autofire with an unavailable-model reason; case=" + std::to_string(invalid));
    }
}

void SpreadPipelineChecks(World& world) {
    const auto enable = [&](bool aim, bool automatic) {
        auto settings = SilentAimGetSettings(); settings.enabled = aim; settings.autofire = automatic;
        settings.removeSpread = true; SilentAimSetSettings(settings); SilentAimTestSpread(true);
    };
    const auto physical = [&](Command& command, bool indexed = true) {
        auto buttons = command.originalButtons; buttons.down |= 1;
        Put(Address(command.command) + move::command::Buttons, buttons);
        Put(world.InputAddress() + fire::InputPrimaryIndex, indexed ? 0 : -1);
    };
    world.Reset(); Command off; physical(off); world.History(off);
    Expect(spreadReads == 0 && spreadSolves == 0, "remove spread off never samples or searches the native spread model");

    for (bool aim : {false, true}) {
        world.Reset(); enable(aim, false); selectAvailable = false;
        Command manual; physical(manual);
        const auto buttons = manual.Buttons(); const auto base = manual.baseAngles;
        world.History(manual);
        const auto expected = AimSpreadCompensate(Command::PhysicalView, {}, .15, -.08);
        Expect(spreadSolves == 1 && Equal(manual.History(), expected.command) &&
            Equal(manual.Buttons(), buttons) && manual.baseAngles == base && recorderCalls == 0 && checksumCalls == 0,
            "standalone indexed manual spread correction preserves camera and primary ownership; aim=" + std::to_string(aim));
    }
    world.Reset(); enable(true, false); Command aimed; physical(aimed); world.History(aimed);
    Expect(spreadSolves == 1 && Equal(aimed.History(), AimSpreadCompensate(Desired(), {}, .15, -.08).command),
        "selected manual target receives spread compensation exactly once");
    world.Reset(); enable(false, false); weapon.name = "C_WeaponSSG08"; spreadRawAlternative = true;
    Command rawManual; physical(rawManual); world.History(rawManual);
    Expect(Equal(rawManual.History(), AimSpreadCompensate(Command::PhysicalView, {}, .15, -.08).command),
        "raw-clock standalone Scout preserves selected-time recoil rather than reintroducing decayed clamped punch");
    world.Reset(); enable(false, false); spreadRawAlternative = spreadPunchOverride = true;
    weapon.aimPunch = {.064690515f, -.001253350f, 0};
    spreadSelectedPunch = {1.350812793f, -.027182225f, .000001516f};
    Command nativePunchManual; physical(nativePunchManual); world.History(nativePunchManual);
    Expect(Equal(spreadRequestedAim, {Command::PhysicalView.pitch + spreadSelectedPunch.pitch,
        Command::PhysicalView.yaw + spreadSelectedPunch.yaw, 0}) && spreadSolves == 1 && recorderCalls == 0,
        "standalone correction uses selected native shot punch for both desired unspread direction and solver input");
    world.Reset(); enable(true, false); Command heldAim; physical(heldAim, false); world.History(heldAim);
    Expect(Equal(heldAim.History(), Desired()) && spreadSolves == 0,
        "unsupported held spread history keeps ordinary manual silent aim available");

    world.Reset(); enable(true, true); weapon.inaccuracy = .8f;
    Put(Address(world.pawn) + move::pawn::Velocity, AimVector{10, -20, 30});
    Command automatic; world.Prepare(automatic); world.History(automatic);
    Expect(recorderCalls == 1 && spreadSolves == 1 && checksumCalls == 0 && shotMetadata.size() == 1 &&
        shotMetadata[0].hitchance == 100 && shotMetadata[0].samples == 1 && shotMetadata[0].spreadCorrected &&
        shotMetadata[0].clientSpreadSeed == 0x12345678 && Equal(shotMetadata[0].predictedRay, Desired()),
        "corrected indexed automatic ray can fire below uncorrected cone chance; metadata is one conditional ray");
    bool copiedMovement = false;
    for (const auto& message : telemetry) if (message.find("AIM_SPREAD corrected=1") == 0)
        copiedMovement = message.size() < 1399 && message.find("revision=8 ") != std::string::npos &&
            message.find("model_inaccuracy=0.800000012 model_spread=0 pattern=0 movement_known=1 flags=0x0 velocity=(10.000000,-20.000000,30.000000)") != std::string::npos &&
            message.ends_with("server seed/timing not observed");
    Expect(copiedMovement, "bounded revision-seven spread trace retains exact model accuracy and copied movement without truncation");

    for (bool alternateMiss : {false, true}) {
        world.Reset(); enable(true, true); weapon.name = "C_WeaponSSG08"; weapon.inaccuracy = .4f;
        auto settings = SilentAimGetSettings(); settings.hitchance = 0; SilentAimSetSettings(settings);
        spreadAlternateMiss = alternateMiss;
        Command scout; world.Prepare(scout); world.History(scout);
        Expect(checksumCalls == 0 && shotMetadata.size() == 1 && shotMetadata[0].spreadVariants == 1 &&
            shotMetadata[0].samples == 1 && shotMetadata[0].hits == 1,
            "Scout selected ray authorizes autofire without waiting for the unselected prediction; alternate_miss=" + std::to_string(alternateMiss));
    }

    for (int rejection = 0; rejection < 5; ++rejection) {
        world.Reset(); enable(true, true); Command rejected; world.Prepare(rejected);
        if (rejection == 0) spreadAvailable = false;
        if (rejection == 1) spreadCurrent = false;
        if (rejection == 2) spreadMiss = true;
        if (rejection == 3) spreadMutation = 2;
        if (rejection == 4) { auto settings = SilentAimGetSettings(); settings.removeSpread = false; SilentAimSetSettings(settings); }
        world.History(rejected);
        Expect(checksumCalls == 1 && Equal(rejected.Buttons(), rejected.originalButtons) && shotMetadata.empty(),
            "unavailable, changed or missed spread prediction cancels only owned autofire; case=" + std::to_string(rejection));
    }
    for (int unavailable = 0; unavailable < 4; ++unavailable) {
        world.Reset(); enable(false, false); Command manual; physical(manual, unavailable != 0);
        if (unavailable == 1) spreadAvailable = false;
        if (unavailable == 2) SilentAimTestForeground(false);
        if (unavailable == 3) SilentAimStop("spread lifecycle test");
        const auto buttons = manual.Buttons(); world.History(manual);
        Expect(Equal(manual.History(), Command::PhysicalView) && Equal(manual.Buttons(), buttons),
            "unsupported held history, missing model, focus loss or stop preserves manual input; case=" + std::to_string(unavailable));
    }
}

void SpreadPublicationChecks(World& world) {
    const auto enable = [&](bool aim, bool automatic) {
        auto settings = SilentAimGetSettings(); settings.enabled = aim; settings.autofire = automatic;
        settings.removeSpread = true; SilentAimSetSettings(settings); SilentAimTestSpread(true);
    };
    const auto physical = [&](Command& command) {
        auto buttons = command.originalButtons; buttons.down |= 1;
        Put(Address(command.command) + move::command::Buttons, buttons);
        Put(world.InputAddress() + fire::InputPrimaryIndex, 0);
    };
    const auto afterFire = [&]() {
        --weapon.clip; weapon.recoilIndex += 1; weapon.inaccuracy = .8f;
        weapon.aimPunch = {3, -4, 0}; weapon.currentTime += .01f; weapon.ready = false;
    };
    world.Reset();
    SilentAimTestConfigure(world.Base(), NativeBuilder, NativeRecorder, &CaptureLog);
    enable(true, true);
    builderKeepsAttackIndex = true;
    Command automatic;
    world.Prepare(automatic); world.History(automatic);
    Expect(spreadTolerance > .0002 && spreadBudget == 4096,
        "automatic spread search receives a capsule interior tolerance and the bounded command-time budget");
    const auto applied = automatic.History();
    const auto captured = shotMetadata;
    const auto selectedBefore = selectionCalls;
    afterFire(); builderKeepsHistory = true;
    world.History(automatic);
    Expect(spreadSolves == 1 && Equal(automatic.History(), applied) && shotMetadata.size() == 1 &&
        selectionCalls == selectedBefore && automatic.Index() == 0,
        "completed indexed automatic shot retains its correction during a repeated native history callback");
    builderKeepsHistory = false;
    world.History(automatic);
    Expect(spreadSolves == 1 && Equal(automatic.History(), applied) && shotMetadata.size() == 1 &&
        shotMetadata[0].inaccuracy == captured[0].inaccuracy && shotMetadata[0].spread == captured[0].spread &&
        Equal(shotMetadata[0].aim, captured[0].aim),
        "exact native automatic rebuild replays the original correction and retains pre-fire shot metadata despite changed clip/recoil/readiness");
    const auto commandBefore = automatic.command;
    const auto inputBefore = world.InputBytes();
    world.Prepare(automatic); world.History(automatic);
    Expect(recorderCalls == 1 && spreadSolves == 1 && shotMetadata.size() == 1 && checksumCalls == 0 &&
        automatic.command == commandBefore && world.InputBytes() == inputBefore && Equal(automatic.History(), applied),
        "repeated preparation keeps a completed owned pulse and cannot request or solve the same shot again");
    SilentAimFlushTelemetry();
    bool noStatistics = false, replayReported = false;
    for (const auto& message : telemetry) {
        if (message.find("AIM_PERFORMANCE ") == 0)
            noStatistics = message.find("estimates=0 ") != std::string::npos && message.find("spread_patterns=0 ") != std::string::npos;
        if (message.find("AIM_SPREAD_DIAGNOSTICS ") == 0)
            replayReported = message.find("corrected=1 ") != std::string::npos && message.find("replayed=2 ") != std::string::npos;
    }
    Expect(noStatistics && replayReported,
        "spread correction performs no unused statistical estimates/patterns and reports native replay separately from solved shots");

    for (int invalid = 0; invalid < 6; ++invalid) {
        world.Reset(); enable(true, true); builderKeepsAttackIndex = true;
        Command rebuilt; world.Prepare(rebuilt); world.History(rebuilt);
        AimAngles expected = Command::PhysicalView;
        if (invalid == 0) builderKeepsAttackIndex = false;
        if (invalid == 1) {
            builderKeepsHistory = true; expected = {7, 8, 0};
            Put(Address(rebuilt.historyAngles) + cmd::angle::Pitch, expected);
        }
        if (invalid == 2) Put(Address(rebuilt.entry) + aim_spread_offsets::HistoryTick, 101);
        if (invalid == 3) Put(Address(rebuilt.entry) + cmd::history::Presence, std::uint32_t{0x8001});
        if (invalid == 4) badHistory = true;
        if (invalid == 5) weaponAvailable = false;
        world.History(rebuilt);
        Expect(spreadSolves == 1 && shotMetadata.size() == 1 && checksumCalls == 1 &&
            Equal(rebuilt.Buttons(), rebuilt.originalButtons) && rebuilt.Index() == -1 && Equal(rebuilt.History(), expected),
            "unvalidated automatic rebuild cancels only its completed owned pulse and preserves native history; case=" + std::to_string(invalid));
    }

    for (bool aim : {false, true}) {
        world.Reset(); enable(aim, false); Command manual; physical(manual);
        world.History(manual); const auto correction = manual.History(); const auto metadataCount = shotMetadata.size();
        Expect(spreadTolerance == .0002 && spreadBudget == 8192,
            "manual spread search retains the precise solver tolerance and its normal budget; aim=" + std::to_string(aim));
        afterFire(); builderKeepsHistory = true; world.History(manual);
        Expect(spreadSolves == 1 && Equal(manual.History(), correction) && shotMetadata.size() == metadataCount,
            "already corrected indexed manual history never compounds its desired angle or recaptures metadata; aim=" + std::to_string(aim));
        builderKeepsHistory = false; world.History(manual);
        Expect(spreadSolves == 1 && Equal(manual.History(), correction) && shotMetadata.size() == metadataCount,
            "exact indexed manual native rebuild reuses the original correction after firing; aim=" + std::to_string(aim));
        builderKeepsHistory = true;
        const AimAngles unknown{7, 8, 0};
        Put(Address(manual.historyAngles) + cmd::angle::Pitch, unknown);
        world.History(manual);
        Expect(spreadSolves == 1 && Equal(manual.History(), unknown),
            "unknown views on an already published indexed shot stay untouched; aim=" + std::to_string(aim));
    }

    world.Reset(); enable(false, false); Command relocated; physical(relocated); world.History(relocated);
    const auto relocatedCorrection = relocated.History();
    std::array<unsigned char, 0x38> replacementAngles{};
    Put(Address(replacementAngles) + cmd::angle::Presence, std::uint32_t{0x8007});
    Put(Address(replacementAngles) + cmd::angle::Pitch, Command::PhysicalView);
    Put(Address(relocated.entry) + cmd::history::Angles, Address(replacementAngles));
    world.History(relocated);
    Expect(spreadSolves == 1 && Equal(Get<AimAngles>(Address(replacementAngles) + cmd::angle::Pitch), relocatedCorrection),
        "native history may rebuild into new message storage; replay follows only revalidated callback addresses");

    for (int changed = 0; changed < 8; ++changed) {
        world.Reset(); enable(false, false); Command fresh; physical(fresh); world.History(fresh);
        if (changed == 0) Put(Address(fresh.entry) + aim_spread_offsets::HistoryTick, 101);
        if (changed == 1) Put(Address(fresh.entry) + aim_spread_offsets::HistoryFraction, .75f);
        if (changed == 2) Put(Address(fresh.command) + move::command::Sequence, 38);
        if (changed == 3) { auto settings = SilentAimGetSettings(); settings.fov = 29; SilentAimSetSettings(settings); }
        if (changed == 4) ++weapon.handle;
        if (changed == 5) Put(weapon.identity + aim_weapon_offsets::IdentityScene, 1);
        if (changed == 6) Put(Address(world.pawnIdentity) + cs2_offsets::entity_layout::HandleOffset, std::uint32_t{0x10003});
        if (changed == 7) ++weapon.mode;
        world.History(fresh);
        Expect(spreadSolves == 2,
            "new timestamp, sequence, settings, weapon ownership, scene or source serial receives a fresh indexed solve; case=" + std::to_string(changed));
    }

    world.Reset(); enable(false, false); Command changedTime; physical(changedTime); world.History(changedTime);
    const auto timedCorrection = changedTime.History();
    builderKeepsHistory = true;
    Put(Address(changedTime.entry) + aim_spread_offsets::HistoryTick, 101);
    world.History(changedTime);
    Expect(spreadSolves == 1 && Equal(changedTime.History(), timedCorrection),
        "a changed timestamp with an unreconstructed old corrected view never compounds standalone input");

    world.Reset(); enable(false, false); Command sourceEpoch; physical(sourceEpoch); world.History(sourceEpoch);
    Put(world.Base() + cs2_offsets::globals::dwLocalPlayerPawn, std::uintptr_t{0});
    world.History(sourceEpoch);
    Put(world.Base() + cs2_offsets::globals::dwLocalPlayerPawn, Address(world.pawn));
    world.History(sourceEpoch);
    Expect(spreadSolves == 2,
        "source loss invalidates the publication epoch even when the same pawn address returns");

    world.Reset(); enable(false, false); Command toggled; physical(toggled); world.History(toggled);
    auto settings = SilentAimGetSettings(); settings.removeSpread = false; SilentAimSetSettings(settings);
    world.History(toggled);
    settings.removeSpread = true; SilentAimSetSettings(settings); world.History(toggled);
    Expect(spreadSolves == 2, "an observed spread disable invalidates completed publications before re-enable");

    world.Reset(); enable(false, false); Command stopped; physical(stopped); world.History(stopped);
    const auto stoppedCorrection = stopped.History();
    SilentAimStop("completed spread publication stop regression");
    builderKeepsHistory = true; world.History(stopped);
    Expect(spreadSolves == 1 && Equal(stopped.History(), stoppedCorrection),
        "stop forwards native unchanged history without recomputing a completed correction");
    builderKeepsHistory = false; world.History(stopped);
    Expect(spreadSolves == 1 && Equal(stopped.History(), Command::PhysicalView),
        "stop invalidates replay and leaves a subsequent native rebuild untouched");
}

void SpreadRebuildRecoveryChecks(World& world) {
    // The failing user logs revisit a completed indexed command after native
    // primary/history fields have been cleared. This is a normal rebuild,
    // rather than an unresolved owned write that may stop the aim component.
    for (bool historyPhase : {false, true}) {
        world.Reset(); AimVisualizationSetSettings({true});
        auto settings = SilentAimGetSettings(); settings.removeSpread = true;
        SilentAimSetSettings(settings); SilentAimTestSpread(true);
        builderKeepsAttackIndex = true;
        Command rebuilt(2461); world.Prepare(rebuilt); world.History(rebuilt);
        Expect(shotMetadata.size() == 1 && shotMetadata[0].spreadCorrected,
            "recovery fixture first publishes a corrected automatic shot");
        --weapon.clip; weapon.recoilIndex += 1;
        Put(Address(rebuilt.command) + move::command::Buttons, rebuilt.originalButtons);
        Put(Address(rebuilt.command) + cmd::command::Presence, std::uint32_t{1});
        Put(Address(rebuilt.command) + cmd::command::Attack1Index, -1);
        Put(Address(rebuilt.command) + cmd::command::HistorySize, 0);
        Put(Address(rebuilt.command) + cmd::command::HistoryCapacity, 0);
        Put(Address(rebuilt.repeated) + cmd::repeated::Allocated, 0);
        const auto nativeCommand = rebuilt.command;
        const auto nativeInput = world.InputBytes();
        AimVisualizationClear();
        if (historyPhase) world.History(rebuilt); else world.Prepare(rebuilt);
        world.Prepare(rebuilt); // A retired sequence must not request fire again.
        bool retiredNative = false;
        for (const auto& message : telemetry)
            retiredNative |= message.find("AIM_SPREAD_REBUILD revision=7 sequence=2461 ") == 0 &&
                message.find("reason=native_primary_rebuilt") != std::string::npos;
        Expect(rebuilt.command == nativeCommand && world.InputBytes() == nativeInput &&
            recorderCalls == 1 && spreadSolves == 1 && checksumCalls == 0 && shotMetadata.size() == 1 &&
            retiredNative && !std::strstr(SilentAimStatus(), "stopped") && AimVisualizationCapture(GetTickCount64()).count == 1,
            "empty native rebuild retires old ownership, refreshes points and cannot stop or refire; history_phase=" + std::to_string(historyPhase));
        weaponAvailable = false; world.Prepare(rebuilt);
        weaponAvailable = true; AimVisualizationClear(); world.Prepare(rebuilt);
        Expect(recorderCalls == 1 && spreadSolves == 1 && shotMetadata.size() == 1 &&
            AimVisualizationCapture(GetTickCount64()).count == 1,
            "temporary model loss cannot erase the completed-sequence guard or disable preview when the model returns");
        settings.removeSpread = false; SilentAimSetSettings(settings);
        Command ordinary(2462); world.Prepare(ordinary); world.History(ordinary);
        Expect(recorderCalls == 2 && spreadSolves == 1 && shotMetadata.size() == 2 &&
            !shotMetadata.back().spreadCorrected && AimVisualizationCapture(GetTickCount64()).count == 1,
            "turning Remove spread off after native retirement immediately keeps normal Autofire and points working");
        settings.removeSpread = true; SilentAimSetSettings(settings);
        Command corrected(2463); world.Prepare(corrected); world.History(corrected);
        Expect(recorderCalls == 3 && spreadSolves == 2 && shotMetadata.size() == 3 &&
            shotMetadata.back().spreadCorrected && AimVisualizationCapture(GetTickCount64()).count == 1,
            "fresh command after retirement and toggle cycle can publish a new corrected shot");
    }
    for (bool takeover : {false, true}) {
        world.Reset(); AimVisualizationSetSettings({true});
        auto settings = SilentAimGetSettings(); settings.removeSpread = true;
        SilentAimSetSettings(settings); SilentAimTestSpread(true);
        builderKeepsAttackIndex = true;
        Command retained; world.Prepare(retained); world.History(retained);
        if (takeover) Put(world.InputAddress() + fire::InputNativeButtons, MovementButtons{1, 1, 0});
        const auto nativeCommand = retained.command;
        const auto nativeInput = world.InputBytes();
        AimVisualizationClear(); world.Prepare(retained);
        Expect(recorderCalls == 1 && spreadSolves == 1 && checksumCalls == 0 &&
            retained.command == nativeCommand && world.InputBytes() == nativeInput &&
            AimVisualizationCapture(GetTickCount64()).count == 1,
            "completed sequence refreshes preview without new attack/angle edits, including physical takeover=" + std::to_string(takeover));
        Put(world.InputAddress() + fire::InputNativeButtons, MovementButtons{});
        Command next(38); world.Prepare(next); world.History(next);
        Expect(recorderCalls == 2 && spreadSolves == 2 && shotMetadata.size() == 2,
            "retained or manually taken-over publication cannot disable a later command");
    }
    for (bool historyPhase : {false, true}) {
        world.Reset(); AimVisualizationSetSettings({true});
        auto settings = SilentAimGetSettings(); settings.removeSpread = true;
        SilentAimSetSettings(settings); SilentAimTestSpread(true);
        builderKeepsAttackIndex = true;
        Command missingIndex; world.Prepare(missingIndex); world.History(missingIndex);
        Put(Address(missingIndex.command) + cmd::command::Attack1Index, -1);
        Put(Address(missingIndex.command) + cmd::command::Presence, Command::OriginalPresence);
        builderKeepsAttackIndex = false;
        if (historyPhase) world.History(missingIndex); else world.Prepare(missingIndex);
        Expect(recorderCalls == 1 && spreadSolves == 1 && checksumCalls == (historyPhase ? 1u : 0u) &&
            Equal(missingIndex.Buttons(), missingIndex.originalButtons),
            "unchanged owned pulse with missing index is removed; only post-history retirement requires immediate CRC repair");
        world.Prepare(missingIndex);
        Expect(recorderCalls == 1 && AimVisualizationCapture(GetTickCount64()).count == 1,
            "retired missing-index command refreshes preview without reacquiring attack");
    }
    world.Reset();
    auto settings = SilentAimGetSettings(); settings.removeSpread = true;
    SilentAimSetSettings(settings); SilentAimTestSpread(true);
    builderKeepsAttackIndex = true;
    Command failedChecksum; world.Prepare(failedChecksum); world.History(failedChecksum);
    checksumAvailable = false; builderKeepsAttackIndex = false;
    world.History(failedChecksum);
    Expect(checksumCalls == 1 && std::strstr(SilentAimStatus(), "checksum unavailable; stopped") &&
        Equal(failedChecksum.Buttons(), failedChecksum.originalButtons),
        "real checksum restoration failure still stops aim after removing its owned automatic pulse");
    AimVisualizationSetSettings({});
}

void SpreadRetryChecks(World& world) {
    const auto enable = [&]() {
        auto settings = SilentAimGetSettings(); settings.removeSpread = true;
        SilentAimSetSettings(settings); SilentAimTestSpread(true);
        builderKeepsAttackIndex = true;
    };
    const auto resetPrimary = [&](Command& command) {
        Put(Address(command.command) + move::command::Buttons, command.originalButtons);
        Put(Address(command.command) + cmd::command::Presence, Command::OriginalPresence);
        Put(Address(command.command) + cmd::command::Attack1Index, -1);
        Put(Address(command.command) + cmd::command::HistorySize, 0);
        Put(Address(command.command) + cmd::command::HistoryCapacity, 0);
        Put(Address(command.repeated) + cmd::repeated::Allocated, 0);
    };
    const auto nativeHistory = [&](Command& command) {
        Put(Address(command.command) + cmd::command::HistorySize, 1);
        Put(Address(command.command) + cmd::command::HistoryCapacity, 1);
        Put(Address(command.repeated) + cmd::repeated::Allocated, 1);
    };
    for (bool historyPhase : {false, true}) for (bool decayed : {false, true}) {
        world.Reset(); enable(); weapon.recoilIndex = 2;
        Command retry(15481); world.Prepare(retry); world.History(retry);
        const auto firstCorrection = retry.History();
        resetPrimary(retry);
        if (decayed) weapon.recoilIndex = 1.75f;
        if (historyPhase) {
            builderKeepsHistory = true; world.History(retry);
            Expect(recorderCalls == 1 && spreadSolves == 1 && Equal(retry.History(), firstCorrection),
                "post-history native reset withdraws unfired ownership without interpreting old corrected history");
        }
        // This base view differs from both the original input and correction.
        // A retry must select from it; history is rebuilt by native code later.
        Put(Address(retry.baseAngles) + cmd::angle::Pitch, AimAngles{0, 5, 0});
        world.Prepare(retry);
        Expect(recorderCalls == 2 && withdrawnCommands.size() == 1 &&
            withdrawnCommands[0].sequence == 15481 && withdrawnCommands[0].weapon == weapon.handle &&
            withdrawnCommands[0].source.controller == shotMetadata[0].source.controller &&
            withdrawnCommands[0].source.pawn == shotMetadata[0].source.pawn && checksumCalls == 0 &&
            (retry.Buttons().down & 1) && !std::strstr(SilentAimStatus(), "stopped"),
            "unconsumed native reset immediately records a fresh native press with exact withdrawal keys; history_phase=" +
                std::to_string(historyPhase) + " recoil_decay=" + std::to_string(decayed));
        nativeHistory(retry); builderKeepsHistory = false; world.History(retry);
        Expect(spreadSolves == 2 && shotMetadata.size() == 2 &&
            Equal(retry.History(), AimSpreadCompensate(Desired(), {}, .15, -.08).command),
            "retry publishes a fresh single-model correction without compounding the previous corrected history");
    }
    // Consumption may be observed from another command, then roll back before
    // the completed sequence is revisited. Its guard must remain latched.
    for (int consumed = 0; consumed < 3; ++consumed) {
        world.Reset(); enable(); weapon.recoilIndex = 2;
        Command original(15481); world.Prepare(original); world.History(original);
        if (consumed != 1) --weapon.clip;
        if (consumed != 0) weapon.recoilIndex += 1;
        weapon.ready = false;
        Command observer(15482); world.Prepare(observer);
        weapon.clip = 30; weapon.recoilIndex = 2; weapon.ready = true;
        resetPrimary(original); world.Prepare(original); world.Prepare(original);
        Expect(recorderCalls == 1 && spreadSolves == 1 && withdrawnCommands.empty() &&
            Equal(original.Buttons(), original.originalButtons) && !std::strstr(SilentAimStatus(), "stopped"),
            "ammo/recoil consumption observed on another command stays latched across prediction rollback; signal=" + std::to_string(consumed));
    }
    for (bool revalidation : {false, true}) {
        world.Reset(); enable(); weapon.recoilIndex = 2;
        Command original(15481); world.Prepare(original); world.History(original);
        Command observer(15482); world.Prepare(observer);
        if (revalidation) { weaponReads = 0; modelReadMutation = 12; }
        else { --weapon.clip; weapon.recoilIndex += 1; }
        world.History(observer);
        weapon.clip = 30; weapon.recoilIndex = 2; modelReadMutation = 0;
        resetPrimary(original); world.Prepare(original); world.Prepare(original);
        Expect(recorderCalls == 2 && withdrawnCommands.empty() &&
            Equal(original.Buttons(), original.originalButtons),
            "consumption first observed in another History or model revalidation survives rollback; revalidation=" + std::to_string(revalidation));
    }
    for (int blocked = 0; blocked < 3; ++blocked) {
        world.Reset(); enable();
        Command original(15481); world.Prepare(original); world.History(original);
        const auto applied = original.History();
        // Native resets only button masks: old index/history can remain live.
        Put(Address(original.command) + move::command::Buttons, original.originalButtons);
        if (blocked == 1) selectAvailable = false;
        if (blocked == 2) weapon.ready = false;
        world.Prepare(original);
        builderKeepsHistory = true; world.History(original);
        world.Prepare(original); world.History(original);
        Expect(recorderCalls == 1 && spreadSolves == 1 && shotMetadata.size() == 1 &&
            withdrawnCommands.size() == 1 && Equal(original.History(), applied) &&
            Equal(original.Buttons(), original.originalButtons),
            "failed replacement preserves the retry tombstone and never reinterprets a live old correction; blocker=" + std::to_string(blocked));
        selectAvailable = true; weapon.ready = true;
        resetPrimary(original); world.Prepare(original);
        nativeHistory(original); builderKeepsHistory = false; world.History(original);
        Expect(recorderCalls == 2 && spreadSolves == 2 && shotMetadata.size() == 2,
            "retry tombstone permits a fresh request once native fields and eligibility permit acquisition");
    }
    world.Reset(); enable();
    Command raw(15481); spreadRawAlternative = true; world.Prepare(raw); world.History(raw);
    bool selectedClock = false;
    for (const auto& message : telemetry)
        selectedClock |= message.find("AIM_SPREAD corrected=1") == 0 &&
            message.find("shot_tick=104 shot_fraction=0.250000") != std::string::npos &&
            message.find("selected_clock=1 clamped_time=100/0.500000") != std::string::npos &&
            message.find("shot_punch=(0.000000,0.000000,0.000000)") != std::string::npos;
    Expect(selectedClock, "spread telemetry reports the selected clock and matching recoil while retaining native clamp diagnostics");
}

void NormalPublicationChecks(World& world) {
    for (bool automatic : {false, true}) {
        world.Reset();
        auto settings = SilentAimGetSettings();
        settings.enabled = true; settings.autofire = automatic; settings.removeSpread = false;
        SilentAimSetSettings(settings);
        builderKeepsAttackIndex = true;
        Command shot;
        if (automatic) world.Prepare(shot);
        else {
            auto buttons = shot.originalButtons; buttons.down |= 1;
            Put(Address(shot.command) + move::command::Buttons, buttons);
            Put(world.InputAddress() + fire::InputPrimaryIndex, 0);
        }
        world.History(shot);
        const auto applied = shot.History();
        const auto captured = shotMetadata;
        const auto selectedBefore = selectionCalls;
        --weapon.clip; weapon.recoilIndex += 1; weapon.aimPunch = {1, -1.04f, 0};
        weapon.inaccuracy = .4f; weapon.ready = false;
        builderKeepsHistory = true;
        world.History(shot);
        Expect(captured.size() == 1 && shotMetadata.size() == 1 && Equal(shot.History(), applied) &&
            selectionCalls == selectedBefore,
            "normal published shot retains first aim/metadata after recoil and accuracy advance; automatic=" + std::to_string(automatic));
        builderKeepsHistory = false;
        world.History(shot);
        Expect(shotMetadata.size() == 1 && Equal(shot.History(), applied) &&
            Equal(shotMetadata[0].aim, captured[0].aim) && shotMetadata[0].inaccuracy == captured[0].inaccuracy,
            "normal native history rebuild restores first published view without replacing its saved cone; automatic=" + std::to_string(automatic));
        if (automatic) {
            const auto requestedBefore = recorderCalls;
            world.Prepare(shot); world.History(shot);
            Expect(recorderCalls == requestedBefore && shotMetadata.size() == 1 && !std::strstr(SilentAimStatus(), "stopped"),
                "normal consumed publication cannot acquire another pulse on the same command");
        }
        // A new indexed command is a new decision even after the prior shot.
        weapon.ready = true; weapon.inaccuracy = 0; weapon.aimPunch = {};
        Command next(38);
        if (automatic) world.Prepare(next);
        else Put(world.InputAddress() + fire::InputPrimaryIndex, 0);
        world.History(next);
        Expect(shotMetadata.size() == 2 && shotMetadata.back().commandSequence == 38,
            "normal publication retention does not block the next command; automatic=" + std::to_string(automatic));
    }
}

void NativeHitchancePipelineChecks(World& world) {
    world.Reset();
    auto settings = SilentAimGetSettings(); settings.nativeHitchance = true;
    settings.hitchance = 90; SilentAimSetSettings(settings); SilentAimTestSpread(true);
    weapon.inaccuracy = .4f;
    Command experiment;
    world.Prepare(experiment);
    Expect(recorderCalls == 1 && nativePatternCalls == 1,
        "enabled experiment gates Autofire with native estimate rather than the differing baseline");
    world.History(experiment);
    Expect(shotMetadata.size() == 1 && shotMetadata[0].hitchance == 100 && !shotMetadata[0].spreadCorrected,
        "publication refresh and shot metadata use the selected statistical experiment");
    bool compared = false, identified = false;
    for (const auto& line : telemetry) {
        compared |= line.find("AIM_HITCHANCE_COMPARE ") == 0 && line.find("threshold_disagrees=1") != std::string::npos;
        identified |= line.find("AIM_PUBLICATION ") == 0 && line.find("sequence=37 ") != std::string::npos &&
            line.find("native_hitchance=1 ") != std::string::npos;
    }
    Expect(compared && identified, "experiment reports paired thresholds and a publication with exact command identity");

    world.Reset(); settings = SilentAimGetSettings(); settings.nativeHitchance = true;
    SilentAimSetSettings(settings); SilentAimTestSpread(true); nativePatternAvailable = false;
    Command unavailable; world.Prepare(unavailable);
    Expect(recorderCalls == 0 && shotMetadata.empty(), "unavailable native experiment cannot silently use baseline or publish Autofire");

    world.Reset(); weapon.inaccuracy = .4f; Command baseline; world.Prepare(baseline);
    Expect(nativePatternCalls == 0 && recorderCalls == 0, "default baseline neither invokes nor gates on native experiment");

    world.Reset(); settings = SilentAimGetSettings(); settings.nativeHitchance = settings.removeSpread = true;
    SilentAimSetSettings(settings); SilentAimTestSpread(true); Command corrected;
    world.Prepare(corrected); world.History(corrected);
    Expect(nativePatternCalls == 0 && spreadSolves == 1 && shotMetadata.size() == 1 && shotMetadata[0].spreadCorrected,
        "Remove spread retains its existing indexed solver even with native preference saved");
}

int main() {
    World world;
    FreshShotChecks(world);
    GateChecks(world);
    SparseViewChecks(world);
    PreparationDiagnosticsChecks(world);
    CancellationChecks(world);
    OwnershipChecks(world);
    OcclusionChecks(world);
    WideFovChecks(world);
    QuickstopChecks(world);
    DamageGateChecks(world);
    MultihitgroupChecks(world);
    PerformanceChecks(world);
    MenuCaptureChecks(world);
    VisualizationChecks(world);
    SpreadPipelineChecks(world);
    SpreadJumpTransitionChecks(world);
    SpreadPublicationChecks(world);
    SpreadRebuildRecoveryChecks(world);
    SpreadRetryChecks(world);
    NormalPublicationChecks(world);
    NativeHitchancePipelineChecks(world);
    SilentAimStop("pipeline checks complete");
    std::printf("Aim pipeline checks: %u, failures: %u\n", checks, failures);
    return failures ? 1 : 0;
}
