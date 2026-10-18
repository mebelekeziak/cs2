#include "silent_aim_command.h"
#include "silent_aim_offsets.h"
#include "aim_autofire_offsets.h"
#include "movement_offsets.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>

namespace {
namespace fire = aim_autofire_offsets;
namespace cmd = silent_aim_offsets;
int failures = 0, checks = 0, calls = 0, producerMode = 0;
std::uintptr_t mutableCommand = 0;
void Expect(bool value, const char* label) {
    ++checks; failures += !value;
    std::printf("%s %s\n", value ? "PASS" : "FAIL", label);
}
template<class T> std::uintptr_t At(T& object) { return reinterpret_cast<std::uintptr_t>(&object); }
template<class T> void Put(std::uintptr_t address, const T& value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}
template<class T> T Get(std::uintptr_t address) {
    T value{}; std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value)); return value;
}
void Recorder(void* object, int slot) {
    ++calls;
    const auto input = reinterpret_cast<std::uintptr_t>(object);
    const int count = Get<int>(input + fire::InputEventCount);
    const auto event = input + fire::InputEvents + (count - 1) * fire::InputEventStride;
    Expect(slot == 0 && Get<std::uint64_t>(event + fire::EventButton) == 1 &&
        Get<std::uint8_t>(event + fire::EventPressed) == 1, "native recorder sees one slot-zero primary press");
    Put(input + fire::InputPrimaryHeld, std::uint8_t{1});
    Put(input + fire::InputEventsProcessed, count);
    Put(input + fire::InputPrimaryIndex, producerMode == 1 ? -1 : producerMode == 2 ? 2 : 1);
    if (producerMode == 3) {
        auto buttons = Get<MovementButtons>(mutableCommand + movement_offsets::command::Buttons);
        buttons.down |= 1;
        Put(mutableCommand + movement_offsets::command::Buttons, buttons);
    }
}
struct Fixture {
    std::array<unsigned char, 0xC20> input{};
    std::array<unsigned char, 0x90> command{}, base{};
    std::array<unsigned char, 0x38> baseAngles{};
    std::array<std::array<unsigned char, 0x38>, 2> entries{}, angles{};
    std::array<unsigned char, 0x20> rep{};
    AimAutofirePatch patch{};
    AimAutofireDiagnostics diagnostic{};
    Fixture() {
        calls = producerMode = 0; mutableCommand = At(command);
        input.fill(0xA5);
        Put(At(input) + fire::InputNativeButtons, MovementButtons{0x208, 0x200, 0x8});
        Put(At(input) + fire::InputEventCount, 1);
        Put(At(input) + fire::InputEventsProcessed, 0);
        Put(At(input) + fire::InputEvents + fire::EventButton, std::uint64_t{2});
        Put(At(input) + fire::InputPrimaryHeld, std::uint8_t{0});
        Put(At(input) + fire::InputPrimaryIndex, -1);
        Put(At(input) + fire::InputHistoryCount, 2);
        Put(At(command) + movement_offsets::command::Sequence, 17);
        Put(At(command) + movement_offsets::command::Buttons, MovementButtons{0x208, 0x200, 0x8});
        Put(At(command) + cmd::command::Presence, std::uint32_t{0x8000});
        Put(At(command) + cmd::command::Attack1Index, -1);
        Put(At(command) + movement_offsets::command::Base, At(base));
        Put(At(base) + movement_offsets::base::Angles, At(baseAngles));
        Put(At(baseAngles) + cmd::angle::Presence, std::uint32_t{7});
        Put(At(baseAngles) + cmd::angle::Pitch, AimAngles{5, 10, 0});
        Put(At(command) + cmd::command::HistorySize, 2);
        Put(At(command) + cmd::command::HistoryCapacity, 2);
        Put(At(command) + cmd::command::HistoryRep, At(rep));
        Put(At(rep) + cmd::repeated::Allocated, 2);
        for (unsigned index = 0; index < 2; ++index) {
            Put(At(rep) + cmd::repeated::Entries + index * sizeof(std::uintptr_t), At(entries[index]));
            Put(At(entries[index]) + cmd::history::Presence, std::uint32_t{1});
            Put(At(entries[index]) + cmd::history::Angles, At(angles[index]));
            Put(At(angles[index]) + cmd::angle::Presence, std::uint32_t{7});
            Put(At(angles[index]) + cmd::angle::Pitch, AimAngles{static_cast<float>(index), 20, 0});
        }
    }
    bool Start() { return SilentAimStartAutofire(At(input), At(command), &Recorder, patch, &diagnostic); }
    void NativeBuilder() {
        const int index = Get<int>(At(input) + fire::InputPrimaryIndex);
        Put(At(command) + cmd::command::Attack1Index, index);
        Put(At(command) + cmd::command::Presence, Get<std::uint32_t>(At(command) + cmd::command::Presence) |
            cmd::command::Attack1Present);
    }
};
void NativeFlow() {
    Fixture fixture;
    const auto inputBefore = fixture.input;
    const auto commandBefore = fixture.command, baseBefore = fixture.base;
    const auto cameraBefore = fixture.baseAngles;
    AimAngles view{};
    Expect(SilentAimReadView(At(fixture.command), view) && view.pitch == 5 && view.yaw == 10,
        "candidate view is available without any primary attack");
    Expect(fixture.Start() && fixture.patch.active && fixture.patch.inputPending && calls == 1,
        "pre-finalizer request calls the native recorder and claims primary");
    auto expectedInput = inputBefore;
    Put(At(expectedInput) + fire::InputPrimaryIndex, 1);
    Expect(fixture.input == expectedInput, "event bytes/count/scan/held state are restored synchronously");
    const auto buttons = Get<MovementButtons>(At(fixture.command) + movement_offsets::command::Buttons);
    Expect(buttons.down == 0x209 && buttons.changed == 0x201 && buttons.third == 8,
        "only primary down and changed bits are claimed before native CRC");
    Expect(!fixture.Start() && calls == 1 && !std::strcmp(fixture.diagnostic.reason, "previous_request_still_owned"),
        "duplicate request cannot allocate another native history record");
    fixture.NativeBuilder();
    Expect(SilentAimFinishAutofireHistory(fixture.patch, &fixture.diagnostic) && fixture.input == inputBefore &&
        fixture.patch.active && !fixture.patch.inputPending, "builder completion restores the native input index and retains rollback ownership");
    AimShot shot{};
    Expect(SilentAimReadShot(At(fixture.command), shot) && shot.index == 1 &&
        SilentAimWriteShot(shot, {-10, 30, 0}), "native generated attack index feeds the ordinary history read/write path");
    Expect(fixture.base == baseBefore && fixture.baseAngles == cameraBefore, "autofire and history edits preserve base view and camera");
    Put(At(fixture.command) + movement_offsets::command::Buttons,
        MovementButtons{buttons.down | 0x10, buttons.changed | 0x10, buttons.third | 0x10});
    Expect(SilentAimCancelAutofire(fixture.patch, &fixture.diagnostic) && !fixture.patch.active &&
        fixture.diagnostic.commandRestored, "failed post-builder plan can cancel its own native request");
    const auto restored = Get<MovementButtons>(At(fixture.command) + movement_offsets::command::Buttons);
    Expect(restored.down == 0x218 && restored.changed == 0x210 && restored.third == 0x18 &&
        Get<int>(At(fixture.command) + cmd::command::Attack1Index) == -1 &&
        Get<std::uint32_t>(At(fixture.command) + cmd::command::Presence) == 0x8000,
        "cancel restores owned primary/index presence and preserves later unrelated actions");
    Expect(SilentAimCancelAutofire(fixture.patch), "request cancellation is idempotent");
    Expect(commandBefore != fixture.command, "later unrelated actions remain after cancellation");
}
void Preservation() {
    for (int field = 0; field < 3; ++field) {
        Fixture fixture;
        auto buttons = Get<MovementButtons>(At(fixture.command) + movement_offsets::command::Buttons);
        (&buttons.down)[field] |= 1;
        Put(At(fixture.command) + movement_offsets::command::Buttons, buttons);
        const auto before = fixture.command;
        Expect(!fixture.Start() && calls == 0 && fixture.command == before,
            "manual primary down/changed/third remains byte-identical");
    }
    {
        Fixture fixture;
        Put(At(fixture.command) + cmd::command::Presence, std::uint32_t{cmd::command::Attack1Present});
        Put(At(fixture.command) + cmd::command::Attack1Index, 0);
        Expect(!fixture.Start() && calls == 0, "native completed tap index preserves manual primary");
    }
    for (int condition = 0; condition < 5; ++condition) {
        Fixture fixture;
        if (condition == 0) Put(At(fixture.input) + fire::InputPrimaryHeld, std::uint8_t{1});
        if (condition == 1) Put(At(fixture.input) + fire::InputPrimaryIndex, 0);
        if (condition == 2) Put(At(fixture.input) + fire::InputEvents + fire::EventButton, std::uint64_t{1});
        if (condition == 3) Put(At(fixture.input) + fire::InputNativeButtons, MovementButtons{1, 0, 0});
        if (condition == 4) Put(At(fixture.input) + fire::InputEventCount, 32);
        const auto before = fixture.input;
        const auto commandBefore = fixture.command;
        Expect(!fixture.Start() && calls == 0 && fixture.input == before && fixture.command == commandBefore,
            "native held/index/event/down or full event capacity prevents a synthetic request");
    }
    {
        Fixture fixture;
        Expect(fixture.Start(), "start request before physical takeover");
        fixture.NativeBuilder();
        Expect(SilentAimFinishAutofireHistory(fixture.patch), "finish native request before physical takeover");
        Put(At(fixture.input) + fire::InputNativeButtons, MovementButtons{1, 1, 0});
        const auto before = fixture.command;
        Expect(!SilentAimCancelAutofire(fixture.patch, &fixture.diagnostic) && fixture.command == before &&
            !std::strcmp(fixture.diagnostic.reason, "manual_primary_takeover"), "cancellation cannot clear an identical physical press");
        Expect(SilentAimRestoreAutofireRebuild(At(fixture.input), At(fixture.command), fixture.patch, &fixture.diagnostic) &&
            !fixture.patch.active && fixture.command == before, "rebuild retires ownership after manual takeover without writes");
    }
}
void FailureAndRebuild() {
    for (int mode = 1; mode <= 2; ++mode) {
        Fixture fixture; producerMode = mode;
        const auto before = fixture.input;
        const auto commandBefore = fixture.command;
        Expect(!fixture.Start() && calls == 1 && fixture.input == before && fixture.command == commandBefore &&
            !fixture.patch.active, "unavailable or out-of-range native attack index rolls back every request byte");
    }
    {
        Fixture fixture; producerMode = 3;
        const auto before = fixture.input;
        Expect(!fixture.Start() && fixture.input == before &&
            (Get<MovementButtons>(At(fixture.command) + movement_offsets::command::Buttons).down & 1),
            "native primary changes during recorder call are preserved");
    }
    {
        Fixture fixture;
        Expect(fixture.Start(), "request before exact command rebuild");
        fixture.NativeBuilder();
        Expect(SilentAimFinishAutofireHistory(fixture.patch), "complete source restoration before exact rebuild");
        Expect(SilentAimRestoreAutofireRebuild(At(fixture.input), At(fixture.command), fixture.patch, &fixture.diagnostic) &&
            !fixture.patch.active && !(Get<MovementButtons>(At(fixture.command) + movement_offsets::command::Buttons).down & 1),
            "same command rebuild removes only the prior owned pulse");
    }
    {
        Fixture fixture;
        Expect(fixture.Start() && SilentAimFinishAutofireHistory(fixture.patch), "request before native primary reset");
        Put(At(fixture.command) + movement_offsets::command::Buttons, fixture.patch.before);
        const auto before = fixture.command;
        Expect(SilentAimRestoreAutofireRebuild(At(fixture.input), At(fixture.command), fixture.patch, &fixture.diagnostic) &&
            !fixture.patch.active && fixture.command == before && !std::strcmp(fixture.diagnostic.reason, "native_primary_rebuilt"),
            "fresh native primary fields abandon copied ownership without edits");
    }
    {
        Fixture fixture;
        fixture.patch.active = true; fixture.patch.input = fixture.patch.command = 1;
        Expect(SilentAimRestoreAutofireRebuild(At(fixture.input), At(fixture.command), fixture.patch, &fixture.diagnostic) &&
            !fixture.patch.active, "current address mismatch never dereferences stale cached pointers");
    }
    {
        Fixture fixture;
        Expect(fixture.Start() && SilentAimFinishAutofireHistory(fixture.patch), "request before sequence replacement");
        Put(At(fixture.command) + movement_offsets::command::Sequence, 18);
        const auto before = fixture.command;
        Expect(SilentAimRestoreAutofireRebuild(At(fixture.input), At(fixture.command), fixture.patch, &fixture.diagnostic) &&
            fixture.command == before && !fixture.patch.active, "new command sequence abandons copied ownership without edits");
    }
    {
        Fixture fixture;
        void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        Expect(page != nullptr, "allocate native input page for writable preflight");
        if (page) {
            std::memcpy(page, fixture.input.data(), fixture.input.size());
            DWORD old = 0;
            const bool protectedPage = VirtualProtect(page, 4096, PAGE_READONLY, &old) != 0;
            const auto before = fixture.command;
            Expect(protectedPage && !SilentAimStartAutofire(reinterpret_cast<std::uintptr_t>(page), At(fixture.command),
                &Recorder, fixture.patch, &fixture.diagnostic) && calls == 0 && fixture.command == before,
                "readonly native storage fails preflight before recorder or attack mutation");
            VirtualFree(page, 0, MEM_RELEASE);
        }
    }
    {
        Fixture fixture;
        void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        Expect(page != nullptr, "allocate native input page for release-failure regression");
        if (page) {
            std::memcpy(page, fixture.input.data(), fixture.input.size());
            const auto input = reinterpret_cast<std::uintptr_t>(page);
            Expect(SilentAimStartAutofire(input, At(fixture.command), &Recorder, fixture.patch, &fixture.diagnostic),
                "request owns a native index before release storage becomes readonly");
            DWORD old = 0;
            const bool protectedPage = VirtualProtect(page, 4096, PAGE_READONLY, &old) != 0;
            fixture.diagnostic.commandRestored = true;
            Expect(protectedPage && !SilentAimFinishAutofireHistory(fixture.patch, &fixture.diagnostic) &&
                fixture.patch.inputPending && !fixture.diagnostic.commandRestored &&
                Get<int>(input + fire::InputPrimaryIndex) == 1,
                "input release failure retains ownership and clears stale command-restored diagnostics");
            Expect(!SilentAimCancelAutofire(fixture.patch, &fixture.diagnostic) && fixture.diagnostic.commandRestored &&
                !(Get<MovementButtons>(At(fixture.command) + movement_offsets::command::Buttons).down & 1) &&
                fixture.patch.active && fixture.patch.inputPending,
                "source release failure still removes owned command primary and reports unresolved source ownership");
            DWORD previous = 0;
            const bool writable = VirtualProtect(page, 4096, PAGE_READWRITE, &previous) != 0;
            Expect(writable && SilentAimFinishAutofireHistory(fixture.patch) &&
                Get<int>(input + fire::InputPrimaryIndex) == -1, "source index release can complete after writable storage returns");
            fixture.patch = {};
            VirtualFree(page, 0, MEM_RELEASE);
        }
    }
}
}
int main() {
    NativeFlow(); Preservation(); FailureAndRebuild();
    std::printf("Autofire command checks: %d; failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
