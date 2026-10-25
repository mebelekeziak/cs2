#pragma once

struct ViewmodelSettings {
    bool enabled = false;
    // Additive offsets in the view's right, forward and up directions.
    float x = 0;
    float y = 0;
    float z = 0;
    float fov = 68;
    bool removeCameraRecoil = false;

    bool operator==(const ViewmodelSettings&) const = default;
};

ViewmodelSettings ViewmodelSanitizeSettings(const ViewmodelSettings& settings) noexcept;
ViewmodelSettings ViewmodelGetSettings() noexcept;
void ViewmodelSetSettings(const ViewmodelSettings& settings) noexcept;
void ViewmodelResetSettings() noexcept;
