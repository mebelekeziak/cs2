#include "esp_draw.h"

#include <Windows.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
EspSettings settings;
EspLogCallback logger = nullptr;
bool keysDown[9]{};
constexpr float nearW = 0.01f;
constexpr int keys[9] = { VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_OEM_MINUS, VK_OEM_PLUS };

struct ClipPoint { float x, y, w; };

bool Finite(const ClipPoint& point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.w);
}

void LogSettings() noexcept {
    if (!logger) return;
    char message[384]{};
    std::snprintf(message, sizeof(message),
        "ESP_SETTINGS enabled=%d names=%d boxes=%d distance=%d health=%d teammates=%d armor=%d max_distance_m=%.0f",
        settings.enabled, settings.names, settings.boxes, settings.distance, settings.health,
        settings.teammates, settings.armor, settings.maxDistanceMeters);
    logger(message);
}

ImU32 TeamColor(int team) {
    if (team == 2) return IM_COL32(255, 192, 72, 255);
    if (team == 3) return IM_COL32(86, 190, 255, 255);
    return IM_COL32(220, 220, 220, 255);
}

void OutlinedText(ImDrawList* draw, const ImVec2& position, ImU32 color, const char* text) {
    const ImU32 black = IM_COL32(0, 0, 0, 230);
    draw->AddText(ImVec2(position.x - 1, position.y), black, text);
    draw->AddText(ImVec2(position.x + 1, position.y), black, text);
    draw->AddText(ImVec2(position.x, position.y - 1), black, text);
    draw->AddText(ImVec2(position.x, position.y + 1), black, text);
    draw->AddText(position, color, text);
}

void CopyName(const char (&input)[128], char (&output)[128]) {
    std::size_t length = 0;
    while (length < sizeof(output) - 1 && input[length]) {
        const unsigned char ch = static_cast<unsigned char>(input[length]);
        output[length] = ch < 32 || ch == 127 ? ' ' : input[length];
        ++length;
    }
    output[length] = '\0';
    if (!length) std::snprintf(output, sizeof(output), "Player");
}

void CenteredText(ImDrawList* draw, float center, float y, float width, ImU32 color, const char* text) {
    const float textWidth = ImGui::CalcTextSize(text).x;
    const float x = std::clamp(center - textWidth * 0.5f, 1.0f, std::max(1.0f, width - textWidth - 1.0f));
    OutlinedText(draw, ImVec2(x, y), color, text);
}
} // namespace

EspProjectionResult EspProjectBox(const EspVec3 (&corners)[8], const EspMatrix4x4& matrix,
                                 float width, float height, EspScreenRect& output) noexcept {
    output = {};
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
        return EspProjectionResult::InvalidInput;
    for (const auto& row : matrix.m)
        for (float value : row)
            if (!std::isfinite(value)) return EspProjectionResult::InvalidInput;

    ClipPoint clip[8]{};
    bool anyInFront = false;
    for (int i = 0; i < 8; ++i) {
        const auto& world = corners[i];
        if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z))
            return EspProjectionResult::InvalidInput;
        clip[i] = {
            matrix.m[0][0] * world.x + matrix.m[0][1] * world.y + matrix.m[0][2] * world.z + matrix.m[0][3],
            matrix.m[1][0] * world.x + matrix.m[1][1] * world.y + matrix.m[1][2] * world.z + matrix.m[1][3],
            matrix.m[3][0] * world.x + matrix.m[3][1] * world.y + matrix.m[3][2] * world.z + matrix.m[3][3]
        };
        if (!Finite(clip[i])) return EspProjectionResult::InvalidInput;
        anyInFront |= clip[i].w >= nearW;
    }
    if (!anyInFront) return EspProjectionResult::BehindCamera;

    float left = std::numeric_limits<float>::max(), top = left;
    float right = -left, bottom = -left;
    const auto include = [&](const ClipPoint& point) {
        const float x = (point.x / point.w * 0.5f + 0.5f) * width;
        const float y = (0.5f - point.y / point.w * 0.5f) * height;
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        left = std::min(left, x); right = std::max(right, x);
        top = std::min(top, y); bottom = std::max(bottom, y);
        return true;
    };
    for (int i = 0; i < 8; ++i) {
        if (clip[i].w >= nearW && !include(clip[i])) return EspProjectionResult::InvalidInput;
        // Each of the twelve box edges occurs once through the bit-index contract.
        for (int bit = 1; bit <= 4; bit <<= 1) {
            if (i & bit) continue;
            const auto& a = clip[i];
            const auto& b = clip[i | bit];
            if ((a.w >= nearW) == (b.w >= nearW)) continue;
            const float t = (nearW - a.w) / (b.w - a.w);
            const ClipPoint intersection { a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), nearW };
            if (!include(intersection)) return EspProjectionResult::InvalidInput;
        }
    }
    if (right < 0 || bottom < 0 || left > width || top > height) return EspProjectionResult::Offscreen;
    output = { std::clamp(left, 0.0f, width), std::clamp(top, 0.0f, height),
               std::clamp(right, 0.0f, width), std::clamp(bottom, 0.0f, height) };
    if (output.right - output.left < 1 || output.bottom - output.top < 1)
        return EspProjectionResult::Degenerate;
    return EspProjectionResult::Visible;
}

void EspSetLogCallback(EspLogCallback callback) noexcept {
    logger = callback;
    LogSettings();
}

