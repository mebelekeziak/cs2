#pragma once
#include "silent_aim_math.h"
#include <cstdint>
#include <array>
#include "movement_command.h"

struct AimHistoryAngle { std::uintptr_t entry = 0, angles = 0; AimAngles view{}; };

struct AimShot {
    std::uintptr_t command = 0;
    int index = -1;
    int count = 0;
    std::uint64_t buttons = 0;
    AimAngles view{};
    std::array<AimHistoryAngle, 256> history{};
};
struct AimCommandDiagnostics {
    const char* reason = "not_read";
    int size = 0, capacity = 0, allocated = 0, index = -1, entry = -1;
    std::uint32_t presence = 0, entryPresence = 0, anglePresence = 0;
    std::uint64_t buttons = 0;
    std::uintptr_t angles = 0, baseAngles = 0;
};
// Borrow native primary-fire history, including held-fire continuation entries.
// No message allocation, attack synthesis, base-angle writes or input writes.
bool SilentAimReadShot(std::uintptr_t command, AimShot& shot,
    AimCommandDiagnostics* diagnostics = nullptr) noexcept;
bool SilentAimWriteShot(const AimShot& shot, const AimAngles& angles) noexcept;
struct AimViewDiagnostics {
    const char* reason = "not_read";
    std::uintptr_t base = 0, angles = 0;
    std::uint32_t basePresence = 0, presence = 0;
    AimAngles values{};
    bool nativeDefault = false;
};
// Read the current native base view even when no attack is present. Call before
// anti aim emits a new command pose, after its prior owned patch is restored.
// Native base QAngles omit zero scalar fields. A missing angle message with no
// parent presence uses the native zero default; no messages are allocated.
bool SilentAimReadView(std::uintptr_t command, AimAngles& view,
    AimViewDiagnostics* diagnostics = nullptr) noexcept;

using AimRecordHistoryFn = void (*)(void*, int);
struct AimAutofireDiagnostics {
    const char* reason = "not_requested";
    int sequence = 0, events = 0, processed = 0, nativeIndex = -1, nativeHistory = 0;
    bool requested = false, inputRestored = false, commandRestored = false;
};
struct AimAutofirePatch {
    std::uintptr_t input = 0, command = 0;
    int sequence = 0, originalInputIndex = -1, nativeIndex = -1;
    int originalCommandIndex = -1, eventCount = 0;
    std::uint32_t originalCommandPresence = 0;
    MovementButtons before{}, applied{};
    bool active = false, inputPending = false;
};
// Current engine owner only, slot zero, before native command CRC finalization
// and anti aim emission. The verified native recorder owns history allocation
// and index choice. A successful patch can be retained as a copied ownership
// record; only re-encountered current callback addresses may be dereferenced.
bool SilentAimStartAutofire(std::uintptr_t input, std::uintptr_t command,
    AimRecordHistoryFn record, AimAutofirePatch& patch,
    AimAutofireDiagnostics* diagnostics = nullptr) noexcept;
// After the original history builder: restore the temporary input index before
// any later callback; command attack and its native index stay for publication.
bool SilentAimFinishAutofireHistory(AimAutofirePatch& patch,
    AimAutofireDiagnostics* diagnostics = nullptr) noexcept;
// Cancel only an owned request, preserving all unrelated command bits. If the
// native finalizer already ran, its original callback MUST run again afterward
// before command publication to regenerate the native movement CRC.
bool SilentAimCancelAutofire(AimAutofirePatch& patch,
    AimAutofireDiagnostics* diagnostics = nullptr) noexcept;
// Before rebuilding a current command, retire its old owned pulse if still
// present. Address/sequence changes abandon copied state without dereferencing
// cached engine addresses. Physical primary takeover is always preserved.
// retainCompleted keeps an unchanged, fully released owned pulse for indexed
// replay. Native resets/takeover still retire its copied ownership. Pending
// temporary input is never retained by this option.
bool SilentAimRestoreAutofireRebuild(std::uintptr_t input, std::uintptr_t command,
    AimAutofirePatch& patch, AimAutofireDiagnostics* diagnostics = nullptr,
    bool retainCompleted = false) noexcept;
