#pragma once
#include "silent_aim_math.h"
#include <cstdint>

using AimFireObserverLog = void (*)(const char*);
// Exact caller-owned result of the installed native shot builder. This is
// client construction/prediction evidence, never a server acknowledgement.
struct AimNativeBuiltShot {
    int tick = 0;
    float fraction = 0, nativeTimeValue = 0;
    int renderTick = 0;
    float renderFraction = 0;
    AimVector origin{};
    AimAngles effective{}, punch{};
    unsigned char nativeFlag38 = 0, nativeFlag39 = 0, padding[2]{};
    int originKind = 0;
};
struct AimFireObservation {
    std::uint64_t receiptMs = 0;
    std::uint32_t controller = UINT32_MAX, pawn = UINT32_MAX, weapon = UINT32_MAX;
    int sequence = 0, mode = 0, historyIndex = -1, historyCount = 0;
    bool historyKnown = false;
    int historyTick = 0;
    float historyFraction = 0;
    AimAngles historyAngles{};
    AimNativeBuiltShot built{};
};
enum class AimNativeAccuracyComponent : unsigned { Spread, Inaccuracy };
struct AimNativeAccuracyObservation {
    std::uint64_t receiptMs = 0;
    std::uint32_t controller = UINT32_MAX, pawn = UINT32_MAX, weapon = UINT32_MAX;
    int sequence = 0;
    AimNativeAccuracyComponent component = AimNativeAccuracyComponent::Spread;
    float value = 0;
    std::uint32_t valueBits = 0;
    bool firstOutputProvided = false, secondOutputProvided = false;
    // Optional copied state at the actual getter return. Failure to copy it
    // leaves the original native value/provenance available and never gates fire.
    bool movementStateKnown = false;
    std::uint32_t movementFlags = 0;
    AimVector velocity{};
};
// Prepare/activate outside DllMain. The hook only observes actual native calls;
// no builder/getter is invoked to manufacture a diagnostic. Gameplay is never
// gated by observer readiness. Callback pointers remain resident after Stop.
bool AimFireObserverPrepare(AimFireObserverLog log) noexcept;
void AimFireObserverActivate() noexcept;
void AimFireObserverStop() noexcept;
void AimFireObserverFlushTelemetry() noexcept;
const char* AimFireObserverStatus() noexcept;

#ifdef CS2_AIM_FIRE_OBSERVER_CHECKS
using AimFireObserverNative = void* (*)(void*, void*, int);
using AimFireObserverSpread = float (*)(void*);
using AimFireObserverInaccuracy = float (*)(void*, float*, float*);
void AimFireObserverTestConfigure(std::uintptr_t client, AimFireObserverNative native,
    AimFireObserverLog log = nullptr) noexcept;
void* AimFireObserverTestCall(void* weapon, void* output, int mode, std::uintptr_t caller) noexcept;
bool AimFireObserverTestPoll(AimFireObservation& observation) noexcept;
void AimFireObserverTestConfigureAccuracy(AimFireObserverSpread spread, AimFireObserverInaccuracy inaccuracy) noexcept;
float AimFireObserverTestSpreadCall(void* weapon, std::uintptr_t caller) noexcept;
float AimFireObserverTestInaccuracyCall(void* weapon, float* first, float* second, std::uintptr_t caller) noexcept;
bool AimFireObserverTestPollAccuracy(AimNativeAccuracyObservation& observation) noexcept;
#endif
