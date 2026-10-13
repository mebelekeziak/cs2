#include "esp_draw.h"
#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <limits>

namespace {
int failures = 0;

void Expect(bool result, const char* label) {
    std::printf("%s %s\n", result ? "PASS" : "FAIL", label);
    failures += !result;
}

void SetBounds(EspVec3 (&corners)[8], float offset = 0) {
    for (int i = 0; i < 8; ++i)
        corners[i] = { ((i & 1) ? 0.5f : -0.5f) + offset,
                       (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f };
}
} // namespace

int main() {
    EspMatrix4x4 matrix{};
    for (int i = 0; i < 4; ++i) matrix.m[i][i] = 1;
    EspVec3 corners[8]{};
    SetBounds(corners);
    EspScreenRect rect{};

    // Known geometric oracle: a centered [-.5,.5] cube in a 100px orthographic viewport.
    Expect(EspProjectBox(corners, matrix, 100, 100, rect) == EspProjectionResult::Visible &&
           rect.left == 25 && rect.top == 25 && rect.right == 75 && rect.bottom == 75,
           "orthographic viewport oracle");
    matrix.m[0][3] = 0.5f;
    Expect(EspProjectBox(corners, matrix, 100, 100, rect) == EspProjectionResult::Visible &&
           rect.left == 50 && rect.right == 100, "row-major translated bounds");

    matrix.m[0][3] = 0;
    SetBounds(corners, 4);
    Expect(EspProjectBox(corners, matrix, 100, 100, rect) == EspProjectionResult::Offscreen,
           "offscreen rejected");
    SetBounds(corners);
    matrix.m[3][3] = -1;
    Expect(EspProjectBox(corners, matrix, 100, 100, rect) == EspProjectionResult::BehindCamera,
           "behind-camera rejected");
    matrix.m[3][3] = 1;
    corners[0].x = std::numeric_limits<float>::quiet_NaN();
    Expect(EspProjectBox(corners, matrix, 100, 100, rect) == EspProjectionResult::InvalidInput,
           "NaN bounds rejected");

    SetBounds(corners);
    matrix.m[3][2] = 1;
    matrix.m[3][3] = 0;
    Expect(EspProjectBox(corners, matrix, 100, 100, rect) == EspProjectionResult::Visible &&
           std::isfinite(rect.left) && rect.left >= 0 && rect.right <= 100,
           "camera-plane crossing clips to viewport");
    matrix.m[3][2] = 0;
    matrix.m[3][3] = 1;
    Expect(EspProjectBox(corners, matrix, 0, 100, rect) == EspProjectionResult::InvalidInput,
           "zero viewport rejected");
    for (auto& corner : corners) corner = { 0, 0, 0 };
    Expect(EspProjectBox(corners, matrix, 100, 100, rect) == EspProjectionResult::Degenerate,
           "zero-volume projection rejected");

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = { 100, 100 };
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();

    EspPlayerSnapshot players[6]{};
    for (auto& player : players) {
        player.health = 100;
        player.distanceMeters = 10;
        player.team = 3;
        SetBounds(player.worldCorners);
    }
    players[0].localPlayer = true;
    players[1].health = 0;
    players[2].distanceMeters = 151;
    players[3].distanceMeters = std::numeric_limits<float>::infinity();
    SetBounds(players[4].worldCorners, 4);
    EspFrameSnapshot frame { matrix, players, 6, 3, true, true, true };
    const EspDrawStats stats = EspDraw(frame, 100, 100);
    Expect(stats.considered == 6 && stats.local == 1 && stats.dead == 1 &&
           stats.distanceFiltered == 1 && stats.invalidDistance == 1 && stats.offscreen == 1 &&
           stats.projected == 1 && stats.drawn == 1,
           "filter accounting and default teammate display");

    ImGui::Render();
    Expect(ImGui::GetDrawData()->TotalVtxCount > 0, "ImGui primitives submitted");
    ImGui::DestroyContext();
    return failures ? 1 : 0;
}
