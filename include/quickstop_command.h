#pragma once
#include "anti_aim_boundary.h"

struct QuickstopAxes {
    bool valid = false;
    float forward = 0, left = 0;
};
// Feedback braking in the physical camera basis. Taper near rest limits
// reversal without assuming unverified ground acceleration or friction rules.
QuickstopAxes QuickstopBrakeAxes(float velocityX, float velocityY, float yaw) noexcept;

struct QuickstopCommandPatch {
    AntiAimBoundaryPatch boundary{};
    std::array<MovementSubtick, 127> editedSteps{};
    MovementButtons originalButtons{}, editedButtons{};
    float originalForward = 0, originalLeft = 0, editedForward = 0, editedLeft = 0;
    std::uint32_t originalPresence = 0, editedPresence = 0;
    bool valid = false;
};

// Engine command owner only. Preserves camera, attack, jump and other actions.
// The caller validates pawn/source identities and physical history beforehand.
// preserveInput inserts only the historical handoff; forward/left then denote
// the previous physical axes, while all current user axes/events remain intact.
bool QuickstopApplyCommand(std::uintptr_t command, float forward, float left,
    float historicalForward, float historicalLeft, std::uint64_t previousDirections,
    std::uintptr_t allocator, std::uintptr_t append, QuickstopCommandPatch& patch,
    bool preserveInput = false) noexcept;
bool QuickstopRestoreCommand(const QuickstopCommandPatch& patch) noexcept;
// A native rebuild may replace the command. If an exact owned event remains
// active after failed restoration, refuse new edits rather than stacking them.
bool QuickstopCommandStillEdited(const QuickstopCommandPatch& patch) noexcept;
