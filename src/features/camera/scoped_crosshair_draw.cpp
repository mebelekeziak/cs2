#include "scoped_crosshair_draw.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace {
ScopedCrosshairState renderState;

// A narrow diamond leaves a gently rounded-looking inner end after ImGui's
// antialiasing and a long, sharp outer tip, like the supplied reference.
void Arm(ImDrawList* draw, const ScopedCrosshairLayout& layout, float dx, float dy,
         float gap, float radius, float halfWidth, ImU32 color) {
    const auto point = [&](float along, float across) {
        return ImVec2(layout.centerX + dx * along - dy * across,
                      layout.centerY + dy * along + dx * across);
    };
    const float shoulder = gap + layout.scale;
    const ImVec2 points[] = { point(gap, 0), point(shoulder, -halfWidth),
                             point(radius, 0), point(shoulder, halfWidth) };
    draw->AddConvexPolyFilled(points, 4, color);
}
} // namespace

bool ScopedCrosshairBuildLayout(const ScopedCrosshairDrawInput& input,
    float width, float height, float deltaTime, ScopedCrosshairState& state,
    ScopedCrosshairLayout& output) noexcept {
    output = {};
    if (!input.visible || !input.sourceKey || !std::isfinite(input.spread) ||
        !std::isfinite(input.inaccuracy) || input.spread < 0 || input.inaccuracy < 0 ||
        !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
        !std::isfinite(deltaTime) || deltaTime < 0) {
        state = {};
        return false;
    }

    // Native spread/inaccuracy are angular values. Deliberately compress them
    // to at most three reference pixels rather than projecting a spread cone.
    const float target = std::min(3.0f, 30.0f *
        (std::min(input.spread, 1.0f) + std::min(input.inaccuracy, 1.0f)));
    if (!state.active || state.sourceKey != input.sourceKey ||
        !std::isfinite(state.extraGap) || state.extraGap < 0 || state.extraGap > 3) {
        state.extraGap = target;
    } else {
        // Limit catch-up after a render stall; smoothing stays frame-rate
        // independent during ordinary rendering and never overshoots.
        const float blend = 1.0f - std::exp(-std::min(deltaTime, 0.1f) / 0.09f);
        state.extraGap += (target - state.extraGap) * blend;
    }
    state.active = true;
    state.sourceKey = input.sourceKey;
    const float scale = std::clamp(height / 1080.0f, 0.6f, 2.0f);
    output = { width * 0.5f, height * 0.5f, scale,
               (2.0f + state.extraGap) * scale, 18.0f * scale };
    return true;
}

void ScopedCrosshairReset() noexcept {
    renderState = {};
}

bool ScopedCrosshairDraw(const ScopedCrosshairDrawInput& input,
    float width, float height, float deltaTime) {
    if (!ImGui::GetCurrentContext()) {
        ScopedCrosshairReset();
        return false;
    }
    ScopedCrosshairLayout layout;
    if (!ScopedCrosshairBuildLayout(input, width, height, deltaTime, renderState, layout))
        return false;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect({ 0, 0 }, { width, height }, true);
    constexpr float directions[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
    for (const auto& direction : directions) {
        const float dx = direction[0], dy = direction[1], s = layout.scale;
        // Nested translucent silhouettes form a restrained neutral glow.
        Arm(draw, layout, dx, dy, layout.gap - 0.8f * s, layout.radius + 2.5f * s,
            3.3f * s, IM_COL32(220, 225, 235, 12));
        Arm(draw, layout, dx, dy, layout.gap - 0.5f * s, layout.radius + 1.4f * s,
            2.3f * s, IM_COL32(235, 238, 245, 22));
        Arm(draw, layout, dx, dy, layout.gap - 0.2f * s, layout.radius + 0.7f * s,
            1.5f * s, IM_COL32(245, 247, 255, 35));
        Arm(draw, layout, dx, dy, layout.gap, layout.radius,
            0.75f * s, IM_COL32(255, 255, 255, 255));
    }
    const ImVec2 center { layout.centerX, layout.centerY };
    draw->AddCircleFilled(center, 1.7f * layout.scale, IM_COL32(235, 238, 245, 28), 12);
    const float halfDot = 0.5f * layout.scale;
    draw->AddRectFilled({ center.x - halfDot, center.y - halfDot },
                        { center.x + halfDot, center.y + halfDot }, IM_COL32_WHITE);
    draw->PopClipRect();
    return true;
}
