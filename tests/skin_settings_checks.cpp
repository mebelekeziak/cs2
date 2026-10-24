#include "skin_settings.h"
#include <cstdio>
#include <limits>
#include <string>

namespace {
unsigned checks = 0, failures = 0;
void Expect(bool result, const char* label) {
    ++checks;
    if (!result) { ++failures; std::printf("FAIL %s\n", label); }
}
}

int main() {
    Expect(SkinGetSettings() == SkinSettings{}, "all cosmetic overrides default off");
    SkinSettings settings;
    settings.enabled = true;
    auto* terrorist = SkinEnsureWeaponSettings(settings.teams[0], 7);
    auto* counterTerrorist = SkinEnsureWeaponSettings(settings.teams[1], 7);
    Expect(terrorist && counterTerrorist && terrorist != counterTerrorist, "same definition has independent team records");
    terrorist->enabled = counterTerrorist->enabled = true;
    terrorist->paintKit = 44;
    counterTerrorist->paintKit = 180;
    settings.teams[0].knife = {true, 500, 38, 27, 0.1f, 4};
    settings.teams[1].knife = {true, 507, 12, 88, 0.8f, -1};
    settings.teams[0].gloves = {true, 5030, 10018, 22, 0.2f};
    settings.teams[1].gloves = {true, 5034, 10033, 99, 0.4f};
    Expect(SkinSetCustomName(*terrorist, "Private loadout"), "bounded plain custom names are accepted");
    Expect(SkinSetCustomName(*terrorist, terrorist->customName.data()) &&
        std::string(terrorist->customName.data()) == "Private loadout", "name assignment safely accepts its own current buffer");
    SkinSetSettings(settings);
    Expect(SkinGetSettings() == settings, "weapons, knives and gloves publish independently for both sides");
    Expect(SkinFindWeaponSettings(settings.teams[0], 7) == terrorist &&
        SkinEnsureWeaponSettings(settings.teams[0], 7) == terrorist, "definition lookup reuses the same record");
    Expect(!SkinEnsureWeaponSettings(settings.teams[0], 0) && !SkinEnsureWeaponSettings(settings.teams[0], -1) &&
        !SkinEnsureWeaponSettings(settings.teams[0], 65536), "invalid definitions never allocate a record");
    for (const auto* invalid : {"bad=name", "bad#name", "bad;name", "bad\nname", " leading", "trailing "})
        Expect(!SkinSetCustomName(*terrorist, invalid) && std::string(terrorist->customName.data()) == "Private loadout",
            "unsafe config names preserve previous value");
    Expect(!SkinSetCustomName(*terrorist, std::string(64, 'x')), "custom name terminator always fits");
    auto malformed = settings;
    auto& invalid = malformed.teams[0].weapons[0];
    invalid.paintKit = -2;
    invalid.seed = 9000;
    invalid.wear = std::numeric_limits<float>::quiet_NaN();
    invalid.statTrak = -99;
    invalid.customName.fill('z');
    malformed.teams[0].weapons[1] = invalid;
    malformed.teams[1].knife.definitionIndex = -1;
    auto bounded = SkinSanitizeSettings(malformed);
    const auto& value = bounded.teams[0].weapons[0];
    Expect(value.paintKit == 0 && value.seed == 1000 && value.wear == 0.0001f && value.statTrak == -1 &&
        !value.customName[0], "malformed numeric and unterminated text fields cannot reach runtime");
    Expect(bounded.teams[0].weapons[1] == SkinItemSettings{} && bounded.teams[1].knife == SkinItemSettings{},
        "duplicate definitions and invalid model ids cannot create conflicting overrides");
    malformed.teams[0].weapons[0].wear = -1;
    malformed.teams[1].weapons[0].wear = std::numeric_limits<float>::infinity();
    malformed.teams[1].gloves.wear = 4;
    bounded = SkinSanitizeSettings(malformed);
    Expect(bounded.teams[0].weapons[0].wear == 0 && bounded.teams[1].weapons[0].wear == 0.0001f &&
        bounded.teams[1].gloves.wear == 1, "wear remains finite across all item kinds");
    SkinTeamLoadout full;
    for (std::size_t index = 0; index < SkinWeaponCapacity; ++index)
        Expect(SkinEnsureWeaponSettings(full, static_cast<int>(index) + 1) != nullptr, "every sparse slot can hold a distinct definition");
    Expect(!SkinEnsureWeaponSettings(full, 999) && SkinFindWeaponSettings(full, 1), "full loadout preserves existing records and rejects overflow");
    SkinSetRuntimeStatus(std::string(500, 'x').c_str());
    Expect(SkinRuntimeStatus().back() == '\0', "runtime diagnostics remain bounded and terminated");
    SkinResetSettings();
    Expect(SkinGetSettings() == SkinSettings{}, "reset clears both team loadouts and disables overrides");
    SkinSetRuntimeStatus(nullptr);
    Expect(!SkinRuntimeStatus()[0], "null status clears diagnostics");
    std::printf("skin_settings_checks: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
