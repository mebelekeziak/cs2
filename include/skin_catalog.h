#pragma once

#include <span>

// Immutable metadata extracted from installed Valve item/localization resources.
// No process memory, installed files, or inventory are modified by this catalog.
namespace skin_catalog {
enum class ItemKind { Weapon, Knife, Gloves };
struct Item {
    int definition_index;
    const char* internal_name;
    const char* display_name;
    const char* model_path;
    ItemKind kind;
    int team_mask; // T = 1, CT = 2; follows installed used_by_classes metadata.
};
struct Paint {
    int id;
    const char* internal_name;
    const char* display_name;
    float wear_min;
    float wear_max;
    bool use_legacy_model;
};
std::span<const Item> items() noexcept;
std::span<const Paint> paints() noexcept;
const char* source_description() noexcept;
const Item* find_item(int definition_index) noexcept;
const Paint* find_paint(int paint_id) noexcept;
// Zero means the definition's default finish. Nonzero compatibility comes from
// installed generated inventory icons and collection/loot-list references.
bool supports_paint(int definition_index, int paint_id) noexcept;
std::span<const int> compatible_paints(int definition_index) noexcept;
} // namespace skin_catalog
