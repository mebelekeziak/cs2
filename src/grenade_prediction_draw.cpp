#include "grenade_prediction_draw.h"
#include "grenade_draw.h"
#include <imgui.h>
#include <cmath>

namespace {
bool KnownKind(GrenadeKind kind) noexcept {
    return kind >= GrenadeKind::HE && kind <= GrenadeKind::Incendiary;
}
void Icon(ImDrawList& draw, ImVec2 center, GrenadeKind kind, ImU32 color) {
    const auto p = [&](float x, float y) { return ImVec2{center.x + x, center.y + y}; };
    const auto line = [&](float ax, float ay, float bx, float by) {
        draw.AddLine(p(ax, ay), p(bx, by), color, 1.6f);
    };
    const auto body = [&](float left, float top, float right, float bottom, float rounding) {
        draw.AddRect(p(left, top), p(right, bottom), color, rounding, 0, 1.8f);
    };
    if (kind == GrenadeKind::HE) {
        // Oval fragmentation body, segment grooves, spoon and pull ring.
        draw.AddEllipse(p(0, 2), {6, 8}, color, 0, 20, 1.8f);
        line(-5, -1, 5, -1); line(-5, 4, 5, 4); line(0, -5, 0, 9);
        body(-3, -9, 2, -5, 1); line(2, -8, 7, -5); line(7, -5, 7, 3);
        draw.AddCircle(p(-5, -9), 2.5f, color, 12, 1.5f);
    } else if (kind == GrenadeKind::Flash) {
        // Long perforated flashbang tube and radial flash marks.
        body(-4, -7, 4, 10, 2);
        line(-4, -3, 4, -3); line(-4, 6, 4, 6);
        line(-1, 0, -1, 3); line(2, 0, 2, 3); body(-2, -10, 2, -7, 1);
        line(-8, -7, -11, -10); line(8, -7, 11, -10); line(0, -13, 0, -16);
    } else if (kind == GrenadeKind::Smoke) {
        // Broad smoke canister with vent bands and curling plume.
        body(-6, -5, 6, 10, 2); body(-3, -8, 3, -5, 1);
        line(-6, -1, 6, -1); line(-6, 6, 6, 6);
        line(-2, 1, -2, 4); line(2, 1, 2, 4);
        draw.AddBezierCubic(p(0, -9), p(-8, -12), p(7, -13), p(1, -17), color, 1.8f, 12);
    } else if (kind == GrenadeKind::Decoy) {
        // Narrow decoy body with outward sound waves.
        body(-3, -7, 3, 10, 2); body(-2, -10, 2, -7, 1);
        line(-3, -1, 3, -1); line(-3, 5, 3, 5);
        draw.AddBezierCubic(p(6, -4), p(10, -2), p(10, 3), p(6, 5), color, 1.6f, 12);
        draw.AddBezierCubic(p(-6, -4), p(-10, -2), p(-10, 3), p(-6, 5), color, 1.6f, 12);
    } else if (kind == GrenadeKind::Molotov) {
        // Glass bottle shoulder, neck and burning cloth.
        const ImVec2 bottle[]{p(-3,-8),p(3,-8),p(3,-3),p(6,1),p(6,10),p(-6,10),p(-6,1),p(-3,-3)};
        draw.AddPolyline(bottle, 8, color, ImDrawFlags_Closed, 1.8f);
        line(-5, 3, 5, 3); line(-3, -9, 4, -11);
        const ImVec2 flame[]{p(4,-10),p(2,-14),p(6,-18),p(6,-14),p(9,-12),p(7,-9)};
        draw.AddPolyline(flame, 6, color, ImDrawFlags_Closed, 1.6f);
    } else {
        // Incendiary canister with a flame emblem.
        body(-6, -7, 6, 10, 2); body(-3, -10, 3, -7, 1);
        line(-6, 7, 6, 7);
        const ImVec2 flame[]{p(-3,4),p(-4,0),p(0,-4),p(1,-1),p(3,-2),p(4,2),p(2,5),p(-1,5)};
        draw.AddPolyline(flame, 8, color, ImDrawFlags_Closed, 1.5f);
    }
}
}

bool GrenadePredictionProjectPoint(const EspVec3& point, const EspMatrix4x4& matrix,
    float width, float height, float (&screen)[2]) noexcept {
    screen[0] = screen[1] = 0;
    float edge[4]{};
    if (!GrenadeProjectEdge({point, point}, matrix, width, height, edge)) return false;
    screen[0] = edge[0]; screen[1] = edge[1];
    return true;
}

GrenadePredictionDrawStats GrenadePredictionDraw(std::span<const GrenadePredictionVisual> visuals,
    const EspMatrix4x4& matrix, float width, float height, const GrenadeSettings& input) {
    GrenadePredictionDrawStats stats;
    if (!ImGui::GetCurrentContext() || !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
        return stats;
    const auto settings = GrenadeSanitizeSettings(input);
    auto& draw = *ImGui::GetBackgroundDrawList();
    draw.PushClipRect({0, 0}, {width, height}, true);
    for (const auto& visual : visuals) {
        if (!(visual.enemy ? settings.enemyPrediction : settings.prediction)) continue;
        const auto& path = visual.path;
        if (!KnownKind(path.kind) || path.nodes.empty() || path.nodes.size() > 4096) continue;
        const float* tint = visual.enemy ? settings.enemyPredictionColor : settings.predictionColor;
        if (tint[3] <= 0) continue;
        const auto color = ImGui::ColorConvertFloat4ToU32({tint[0], tint[1], tint[2], tint[3]});
        const auto shadow = ImGui::ColorConvertFloat4ToU32({0.025f, 0.035f, 0.04f, tint[3] * 0.9f});
        for (std::size_t i = 1; i < path.nodes.size(); ++i) {
            float p[4]{};
            if (!GrenadeProjectEdge({path.nodes[i-1].position, path.nodes[i].position}, matrix, width, height, p)) continue;
            draw.AddLine({p[0], p[1]}, {p[2], p[3]}, shadow, settings.predictionWidth + 2);
            draw.AddLine({p[0], p[1]}, {p[2], p[3]}, color, settings.predictionWidth);
            ++stats.segments;
        }
        for (const auto& node : path.nodes) {
            if (!node.impact) continue;
            float p[2]{};
            if (!GrenadePredictionProjectPoint(node.position, matrix, width, height, p)) continue;
            draw.AddCircleFilled({p[0], p[1]}, 4, shadow, 12);
            draw.AddCircle({p[0], p[1]}, 3, color, 12, 1.5f);
            ++stats.bounces;
        }
        // A finite simulation budget or failed trace is never a landing.
        float p[2]{};
        if (path.Terminal() && GrenadePredictionProjectPoint(path.finalPosition, matrix, width, height, p)) {
            const ImVec2 center{p[0], p[1]};
            draw.AddCircleFilled(center, 20, shadow, 32);
            draw.AddCircle(center, 20, color, 32, 1.6f);
            Icon(draw, center, path.kind, color);
            ++stats.landings;
        }
    }
    draw.PopClipRect();
    return stats;
}
