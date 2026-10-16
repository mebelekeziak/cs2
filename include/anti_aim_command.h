#pragma once
#include "anti_aim_math.h"
#include "movement_command.h"
#include <array>
#include <cstdint>

struct AntiAimCommandStep {
    std::uintptr_t address = 0;
    MovementSubtick original{}, edited{};
};
// Callback-owned transaction. Never retain after command allocation/lifecycle
// changes. No allocations or history/camera writes are performed.
struct AntiAimCommandPatch {
    std::uintptr_t command = 0, base = 0, angles = 0, rep = 0;
    int sequence = 0, count = 0, capacity = 0, allocated = 0;
    AimAngles originalView{}, editedView{};
    float originalForward = 0, originalLeft = 0, editedForward = 0, editedLeft = 0;
    MovementButtons originalButtons{}, editedButtons{};
    std::uint32_t commandPresence = 0, originalPresence = 0, editedPresence = 0, anglePresence = 0, editedAnglePresence = 0;
    std::array<AntiAimCommandStep, 128> steps{};
    bool valid = false;
};
struct AntiAimCommandDiagnostics {
    const char* reason = "not_read";
    std::uint32_t commandPresence = 0, basePresence = 0, anglePresence = 0;
    int count = 0, capacity = 0, allocated = 0;
};
bool AntiAimApplyCommand(std::uintptr_t command, AntiAimCommandPatch& patch,
                         const char** reason = nullptr, AntiAimCommandDiagnostics* diagnostics = nullptr) noexcept;
bool AntiAimRestoreCommand(const AntiAimCommandPatch& patch) noexcept;
bool AntiAimReapplyCommand(const AntiAimCommandPatch& patch) noexcept;
// Switch only angle payload while movement replay reads physical input.
// physical=true exposes original view; false restores outgoing view.
bool AntiAimSetReplayView(const AntiAimCommandPatch& patch, bool physical) noexcept;
