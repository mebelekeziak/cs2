#pragma once

#include <array>
#include <cstddef>
#include <string_view>

// Configuration identities come from the installed item schema, never menu row
// numbers. Team slots are stable on disk: 0 = Terrorists, 1 = Counter-Terrorists.
constexpr std::size_t SkinTeamCount = 2;
constexpr std::size_t SkinWeaponCapacity = 128;
enum class SkinTeam : std::size_t { Terrorist = 0, CounterTerrorist = 1 };

struct SkinItemSettings {
    bool enabled = false;
    int definitionIndex = 0;
    int paintKit = 0;
    int seed = 0;
    float wear = 0.0001f;
    int statTrak = -1; // -1 leaves StatTrak disabled; zero is a valid counter.
    std::array<char, 64> customName{};
    bool operator==(const SkinItemSettings&) const = default;
};

struct SkinTeamLoadout {
    // Sparse records keyed by definitionIndex. Zero denotes an unused slot.
    std::array<SkinItemSettings, SkinWeaponCapacity> weapons{};
    SkinItemSettings knife;
    SkinItemSettings gloves;
    bool operator==(const SkinTeamLoadout&) const = default;
};

struct SkinSettings {
    bool enabled = false;
    std::array<SkinTeamLoadout, SkinTeamCount> teams{};
    bool operator==(const SkinSettings&) const = default;
};

const SkinItemSettings* SkinFindWeaponSettings(const SkinTeamLoadout& loadout, int definitionIndex) noexcept;
SkinItemSettings* SkinEnsureWeaponSettings(SkinTeamLoadout& loadout, int definitionIndex) noexcept;
// Printable ASCII only; excludes config delimiters '=', '#' and ';'. Invalid
// input preserves the old name. Outer spaces are rejected for lossless config
// round trips. Empty clears it. Maximum 63 characters.
bool SkinSetCustomName(SkinItemSettings& settings, std::string_view name) noexcept;
SkinItemSettings SkinSanitizeItemSettings(const SkinItemSettings& settings) noexcept;
SkinSettings SkinSanitizeSettings(const SkinSettings& settings) noexcept;
SkinSettings SkinGetSettings() noexcept;
void SkinSetSettings(const SkinSettings& settings) noexcept;
void SkinResetSettings() noexcept;
using SkinStatus = std::array<char, 256>;
SkinStatus SkinRuntimeStatus() noexcept;
void SkinSetRuntimeStatus(const char* message) noexcept;
