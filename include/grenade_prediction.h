#pragma once
#include "esp_draw.h"
#include <cstddef>
#include <vector>

enum class GrenadeKind { HE, Flash, Smoke, Decoy, Molotov, Incendiary };
enum class GrenadeEnd { Invalid, TraceFailure, Budget, Detonated, Settled };

// Copied trace output. The adapter owns the native swept hull and its filter.
struct GrenadeTrace {
    float fraction = 1;
    EspVec3 normal{};
    float elasticity = 1;
    bool startSolid = false;
};
using GrenadeTraceFn = bool (*)(void*, const EspVec3&, const EspVec3&, GrenadeTrace&) noexcept;

// Physical parameters come from the verified native adapter, never from a
// historical Source recipe. A negative fuse disables the timed deadline.
struct GrenadePhysics {
    float gravity = 0;
    float elasticity = 0;
    float elasticityLimit = 1;
    float overbounce = 2;
    float clipEpsilon = 0;
    float groundSpeedScale = 1;
    float stopSpeed = 0;
    float restSpeed = 0;
    float fuseTime = -1;
    float minRestTime = 0;
    float fireNormalZ = 1;
    float groundNormalZ = 1;
    float softGroundNormalZ = 1;
    // Zero disables speed-dependent floor damping.
    float fastSpeed = 0;
    float fastNormalDot = 1;
    float fastDampingBias = 1;
    // Zero checks at each integration step (strict deadlines can advance a
    // full step beyond equality). Otherwise these checks run on
    // firstThinkTime + N*thinkInterval, preserving projectile age.
    float thinkInterval = 0;
    float firstThinkTime = 0;
    bool strictFuse = false;
    // The installed native step updates vertical velocity before its first
    // sweep and consumes bounce remainder without applying gravity again.
    bool nativeStepGravity = false;
    // Numerical query-only separation for repeated zero-fraction contacts.
    // It never moves the stored trajectory or terminal position.
    float collisionSkin = 0;
    float step = 1.0f / 64;
    float maxTime = 8;
    std::size_t maxSteps = 1024;
    std::size_t maxBounces = 64;
};
struct GrenadeState {
    GrenadeKind kind = GrenadeKind::HE;
    EspVec3 position{}, velocity{};
    float age = 0;
};
struct GrenadePathNode {
    EspVec3 position{};
    float time = 0; // Projectile age, including time before this prediction.
    bool impact = false;
};
struct GrenadePath {
    GrenadeKind kind = GrenadeKind::HE;
    std::vector<GrenadePathNode> nodes;
    GrenadeEnd end = GrenadeEnd::Invalid;
    EspVec3 finalPosition{};
    float finalAge = 0;
    bool Terminal() const noexcept {
        return end == GrenadeEnd::Detonated || end == GrenadeEnd::Settled;
    }
};
GrenadePath GrenadeSimulate(const GrenadeState&, const GrenadePhysics&,
    GrenadeTraceFn, void* context = nullptr);
