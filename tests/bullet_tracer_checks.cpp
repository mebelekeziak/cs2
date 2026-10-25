#include "bullet_tracers.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
constexpr std::uint32_t Pawn = 0x18001;
unsigned checks = 0, failures = 0;
void Expect(bool ok, const char* label) {
    ++checks; failures += !ok;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
}
ShotNativeEvent Event(ShotNativeEventKind kind, std::uint64_t now, std::uint64_t id = 0) {
    ShotNativeEvent event{};
    event.kind = kind; event.receiptMs = now; event.deliveryId = id;
    event.actor.known = event.actor.local = true; event.actor.pawn = Pawn;
    event.positionKnown = true; event.position = {100, 20, 30};
    return event;
}
void Ready(std::uint64_t now = 1000, EspVec3 eye = {1, 2, 3}) {
    BulletTracerClear();
    BulletTracerSettings settings; settings.enabled = true;
    BulletTracerSetSettings(settings);
    BulletTracerSource(1, Pawn, eye, true, now);
}
void Shot(std::uint64_t now = 1000) {
    BulletTracerEvent(Event(ShotNativeEventKind::WeaponFire, now, 1));
    BulletTracerEvent(Event(ShotNativeEventKind::BulletImpact, now + 1, 2));
}
struct Geometry { std::size_t drawn = 0; int vertices = 0; unsigned alpha = 0; float minY = 0, maxY = 0; ImU32 color = 0; };
Geometry Draw(const BulletTracerFrame& frame, const EspFrameSnapshot& players,
    const BulletTracerSettings& settings, std::uint64_t now = 1000, float width = 1000, float height = 600) {
    auto* list = ImGui::GetBackgroundDrawList();
    const int before = list->VtxBuffer.Size;
    Geometry geometry;
    geometry.drawn = BulletTracerDraw(frame, players, width, height, now, settings);
    geometry.vertices = list->VtxBuffer.Size - before;
    geometry.minY = std::numeric_limits<float>::max();
    geometry.maxY = std::numeric_limits<float>::lowest();
    bool finite = true;
    for (int i = before; i < list->VtxBuffer.Size; ++i) {
        const auto& vertex = list->VtxBuffer[i];
        const auto alpha = (vertex.col >> IM_COL32_A_SHIFT) & 255;
        if (alpha > geometry.alpha) { geometry.alpha = alpha; geometry.color = vertex.col; }
        geometry.minY = std::min(geometry.minY, vertex.pos.y);
        geometry.maxY = std::max(geometry.maxY, vertex.pos.y);
        finite &= std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y);
    }
    Expect(finite, "draw vertices remain finite");
    return geometry;
}
}

