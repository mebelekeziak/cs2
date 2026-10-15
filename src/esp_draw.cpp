#include "esp_draw.h"

#include <Windows.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>

namespace {
EspSettings settings;
EspLogCallback logger = nullptr;
bool keysDown[7]{};
constexpr float nearW = 0.01f;
constexpr int keys[7] = { VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7 };

struct ClipPoint { float x, y, w; };

bool Finite(const ClipPoint& point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.w);
}

void LogSettings() noexcept {
    if (!logger) return;
    char message[768]{};
    std::snprintf(message, sizeof(message),
        "ESP_SETTINGS enabled=%d names=%d boxes=%d distance=%d health=%d teammates=%d armor=%d range=unlimited "
        "team_colors=%d box_style=%d thickness=%.2f fill=%d font=%d name_size=%.1f detail_size=%.1f "
        "box_rgba=%.2f,%.2f,%.2f,%.2f name_rgba=%.2f,%.2f,%.2f,%.2f",
        settings.enabled, settings.names, settings.boxes, settings.distance, settings.health,
        settings.teammates, settings.armor, settings.teamColors, settings.boxStyle, settings.boxThickness,
        settings.boxFill, settings.nameFont, settings.nameSize, settings.detailSize,
        settings.boxColor[0], settings.boxColor[1], settings.boxColor[2], settings.boxColor[3],
        settings.nameColor[0], settings.nameColor[1], settings.nameColor[2], settings.nameColor[3]);
    logger(message);
}

ImU32 Color(const float (&rgba)[4]) {
    return ImGui::ColorConvertFloat4ToU32({ rgba[0], rgba[1], rgba[2], rgba[3] });
}

ImU32 BoxColor(int team) {
    if (!settings.teamColors) return Color(settings.boxColor);
    if (team == 2) return Color(settings.tColor);
    if (team == 3) return Color(settings.ctColor);
    return Color(settings.boxColor);
}

constexpr const char* fontNames[] = { "", "ESP Segoe UI Bold", "ESP Tahoma", "ESP Consolas" };
constexpr const wchar_t* fontFiles[] = { L"", L"segoeuib.ttf", L"tahoma.ttf", L"consola.ttf" };

// Resolve through the current atlas instead of retaining pointers across context destruction.
ImFont* FindFont(int choice) noexcept {
    if (!ImGui::GetCurrentContext() || choice < 1 || choice > 3) return nullptr;
    for (const ImFontConfig& source : ImGui::GetIO().Fonts->Sources)
        if (std::strcmp(source.Name, fontNames[choice]) == 0) return source.DstFont;
    return nullptr;
}

ImFont* SelectedFont() {
    if (ImFont* font = FindFont(settings.nameFont)) return font;
    return ImGui::GetFont();
}

