#include "grenade_prediction_draw.h"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
int failures = 0, checks = 0;
void Expect(bool result, const char* label) {
    ++checks;
    if (!result) { ++failures; std::printf("FAIL %s\n", label); }
}
bool Empty(const GrenadePredictionDrawStats& stats) {
    return stats.segments == 0 && stats.bounces == 0 && stats.landings == 0;
}
}
int main() {
    EspMatrix4x4 matrix{};
    matrix.m[0][0] = matrix.m[1][1] = matrix.m[3][3] = 1;
    float p[2]{};
    Expect(GrenadePredictionProjectPoint({0, 0, 0}, matrix, 400, 300, p) && p[0] == 200 && p[1] == 150,
        "point projects to viewport center");
    Expect(!GrenadePredictionProjectPoint({2, 0, 0}, matrix, 400, 300, p), "offscreen marker is omitted");
    Expect(!GrenadePredictionProjectPoint({}, matrix, 0, 300, p), "zero viewport rejected");
    Expect(!GrenadePredictionProjectPoint({}, matrix, 400, std::numeric_limits<float>::infinity(), p), "infinite viewport rejected");
    Expect(!GrenadePredictionProjectPoint({std::numeric_limits<float>::quiet_NaN(), 0, 0}, matrix, 400, 300, p), "nonfinite point rejected");
    auto badMatrix = matrix; badMatrix.m[0][0] = std::numeric_limits<float>::quiet_NaN();
    Expect(!GrenadePredictionProjectPoint({}, badMatrix, 400, 300, p), "nonfinite clip transform rejected");
    auto perspective = matrix; perspective.m[3][3] = 0; perspective.m[3][2] = 1;
    Expect(!GrenadePredictionProjectPoint({0, 0, -1}, perspective, 400, 300, p), "behind-camera marker omitted");

    GrenadePredictionVisual visual;
    visual.path.kind = GrenadeKind::HE;
    visual.path.nodes = {{{-.5f, 0, 0}, 0, false}, {{0, .3f, 0}, .5f, true}, {{.5f, 0, 0}, 1, false}};
    visual.path.end = GrenadeEnd::Detonated;
    visual.path.finalPosition = {.5f, 0, 0};
    auto visuals = std::span(&visual, 1);
    GrenadeSettings settings; settings.prediction = true;
    Expect(Empty(GrenadePredictionDraw(visuals, matrix, 400, 300, settings)), "drawing safe without ImGui context");

    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.DisplaySize = {400, 300}; io.DeltaTime = 1.0f / 60; io.IniFilename = nullptr;
    unsigned char* pixels = nullptr; int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    auto stats = GrenadePredictionDraw(visuals, matrix, 400, 300, settings);
    Expect(stats.segments == 2 && stats.bounces == 1 && stats.landings == 1, "trajectory, impact and terminal icon draw together");
    settings.prediction = false;
    Expect(Empty(GrenadePredictionDraw(visuals, matrix, 400, 300, settings)), "local prediction disabled independently");
    settings.enemyPrediction = true;
    Expect(Empty(GrenadePredictionDraw(visuals, matrix, 400, 300, settings)), "enemy toggle does not enable local paths");
    visual.enemy = true;
    stats = GrenadePredictionDraw(visuals, matrix, 400, 300, settings);
    Expect(stats.segments == 2 && stats.landings == 1, "enemy paths use independent prediction toggle");
    settings.enemyPredictionColor[3] = 0;
    Expect(Empty(GrenadePredictionDraw(visuals, matrix, 400, 300, settings)), "transparent enemy color suppresses all markers");
    settings.enemyPredictionColor[3] = 1;
    visual.enemy = false; settings.prediction = true;
    for (auto end : {GrenadeEnd::Invalid, GrenadeEnd::TraceFailure, GrenadeEnd::Budget}) {
        visual.path.end = end;
        Expect(GrenadePredictionDraw(visuals, matrix, 400, 300, settings).landings == 0,
            "incomplete prediction never displays terminal icon");
    }
    visual.path.end = GrenadeEnd::Settled;
    Expect(GrenadePredictionDraw(visuals, matrix, 400, 300, settings).landings == 1, "settled path displays final marker");
    visual.path.finalPosition = {2, 0, 0};
    Expect(GrenadePredictionDraw(visuals, matrix, 400, 300, settings).landings == 0, "offscreen terminal is omitted");
    visual.path.finalPosition = {.5f, 0, 0};
    visual.path.nodes = {{{-2, 0, 0}, 0, false}, {{2, 0, 0}, 1, true}};
    stats = GrenadePredictionDraw(visuals, matrix, 400, 300, settings);
    Expect(stats.segments == 1 && stats.bounces == 0, "viewport crossing path clips while offscreen bounce is omitted");
    visual.path.nodes = {{{0, 0, -1}, 0, true}, {{.5f, 0, 1}, 1, false}};
    visual.path.finalPosition = {.5f, 0, 1};
    stats = GrenadePredictionDraw(visuals, perspective, 400, 300, settings);
    Expect(stats.segments == 1 && stats.bounces == 0 && stats.landings == 1, "camera crossing clips path without behind-camera bounce");
    Expect(Empty(GrenadePredictionDraw(visuals, badMatrix, 400, 300, settings)), "invalid matrix produces no drawing");
    Expect(Empty(GrenadePredictionDraw(visuals, matrix, -1, 300, settings)), "invalid viewport produces no drawing");
    visual.path.nodes = {{{0, 0, 0}, 0, false}};
    visual.path.finalPosition = {};
    for (auto kind : {GrenadeKind::HE, GrenadeKind::Flash, GrenadeKind::Smoke,
        GrenadeKind::Decoy, GrenadeKind::Molotov, GrenadeKind::Incendiary}) {
        visual.path.kind = kind;
        auto* draw = ImGui::GetBackgroundDrawList();
        const int vertices = draw->VtxBuffer.Size;
        stats = GrenadePredictionDraw(visuals, matrix, 400, 300, settings);
        Expect(stats.landings == 1 && draw->VtxBuffer.Size - vertices > 150, "each grenade type has a vector icon at its final marker");
    }
    visual.path.kind = static_cast<GrenadeKind>(99);
    Expect(Empty(GrenadePredictionDraw(visuals, matrix, 400, 300, settings)), "unknown grenade type has no misleading icon");
    for (const auto& vertex : ImGui::GetBackgroundDrawList()->VtxBuffer) {
        if (!std::isfinite(vertex.pos.x) || !std::isfinite(vertex.pos.y)) {
            Expect(false, "all submitted geometry remains finite"); break;
        }
    }
    ImGui::Render(); ImGui::DestroyContext();
    std::printf("Grenade prediction draw: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
