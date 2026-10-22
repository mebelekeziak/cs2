#include "aim_visualization.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

std::size_t AimVisualizationDraw(const AimVisualizationFrame& points, const EspFrameSnapshot& players,
    float width, float height, const AimVisualizationSettings& settings) {
    if (!settings.enabled || !players.valid || !players.matrixValid || !players.players ||
        points.count > points.points.size() || players.playerCount > 128 ||
        !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) return 0;
    bool localAlive = false;
    for (std::size_t i = 0; i < players.playerCount; ++i) {
        const auto& player = players.players[i];
        if (player.localPlayer && player.health > 0 && player.handle == points.localPawn) localAlive = true;
    }
    if (!localAlive) return 0;
    const auto& m = players.worldToClip.m;
    for (const auto& row : m) for (float value : row) if (!std::isfinite(value)) return 0;
    float color[4]{};
    for (unsigned i = 0; i < 4; ++i)
        color[i] = std::isfinite(settings.color[i]) ? std::clamp(settings.color[i], 0.0f, 1.0f) : 1.0f;
    if (color[3] <= 0) return 0;
    const auto tint = ImGui::ColorConvertFloat4ToU32({color[0], color[1], color[2], color[3]});
    const auto outline = ImGui::ColorConvertFloat4ToU32({0, 0, 0, color[3] * .85f});
    const float radius = std::clamp(height / 1080.0f, .75f, 2.0f) * 2.5f;
    auto* draw = ImGui::GetBackgroundDrawList();
    std::size_t drawn = 0;
    for (std::size_t i = 0; i < points.count; ++i) {
        const auto& point = points.points[i];
        bool enemyAlive = false;
        for (std::size_t j = 0; j < players.playerCount; ++j) {
            const auto& player = players.players[j];
            if (player.handle == point.pawn && !player.localPlayer && player.health > 0 &&
                player.team != players.localTeam) { enemyAlive = true; break; }
        }
        if (!enemyAlive) continue;
        const auto& p = point.center;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
        const float w = m[3][0]*p.x + m[3][1]*p.y + m[3][2]*p.z + m[3][3];
        if (!std::isfinite(w) || w < .01f) continue;
        const float x = (m[0][0]*p.x + m[0][1]*p.y + m[0][2]*p.z + m[0][3]) / w;
        const float y = (m[1][0]*p.x + m[1][1]*p.y + m[1][2]*p.z + m[1][3]) / w;
        if (!std::isfinite(x) || !std::isfinite(y) || x < -1 || x > 1 || y < -1 || y > 1) continue;
        const ImVec2 screen{(x + 1)*width*.5f, (1 - y)*height*.5f};
        draw->AddCircleFilled(screen, radius + 1, outline, 12);
        draw->AddCircleFilled(screen, radius, tint, 12);
        ++drawn;
    }
    return drawn;
}
