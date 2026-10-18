#include "scoped_crosshair_draw.h"
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {
int failures = 0;
void Expect(bool value, const char* label) {
    std::printf("%s %s\n", value ? "PASS" : "FAIL", label);
    failures += !value;
}
bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }

void LayoutChecks() {
    ScopedCrosshairState state;
    ScopedCrosshairLayout layout;
    ScopedCrosshairDrawInput input { true, 0, 0, 1 };
    Expect(ScopedCrosshairBuildLayout(input, 1920, 1080, 1.0f / 60, state, layout) &&
        layout.centerX == 960 && layout.centerY == 540 && layout.radius == 18 && layout.gap == 2,
        "1080p centered compact crosshair has a two-pixel resting gap");
    float previous = 2;
    bool monotonic = true;
    for (int i = 0; i <= 100; ++i) {
        state = {};
        input.spread = static_cast<float>(i) / 1000;
        ScopedCrosshairBuildLayout(input, 1920, 1080, 1.0f / 60, state, layout);
        monotonic &= layout.gap >= previous && layout.gap <= 5;
        previous = layout.gap;
    }
    Expect(monotonic && Near(previous, 5), "spread response is monotonic and adds at most three pixels");
    state = {};
    input.spread = 0;
    input.inaccuracy = 0.05f;
    ScopedCrosshairBuildLayout(input, 1920, 1080, 1.0f / 60, state, layout);
    Expect(Near(layout.gap, 3.5f), "native inaccuracy contributes alongside spread");
    input.spread = input.inaccuracy = std::numeric_limits<float>::max();
    ScopedCrosshairBuildLayout(input, 1920, 1080, 1, state, layout);
    Expect(std::isfinite(layout.gap) && layout.gap <= 5, "large finite values cannot overflow the bounded gap");

    input = { true, 0, 0, 1 };
    state = {};
    ScopedCrosshairBuildLayout(input, 1920, 1080, 0, state, layout);
    input.spread = 0.1f;
    ScopedCrosshairBuildLayout(input, 1920, 1080, 1.0f / 60, state, layout);
    const float first = layout.gap;
    ScopedCrosshairBuildLayout(input, 1920, 1080, 1.0f / 60, state, layout);
    Expect(first > 2 && first < layout.gap && layout.gap < 5,
        "spread increases smoothly without jumping to its target");
    const float raised = layout.gap;
    input.spread = 0;
    ScopedCrosshairBuildLayout(input, 1920, 1080, 1.0f / 60, state, layout);
    Expect(layout.gap > 2 && layout.gap < raised, "spread recovery also smooths without overshoot");
    input.sourceKey = 2;
    ScopedCrosshairBuildLayout(input, 1920, 1080, 1.0f / 60, state, layout);
    Expect(layout.gap == 2, "weapon or pawn identity changes discard old spread smoothing");

    input.spread = 0.1f;
    input.visible = false;
    Expect(!ScopedCrosshairBuildLayout(input, 1920, 1080, 1.0f / 60, state, layout) &&
        !state.active && state.sourceKey == 0 && layout.radius == 0,
        "hidden input clears the crosshair and state immediately");
    input.visible = true;
    ScopedCrosshairBuildLayout(input, 1920, 1080, 1.0f / 60, state, layout);
    Expect(layout.gap == 5, "visibility reentry starts from current spread with no stale state");

    ScopedCrosshairState thirty, sixty;
    input.spread = 0;
    ScopedCrosshairBuildLayout(input, 1920, 1080, 0, thirty, layout);
    ScopedCrosshairBuildLayout(input, 1920, 1080, 0, sixty, layout);
    input.spread = 0.1f;
    for (int i = 0; i < 3; ++i)
        ScopedCrosshairBuildLayout(input, 1920, 1080, 1.0f / 30, thirty, layout);
    for (int i = 0; i < 6; ++i)
        ScopedCrosshairBuildLayout(input, 1920, 1080, 1.0f / 60, sixty, layout);
    Expect(Near(thirty.extraGap, sixty.extraGap), "normal-frame smoothing is independent of frame rate");
    ScopedCrosshairBuildLayout(input, 3840, 2160, 0, state, layout);
    Expect(layout.radius == 36 && layout.gap <= 10 && layout.centerX == 1920,
        "resolution scaling keeps the same relative size and spread response");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    struct InvalidCase { ScopedCrosshairDrawInput input; float width, height, delta; };
    const InvalidCase invalid[] = {
        { { true, nan, 0, 1 }, 1920, 1080, 0.016f },
        { { true, 0, infinity, 1 }, 1920, 1080, 0.016f },
        { { true, -0.001f, 0, 1 }, 1920, 1080, 0.016f },
        { { true, 0, -0.001f, 1 }, 1920, 1080, 0.016f },
        { { true, 0, 0, 0 }, 1920, 1080, 0.016f },
        { { true, 0, 0, 1 }, nan, 1080, 0.016f },
        { { true, 0, 0, 1 }, 1920, infinity, 0.016f },
        { { true, 0, 0, 1 }, 0, 1080, 0.016f },
        { { true, 0, 0, 1 }, 1920, -1, 0.016f },
        { { true, 0, 0, 1 }, 1920, 1080, nan },
        { { true, 0, 0, 1 }, 1920, 1080, -1 }
    };
    bool rejected = true;
    for (const auto& sample : invalid) {
        state = { 1, 2, true };
        rejected &= !ScopedCrosshairBuildLayout(sample.input, sample.width, sample.height,
            sample.delta, state, layout) && !state.active && state.extraGap == 0 && layout.gap == 0;
    }
    Expect(rejected, "invalid spread, identity, viewport, and timing hide immediately and reset state");
}

