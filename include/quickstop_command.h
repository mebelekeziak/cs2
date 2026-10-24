#pragma once
#include "anti_aim_boundary.h"

struct QuickstopAxes {
    bool valid = false;
    float forward = 0, left = 0;
    float speedAfterFriction = 0, maximumImpulse = 0;
    bool settling = false;
};
struct QuickstopPhysics {
    float wishSpeed = 0, acceleration = 0, accelerationSpeedFloor = 0;
    float surfaceFriction = 0, groundFriction = 0, pawnFriction = 0;
    float stopSpeed = 0, interval = 0;
};
// Whole-command impulse bound selects full counter-input or a small counter
// wish speed. CS2's max(wishSpeed,250) accelerator makes a linear input taper
// unreliable near rest. The native add-speed clamp bounds settling reversal.
// This is a movement controller, never predicted shot accuracy or position.
QuickstopAxes QuickstopBrakeAxes(float velocityX, float velocityY, float velocityZ,
    float yaw, const QuickstopPhysics& physics) noexcept;

// Read-only, engine command owner only. A final release or timed jump action
// still excludes a ground brake. Unreadable/malformed input also returns true.
bool QuickstopCommandHasJump(std::uintptr_t command) noexcept;

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
