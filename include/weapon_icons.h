#pragma once

#include <cstdint>
#include <imgui.h>

inline constexpr std::uint16_t WeaponIconPlantedC4 = 0xFFFE;

// Names match installed item definitions. Artwork is loaded from a locally
// generated font containing installed CS2 HUD SVGs; see assets/README.md.
const char* WeaponIconName(std::uint16_t definition) noexcept;
// Once per ImGui atlas, before building its GPU texture; never during a frame.
// Missing local artwork leaves icons unavailable, without fabricated substitutes.
void WeaponIconConfigure();
bool WeaponIconAvailable(std::uint16_t definition) noexcept;

// Render-owner only, during an ImGui frame. Preserve actual silhouette aspect
// inside a 3*height by height box centered on center; C4/planted C4 use 1.3*height.
// Unknown definitions, missing glyphs, invalid or transparent inputs emit nothing.
// Supported height: 4..128 pixels. Atlas pointers are never retained globally.
bool WeaponIconDraw(ImDrawList& draw, ImVec2 center, std::uint16_t definition,
                    float height, ImU32 color);