struct Drawing {
    bool drawn = false;
    std::vector<ImDrawVert> vertices;
    std::vector<ImDrawIdx> indices;

    bool CoversWhite(float x, float y) const {
        const auto cross = [](ImVec2 a, ImVec2 b, ImVec2 c) {
            return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        };
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            const auto& a = vertices[indices[i]];
            const auto& b = vertices[indices[i + 1]];
            const auto& c = vertices[indices[i + 2]];
            if (a.col != IM_COL32_WHITE || b.col != IM_COL32_WHITE || c.col != IM_COL32_WHITE) continue;
            if (std::abs(cross(a.pos, b.pos, c.pos)) < 0.00001f) continue;
            const ImVec2 p { x, y };
            const float u = cross(a.pos, b.pos, p), v = cross(b.pos, c.pos, p),
                        w = cross(c.pos, a.pos, p);
            if ((u >= 0 && v >= 0 && w >= 0) || (u <= 0 && v <= 0 && w <= 0)) return true;
        }
        return false;
    }
};

Drawing Draw(ScopedCrosshairDrawInput input, float delta = 1.0f / 60) {
    ImGui::NewFrame();
    const bool drawn = ScopedCrosshairDraw(input, 1920, 1080, delta);
    const auto* list = ImGui::GetBackgroundDrawList();
    Drawing result { drawn, { list->VtxBuffer.begin(), list->VtxBuffer.end() },
                    { list->IdxBuffer.begin(), list->IdxBuffer.end() } };
    ImGui::Render();
    return result;
}

void GeometryChecks() {
    Expect(!ScopedCrosshairDraw({ true, 0, 0, 1 }, 1920, 1080, 0.016f),
        "missing ImGui context hides safely");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = io.LogFilename = nullptr;
    io.DisplaySize = { 1920, 1080 };
    io.DeltaTime = 1.0f / 60;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    ScopedCrosshairReset();
    const auto resting = Draw({ true, 0, 0, 1 });
    Expect(resting.drawn && !resting.indices.empty() && resting.CoversWhite(960, 540) &&
        resting.CoversWhite(966, 540) && resting.CoversWhite(954, 540) &&
        resting.CoversWhite(960, 546) && resting.CoversWhite(960, 534),
        "submitted ImGui geometry contains a white center dot and all four cardinal arms");
    Expect(!resting.CoversWhite(962, 542) && !resting.CoversWhite(966, 546) &&
        !resting.CoversWhite(980, 540), "crosshair preserves empty diagonal space and compact tips");
    bool halo = false, compact = true, indicesValid = true;
    float minX = 1920, maxX = 0, minY = 1080, maxY = 0;
    for (const auto& vertex : resting.vertices) {
        const auto alpha = (vertex.col >> IM_COL32_A_SHIFT) & 255;
        halo |= alpha > 0 && alpha < 255;
        // ImGui's transparent AA miter extends the acute halo tips past their
        // 20.5px silhouette; the visible white body remains within 18px.
        compact &= std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y) &&
            std::abs(vertex.pos.x - 960) <= 24 && std::abs(vertex.pos.y - 540) <= 24;
        minX = std::min(minX, vertex.pos.x); maxX = std::max(maxX, vertex.pos.x);
        minY = std::min(minY, vertex.pos.y); maxY = std::max(maxY, vertex.pos.y);
    }
    for (auto index : resting.indices) indicesValid &= index < resting.vertices.size();
    if (!halo || !compact || !indicesValid || !Near(minX + maxX, 1920) || !Near(minY + maxY, 1080))
        std::printf("geometry halo=%d compact=%d indices=%d bounds=%.4f,%.4f,%.4f,%.4f\n",
            halo, compact, indicesValid, minX, minY, maxX, maxY);
    Expect(halo && compact && indicesValid && Near(minX + maxX, 1920) && Near(minY + maxY, 1080),
        "soft glow geometry stays finite, symmetric, compact, and correctly indexed");

    const auto spread = Draw({ true, 0.1f, 0, 1 }, 0.1f);
    Expect(resting.CoversWhite(963, 540) && !spread.CoversWhite(963, 540) &&
        spread.CoversWhite(968, 540) && spread.CoversWhite(960, 540),
        "spread moves the rendered inner arm edge while preserving center dot and outer arm");
    const auto hidden = Draw({ false, 0, 0, 1 });
    Expect(!hidden.drawn && hidden.vertices.empty(), "hidden input submits no crosshair geometry");
    const auto invalid = Draw({ true, std::numeric_limits<float>::quiet_NaN(), 0, 1 });
    Expect(!invalid.drawn && invalid.vertices.empty(), "invalid native spread submits no geometry");
    const auto reentered = Draw({ true, 0, 0, 1 });
    Expect(reentered.CoversWhite(963, 540), "render visibility reentry discards old smoothed spread");
    Draw({ true, 0.1f, 0, 1 }, 0.1f);
    const auto replacement = Draw({ true, 0, 0, 2 });
    Expect(replacement.CoversWhite(963, 540), "render identity replacement resets the gap immediately");
    Draw({ true, 0.1f, 0, 2 }, 0.1f);
    ScopedCrosshairReset();
    Expect(Draw({ true, 0, 0, 2 }).CoversWhite(963, 540), "explicit render teardown reset clears smoothing");
    ImGui::DestroyContext();
    ScopedCrosshairReset();
}
} // namespace

int main() {
    LayoutChecks();
    GeometryChecks();
    return failures ? 1 : 0;
}
