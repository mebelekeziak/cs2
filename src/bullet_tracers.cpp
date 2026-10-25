#include "bullet_tracers.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
std::mutex mutex;
BulletTracerSettings settings;
BulletTracerFrame frame;
std::uint64_t generation = 0, sourceMs = 0, fireMs = 0;
EspVec3 eye{}, fireOrigin{};
bool sourceValid = false, fireValid = false;
struct Delivery { std::uint64_t id = 0; ShotNativeEventKind kind{}; };
std::array<Delivery, 256> deliveries{};
std::size_t nextDelivery = 0;
constexpr std::uint64_t FreshMs = 500;
// Receipt/capture timestamps can precede acquisition of our mutex slightly.
constexpr std::uint64_t ClockSlackMs = 50;
bool Finite(EspVec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
        std::abs(value.x) <= 1e7f && std::abs(value.y) <= 1e7f && std::abs(value.z) <= 1e7f;
}
void ClearEntries() noexcept {
    frame.count = 0; fireValid = false; fireMs = 0;
    deliveries = {}; nextDelivery = 0;
}
void ClearSource() noexcept {
    ClearEntries(); generation = sourceMs = 0;
    frame.pawn = UINT32_MAX; sourceValid = false;
}
float Clamp(float value, float low, float high, float fallback) noexcept {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}
}

BulletTracerSettings BulletTracerSanitizeSettings(const BulletTracerSettings& value) noexcept {
    auto result = value;
    const BulletTracerSettings defaults;
    if (result.material < BulletTracerMaterial::Solid || result.material > BulletTracerMaterial::Plasma)
        result.material = defaults.material;
    for (unsigned i = 0; i < 4; ++i) result.color[i] = Clamp(value.color[i], 0, 1, defaults.color[i]);
    result.glowStrength = Clamp(value.glowStrength, 0, 1, defaults.glowStrength);
    result.duration = Clamp(value.duration, .1f, 10, defaults.duration);
    result.width = Clamp(value.width, .5f, 8, defaults.width);
    result.originOffset = Clamp(value.originOffset, 0, 16, defaults.originOffset);
    return result;
}
BulletTracerSettings BulletTracerGetSettings() noexcept {
    std::lock_guard lock(mutex); return settings;
}
void BulletTracerSetSettings(const BulletTracerSettings& value) noexcept {
    const auto sanitized = BulletTracerSanitizeSettings(value);
    std::lock_guard lock(mutex);
    if (settings.enabled != sanitized.enabled) ClearEntries();
    settings = sanitized;
}
void BulletTracerResetSettings() noexcept { BulletTracerSetSettings({}); }
void BulletTracerClear() noexcept { std::lock_guard lock(mutex); ClearSource(); }
void BulletTracerSource(std::uint64_t sourceGeneration, std::uint32_t pawn,
    EspVec3 position, bool valid, std::uint64_t now) noexcept {
    std::lock_guard lock(mutex);
    if (!valid || !sourceGeneration || pawn == UINT32_MAX || !Finite(position)) { ClearSource(); return; }
    const bool sameSource = sourceValid && generation == sourceGeneration && frame.pawn == pawn;
    if (!sameSource || (now < sourceMs ? sourceMs-now > ClockSlackMs : now-sourceMs > FreshMs)) ClearEntries();
    sourceValid = true; generation = sourceGeneration; frame.pawn = pawn; eye = position;
    sourceMs = sameSource && now < sourceMs && sourceMs-now <= ClockSlackMs ? sourceMs : now;
}
void BulletTracerEvent(const ShotNativeEvent& event) noexcept {
    std::lock_guard lock(mutex);
    switch (event.kind) {
    case ShotNativeEventKind::RoundStart: case ShotNativeEventKind::RoundEnd:
    case ShotNativeEventKind::RoundPrestart: case ShotNativeEventKind::NewMap:
    case ShotNativeEventKind::ClientDisconnected: case ShotNativeEventKind::PlayerDisconnect:
        ClearSource(); return;
    case ShotNativeEventKind::PlayerSpawn:
        if (event.actor.local || event.actor.pawn == frame.pawn) ClearSource();
        return;
    case ShotNativeEventKind::PlayerDeath:
        if (event.victim.local || event.victim.pawn == frame.pawn) ClearSource();
        return;
    default: break;
    }
    if (!settings.enabled || !sourceValid || !event.actor.known || event.actor.pawn != frame.pawn ||
        (event.receiptMs < sourceMs ? sourceMs-event.receiptMs > ClockSlackMs : event.receiptMs-sourceMs > FreshMs) ||
        (event.kind != ShotNativeEventKind::WeaponFire && event.kind != ShotNativeEventKind::BulletImpact)) return;
    if (event.deliveryId) {
        for (const auto& delivery : deliveries)
            if (delivery.id == event.deliveryId && delivery.kind == event.kind) return;
        deliveries[nextDelivery] = {event.deliveryId, event.kind};
        nextDelivery = (nextDelivery + 1) % deliveries.size();
    }
    if (event.kind == ShotNativeEventKind::WeaponFire) {
        if (fireValid && event.receiptMs < fireMs) return;
        fireOrigin = eye; fireMs = event.receiptMs; fireValid = true; return;
    }
    const EspVec3 end{event.position.x, event.position.y, event.position.z};
    if (!event.positionKnown || !Finite(end) || !fireValid || event.receiptMs < fireMs ||
        event.receiptMs - fireMs > FreshMs) return;
    const double dx = double(end.x) - fireOrigin.x, dy = double(end.y) - fireOrigin.y,
        dz = double(end.z) - fireOrigin.z;
    if (dx*dx + dy*dy + dz*dz < .0001) return;
    if (frame.count == frame.segments.size()) {
        std::move(frame.segments.begin() + 1, frame.segments.end(), frame.segments.begin()); --frame.count;
    }
    frame.segments[frame.count++] = {fireOrigin, end, event.receiptMs};
}
BulletTracerFrame BulletTracerCapture(std::uint64_t now) noexcept {
    std::lock_guard lock(mutex);
    if (!settings.enabled || !sourceValid ||
        (now < sourceMs ? sourceMs-now > ClockSlackMs : now-sourceMs > FreshMs)) {
        ClearEntries(); return {};
    }
    const auto lifetime = static_cast<std::uint64_t>(settings.duration * 1000);
    std::size_t kept = 0;
    for (std::size_t i = 0; i < frame.count; ++i) {
        const auto& segment = frame.segments[i];
        if (now < segment.receiptMs ? segment.receiptMs-now <= ClockSlackMs : now-segment.receiptMs < lifetime)
            frame.segments[kept++] = segment;
    }
    frame.count = kept; return frame;
}
