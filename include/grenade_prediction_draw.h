#pragma once
#include "grenade_prediction.h"
#include "grenade_settings.h"
#include <span>

struct GrenadePredictionVisual {
    GrenadePath path;
    bool enemy = false;
};
struct GrenadePredictionDrawStats {
    std::size_t segments = 0;
    std::size_t bounces = 0;
    std::size_t landings = 0;
};
// Shares the trajectory's homogeneous clipping rules; markers outside the
// viewport or behind the camera are omitted rather than pinned to an edge.
bool GrenadePredictionProjectPoint(const EspVec3&, const EspMatrix4x4&,
    float width, float height, float (&screen)[2]) noexcept;
GrenadePredictionDrawStats GrenadePredictionDraw(std::span<const GrenadePredictionVisual>,
    const EspMatrix4x4&, float width, float height, const GrenadeSettings&);
