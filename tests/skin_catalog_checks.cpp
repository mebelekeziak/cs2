#include "skin_catalog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

namespace {
unsigned checks = 0, failures = 0;
void Check(bool condition, const char* label) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", label); }
}
}

int main() {
    using namespace skin_catalog;
    const auto all_items = items();
    const auto all_paints = paints();
    Check(all_items.size() == 68, "installed item definition count");
    Check(all_paints.size() == 1480, "installed finishes including default");
    Check(std::strstr(source_description(), "1.41.9.2") != nullptr, "source version available");
    Check(std::count_if(all_items.begin(), all_items.end(), [](const Item& i) { return i.kind == ItemKind::Weapon; }) == 35,
          "every firearm and Zeus present");
    Check(std::count_if(all_items.begin(), all_items.end(), [](const Item& i) { return i.kind == ItemKind::Knife; }) == 23,
          "knife definitions present");
    Check(std::count_if(all_items.begin(), all_items.end(), [](const Item& i) { return i.kind == ItemKind::Gloves; }) == 10,
          "glove definitions present");

    std::set<int> observed_paints;
    std::size_t nondefault_pairs = 0;
    int previous_item = -1;
    for (const auto& item : all_items) {
        Check(item.definition_index > previous_item, "definition indices unique and sorted");
        previous_item = item.definition_index;
        Check(find_item(item.definition_index) == &item, "item lookup returns immutable record");
        Check(item.internal_name && *item.internal_name && item.display_name && *item.display_name,
              "definition names available");
        Check(item.model_path && std::strstr(item.model_path, ".vmdl") != nullptr, "installed model path available");
        Check(item.team_mask >= 0 && item.team_mask <= 3, "team availability bounded");
        const auto supported = compatible_paints(item.definition_index);
        Check(!supported.empty() && supported.front() == 0, "every definition supports default finish");
        Check(std::is_sorted(supported.begin(), supported.end()), "finish ids sorted");
        Check(std::adjacent_find(supported.begin(), supported.end()) == supported.end(), "finish ids unique");
        nondefault_pairs += supported.size() - 1;
        for (const int paint : supported) {
            Check(find_paint(paint) != nullptr, "compatible id resolves to finish");
            Check(supports_paint(item.definition_index, paint), "membership agrees with range");
            observed_paints.insert(paint);
        }
        Check(!supports_paint(item.definition_index, -1) && !supports_paint(item.definition_index, 9001),
              "invalid and workshop template finishes reject");
    }
    Check(nondefault_pairs == 2128, "installed nondefault compatibility pair count");
    Check(observed_paints.size() == all_paints.size(), "every finish has installed compatibility evidence");
    int previous_paint = -1;
    for (const auto& paint : all_paints) {
        Check(paint.id > previous_paint, "paint indices unique and sorted");
        previous_paint = paint.id;
        Check(find_paint(paint.id) == &paint, "finish lookup returns immutable record");
        Check(paint.internal_name && *paint.internal_name && paint.display_name && *paint.display_name,
              "finish names available");
        Check(std::isfinite(paint.wear_min) && std::isfinite(paint.wear_max) &&
              paint.wear_min >= 0.f && paint.wear_min <= paint.wear_max && paint.wear_max <= 1.f,
              "finish wear range bounded");
    }

    const auto* ak = find_item(7);
    const auto* m4 = find_item(16);
    const auto* zeus = find_item(31);
    const auto* knife = find_item(507);
    const auto* gloves = find_item(5030);
    Check(ak && ak->team_mask == 1 && m4 && m4->team_mask == 2, "weapon native team metadata");
    Check(zeus && zeus->kind == ItemKind::Weapon && compatible_paints(31).size() == 8, "Zeus finishes included");
    Check(knife && knife->kind == ItemKind::Knife && knife->team_mask == 3, "Karambit model valid for both teams");
    Check(gloves && gloves->kind == ItemKind::Gloves && gloves->team_mask == 3, "Sport Gloves valid for both teams");
    Check(supports_paint(7, 180) && !supports_paint(16, 180), "Fire Serpent belongs to AK not M4A4");
    Check(supports_paint(507, 418) && supports_paint(507, 419), "Doppler phase ids both present");
    Check(find_paint(418) && find_paint(419) &&
          std::strcmp(find_paint(418)->display_name, find_paint(419)->display_name) == 0 &&
          std::strcmp(find_paint(418)->internal_name, find_paint(419)->internal_name) != 0,
          "same localized finish name preserves phase identity");
    Check(supports_paint(5030, 10037) && !supports_paint(507, 10037), "glove finish cannot leak to knife");
    Check(supports_paint(5030, 1405), "latest installed glove finish included");
    Check(find_item(5028) && find_item(5028)->team_mask == 1 && find_item(5029) && find_item(5029)->team_mask == 2,
          "default glove side metadata");
    Check(find_item(-1) == nullptr && find_item(999999) == nullptr, "invalid item lookup rejects");
    Check(find_paint(-1) == nullptr && find_paint(9001) == nullptr && find_paint(999999) == nullptr,
          "invalid and workshop template paint lookup rejects");
    Check(compatible_paints(-1).empty() && compatible_paints(999999).empty() && !supports_paint(-1, 0),
          "unknown definition cannot support default or other finish");
    std::printf("skin_catalog_checks: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
