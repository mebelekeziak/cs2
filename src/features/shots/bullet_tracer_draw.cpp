#include "bullet_tracers.h"
#include "grenade_draw.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace {
ImU32 Tint(const BulletTracerSettings& settings, float opacity, float white = 0) {
    return ImGui::ColorConvertFloat4ToU32({settings.color[0] + (1 - settings.color[0])*white,
        settings.color[1] + (1 - settings.color[1])*white,
        settings.color[2] + (1 - settings.color[2])*white, settings.color[3]*opacity});
}
void Beam(ImDrawList& draw, ImVec2 a, ImVec2 b, const BulletTracerSettings& settings,
    float alpha, bool endpoint) {
    if (settings.glow && settings.glowStrength > 0) {
        // Layered alpha halo in the overlay; no scene lighting or bloom mutation.
        for (int layer = 4; layer >= 1; --layer)
            draw.AddLine(a, b, Tint(settings, alpha*settings.glowStrength*.07f), settings.width + layer*3.0f);
    }
    draw.AddLine(a, b, Tint(settings, alpha), settings.width);
    if (settings.material == BulletTracerMaterial::Laser)
        draw.AddLine(a, b, Tint(settings, alpha, .85f), std::max(.5f, settings.width*.3f));
    else if (settings.material == BulletTracerMaterial::Plasma) {
        const float dx = b.x-a.x, dy = b.y-a.y, length = std::hypot(dx, dy);
        if (length > 1) {
            const int pieces = std::clamp(static_cast<int>(length/5), 2, 128);
            ImVec2 previous = a;
            for (int i = 1; i <= pieces; ++i) {
                const float t = static_cast<float>(i)/pieces;
                const float wave = std::sin(t*37.6991118f)*std::sin(t*3.14159265f)*settings.width*.65f;
                const ImVec2 next{a.x + dx*t - dy/length*wave, a.y + dy*t + dx/length*wave};
                draw.AddLine(previous, next, Tint(settings, alpha*.9f, .6f), std::max(.5f, settings.width*.4f));
                previous = next;
            }
        }
    }
    if (settings.impactMarker && endpoint) {
        const float radius = settings.width + 2;
        draw.AddCircle(b, radius, Tint(settings, alpha), 16, 1);
        draw.AddCircleFilled(b, 1.25f, Tint(settings, alpha, .6f), 8);
    }
}
}

std::size_t BulletTracerDraw(const BulletTracerFrame& frame, const EspFrameSnapshot& players,
    float width, float height, std::uint64_t now, const BulletTracerSettings& input) {
    const auto settings = BulletTracerSanitizeSettings(input);
    if (!settings.enabled || !ImGui::GetCurrentContext() || !players.valid || !players.matrixValid ||
        !players.players || players.playerCount > 128 || frame.count > frame.segments.size() ||
        frame.pawn == UINT32_MAX || settings.color[3] <= 0 ||
        !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
        width > 32768 || height > 32768) return 0;
    bool alive = false;
    for (std::size_t i = 0; i < players.playerCount; ++i) {
        const auto& player = players.players[i];
        if (player.localPlayer && player.handle == frame.pawn && player.health > 0) alive = true;
    }
    if (!alive) return 0;
    for (const auto& row : players.worldToClip.m) for (float value : row) if (!std::isfinite(value)) return 0;
    const auto& matrix = players.worldToClip.m;
    const float rightLength = std::hypot(matrix[0][0], matrix[0][1], matrix[0][2]);
    const float upLength = std::hypot(matrix[1][0], matrix[1][1], matrix[1][2]);
    const float forwardLength = std::hypot(matrix[3][0], matrix[3][1], matrix[3][2]);
    if (rightLength < .0001f || upLength < .0001f) return 0;
    const float forwardScale = forwardLength > .0001f ? 4.0f/forwardLength : 0;
    const EspVec3 offset{
        settings.originOffset*(matrix[0][0]/rightLength - matrix[1][0]/upLength*.667f + matrix[3][0]*forwardScale),
        settings.originOffset*(matrix[0][1]/rightLength - matrix[1][1]/upLength*.667f + matrix[3][1]*forwardScale),
        settings.originOffset*(matrix[0][2]/rightLength - matrix[1][2]/upLength*.667f + matrix[3][2]*forwardScale)};
    auto& draw = *ImGui::GetBackgroundDrawList();
    draw.PushClipRect({0, 0}, {width, height}, true);
    std::size_t drawn = 0;
    const auto lifetime = static_cast<std::uint64_t>(settings.duration*1000);
    for (std::size_t i = 0; i < frame.count; ++i) {
        const auto& segment = frame.segments[i];
        if (now < segment.receiptMs || now - segment.receiptMs >= lifetime) continue;
        float screen[4]{}, impact[4]{};
        // A ray beginning exactly at the eye projects to a point in first
        // person. Offset only its visual start toward the approximate muzzle.
        const EspVec3 start{segment.start.x + offset.x, segment.start.y + offset.y, segment.start.z + offset.z};
        if (!GrenadeProjectEdge({start, segment.end}, players.worldToClip, width, height, screen) ||
            std::hypot(screen[2]-screen[0], screen[3]-screen[1]) < .25f) continue;
        const float alpha = settings.fade ? 1-static_cast<float>(now-segment.receiptMs)/static_cast<float>(lifetime) : 1;
        const bool endpoint = GrenadeProjectEdge({segment.end, segment.end}, players.worldToClip, width, height, impact);
        Beam(draw, {screen[0], screen[1]}, {screen[2], screen[3]}, settings, alpha, endpoint);
        ++drawn;
    }
    draw.PopClipRect(); return drawn;
}

void BulletTracerDrawPreview(float width, float height, const BulletTracerSettings& input) {
    if (!ImGui::GetCurrentContext() || !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) return;
    const auto settings = BulletTracerSanitizeSettings(input);
    const ImVec2 start = ImGui::GetCursorScreenPos(), end{start.x + width, start.y + height};
    auto& draw = *ImGui::GetWindowDrawList();
    draw.PushClipRect(start, end, true);
    draw.AddRectFilled(start, end, IM_COL32(16, 19, 24, 255), 4);
    draw.AddText({start.x + 8, start.y + 6}, IM_COL32(140, 152, 169, 255), "Appearance preview");
    Beam(draw, {start.x + width*.12f, start.y + height*.78f},
        {start.x + width*.88f, start.y + height*.4f}, settings, 1, true);
    draw.PopClipRect();
    ImGui::Dummy({width, height});
}
