#include "world_fullbright.h"
#include "world_fullbright_offsets.h"
#include "world_settings.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
namespace off = world_fullbright_offsets;
constexpr std::uintptr_t base = 0x10000000;
struct Integers { int direct, indirect, marker, unrelated; };
struct Source {
    alignas(16) std::array<unsigned char, off::SourceRootToIntegers> vectors;
    Integers integers;
};
static_assert(offsetof(Source, integers) == off::SourceRootToIntegers);
unsigned checks = 0, failures = 0, markerCalls = 0, floatWrites = 0, directWrites = 0, indirectWrites = 0;
std::uint32_t lastMarkerKey = 0;
int lastMarkerValue = 0;
void* lastMarkerCollection = nullptr;
void* lastFloatRoot = nullptr;
bool failVector = false, blockNative = false;
HANDLE vectorEntered = nullptr, vectorResume = nullptr, nativeEntered = nullptr, nativeResume = nullptr;
std::array<char, 16> order{};
unsigned orderSize = 0;
void Check(bool value, const char* label) { ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", label); } }
std::array<float, 4> Ambient(const Source& source) {
    std::array<float, 4> value{}; std::memcpy(value.data(), source.vectors.data(), sizeof(value)); return value;
}
void BeginNativeFrame(Source& source, int direct = 1, int indirect = 1, std::array<float, 4> ambient = {0,0,0,1}) {
    // Independent fixture for the ordinary producer's existing local source:
    // each invocation resets native ambient and current direct/indirect prefs.
    source.vectors.fill(0x5A);
    std::memcpy(source.vectors.data(), ambient.data(), sizeof(ambient));
    source.integers = {direct, indirect, -99, 42};
}
void Writer(void* collection, std::uint32_t key, int value) {
    auto& integers = *static_cast<Integers*>(collection);
    if (key == off::DirectKey) { ++directWrites; integers.direct = value; if (orderSize < order.size()) order[orderSize++] = 'D'; }
    else if (key == off::IndirectKey) { ++indirectWrites; integers.indirect = value; if (orderSize < order.size()) order[orderSize++] = 'I'; }
    else {
        if (blockNative && nativeEntered) { SetEvent(nativeEntered); WaitForSingleObject(nativeResume, INFINITE); }
        ++markerCalls; lastMarkerKey = key; lastMarkerValue = value; lastMarkerCollection = collection;
        if (key == off::FullbrightKey) integers.marker = value; else integers.unrelated = value;
        if (orderSize < order.size()) order[orderSize++] = 'M';
    }
}
void VectorWriter(void* root, std::uint32_t key, const float* value) {
    if (vectorEntered) { SetEvent(vectorEntered); WaitForSingleObject(vectorResume, INFINITE); }
    if (failVector) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    lastFloatRoot = root; ++floatWrites;
    Check(key == off::AmbientKey, "native float4 writer uses the independently named ambient key");
    std::memcpy(root, value, 4 * sizeof(float));
    if (orderSize < order.size()) order[orderSize++] = 'A';
}
void Configure(bool active = true) {
    markerCalls = floatWrites = directWrites = indirectWrites = 0;
    lastMarkerCollection = lastFloatRoot = nullptr; orderSize = 0; order.fill(0);
    failVector = blockNative = false;
    WorldFullbrightTestConfigure(base, Writer, VectorWriter);
    WorldSettings settings{}; settings.fullbrightEnabled = active; WorldSetSettings(settings);
}
void Set(Source& source, int value = 0, std::uint32_t key = off::FullbrightKey, std::uintptr_t caller = base + off::ClientSetterReturn) {
    WorldFullbrightTestSet(&source.integers, key, value, caller);
}
}
int main() {
    Source source{};
    Configure(false); BeginNativeFrame(source, 0, 1, {.2f,.3f,.4f,1});
    const auto initialVectors = source.vectors;
    Set(source, 3);
    Check(Ambient(source) == std::array<float,4>{.2f,.3f,.4f,1} && source.integers.direct == 0 && source.integers.indirect == 1,
        "disabled source preserves native lighting preferences");
    Check(markerCalls == 1 && source.integers.marker == 3 && floatWrites == 0 && directWrites == 0 && indirectWrites == 0,
        "disabled path forwards original marker exactly once with no lighting writes");
    Configure(); BeginNativeFrame(source);
    Check(WorldFullbrightTestSourceRoot(&source.integers), "verified source root belongs to current producer stack");
    Set(source);
    Check(Ambient(source) == std::array<float,4>{1,1,1,1} && source.integers.direct == 0 && source.integers.indirect == 0,
        "native fullbright-one recipe supplies white ambient and disables both lighting classes");
    Check(lastFloatRoot == &source && lastMarkerCollection == &source.integers,
        "float4 root and integer collection use their distinct verified ABI receivers");
    Check(markerCalls == 1 && source.integers.marker == 0 && lastMarkerValue == 0,
        "native rendering-mode marker remains zero instead of forcing ToolsVis");
    Check(orderSize == 4 && std::memcmp(order.data(), "ADIM", 4) == 0,
        "complete lighting recipe is copied before the original marker returns");
    Check(!WorldGetSettings().lightingEnabled && source.integers.unrelated == 42,
        "fullbright is independent of other lighting controls and preserves unrelated values");
    Check(std::memcmp(source.vectors.data() + 16, initialVectors.data() + 16, source.vectors.size() - 16) == 0,
        "only native ambient value storage changes in the source-root fixture");
    for (int nativeMarker : {-1,0,1,2,3,4,1000}) {
        BeginNativeFrame(source); Set(source, nativeMarker);
        Check(source.integers.marker == nativeMarker && lastMarkerValue == nativeMarker,
            "all native marker values remain unchanged while lighting recipe applies");
        BeginNativeFrame(source); Set(source, nativeMarker, off::FullbrightKey + 1);
        Check(Ambient(source) == std::array<float,4>{0,0,0,1} && source.integers.direct == 1 && source.integers.indirect == 1,
            "unrelated parameter keys do not authorize the lighting recipe");
        BeginNativeFrame(source); Set(source, nativeMarker, off::FullbrightKey, base + off::ClientSetterReturn + 1);
        Check(Ambient(source) == std::array<float,4>{0,0,0,1} && source.integers.direct == 1 && source.integers.indirect == 1,
            "other native callers retain their own source inputs");
    }
    auto settings = WorldGetSettings(); settings.fullbrightEnabled = false; WorldSetSettings(settings);
    const auto writesBeforeDisable = floatWrites;
    BeginNativeFrame(source, 1, 0, {.1f,.5f,.2f,1}); Set(source, 0);
    Check(Ambient(source) == std::array<float,4>{.1f,.5f,.2f,1} && source.integers.direct == 1 && source.integers.indirect == 0 && floatWrites == writesBeforeDisable,
        "next native producer restores current preferences on disable without cached scene pointers");
    Configure(); BeginNativeFrame(source); Set(source); WorldResetLightingSettings();
    BeginNativeFrame(source); Set(source);
    Check(Ambient(source) == std::array<float,4>{0,0,0,1} && source.integers.direct == 1 && source.integers.indirect == 1,
        "lighting reset restores normal native frame inputs");
    std::array<unsigned char, 0x200> *heapSource = new std::array<unsigned char, 0x200>{};
    Check(!WorldFullbrightTestSourceRoot(heapSource->data() + off::SourceRootToIntegers), "source ownership rejects a heap context at this stack-only caller");
    delete heapSource;
    Check(!WorldFullbrightTestSourceRoot(nullptr), "null source ownership rejects safely");
    Configure(); BeginNativeFrame(source); failVector = true; Set(source);
    Check(markerCalls == 1 && source.integers.marker == 0 && std::strstr(WorldFullbrightStatus(), "failed"),
        "native ambient writer failure is reported and original marker still forwards once");
    Configure(); BeginNativeFrame(source); Set(source); WorldFullbrightStop("fixture"); WorldFullbrightActivate();
    BeginNativeFrame(source); Set(source);
    Check(Ambient(source) == std::array<float,4>{0,0,0,1} && source.integers.direct == 1 && std::strstr(WorldFullbrightStatus(), "resident"),
        "cooperative stop remains disarmed and future frames retain native inputs");

    Configure();
    vectorEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr); vectorResume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE stopping = CreateEventW(nullptr, TRUE, FALSE, nullptr), stopped = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(vectorEntered && vectorResume && stopping && stopped, "source-writer barrier events created");
    Source completed{};
    std::thread producer([&] { Source ownStack{}; BeginNativeFrame(ownStack); Set(ownStack); completed = ownStack; });
    Check(WaitForSingleObject(vectorEntered, 5000) == WAIT_OBJECT_0, "native source writer entered on its actual owner stack");
    std::thread stopper([&] { SetEvent(stopping); WorldFullbrightStop("source write in flight"); SetEvent(stopped); });
    WaitForSingleObject(stopping, 5000);
    Check(WaitForSingleObject(stopped, 30) == WAIT_TIMEOUT, "stop drains the admitted lighting recipe");
    SetEvent(vectorResume); producer.join(); stopper.join();
    Check(Ambient(completed) == std::array<float,4>{1,1,1,1} && completed.integers.direct == 0 && completed.integers.indirect == 0 && completed.integers.marker == 0,
        "in-flight view completes a coherent lighting recipe and exact native marker");
    CloseHandle(vectorEntered); CloseHandle(vectorResume); CloseHandle(stopping); CloseHandle(stopped);
    vectorEntered = vectorResume = nullptr;
    BeginNativeFrame(source); Set(source);
    Check(Ambient(source) == std::array<float,4>{0,0,0,1} && source.integers.direct == 1 && source.integers.indirect == 1,
        "post-stop frame needs no deferred scene restoration or zero-mode normalizer");

    Configure(false); blockNative = true;
    nativeEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr); nativeResume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread nativeWork([&] { Source ownStack{}; BeginNativeFrame(ownStack); Set(ownStack); });
    Check(WaitForSingleObject(nativeEntered, 5000) == WAIT_OBJECT_0, "ordinary forwarding callback entered native writer");
    WorldFullbrightStop("ordinary forwarding"); SetEvent(nativeResume); nativeWork.join();
    Check(floatWrites == 0 && directWrites == 0 && indirectWrites == 0, "stop does not wait on unrelated unmodified forwarding work");
    CloseHandle(nativeEntered); CloseHandle(nativeResume); nativeEntered = nativeResume = nullptr;
    std::printf("world fullbright checks: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
