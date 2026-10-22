#include "aim_visualization.h"
#include <imgui.h>
#include <cstdio>
#include <limits>

int main() {
    unsigned checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* label) {
        ++checks; failures += !ok;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    };
    AimVisualizationFrame frame{};
    expect(AimVisualizationGetSettings() == AimVisualizationSettings{} && !AimVisualizationBegin(frame),
        "visualization defaults disabled with white RGBA");
    AimVisualizationSettings settings{true, {.25f, .5f, .75f, .8f}};
    AimVisualizationSetSettings(settings);
    expect(AimVisualizationBegin(frame), "enabled visualization accepts an evaluation frame");
    frame.localPawn = 0x8001;
    frame.points[0] = {{0, 0, 0}, 0x8002}; frame.count = 1;
    AimVisualizationPublish(frame, 1000);
    auto changedCriteria = frame.criteria; changedCriteria.minDamage = 100;
    expect(AimVisualizationCapture(1000, &changedCriteria).count == 1,
        "minimum damage changes do not hide existing damage-path points");
    changedCriteria.autowall = !changedCriteria.autowall;
    expect(AimVisualizationCapture(1000, &changedCriteria).count == 0 &&
        AimVisualizationCapture(1000, &frame.criteria).count == 1,
        "changed path criteria immediately hide old eligible points");
    expect(AimVisualizationCapture(1000).count == 1 && AimVisualizationCapture(1100).count == 1 &&
        AimVisualizationCapture(1101).count == 0 && AimVisualizationCapture(999).count == 0,
        "copied evaluation expires after 100ms and rejects reversed time");
    settings.enabled = false; AimVisualizationSetSettings(settings);
    settings.enabled = true; AimVisualizationSetSettings(settings);
    AimVisualizationPublish(frame, 1001);
    expect(AimVisualizationCapture(1001).count == 0, "disable/re-enable rejects in-flight old-generation data");
    AimVisualizationBegin(frame); AimVisualizationPublish(frame, 1002);
    expect(AimVisualizationCapture(1002).count == 0, "empty scan replaces previous points");
    frame.count = 1; AimVisualizationClear(); AimVisualizationPublish(frame, 1003);
    expect(AimVisualizationCapture(1003).count == 0, "cooperative clear rejects late publication");
    settings.color[0] = -2; settings.color[1] = 2;
    settings.color[2] = std::numeric_limits<float>::quiet_NaN();
    settings.color[3] = std::numeric_limits<float>::infinity();
    AimVisualizationSetSettings(settings);
    expect(AimVisualizationGetSettings() == AimVisualizationSettings{true, {0, 1, 1, 1}},
        "color channels clamp and nonfinite values restore defaults");
    settings = {true, {.25f, .5f, .75f, .8f}};
    AimVisualizationSetSettings(settings);

    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = {1000, 600}; io.DeltaTime = 1.0f/60;
    unsigned char* pixels = nullptr; int fontWidth = 0, fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &fontWidth, &fontHeight);
    EspPlayerSnapshot actors[2]{};
    actors[0].handle = 0x8001; actors[0].localPlayer = true; actors[0].health = 100; actors[0].team = 2;
    actors[1].handle = 0x8002; actors[1].health = 100; actors[1].team = 3;
    EspFrameSnapshot players{}; players.players = actors; players.playerCount = 2;
    players.localTeam = 2; players.valid = players.matrixValid = true;
    players.worldToClip.m[0][0] = players.worldToClip.m[1][1] = players.worldToClip.m[3][3] = 1;
    frame.localPawn = 0x8001; frame.count = 1; frame.points[0] = {{0, 0, 0}, 0x8002};
    ImGui::NewFrame();
    expect(AimVisualizationDraw(frame, players, 1000, 600, settings) == 1,
        "renderer draws copied eligible center independently of ESP/chams");
    auto* draw = ImGui::GetBackgroundDrawList();
    const auto tint = ImGui::ColorConvertFloat4ToU32({.25f, .5f, .75f, .8f});
    bool tinted = false;
    for (const auto& vertex : draw->VtxBuffer) if (vertex.col == tint) tinted = true;
    expect(tinted, "single marker uses customized RGBA");
    const int before = draw->VtxBuffer.Size;
    actors[1].handle = 0x10002;
    expect(AimVisualizationDraw(frame, players, 1000, 600, settings) == 0, "recycled enemy handle is hidden");
    actors[1].handle = 0x8002; actors[1].health = 0;
    expect(AimVisualizationDraw(frame, players, 1000, 600, settings) == 0, "dead enemy is hidden");
    actors[1].health = 100; actors[1].team = 2;
    expect(AimVisualizationDraw(frame, players, 1000, 600, settings) == 0, "teammate is hidden");
    actors[1].team = 3; actors[0].handle = 0x10001;
    expect(AimVisualizationDraw(frame, players, 1000, 600, settings) == 0, "replaced local pawn is hidden");
    actors[0].handle = 0x8001; actors[0].health = 0;
    expect(AimVisualizationDraw(frame, players, 1000, 600, settings) == 0, "local death hides points");
    actors[0].health = 100; players.matrixValid = false;
    expect(AimVisualizationDraw(frame, players, 1000, 600, settings) == 0, "missing projection hides points");
    players.matrixValid = true; frame.points[0].center.x = 2;
    expect(AimVisualizationDraw(frame, players, 1000, 600, settings) == 0, "offscreen center is clipped");
    frame.points[0].center.x = 0; players.worldToClip.m[3][3] = -1;
    expect(AimVisualizationDraw(frame, players, 1000, 600, settings) == 0, "behind-camera center is clipped");
    players.worldToClip.m[3][3] = 1; frame.points[0].center.z = std::numeric_limits<float>::quiet_NaN();
    expect(AimVisualizationDraw(frame, players, 1000, 600, settings) == 0, "nonfinite point is rejected");
    frame.points[0].center.z = 0; settings.color[3] = 0;
    expect(AimVisualizationDraw(frame, players, 1000, 600, settings) == 0, "zero alpha draws nothing");
    settings.color[3] = .8f;
    expect(AimVisualizationDraw(frame, players, -1, 600, settings) == 0 &&
        AimVisualizationDraw(frame, players, std::numeric_limits<float>::infinity(), 600, settings) == 0,
        "invalid viewport draws nothing");
    expect(draw->VtxBuffer.Size == before, "all rejected draw inputs leave the draw list untouched");
    frame.count = frame.points.size() + 1;
    expect(AimVisualizationDraw(frame, players, 1000, 600, settings) == 0, "oversized frame is rejected");
    ImGui::Render(); ImGui::DestroyContext();
    std::printf("Aim visualization checks: %u, failures: %u\n", checks, failures);
    return failures ? 1 : 0;
}
