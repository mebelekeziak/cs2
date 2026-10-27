#include "skin_catalog.h"

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace skin_catalog {
namespace {
struct PaintRange {
    int definition_index;
    std::size_t begin;
    std::size_t count;
};
#include "skin_catalog_data.inc"
} // namespace

std::span<const Item> items() noexcept { return kItems; }
std::span<const Paint> paints() noexcept { return kPaints; }
const char* source_description() noexcept { return kSourceDescription; }

const Item* find_item(int definition_index) noexcept {
    const auto found = std::lower_bound(std::begin(kItems), std::end(kItems), definition_index,
        [](const Item& item, int index) { return item.definition_index < index; });
    return found != std::end(kItems) && found->definition_index == definition_index ? found : nullptr;
}

const Paint* find_paint(int paint_id) noexcept {
    const auto found = std::lower_bound(std::begin(kPaints), std::end(kPaints), paint_id,
        [](const Paint& paint, int id) { return paint.id < id; });
    return found != std::end(kPaints) && found->id == paint_id ? found : nullptr;
}

std::span<const int> compatible_paints(int definition_index) noexcept {
    const auto range = std::lower_bound(std::begin(kPaintRanges), std::end(kPaintRanges), definition_index,
        [](const PaintRange& entry, int index) { return entry.definition_index < index; });
    if (range == std::end(kPaintRanges) || range->definition_index != definition_index) return {};
    return {kCompatiblePaints + range->begin, range->count};
}

bool supports_paint(int definition_index, int paint_id) noexcept {
    const auto supported = compatible_paints(definition_index);
    return std::binary_search(supported.begin(), supported.end(), paint_id);
}
} // namespace skin_catalog
