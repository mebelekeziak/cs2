#pragma once

struct ZoomSettings {
    bool enabled = false;
    float zoom1 = 100.0f;
    float zoom2 = 100.0f;
    float sensitivity = 100.0f;
    bool removeScope = false;

    bool operator==(const ZoomSettings&) const = default;
};

ZoomSettings ZoomSanitizeSettings(const ZoomSettings& settings) noexcept;
ZoomSettings ZoomGetSettings() noexcept;
void ZoomSetSettings(const ZoomSettings& settings) noexcept;
void ZoomResetSettings() noexcept;
