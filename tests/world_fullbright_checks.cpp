#include "world_fullbright.h"
#include "world_fullbright_offsets.h"
#include "world_settings.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
namespace off = world_fullbright_offsets;
constexpr std::uintptr_t clientBase = 0x10000000, sceneBase = 0x20000000;
unsigned checks = 0, failures = 0, originalCalls = 0, writes = 0;
alignas(16) std::array<unsigned char, 0x5400> view{};
alignas(8) std::array<std::uintptr_t, 64> table{};
std::array<std::uint32_t, 12> normal{}, tools{}, specialized{};
std::uintptr_t nativeDescriptor = 0;
int nativeToolsMode = 3;
bool nativeTools = false, stopInOriginal = false, failWriter = false;
void* seenSource = nullptr;
void* seenOwner = nullptr;
void* seenViewport = nullptr;
void* seenCollection = nullptr;
std::uint32_t seenKey = 0;
HANDLE writerEntered = nullptr, writerResume = nullptr, originalEntered = nullptr, originalResume = nullptr;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", message); }
}
template<class T> void Put(std::size_t at, T value) { std::memcpy(view.data() + at, &value, sizeof(value)); }
int Mode() {
    int value = 0;
    std::memcpy(&value, view.data() + off::SceneContext + off::IntegerCollection, sizeof(value));
    return value;
}
void Mode(int value) { Put(off::SceneContext + off::IntegerCollection, value); }
std::uintptr_t Original(void* source, void* owner, void* viewport) {
    ++originalCalls; seenSource = source; seenOwner = owner; seenViewport = viewport;
    if (originalEntered) { SetEvent(originalEntered); WaitForSingleObject(originalResume, INFINITE); }
    if (stopInOriginal) WorldFullbrightStop("original native selector busy");
    // Capability-disabled CsgoForward does NOT clear the reused output map.
    // A native ToolsVis selection repopulates its own desired tools mode.
    if (nativeTools) Mode(nativeToolsMode);
    return nativeDescriptor;
}
void Writer(void* collection, std::uint32_t key, int value) {
    if (writerEntered) { SetEvent(writerEntered); WaitForSingleObject(writerResume, INFINITE); }
    if (failWriter) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    seenCollection = collection; seenKey = key; ++writes;
    std::memcpy(collection, &value, sizeof(value));
}
void Configure(bool active = true) {
    view.fill(0); table.fill(0); normal.fill(0); tools.fill(0); specialized.fill(0);
    normal[0] = off::DefaultToken; tools[0] = off::ToolsToken; specialized[0] = 0x12345678;
    table[off::SceneContextAccessorSlot / sizeof(void*)] = sceneBase + off::SceneContextAccessor;
    Put(0, reinterpret_cast<std::uintptr_t>(table.data()));
    Mode(0); Put(off::SceneContext + off::IntegerCollection + 4, 42);
    nativeDescriptor = reinterpret_cast<std::uintptr_t>(normal.data());
    nativeTools = stopInOriginal = failWriter = false; writes = originalCalls = 0;
    seenSource = seenOwner = seenViewport = seenCollection = nullptr;
    WorldFullbrightTestConfigure(clientBase, sceneBase, nativeDescriptor,
        reinterpret_cast<std::uintptr_t>(tools.data()), Original, Writer);
    WorldSettings settings{}; settings.fullbrightEnabled = active; WorldSetSettings(settings);
}
std::uintptr_t Select(std::uintptr_t caller = clientBase + off::SelectorReturn, void* owner = view.data()) {
    return WorldFullbrightTestSelect(&nativeToolsMode, owner, &nativeDescriptor, caller);
}
}
int main() {
    Configure(false);
    Mode(7);
    Check(Select() == nativeDescriptor && Mode() == 7 && writes == 0, "never-enabled path preserves native result and output without new writes");
    Configure();
    const auto sourceValue = nativeToolsMode;
    Check(Select() == reinterpret_cast<std::uintptr_t>(tools.data()) && Mode() == 1,
        "capability-disabled native default becomes paired ToolsVis and GPU mode one");
    Check(seenSource == &nativeToolsMode && seenOwner == view.data() && seenViewport == &nativeDescriptor && originalCalls == 1,
        "native original runs once with all three exact arguments");
    Check(seenCollection == view.data() + off::SceneContext + off::IntegerCollection && seenKey == off::ToolsModeKey,
        "only current owner's verified integer collection and shader key are written");
    int otherValue = 0;
    std::memcpy(&otherValue, view.data() + off::SceneContext + off::IntegerCollection + 4, sizeof(otherValue));
    Check(otherValue == 42 && nativeToolsMode == sourceValue && !WorldGetSettings().lightingEnabled,
        "source context and unrelated values remain intact with other lighting disabled");
    for (unsigned i = 0; i < 32; ++i)
        Check(Select() == reinterpret_cast<std::uintptr_t>(tools.data()) && Mode() == 1, "repeated native views preserve coherent descriptor and mode");
    auto settings = WorldGetSettings(); settings.fullbrightEnabled = false; WorldSetSettings(settings);
    Check(Select() == reinterpret_cast<std::uintptr_t>(normal.data()) && Mode() == 0,
        "disable clears our mode in a reused owner even when original skips that key");
    nativeTools = true; nativeDescriptor = reinterpret_cast<std::uintptr_t>(tools.data()); nativeToolsMode = 4;
    const unsigned beforeNativeTools = writes;
    Check(Select() == nativeDescriptor && Mode() == 4 && writes == beforeNativeTools,
        "disabled feature preserves native nondefault ToolsVis selection and value");
    Configure(); nativeTools = true; nativeDescriptor = reinterpret_cast<std::uintptr_t>(tools.data());
    Check(Select() == nativeDescriptor && Mode() == 1, "enabled fullbright pairs with an already-selected native ToolsVis descriptor");
    WorldResetLightingSettings();
    Check(Select() == nativeDescriptor && Mode() == nativeToolsMode, "lighting reset restores native ToolsVis settings on next view");
    Configure(); nativeDescriptor = reinterpret_cast<std::uintptr_t>(specialized.data()); Mode(5);
    Check(Select() == nativeDescriptor && Mode() == 5 && writes == 0, "specialized native rendering descriptors are preserved");
    Configure();
    Check(Select(clientBase + off::SelectorReturn + 1) == nativeDescriptor && writes == 0,
        "unrelated selector callers forward without output mutation");
    Configure(); table[off::SceneContextAccessorSlot / sizeof(void*)] += 1;
    Check(Select() == nativeDescriptor && writes == 0, "unknown native context accessor rejects without writing");
    Configure(); tools[0] ^= 1;
    Check(Select() == nativeDescriptor && writes == 0, "uninitialized or mismatched native descriptor rejects without writing");
    Configure(); failWriter = true;
    Check(Select() == nativeDescriptor && Mode() == 0 && std::strstr(WorldFullbrightStatus(), "write failed"),
        "native writer failure preserves descriptor and reports failure accurately");
    Configure(); Select(); failWriter = true;
    settings = WorldGetSettings(); settings.fullbrightEnabled = false; WorldSetSettings(settings);
    Check(Select() == nativeDescriptor && std::strstr(WorldFullbrightStatus(), "restoration failed"),
        "default restoration failure cannot report successful restoration");
    Configure();
    Check(Select(clientBase + off::SelectorReturn, nullptr) == nativeDescriptor && writes == 0,
        "unreadable current owner forwards native result");
    Configure(); Select(); WorldFullbrightStop("fixture"); WorldFullbrightActivate();
    Check(Select() == reinterpret_cast<std::uintptr_t>(normal.data()) && Mode() == 0 && std::strstr(WorldFullbrightStatus(), "resident"),
        "stop stays disarmed while retained callback restores native default on current owner");
    Configure(); stopInOriginal = true;
    Check(Select() == nativeDescriptor && Mode() == 0 && writes == 0, "stop during native original prevents new fullbright selection");

    Configure();
    writerEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    writerResume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE stopEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr), stopDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(writerEntered && writerResume && stopEntered && stopDone, "writer stop-barrier events created");
    std::uintptr_t pendingResult = 0;
    std::thread producer([&] { pendingResult = Select(); });
    Check(WaitForSingleObject(writerEntered, 5000) == WAIT_OBJECT_0, "paired mode writer entered");
    std::thread stopper([&] { SetEvent(stopEntered); WorldFullbrightStop("paired native writer"); SetEvent(stopDone); });
    WaitForSingleObject(stopEntered, 5000);
    Check(WaitForSingleObject(stopDone, 30) == WAIT_TIMEOUT, "stop drains admitted native mode write");
    SetEvent(writerResume); producer.join(); stopper.join();
    Check(pendingResult == reinterpret_cast<std::uintptr_t>(tools.data()) && Mode() == 1,
        "already-admitted view retains coherent mode/descriptor while stop drains");
    CloseHandle(writerEntered); CloseHandle(writerResume); CloseHandle(stopEntered); CloseHandle(stopDone);
    writerEntered = writerResume = nullptr;
    Check(Select() == nativeDescriptor && Mode() == 0, "next post-stop view uses native default and clears reused mode");

    Configure();
    originalEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    originalResume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread waiting([&] { pendingResult = Select(); });
    Check(WaitForSingleObject(originalEntered, 5000) == WAIT_OBJECT_0, "native selector entered outside stop lock");
    WorldFullbrightStop("native render job wait");
    SetEvent(originalResume); waiting.join();
    Check(pendingResult == nativeDescriptor && writes == 0, "stop does not wait on full native selector or modify its later result");
    CloseHandle(originalEntered); CloseHandle(originalResume); originalEntered = originalResume = nullptr;
    std::printf("world fullbright checks: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