int main() {
    Expect(BulletTracerGetSettings() == BulletTracerSettings{}, "tracers start disabled");
    BulletTracerSource(1, Pawn, {1, 2, 3}, true, 1000); Shot();
    Expect(BulletTracerCapture(1001).count == 0, "disabled tracers ignore ordinary copied fire and impact events");
    Ready(); Shot();
    auto frame = BulletTracerCapture(1001);
    Expect(frame.count == 1 && frame.pawn == Pawn && frame.segments[0].start.x == 1 &&
        frame.segments[0].end.x == 100, "manual fire and impact produce a copied segment without aim or shot-log metadata");
    BulletTracerSource(1, Pawn, {8, 9, 10}, true, 1010);
    BulletTracerEvent(Event(ShotNativeEventKind::BulletImpact, 1011, 3));
    frame = BulletTracerCapture(1011);
    Expect(frame.count == 2 && frame.segments[1].start.x == 1 && frame.segments[1].start.z == 3,
        "multiple pellet or penetration impacts retain the fire-time origin after player movement");
    BulletTracerEvent(Event(ShotNativeEventKind::BulletImpact, 1012, 3));
    Expect(BulletTracerCapture(1012).count == 2, "repeated nonzero impact delivery is suppressed");
    BulletTracerEvent(Event(ShotNativeEventKind::WeaponFire, 1012, 1));
    BulletTracerEvent(Event(ShotNativeEventKind::BulletImpact, 1013, 4));
    Expect(BulletTracerCapture(1013).segments[2].start.x == 1, "duplicate fire does not replace the saved origin");
    BulletTracerEvent(Event(ShotNativeEventKind::BulletImpact, 1014));
    BulletTracerEvent(Event(ShotNativeEventKind::BulletImpact, 1014));
    Expect(BulletTracerCapture(1014).count == 5, "zero delivery IDs preserve legitimately identical impact occurrences");
    auto wrong = Event(ShotNativeEventKind::BulletImpact, 1015, 5); wrong.actor.pawn++;
    BulletTracerEvent(wrong); wrong.actor.pawn = Pawn; wrong.actor.known = false; BulletTracerEvent(wrong);
    Expect(BulletTracerCapture(1015).count == 5, "foreign and unknown actors cannot create local tracers");
    auto bad = Event(ShotNativeEventKind::BulletImpact, 1016);
    bad.position.x = std::numeric_limits<float>::quiet_NaN(); BulletTracerEvent(bad);
    bad.position.x = std::numeric_limits<float>::infinity(); BulletTracerEvent(bad);
    bad.position = {1, 2, 3}; BulletTracerEvent(bad);
    bad.positionKnown = false; BulletTracerEvent(bad);
    Expect(BulletTracerCapture(1016).count == 5, "nonfinite, missing and zero-length impacts are rejected");
    Ready();
    BulletTracerEvent(Event(ShotNativeEventKind::BulletImpact, 1001));
    Expect(BulletTracerCapture(1001).count == 0, "unmatched impact does not invent a firing origin");
    BulletTracerEvent(Event(ShotNativeEventKind::WeaponFire, 1002));
    BulletTracerEvent(Event(ShotNativeEventKind::BulletImpact, 1001));
    BulletTracerSource(1, Pawn, {1, 2, 3}, true, 1450);
    BulletTracerEvent(Event(ShotNativeEventKind::BulletImpact, 1503));
    Expect(BulletTracerCapture(1503).count == 0, "reordered and over-age impacts cannot reuse a pending fire");
    Ready(); BulletTracerEvent(Event(ShotNativeEventKind::WeaponFire, 1000));
    for (unsigned i = 0; i < 140; ++i) {
        auto impact = Event(ShotNativeEventKind::BulletImpact, 1001 + i, 10 + i);
        impact.position.x = static_cast<float>(i + 100); BulletTracerEvent(impact);
    }
    frame = BulletTracerCapture(1140);
    Expect(frame.count == frame.segments.size() && frame.segments[0].end.x == 112 &&
        frame.segments.back().end.x == 239, "bounded segment history retains the newest 128 impacts");
    Ready(); Shot();
    auto settings = BulletTracerGetSettings(); settings.duration = 1; BulletTracerSetSettings(settings);
    for (std::uint64_t time : {1400ull, 1800ull, 2000ull}) BulletTracerSource(1, Pawn, {}, true, time);
    Expect(BulletTracerCapture(2000).count == 1 && BulletTracerCapture(2001).count == 0,
        "configured lifetime expires at its boundary while a fresh source remains available");
    Ready(); Shot(); settings = BulletTracerGetSettings(); settings.enabled = false; BulletTracerSetSettings(settings);
    settings.enabled = true; BulletTracerSetSettings(settings);
    BulletTracerEvent(Event(ShotNativeEventKind::BulletImpact, 1002));
    Expect(BulletTracerCapture(1002).count == 0, "disable and re-enable clear previous segments and pending fire");
    Ready(); Shot(); BulletTracerSource(2, Pawn, {}, true, 1002);
    Expect(BulletTracerCapture(1002).count == 0, "generation change invalidates segment history");
    Ready(); Shot(); BulletTracerSource(1, Pawn + 1, {}, true, 1002);
    Expect(BulletTracerCapture(1002).count == 0, "serialized local pawn replacement invalidates history");
    Ready(); Shot(); BulletTracerSource(1, Pawn, {}, true, 1501);
    Expect(BulletTracerCapture(1501).count == 0, "source update gap invalidates pending and rendered data");
    Ready(); Shot();
    Expect(BulletTracerCapture(1501).count == 0, "render capture hides stale sources");
    Ready(); Shot(); BulletTracerSource(1, Pawn, {}, false, 1002);
    Expect(BulletTracerCapture(1002).count == 0, "invalid source clears tracers");
    Ready(); Shot(); BulletTracerSource(1, Pawn, {0, std::numeric_limits<float>::quiet_NaN(), 0}, true, 1002);
    Expect(BulletTracerCapture(1002).count == 0, "nonfinite source clears tracers");
    Ready(); Shot();
    Expect(BulletTracerCapture(949).count == 0, "substantially reversed capture time clears pending data");
    Ready(); Shot(); BulletTracerSource(1, Pawn, {1, 2, 3}, true, 1002);
    Expect(BulletTracerCapture(1000).count == 1 && BulletTracerCapture(1003).count == 1,
        "render timestamp sampled just before source mutex acquisition preserves future segment");
    BulletTracerSource(1, Pawn, {1, 2, 3}, true, 1001);
    BulletTracerEvent(Event(ShotNativeEventKind::BulletImpact, 1001, 3));
    Expect(BulletTracerCapture(1003).count == 2,
        "event receipt slightly before an owner refresh retains the current source generation");
    for (auto kind : {ShotNativeEventKind::RoundStart, ShotNativeEventKind::RoundEnd,
            ShotNativeEventKind::RoundPrestart, ShotNativeEventKind::NewMap,
            ShotNativeEventKind::ClientDisconnected, ShotNativeEventKind::PlayerDisconnect,
            ShotNativeEventKind::PlayerSpawn, ShotNativeEventKind::PlayerDeath}) {
        Ready(); Shot(); auto event = Event(kind, 1002); event.victim = event.actor;
        BulletTracerEvent(event);
        Expect(BulletTracerCapture(1002).count == 0, "round, map and local player lifecycle events clear history");
    }
    Ready(); Shot(); BulletTracerClear();
    BulletTracerEvent(Event(ShotNativeEventKind::BulletImpact, 1002));
    Expect(BulletTracerCapture(1002).count == 0, "cooperative clear rejects impacts until a new valid source and fire");
    settings = {}; settings.enabled = true;
    settings.material = static_cast<BulletTracerMaterial>(99); settings.duration = -10; settings.width = 100;
    settings.color[0] = -1; settings.color[1] = 2; settings.color[2] = std::numeric_limits<float>::quiet_NaN();
    settings.glowStrength = std::numeric_limits<float>::infinity();
    auto sanitized = BulletTracerSanitizeSettings(settings);
    Expect(sanitized.material == BulletTracerMaterial::Laser && sanitized.duration == .1f && sanitized.width == 8 &&
        sanitized.color[0] == 0 && sanitized.color[1] == 1 && sanitized.color[2] == 1 && sanitized.glowStrength == .6f,
        "appearance sanitization bounds values and restores finite defaults");
    BulletTracerResetSettings();
    Expect(BulletTracerGetSettings() == BulletTracerSettings{}, "reset restores all disabled appearance defaults");

    settings = {}; settings.enabled = true; settings.material = BulletTracerMaterial::Solid;
    settings.originOffset = 0;
    settings.glow = false; settings.fade = false; settings.duration = 2;
    frame = {}; frame.pawn = Pawn; frame.count = 1;
    frame.segments[0] = {{-.5f, 0, 0}, {.5f, 0, 0}, 1000};
    EspPlayerSnapshot actor{}; actor.handle = Pawn; actor.localPlayer = true; actor.health = 100;
    EspFrameSnapshot players{}; players.players = &actor; players.playerCount = 1;
    players.valid = players.matrixValid = true;
    players.worldToClip.m[0][0] = players.worldToClip.m[1][1] = players.worldToClip.m[3][3] = 1;
    Expect(BulletTracerDraw(frame, players, 1000, 600, 1000, settings) == 0, "renderer safely rejects absent ImGui context");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = {1000, 600}; io.DeltaTime = 1.0f/60;
    unsigned char* pixels = nullptr; int fontWidth = 0, fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &fontWidth, &fontHeight);
    ImGui::NewFrame();
    const auto solid = Draw(frame, players, settings);
    Expect(solid.drawn == 1 && solid.vertices > 0 && solid.alpha == 255, "solid material produces visible geometry");
    settings.material = BulletTracerMaterial::Laser; const auto laser = Draw(frame, players, settings);
    settings.material = BulletTracerMaterial::Plasma; const auto plasma = Draw(frame, players, settings);
    Expect(laser.vertices > solid.vertices && plasma.vertices > laser.vertices, "laser core and plasma shape produce distinct material geometry");
    settings.material = BulletTracerMaterial::Solid; settings.glow = true;
    const auto glow = Draw(frame, players, settings);
    Expect(glow.vertices > solid.vertices && glow.maxY - glow.minY > solid.maxY - solid.minY,
        "optional glow adds a wider halo");
    settings.glowStrength = 0;
    Expect(Draw(frame, players, settings).vertices == solid.vertices, "zero glow strength removes halo geometry");
    settings.glow = false; settings.width = 6;
    const auto wide = Draw(frame, players, settings);
    Expect(wide.maxY - wide.minY > solid.maxY - solid.minY, "width changes the beam footprint");
    settings.width = 2; settings.impactMarker = true;
    const auto marker = Draw(frame, players, settings);
    Expect(marker.vertices > solid.vertices, "visible impact marker adds endpoint geometry");
    settings.impactMarker = false; settings.color[0] = 1; settings.color[1] = settings.color[2] = 0; settings.color[3] = .8f;
    const auto red = Draw(frame, players, settings);
    Expect((red.color & IM_COL32(255, 255, 255, 0)) == IM_COL32(255, 0, 0, 0) && red.alpha == 204,
        "RGBA controls the submitted beam color and opacity");
    settings.fade = true;
    const auto faded = Draw(frame, players, settings, 2000);
    settings.fade = false;
    const auto steady = Draw(frame, players, settings, 2000);
    Expect(faded.alpha == 102 && steady.alpha == 204, "fade decreases opacity with age and can be disabled independently");
    Expect(Draw(frame, players, settings, 3000).vertices == 0 && Draw(frame, players, settings, 999).vertices == 0,
        "expired and future segments produce no geometry");
    const auto rejected = [&](const char* label) { const auto result = Draw(frame, players, settings); Expect(!result.drawn && !result.vertices, label); };
    actor.health = 0; rejected("local death suppresses tracer rendering"); actor.health = 100;
    actor.handle++; rejected("changed local serialized handle suppresses rendering"); actor.handle = Pawn;
    players.valid = false; rejected("invalid player snapshot suppresses rendering"); players.valid = true;
    players.matrixValid = false; rejected("missing camera projection suppresses rendering"); players.matrixValid = true;
    players.worldToClip.m[0][0] = std::numeric_limits<float>::quiet_NaN(); rejected("nonfinite projection suppresses rendering");
    players.worldToClip.m[0][0] = 1;
    Expect(Draw(frame, players, settings, 1000, 0).vertices == 0 &&
        Draw(frame, players, settings, 1000, 1000, std::numeric_limits<float>::infinity()).vertices == 0,
        "invalid viewport suppresses rendering");
    frame.count = frame.segments.size() + 1; rejected("oversized copied frame is rejected"); frame.count = 1;
    settings.color[3] = 0; rejected("transparent beam produces no geometry"); settings.color[3] = 1;
    frame.segments[0].end.x = std::numeric_limits<float>::infinity(); rejected("nonfinite segment is rejected");
    frame.segments[0] = {{2, 0, 0}, {3, 0, 0}, 1000}; rejected("fully offscreen segment is rejected");
    frame.segments[0] = {{-2, 0, 0}, {2, 0, 0}, 1000};
    Expect(Draw(frame, players, settings).drawn == 1, "segment crossing the viewport is clipped and drawn");
    settings.impactMarker = true;
    Expect(Draw(frame, players, settings).vertices == solid.vertices, "offscreen impact endpoint does not create a marker at clipped viewport edge");
    settings.impactMarker = false;
    players.worldToClip.m[3][3] = 0; players.worldToClip.m[3][2] = 1;
    frame.segments[0] = {{0, 0, 0}, {0, 0, 1000}, 1000};
    Expect(Draw(frame, players, settings).vertices == 0, "exact eye-aligned ray projects to a point");
    settings.originOffset = 6;
    Expect(Draw(frame, players, settings).drawn == 1, "cosmetic muzzle offset makes stationary first-person shots visible");
    settings.originOffset = 0;
    frame.segments[0] = {{0, 0, -1}, {.5f, 0, 1}, 1000};
    Expect(Draw(frame, players, settings).drawn == 1, "camera near-plane crossing remains bounded and visible");
    frame.segments[0] = {{0, 0, -1}, {.5f, 0, -2}, 1000}; rejected("segment entirely behind camera is rejected");
    ImGui::Render(); ImGui::DestroyContext();
    std::printf("Bullet tracer checks: %u, failures: %u\n", checks, failures);
    return failures ? 1 : 0;
}