void LoadFont(ImFontAtlas& atlas, int choice) {
    wchar_t directory[MAX_PATH]{}, path[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(directory, MAX_PATH);
    if (!length || length + 8 + std::wcslen(fontFiles[choice]) >= MAX_PATH ||
        swprintf_s(path, L"%ls\\Fonts\\%ls", directory, fontFiles[choice]) <= 0) return;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 100 || size.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(file);
        return;
    }
    void* bytes = ImGui::MemAlloc(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const bool complete = bytes && ReadFile(file, bytes, static_cast<DWORD>(size.QuadPart), &read, nullptr) &&
                          read == static_cast<DWORD>(size.QuadPart);
    CloseHandle(file);
    if (!complete) { ImGui::MemFree(bytes); return; }
    ImFontConfig config{};
    strcpy_s(config.Name, fontNames[choice]);
    config.FontDataOwnedByAtlas = true;
    // Rasterize once before backend creation; changing face or size never rebuilds the texture.
    // Windows owns the installed font files; only their in-memory atlas data is retained.
    atlas.AddFontFromMemoryTTF(bytes, static_cast<int>(size.QuadPart), 20, &config);
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

void CenteredText(ImDrawList* draw, ImFont* font, float size, float center, float y,
                  const EspScreenRect& viewport, ImU32 color, const char* text) {
    const float textWidth = font->CalcTextSizeA(size, FLT_MAX, 0, text).x;
    const float x = std::clamp(center - textWidth * 0.5f, viewport.left + 1,
                             std::max(viewport.left + 1, viewport.right - textWidth - 1));
    const ImVec4 clip { viewport.left, viewport.top, viewport.right, viewport.bottom };
    if (settings.textOutline && (color & IM_COL32_A_MASK)) {
        // Keep outlines within the foreground's opacity, including completely hidden text.
        const float alpha = static_cast<float>((color >> IM_COL32_A_SHIFT) & 255) / 255.0f;
        const ImU32 outline = ImGui::ColorConvertFloat4ToU32({ settings.outlineColor[0],
            settings.outlineColor[1], settings.outlineColor[2], settings.outlineColor[3] * alpha });
        for (const ImVec2 offset : { ImVec2(-1, 0), ImVec2(1, 0), ImVec2(0, -1), ImVec2(0, 1) })
            draw->AddText(font, size, { x + offset.x, y + offset.y }, outline, text, nullptr, 0, &clip);
    }
    draw->AddText(font, size, { x, y }, color, text, nullptr, 0, &clip);
}

void StrokeBox(ImDrawList* draw, const EspScreenRect& box, ImU32 color, float thickness) {
    if (settings.boxStyle == 0) {
        draw->AddRect({ box.left, box.top }, { box.right, box.bottom }, color, 0, 0, thickness);
        return;
    }
    const float dx = (box.right - box.left) * 0.25f, dy = (box.bottom - box.top) * 0.25f;
    for (int corner = 0; corner < 4; ++corner) {
        const float x = (corner & 1) ? box.right : box.left;
        const float y = (corner & 2) ? box.bottom : box.top;
        const float sx = (corner & 1) ? -dx : dx, sy = (corner & 2) ? -dy : dy;
        const ImVec2 points[] = { { x + sx, y }, { x, y }, { x, y + sy } };
        draw->AddPolyline(points, 3, color, 0, thickness);
    }
}

void DrawPlayer(ImDrawList* draw, const EspPlayerSnapshot& player, const EspScreenRect& box,
                const EspScreenRect& viewport, bool validDistance) {
    const ImU32 color = BoxColor(player.team);
    if (settings.boxes) {
        if (settings.boxFill)
            draw->AddRectFilled({ box.left, box.top }, { box.right, box.bottom }, Color(settings.fillColor));
        if (settings.boxOutline && (color & IM_COL32_A_MASK)) {
            const float alpha = static_cast<float>((color >> IM_COL32_A_SHIFT) & 255) / 255.0f;
            const ImU32 outline = ImGui::ColorConvertFloat4ToU32({ settings.outlineColor[0],
                settings.outlineColor[1], settings.outlineColor[2], settings.outlineColor[3] * alpha });
            StrokeBox(draw, box, outline, settings.boxThickness + 2);
        }
        StrokeBox(draw, box, color, settings.boxThickness);
    }
    const float center = (box.left + box.right) * 0.5f;
    if (settings.names) {
        char name[128]{};
        CopyName(player.name, name);
        CenteredText(draw, SelectedFont(), settings.nameSize, center,
            std::max(viewport.top + 1, box.top - settings.nameSize - 3), viewport,
            settings.nameUseBoxColor ? color : Color(settings.nameColor), name);
    }
    if (settings.health) {
        const float ratio = static_cast<float>(std::clamp(player.health, 0, 100)) / 100.0f;
        const float barX = std::max(viewport.left + 1, box.left - settings.healthBarWidth - 4);
        float rgba[4]{};
        for (int component = 0; component < 4; ++component)
            rgba[component] = settings.healthGradient ? settings.healthLowColor[component] +
                ratio * (settings.healthColor[component] - settings.healthLowColor[component]) :
                settings.healthColor[component];
        // Match background opacity to the bar so alpha zero really hides it.
        draw->AddRectFilled({ barX - 1, box.top - 1 }, { barX + settings.healthBarWidth + 1, box.bottom + 1 },
            ImGui::ColorConvertFloat4ToU32({ 0, 0, 0, rgba[3] * 0.94f }));
        draw->AddRectFilled({ barX, box.bottom - (box.bottom - box.top) * ratio },
                            { barX + settings.healthBarWidth, box.bottom }, Color(rgba));
    }
    char details[96]{};
    std::size_t used = 0;
    if (settings.distance && validDistance)
        used = static_cast<std::size_t>(std::snprintf(details, sizeof(details), "%.0fm", player.distanceMeters));
    if (settings.health && settings.healthText)
        used += static_cast<std::size_t>(std::snprintf(details + used, sizeof(details) - used,
            "%s%d HP", used ? " | " : "", std::clamp(player.health, 0, 999)));
    if (settings.armor)
        std::snprintf(details + used, sizeof(details) - used, "%s%d AP", used ? " | " : "",
                      std::clamp(player.armor, 0, 999));
    if (details[0])
        CenteredText(draw, ImGui::GetFont(), settings.detailSize, center,
            std::min(box.bottom + 3, std::max(viewport.top + 1, viewport.bottom - settings.detailSize - 1)),
            viewport, Color(settings.detailColor), details);
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

EspSettings EspSanitizeSettings(const EspSettings& value) noexcept {
    EspSettings result = value;
    const EspSettings defaults{};
    const auto bounded = [](float value, float minimum, float maximum, float fallback) {
        return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
    };
    const auto color = [&](float (&rgba)[4], const float (&fallback)[4]) {
        for (int i = 0; i < 4; ++i) rgba[i] = bounded(rgba[i], 0, 1, fallback[i]);
    };
    color(result.boxColor, defaults.boxColor);
    color(result.tColor, defaults.tColor);
    color(result.ctColor, defaults.ctColor);
    color(result.fillColor, defaults.fillColor);
    color(result.nameColor, defaults.nameColor);
    color(result.detailColor, defaults.detailColor);
    color(result.outlineColor, defaults.outlineColor);
    color(result.healthColor, defaults.healthColor);
    color(result.healthLowColor, defaults.healthLowColor);
    result.boxStyle = std::clamp(result.boxStyle, 0, 1);
    result.nameFont = std::clamp(result.nameFont, 0, 3);
    result.boxThickness = bounded(result.boxThickness, 0.5f, 5, defaults.boxThickness);
    result.nameSize = bounded(result.nameSize, 10, 28, defaults.nameSize);
    result.detailSize = bounded(result.detailSize, 9, 22, defaults.detailSize);
    result.healthBarWidth = bounded(result.healthBarWidth, 1, 8, defaults.healthBarWidth);
    return result;
}

void EspSetSettings(const EspSettings& value) noexcept {
    const EspSettings sanitized = EspSanitizeSettings(value);
    if (settings == sanitized) return;
    settings = sanitized;
    LogSettings();
}

void EspResetStyle() noexcept {
    EspSettings defaults{};
    defaults.enabled = settings.enabled;
    defaults.names = settings.names;
    defaults.boxes = settings.boxes;
    defaults.distance = settings.distance;
    defaults.health = settings.health;
    defaults.teammates = settings.teammates;
    defaults.armor = settings.armor;
    EspSetSettings(defaults);
}

void EspConfigureFonts() {
    if (!ImGui::GetCurrentContext()) return;
    ImFontAtlas& atlas = *ImGui::GetIO().Fonts;
    if (atlas.Locked || atlas.IsBuilt()) return;
    // Keep the embedded menu default first in diagnostic/headless contexts too.
    if (atlas.Fonts.empty()) ImGui::GetIO().FontDefault = atlas.AddFontDefault();
    for (int choice = 1; choice <= 3; ++choice)
        if (!FindFont(choice)) LoadFont(atlas, choice);
}

bool EspFontAvailable(int font) noexcept {
    return ImGui::GetCurrentContext() && (font == 0 || FindFont(font) != nullptr);
}

void EspDrawPreview(float width, float height) {
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) return;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy({ width, height });
    if (!ImGui::IsItemVisible()) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const EspScreenRect viewport { origin.x, origin.y, origin.x + width, origin.y + height };
    draw->PushClipRect(origin, { viewport.right, viewport.bottom }, true);
    draw->AddRectFilled(origin, { viewport.right, viewport.bottom }, IM_COL32(19, 24, 31, 255));
    const int samples = width >= 300 ? 2 : 1;
    const float halfBoxWidth = std::min(width / samples * 0.25f, std::max(1.0f, height - 70) * 0.25f);
    for (int sample = 0; sample < samples; ++sample) {
        const float center = origin.x + width * (static_cast<float>(sample) + 0.5f) / samples;
        const EspScreenRect box { center - halfBoxWidth, origin.y + 36,
                                  center + halfBoxWidth, origin.y + height - 34 };
        EspPlayerSnapshot player{};
        const bool terrorist = samples == 2 && sample == 0;
        strcpy_s(player.name, terrorist ? "T preview" : "CT preview");
        player.team = terrorist ? 2 : 3;
        player.health = 72;
        player.armor = 50;
        player.distanceMeters = 24;
        if (settings.enabled && box.bottom > box.top && box.right > box.left)
            DrawPlayer(draw, player, box, viewport, true);
    }
    draw->PopClipRect();
}

void EspPollControls(bool foreground) noexcept {
    bool changed = false;
    for (int i = 0; i < 7; ++i) {
        const bool down = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
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
    // Keep player primitives behind either menu so the controls remain readable.
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    for (std::size_t i = 0; i < frame.playerCount; ++i) {
        const EspPlayerSnapshot& player = frame.players[i];
        ++stats.considered;
        if (player.localPlayer) { ++stats.local; continue; }
        if (player.health <= 0) { ++stats.dead; continue; }
        if (!settings.teammates && frame.localTeam > 0 && player.team == frame.localTeam) {
            ++stats.teammates; continue;
        }
        const bool validDistance = frame.distanceAvailable &&
            std::isfinite(player.distanceMeters) && player.distanceMeters >= 0;
        if (frame.distanceAvailable && !validDistance) ++stats.invalidDistance;
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
        DrawPlayer(draw, player, box, { 0, 0, width, height }, validDistance);
        if (draw->VtxBuffer.Size > verticesBefore) ++stats.drawn;
    }
    return stats;
}
