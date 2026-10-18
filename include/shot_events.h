#pragma once
#include "silent_aim_math.h"
#include <cstdint>

enum class ShotNativeEventKind : unsigned {
    WeaponFire, BulletImpact, PlayerHurt, PlayerDeath,
    RoundStart, RoundEnd, NewMap, PlayerSpawn, PlayerDisconnect,
    RoundPrestart, ClientDisconnected
};
struct ShotNativeActor {
    std::uint32_t pawn = UINT32_MAX, controller = UINT32_MAX;
    bool known = false, local = false;
    char name[64]{};
};
// A copied delivery observation, not a shot ID or a server-tick claim. The
// event's full serialized pawn handles survive despawn; current identity data
// only enriches them after exact serialized-handle validation.
struct ShotNativeEvent {
    ShotNativeEventKind kind = ShotNativeEventKind::WeaponFire;
    std::uint64_t receiptMs = 0, deliveryId = 0;
    ShotNativeActor actor{}, victim{};
    AimVector position{};
    int damage = -1, damageArmor = -1, remainingHp = -1, remainingArmor = -1, hitgroup = -1;
    bool positionKnown = false, damageKnown = false, silenced = false, silencedKnown = false;
    char weapon[48]{};
};
using ShotEventsLogCallback = void (*)(const char*);
using ShotEventsValueCallback = void (*)(const ShotNativeEvent&);
// Preparation verifies installed native contracts only. Registration happens
// on the engine owner through Pump; no event/message is retained by this API.
bool ShotEventsPrepare(ShotEventsLogCallback logger, ShotEventsValueCallback callback) noexcept;
void ShotEventsActivate() noexcept;
void ShotEventsPump() noexcept;
void ShotEventsStop(const char* reason) noexcept;
void ShotEventsFlushTelemetry() noexcept;
bool ShotEventsRegistered() noexcept;
const char* ShotEventsStatus() noexcept;

#ifdef CS2_SHOT_EVENTS_CHECKS
void ShotEventsTestConfigure(std::uintptr_t client, ShotEventsValueCallback callback) noexcept;
void ShotEventsTestDeliver(void* event) noexcept;
std::uint32_t ShotEventsTestHash(const char* name) noexcept;
void ShotEventsTestTime(std::uint64_t now) noexcept;
#endif