const EspSettings& EspGetSettings() noexcept { return settings; }

void EspPollControls(bool foreground) noexcept {
    bool changed = false;
    for (int i = 0; i < 9; ++i) {
        const bool down = (GetAsyncKeyState(keys[i]) & 0x8000) != 0 ||
            (i == 7 && (GetAsyncKeyState(VK_SUBTRACT) & 0x8000) != 0) ||
            (i == 8 && (GetAsyncKeyState(VK_ADD) & 0x8000) != 0);
        // Record held keys even in the background so focus changes do not toggle settings.
        const bool pressed = foreground && down && !keysDown[i];
        keysDown[i] = down;
        if (!pressed) continue;
        changed = true;
        switch (i) {
        case 0: settings.enabled = !settings.enabled; break;
        case 1: settings.names = !settings.names; break;
        case 2: settings.boxes = !settings.boxes; break;
        case 3: settings.distance = !settings.distance; break;
        case 4: settings.health = !settings.health; break;
        case 5: settings.teammates = !settings.teammates; break;
        case 6: settings.armor = !settings.armor; break;
        case 7: settings.maxDistanceMeters = std::max(10.0f, settings.maxDistanceMeters - 25.0f); break;
        case 8: settings.maxDistanceMeters = std::min(1000.0f, settings.maxDistanceMeters + 25.0f); break;
        default: break;
        }
    }
    if (changed) LogSettings();
}

EspDrawStats EspDraw(const EspFrameSnapshot& frame, float width, float height) {
    EspDrawStats stats{};
    stats.sourceValid = frame.valid;
    stats.matrixValid = frame.matrixValid;
    stats.distanceAvailable = frame.distanceAvailable;
    stats.enabled = settings.enabled;
    if (!frame.valid || !frame.matrixValid || !settings.enabled ||
        (!frame.players && frame.playerCount) || !ImGui::GetCurrentContext()) return stats;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float fontHeight = ImGui::GetFontSize();
    for (std::size_t i = 0; i < frame.playerCount; ++i) {
        const EspPlayerSnapshot& player = frame.players[i];
        ++stats.considered;
        if (player.localPlayer) { ++stats.local; continue; }
        if (player.health <= 0) { ++stats.dead; continue; }
        if (!settings.teammates && frame.localTeam > 0 && player.team == frame.localTeam) {
            ++stats.teammates; continue;
        }
        if (frame.distanceAvailable) {
            if (!std::isfinite(player.distanceMeters) || player.distanceMeters < 0) {
                ++stats.invalidDistance; continue;
            }
            if (player.distanceMeters > settings.maxDistanceMeters) { ++stats.distanceFiltered; continue; }
        }
        EspScreenRect box{};
        switch (EspProjectBox(player.worldCorners, frame.worldToClip, width, height, box)) {
        case EspProjectionResult::InvalidInput: ++stats.invalidProjection; continue;
        case EspProjectionResult::BehindCamera: ++stats.behindCamera; continue;
        case EspProjectionResult::Offscreen: ++stats.offscreen; continue;
        case EspProjectionResult::Degenerate: ++stats.degenerate; continue;
        case EspProjectionResult::Visible: break;
        }
        ++stats.projected;
        const int verticesBefore = draw->VtxBuffer.Size;
        const ImU32 color = TeamColor(player.team);
        const ImU32 black = IM_COL32(0, 0, 0, 240);
        const ImU32 white = IM_COL32(255, 255, 255, 255);
        if (settings.boxes) {
            draw->AddRect(ImVec2(box.left, box.top), ImVec2(box.right, box.bottom), black, 0, 0, 3);
            draw->AddRect(ImVec2(box.left, box.top), ImVec2(box.right, box.bottom), color, 0, 0, 1);
        }
        const float center = (box.left + box.right) * 0.5f;
        if (settings.names) {
            char name[128]{};
            CopyName(player.name, name);
            CenteredText(draw, center, std::max(1.0f, box.top - fontHeight - 3), width, color, name);
        }
        if (settings.health) {
            const float ratio = static_cast<float>(std::clamp(player.health, 0, 100)) / 100.0f;
            const float barX = std::max(1.0f, box.left - 7);
            draw->AddRectFilled(ImVec2(barX - 1, box.top - 1), ImVec2(barX + 4, box.bottom + 1), black);
            draw->AddRectFilled(ImVec2(barX, box.bottom - (box.bottom - box.top) * ratio),
                                ImVec2(barX + 3, box.bottom), IM_COL32(80, 225, 100, 255));
        }
        char details[96]{};
        std::size_t used = 0;
        if (settings.distance && frame.distanceAvailable)
            used = static_cast<std::size_t>(std::snprintf(details, sizeof(details), "%.0fm", player.distanceMeters));
        if (settings.health)
            used += static_cast<std::size_t>(std::snprintf(details + used, sizeof(details) - used,
                "%s%d HP", used ? " | " : "", std::clamp(player.health, 0, 999)));
        if (settings.armor)
            std::snprintf(details + used, sizeof(details) - used, "%s%d AP", used ? " | " : "",
                          std::clamp(player.armor, 0, 999));
        if (details[0])
            CenteredText(draw, center, std::min(box.bottom + 3, std::max(1.0f, height - fontHeight - 1)), width, white, details);
        if (draw->VtxBuffer.Size > verticesBefore) ++stats.drawn;
    }
    return stats;
}
