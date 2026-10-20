#pragma once
#include "esp_draw.h"
#include "grenade_settings.h"
#include <span>
#include <vector>

enum class GrenadeAreaKind { Smoke, Molotov };
struct GrenadeEdge { EspVec3 a{}, b{}; };
struct GrenadeDisk { EspVec3 center{}; float radius = 0; };
// Copied world-space footprints. No entity pointer survives acquisition.
struct GrenadeArea {
    GrenadeAreaKind kind = GrenadeAreaKind::Smoke;
    std::vector<GrenadeEdge> edges;
};
// Boundary of the union: intersections/covered arcs are removed, preserving
// separated islands and concavities rather than enclosing everything in a hull.
std::vector<GrenadeEdge> GrenadeDiskBoundary(std::span<const GrenadeDisk> disks);
// XY footprint of the native 32-cubed smoke simulation, projected onto its
// detonation plane. Occupancy is indexed x + 32*y; no nominal-radius circle.
std::vector<GrenadeEdge> GrenadeSmokeBoundary(std::span<const bool, 1024> columns,
    const EspVec3& origin, float cellSize);
bool GrenadeProjectEdge(const GrenadeEdge& edge, const EspMatrix4x4& matrix,
    float width, float height, float (&screen)[4]) noexcept;
std::size_t GrenadeDraw(std::span<const GrenadeArea> areas,
    const EspMatrix4x4& matrix, float width, float height, const GrenadeSettings& settings);
