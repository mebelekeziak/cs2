#include "aim_fire_observer.h"
#include "aim_fire_observer_offsets.h"
#include "aim_weapon_offsets.h"
#include "aim_spread_offsets.h"
#include "silent_aim_offsets.h"
#include "movement_offsets.h"
#include "cs2_offsets.h"
#include "shot_log_settings.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <thread>

namespace {
namespace ent = cs2_offsets;
namespace off = aim_fire_observer_offsets;
namespace wp = aim_weapon_offsets;
namespace input = silent_aim_offsets;
unsigned checks = 0, failures = 0;
void Check(bool result, const char* text) { ++checks; if (!result) { ++failures; std::printf("FAIL %s\n", text); } }
template<class T> std::uintptr_t Address(const T& value) { return reinterpret_cast<std::uintptr_t>(value.data()); }
template<class T> void Put(std::uintptr_t at, const T& value) { std::memcpy(reinterpret_cast<void*>(at), &value, sizeof(value)); }
unsigned char* module = nullptr;
std::uintptr_t client = 0;
constexpr std::uint32_t PawnHandle = 0x18003, ControllerHandle = 0x20004, WeaponHandle = 0x28005;
std::array<unsigned char, 0x2200> pawn{}, weapon{};
std::array<unsigned char, 0x1000> controller{};
std::array<unsigned char, 0x100> services{}, system{}, command{};
std::array<unsigned char, 0x70 * 512> bucket{};
std::array<unsigned char, 0x70> history{};
std::array<unsigned char, 0x38> angle{};
std::array<unsigned char, 0x18> repeated{};
struct GuardedOutput {
    std::uint64_t prefix = 0x123456789abcdef0;
    AimNativeBuiltShot shot{};
    std::uint64_t suffix = 0x123456789abcdef0;
} output, nestedOutput;
AimNativeBuiltShot built{};
struct Mock {
    unsigned calls = 0;
    int mutation = 0;
    bool recurse = false, inside = false, forward = true, foreign = false;
    HANDLE entered = nullptr, resume = nullptr;
} mock;
struct AccuracyMock {
    unsigned spreadCalls = 0, inaccuracyCalls = 0;
    std::uint32_t spreadBits = 0x3ba3d70a, inaccuracyBits = 0x3c23d70a;
    bool forward = true, recurse = false, inside = false, disableLogging = false;
    int mutation = 0;
    float* expectedFirst = nullptr;
    float* expectedSecond = nullptr;
    HANDLE entered = nullptr, resume = nullptr;
} accuracyMock;
std::string telemetry;
void Log(const char* line) { telemetry += line; telemetry += '\n'; }
void* Invoke(std::uintptr_t caller = 0, int mode = 0) {
    return AimFireObserverTestCall(weapon.data(), &output.shot, mode, caller ? caller : client + off::BuilderReturn);
}
void MutateSource(int mutation) {
    switch (mutation) {
    case 1: Put(client + ent::globals::dwLocalPlayerPawn, std::uintptr_t{0}); break;
    case 2: Put(Address(controller) + off::CurrentCommand, std::uintptr_t{0}); break;
    case 3: Put(Address(command) + movement_offsets::command::Sequence, 124); break;
    case 4: Put(Address(bucket) + 5 * 0x70 + ent::entity_layout::HandleOffset, WeaponHandle + 0x8000); break;
    case 5: Put(Address(services) + wp::ActiveWeapon, WeaponHandle + 0x8000); break;
    }
}
void* Native(void* incoming, void* target, int mode) {
    ++mock.calls;
    mock.forward &= incoming == weapon.data() && (target == &output.shot || target == &nestedOutput.shot) && (mode == 0 || mode == 1);
    if (target) std::memcpy(target, &built, sizeof(built));
    if (mock.recurse && !mock.inside) {
        mock.inside = true;
        AimFireObserverTestCall(incoming, &nestedOutput.shot, 0, client + off::BuilderReturn);
        mock.inside = false;
    }
    MutateSource(mock.mutation);
    if (mock.entered) { SetEvent(mock.entered); WaitForSingleObject(mock.resume, INFINITE); }
    return mock.foreign ? reinterpret_cast<void*>(0x111000) : target;
}
void FinishAccuracy() {
    MutateSource(accuracyMock.mutation);
    if (accuracyMock.disableLogging) {
        auto settings = ShotLogGetSettings(); settings.enabled = false; ShotLogSetSettings(settings);
    }
    if (accuracyMock.entered) { SetEvent(accuracyMock.entered); WaitForSingleObject(accuracyMock.resume, INFINITE); }
}
float NativeSpread(void* incoming) {
    ++accuracyMock.spreadCalls; accuracyMock.forward &= incoming == weapon.data();
    if (accuracyMock.recurse && !accuracyMock.inside) {
        accuracyMock.inside = true;
        AimFireObserverTestInaccuracyCall(incoming, nullptr, nullptr, client + off::InaccuracyReturn);
        Invoke();
        accuracyMock.inside = false;
    }
    FinishAccuracy();
    return std::bit_cast<float>(accuracyMock.spreadBits);
}
float NativeInaccuracy(void* incoming, float* first, float* second) {
    ++accuracyMock.inaccuracyCalls;
    accuracyMock.forward &= incoming == weapon.data() && first == accuracyMock.expectedFirst && second == accuracyMock.expectedSecond;
    if (first) *first = 7.5f;
    if (second) *second = -8.25f;
    FinishAccuracy();
    return std::bit_cast<float>(accuracyMock.inaccuracyBits);
}
float AccuracyCall(bool inaccuracy, std::uintptr_t caller = 0, float* first = nullptr, float* second = nullptr) {
    return inaccuracy ? AimFireObserverTestInaccuracyCall(weapon.data(), first, second,
        caller ? caller : client + off::InaccuracyReturn) :
        AimFireObserverTestSpreadCall(weapon.data(), caller ? caller : client + off::SpreadReturn);
}
void Reset() {
    std::memset(module, 0, ent::ClientImageSize);
    pawn.fill(0); weapon.fill(0); controller.fill(0); services.fill(0); system.fill(0); command.fill(0);
    bucket.fill(0); history.fill(0); angle.fill(0); repeated.fill(0);
    mock = {}; accuracyMock = {}; output = {}; nestedOutput = {}; telemetry.clear();
    ShotLogSetSettings({});
    Put(client + ent::globals::dwGameEntitySystem, Address(system));
    Put(client + ent::globals::dwLocalPlayerPawn, Address(pawn));
    Put(client + ent::globals::dwLocalPlayerController, Address(controller));
    Put(Address(system) + ent::entity_layout::TableOffset, Address(bucket));
    const auto entity = [](auto& bytes, unsigned index, std::uint32_t handle) {
        Put(Address(bytes) + ent::CEntityInstance::m_pEntity, Address(bucket) + index * 0x70);
        Put(Address(bucket) + index * 0x70, Address(bytes));
        Put(Address(bucket) + index * 0x70 + ent::entity_layout::HandleOffset, handle);
    };
    entity(pawn, 3, PawnHandle); entity(controller, 4, ControllerHandle); entity(weapon, 5, WeaponHandle);
    Put(Address(pawn) + ent::C_BaseEntity::m_iHealth, 100);
    Put(Address(pawn) + ent::C_BasePlayerPawn::m_hController, ControllerHandle);
    Put(Address(controller) + ent::CCSPlayerController::m_hPlayerPawn, PawnHandle);
    Put(Address(controller) + ent::CBasePlayerController::m_bIsLocalPlayerController, static_cast<unsigned char>(1));
    Put(Address(pawn) + wp::WeaponServices, Address(services));
    Put(Address(services), client + wp::ServicesVtable);
    Put(Address(services) + wp::ServicePawn, Address(pawn));
    Put(Address(services) + wp::ActiveWeapon, WeaponHandle);
    Put(Address(weapon), client + wp::Tables[0].rva);
    Put(Address(weapon) + off::OwnerHandle, PawnHandle);
    Put(Address(controller) + off::CurrentCommand, Address(command));
    Put(Address(command) + movement_offsets::command::Sequence, 123);
    Put(Address(command) + input::command::Attack1Index, 0);
    Put(Address(command) + input::command::HistorySize, 1);
    Put(Address(command) + input::command::HistoryCapacity, 1);
    Put(Address(command) + input::command::HistoryRep, Address(repeated));
    Put(Address(repeated) + input::repeated::Entries, Address(history));
    Put(Address(repeated) + input::repeated::Allocated, 1);
    Put(Address(history) + input::history::Presence, static_cast<std::uint32_t>(input::history::AnglesPresent | aim_spread_offsets::HistoryTimePresent));
    Put(Address(history) + input::history::Angles, Address(angle));
    Put(Address(angle) + input::angle::Pitch, AimAngles{2, 3, 0});
    Put(Address(angle) + input::angle::Presence, input::angle::AllPresent);
    Put(Address(history) + aim_spread_offsets::HistoryTick, 100);
    Put(Address(history) + aim_spread_offsets::HistoryFraction, .25f);
    built = {}; built.tick = 98; built.fraction = .5f; built.nativeTimeValue = 1.5f; built.renderTick = 97; built.renderFraction = .25f;
    built.origin = {5, 6, 7}; built.effective = {4, 5, 0}; built.punch = {2, 2, 0}; built.nativeFlag38 = 1;
    AimFireObserverTestConfigure(client, Native, Log);
    AimFireObserverTestConfigureAccuracy(NativeSpread, NativeInaccuracy);
}
void Basic() {
    Reset();
    const auto pawnBefore = pawn, weaponBefore = weapon;
    const auto controllerBefore = controller;
    Check(Invoke() == &output.shot && mock.calls == 1 && mock.forward, "original arguments/result forwarded exactly once");
    Check(output.prefix == 0x123456789abcdef0 && output.suffix == 0x123456789abcdef0 &&
        !std::memcmp(&output.shot, &built, sizeof(built)), "observer preserves complete caller output and guards");
    Check(pawn == pawnBefore && weapon == weaponBefore && controller == controllerBefore, "observer never changes gameplay entity/command-owner state");
    AimFireObservation sample{};
    Check(AimFireObserverTestPoll(sample) && !AimFireObserverTestPoll(sample), "one copied record is queued per eligible native builder");
    Check(sample.sequence == 123 && sample.controller == ControllerHandle && sample.pawn == PawnHandle && sample.weapon == WeaponHandle,
        "native command sequence and full serialized ownership copied");
    Check(sample.historyKnown && sample.historyIndex == 0 && sample.historyCount == 1 && sample.historyTick == 100 && sample.historyFraction == .25f &&
        sample.historyAngles.pitch == 2 && sample.historyAngles.yaw == 3, "exact native indexed input history copied before original");
    Check(!std::memcmp(&sample.built, &built, sizeof(built)), "actual post-native historical origin, effective angles, recoil and clocks copied");
    Reset(); Invoke(); AimFireObserverFlushTelemetry();
    Check(telemetry.find("AIM_NATIVE_BUILT_SHOT ") != std::string::npos && telemetry.find("sequence=123 ") != std::string::npos &&
        telemetry.find("built_client_shot=1 server_fire=0 observer_only=1") != std::string::npos, "telemetry explicitly distinguishes client construction from server acknowledgement");
}
void Rejections() {
    AimFireObservation sample{};
    for (unsigned mode = 0; mode < 14; ++mode) {
        Reset();
        if (mode == 0) Put(client + ent::globals::dwLocalPlayerPawn, std::uintptr_t{0});
        if (mode == 1) Put(Address(controller) + ent::CCSPlayerController::m_hPlayerPawn, PawnHandle + 0x8000);
        if (mode == 2) Put(Address(pawn) + ent::C_BasePlayerPawn::m_hController, ControllerHandle + 0x8000);
        if (mode == 3) Put(Address(weapon) + off::OwnerHandle, PawnHandle + 0x8000);
        if (mode == 4) Put(Address(services) + wp::ServicePawn, std::uintptr_t{0});
        if (mode == 5) Put(Address(services) + wp::ActiveWeapon, WeaponHandle + 0x8000);
        if (mode == 6) Put(Address(controller) + off::CurrentCommand, std::uintptr_t{0});
        if (mode == 7) Put(Address(command) + movement_offsets::command::Sequence, 0);
        if (mode == 8) Put(Address(weapon), std::uintptr_t{0});
        if (mode == 9) Put(Address(pawn) + ent::C_BaseEntity::m_iHealth, 0);
        if (mode == 10) Put(Address(controller) + ent::CBasePlayerController::m_bIsLocalPlayerController, static_cast<unsigned char>(0));
        if (mode == 11) Put(Address(bucket) + 5 * 0x70 + ent::entity_layout::HandleOffset, WeaponHandle + 0x8000);
        if (mode == 12) Put(Address(services), std::uintptr_t{0});
        if (mode == 13) Put(Address(system) + ent::entity_layout::TableOffset, std::uintptr_t{0});
        Check(Invoke() == &output.shot && mock.calls == 1 && !AimFireObserverTestPoll(sample), "invalid source rejects only observation and still forwards native");
    }
    for (int mutation = 1; mutation <= 5; ++mutation) {
        Reset(); mock.mutation = mutation; Invoke();
        Check(!AimFireObserverTestPoll(sample), "source or command change inside original rejects stale provenance");
    }
    Reset(); Invoke(client + 0x10); Check(!AimFireObserverTestPoll(sample), "unrelated native caller is not a firing construction");
    Reset(); Invoke(0, 1); Check(!AimFireObserverTestPoll(sample) && mock.forward, "alternate mode forwards unchanged without mislabeling primary history");
    Reset(); mock.foreign = true; Check(Invoke() == reinterpret_cast<void*>(0x111000) && !AimFireObserverTestPoll(sample), "foreign result preserved but not copied as output");
    for (int invalid = 0; invalid < 7; ++invalid) {
        Reset();
        if (invalid == 0) built.origin.x = std::numeric_limits<float>::infinity();
        if (invalid == 1) built.effective.yaw = std::numeric_limits<float>::quiet_NaN();
        if (invalid == 2) built.punch.pitch = std::numeric_limits<float>::infinity();
        if (invalid == 3) built.tick = -1;
        if (invalid == 4) built.fraction = 1;
        if (invalid == 5) built.renderFraction = -1;
        if (invalid == 6) built.origin.z = 1000001;
        Invoke(); Check(!AimFireObserverTestPoll(sample), "invalid native result rejects diagnostic without replacing native output");
    }
    Reset(); Put(Address(command) + input::command::Attack1Index, -1); Invoke();
    Check(AimFireObserverTestPoll(sample) && !sample.historyKnown && sample.historyIndex == -1,
        "native continuation remains observable with explicitly unavailable indexed input");
    for (int malformed = 0; malformed < 5; ++malformed) {
        Reset();
        if (malformed == 0) Put(Address(command) + input::command::HistoryCapacity, 0);
        if (malformed == 1) Put(Address(repeated) + input::repeated::Allocated, 0);
        if (malformed == 2) Put(Address(angle) + input::angle::Presence, static_cast<std::uint32_t>(1));
        if (malformed == 3) Put(Address(history) + input::history::Presence, static_cast<std::uint32_t>(1));
        if (malformed == 4) Put(Address(command) + input::command::HistorySize, 257);
        Invoke(); Check(AimFireObserverTestPoll(sample) && !sample.historyKnown,
            "malformed or sparse indexed history is explicitly unknown without dropping native-built output");
    }
    Reset(); built.nativeTimeValue = std::numeric_limits<float>::quiet_NaN(); Invoke();
    Check(AimFireObserverTestPoll(sample), "unused native output fields do not falsely invalidate observed shot geometry");
}
void Lifecycle() {
    Reset();
    auto settings = ShotLogGetSettings(); settings.enabled = false; ShotLogSetSettings(settings);
    Invoke(); AimFireObservation disabled{};
    Check(mock.calls == 1 && !AimFireObserverTestPoll(disabled), "disabled shot logging forwards native without copying per-shot diagnostics");
    Reset(); Invoke(); settings = ShotLogGetSettings(); settings.enabled = false; ShotLogSetSettings(settings);
    AimFireObserverFlushTelemetry();
    Check(telemetry.find("AIM_NATIVE_BUILT_SHOT ") == std::string::npos && !AimFireObserverTestPoll(disabled),
        "turning logging off discards queued details instead of emitting or replaying them later");
    Reset(); mock.recurse = true;
    Invoke(); AimFireObservation sample{}; unsigned records = 0;
    while (AimFireObserverTestPoll(sample)) ++records;
    Check(records == 2 && mock.calls == 2 && mock.forward, "native recursion completes without observer lock and retains independent stack snapshots");
    Reset();
    for (unsigned index = 0; index < 70; ++index) Invoke();
    records = 0; while (AimFireObserverTestPoll(sample)) ++records;
    AimFireObserverFlushTelemetry();
    Check(records == 64 && telemetry.find("dropped=6 ") != std::string::npos, "bounded queue exposes overflow rather than blocking or inventing provenance");
    Reset();
    mock.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr); mock.resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(mock.entered && mock.resume, "native stop-barrier events allocated");
    std::thread callback([] { Invoke(); });
    Check(WaitForSingleObject(mock.entered, 3000) == WAIT_OBJECT_0, "native builder entered before stop");
    std::atomic<bool> done{false};
    std::thread stopping([&] { AimFireObserverStop(); done.store(true, std::memory_order_release); });
    Sleep(30);
    Check(!done.load(std::memory_order_acquire), "cooperative stop includes the original native builder and post-copy lifetime");
    SetEvent(mock.resume); callback.join(); stopping.join();
    Check(done.load() && !AimFireObserverTestPoll(sample), "stop completes after native returns and suppresses post-stop publication");
    CloseHandle(mock.entered); CloseHandle(mock.resume); mock.entered = mock.resume = nullptr;
    const auto before = mock.calls; AimFireObserverActivate(); Invoke();
    Check(mock.calls == before + 1 && !AimFireObserverTestPoll(sample), "stopped resident hook forwards native and cannot be reactivated");
}
void AccuracyBasic() {
    Reset(); Invoke();
    Check(accuracyMock.spreadCalls == 0 && accuracyMock.inaccuracyCalls == 0,
        "builder observation never invokes extra accuracy getters");
    const auto pawnBefore = pawn, weaponBefore = weapon;
    const auto controllerBefore = controller;
    const auto commandBefore = command;
    for (bool inaccuracy : {false, true}) {
        const auto bits = inaccuracy ? accuracyMock.inaccuracyBits : accuracyMock.spreadBits;
        Check(std::bit_cast<std::uint32_t>(AccuracyCall(inaccuracy)) == bits,
            "actual engine accuracy invocation forwards the original float return bits");
        AimNativeAccuracyObservation sample{};
        Check(AimFireObserverTestPollAccuracy(sample) && !AimFireObserverTestPollAccuracy(sample),
            "one record is copied per exact engine accuracy call");
        Check(sample.sequence == 123 && sample.controller == ControllerHandle && sample.pawn == PawnHandle && sample.weapon == WeaponHandle,
            "accuracy observation copies current native command and serialized local ownership");
        Check(sample.component == (inaccuracy ? AimNativeAccuracyComponent::Inaccuracy : AimNativeAccuracyComponent::Spread) &&
            sample.valueBits == bits && std::bit_cast<std::uint32_t>(sample.value) == bits &&
            !sample.firstOutputProvided && !sample.secondOutputProvided,
            "component, native float bits and actual optional argument presence are retained");
    }
    Check(accuracyMock.spreadCalls == 1 && accuracyMock.inaccuracyCalls == 1 && accuracyMock.forward,
        "spread and inaccuracy arguments forwarded exactly once without manufactured samples");
    Check(pawn == pawnBefore && weapon == weaponBefore && controller == controllerBefore && command == commandBefore,
        "accuracy observer leaves all gameplay source and command bytes unchanged");
    Reset(); float first = 1, second = 2;
    accuracyMock.expectedFirst = &first; accuracyMock.expectedSecond = &second;
    const auto value = AccuracyCall(true, 0, &first, &second);
    AimNativeAccuracyObservation sample{};
    Check(accuracyMock.forward && first == 7.5f && second == -8.25f &&
        std::bit_cast<std::uint32_t>(value) == accuracyMock.inaccuracyBits,
        "optional inaccuracy pointers and their native writes/return remain unmodified");
    Check(AimFireObserverTestPollAccuracy(sample) && sample.firstOutputProvided && sample.secondOutputProvided,
        "pointer presence is observable without reading optional output storage");
    Reset(); AccuracyCall(false); AccuracyCall(true); AimFireObserverFlushTelemetry();
    Check(telemetry.find("AIM_NATIVE_ACCURACY ") != std::string::npos && telemetry.find("component=spread ") != std::string::npos &&
        telemetry.find("component=inaccuracy ") != std::string::npos && telemetry.find("sequence=123 ") != std::string::npos &&
        telemetry.find("attack_selector_known=0 attack_selector=-1; native_client_input=1 server_fire=0 observer_only=1") != std::string::npos,
        "accuracy telemetry explicitly states client input evidence and unknown attack selector");
    for (const auto bits : {0u, 0x80000000u, 0x00000001u, 0x3f123456u, 0xbf123456u, 0x7f800000u, 0x7fc12345u}) {
        for (bool inaccuracy : {false, true}) {
            Reset(); accuracyMock.spreadBits = accuracyMock.inaccuracyBits = bits;
            const auto result = AccuracyCall(inaccuracy);
            Check(std::bit_cast<std::uint32_t>(result) == bits && AimFireObserverTestPollAccuracy(sample) && sample.valueBits == bits,
                "native finite, signed-zero, denormal and nonfinite float bits are preserved without filtering gameplay");
        }
    }
}
void AccuracyRejections() {
    for (bool inaccuracy : {false, true}) {
        AimNativeAccuracyObservation sample{};
        Reset();
        AccuracyCall(inaccuracy, client + off::BuilderReturn);
        Check(!AimFireObserverTestPollAccuracy(sample) && accuracyMock.forward,
            "our own or unrelated getter calls cannot be attributed to final engine accuracy");
        for (int invalid = 1; invalid <= 5; ++invalid) {
            Reset(); MutateSource(invalid);
            AccuracyCall(inaccuracy);
            // A different positive sequence is valid provenance, not a source
            // failure; the observer must record the engine's current command.
            const bool queued = AimFireObserverTestPollAccuracy(sample);
            Check((invalid == 3 ? queued && sample.sequence == 124 : !queued) && accuracyMock.forward,
                "accuracy source validation uses real current local command and rejects invalid ownership");
            Reset(); accuracyMock.mutation = invalid;
            AccuracyCall(inaccuracy);
            Check(!AimFireObserverTestPollAccuracy(sample) && accuracyMock.forward,
                "source or command changes during a getter reject only diagnostic provenance");
        }
        Reset(); auto settings = ShotLogGetSettings(); settings.enabled = false; ShotLogSetSettings(settings);
        AccuracyCall(inaccuracy);
        Check(!AimFireObserverTestPollAccuracy(sample) && accuracyMock.forward,
            "disabled logging preserves native accuracy execution without copying details");
        Reset(); accuracyMock.disableLogging = true; AccuracyCall(inaccuracy);
        Check(!AimFireObserverTestPollAccuracy(sample), "logging disabled during a getter suppresses its post-native queued detail");
        Reset(); AccuracyCall(inaccuracy); settings = ShotLogGetSettings(); settings.enabled = false; ShotLogSetSettings(settings);
        AimFireObserverFlushTelemetry();
        Check(telemetry.find("AIM_NATIVE_ACCURACY ") == std::string::npos && !AimFireObserverTestPollAccuracy(sample),
            "logging-off flush discards queued accuracy records instead of replaying them later");
    }
}
void AccuracyLifecycle() {
    Reset(); accuracyMock.recurse = true;
    AccuracyCall(false);
    AimFireObservation builtSample{}; AimNativeAccuracyObservation sample{};
    unsigned records = 0; while (AimFireObserverTestPollAccuracy(sample)) ++records;
    Check(records == 2 && accuracyMock.spreadCalls == 1 && accuracyMock.inaccuracyCalls == 1 &&
        mock.calls == 1 && AimFireObserverTestPoll(builtSample) && accuracyMock.forward && mock.forward,
        "nested builder and both accuracy originals share no observer lock or borrowed stack snapshots");
    Reset();
    for (unsigned index = 0; index < 70; ++index) AccuracyCall(false);
    Invoke(); records = 0; while (AimFireObserverTestPollAccuracy(sample)) ++records;
    AimFireObserverFlushTelemetry();
    Check(records == 64 && telemetry.find("AIM_NATIVE_ACCURACY_OBSERVER calls=70 selected=70 captured=64 rejected=0 dropped=6 ") != std::string::npos &&
        telemetry.find("AIM_NATIVE_BUILT_SHOT ") != std::string::npos,
        "bounded accuracy overflow is exposed without consuming the separate builder queue");
    for (bool inaccuracy : {false, true}) {
        Reset();
        accuracyMock.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        accuracyMock.resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Check(accuracyMock.entered && accuracyMock.resume, "accuracy stop-barrier events allocated");
        std::thread callback([&] { AccuracyCall(inaccuracy); });
        Check(WaitForSingleObject(accuracyMock.entered, 3000) == WAIT_OBJECT_0, "native accuracy original entered before stop");
        std::atomic<bool> done{false};
        std::thread stopping([&] { AimFireObserverStop(); done.store(true, std::memory_order_release); });
        Sleep(30);
        Check(!done.load(std::memory_order_acquire), "cooperative stop includes each accuracy original and its post-copy lifetime");
        SetEvent(accuracyMock.resume); callback.join(); stopping.join();
        Check(done.load() && !AimFireObserverTestPollAccuracy(sample),
            "accuracy stop returns only after blocked original finishes without post-stop publication");
        CloseHandle(accuracyMock.entered); CloseHandle(accuracyMock.resume);
        accuracyMock.entered = accuracyMock.resume = nullptr;
        const auto count = accuracyMock.spreadCalls + accuracyMock.inaccuracyCalls;
        AimFireObserverActivate(); AccuracyCall(inaccuracy);
        Check(accuracyMock.spreadCalls + accuracyMock.inaccuracyCalls == count + 1 && !AimFireObserverTestPollAccuracy(sample),
            "stopped resident accuracy hooks continue native forwarding and cannot rearm");
    }
}
}
int main() {
    module = static_cast<unsigned char*>(VirtualAlloc(nullptr, ent::ClientImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!module) return 2;
    client = reinterpret_cast<std::uintptr_t>(module);
    Basic(); Rejections(); Lifecycle(); AccuracyBasic(); AccuracyRejections(); AccuracyLifecycle();
    VirtualFree(module, 0, MEM_RELEASE);
    std::printf("Native fire observer checks: %u, failures: %u\n", checks, failures);
    return failures ? 1 : 0;
}
