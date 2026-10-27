#include "skin_settings.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
std::mutex settingsMutex;
SkinSettings currentSettings;
SkinStatus runtimeStatus = [] {
    SkinStatus value{};
    constexpr char message[] = "Runtime has not initialized. Saved selections are available.";
    std::copy_n(message, sizeof(message), value.begin());
    return value;
}();
bool ValidDefinition(int definitionIndex) noexcept { return definitionIndex > 0 && definitionIndex <= 65535; }
}

const SkinItemSettings* SkinFindWeaponSettings(const SkinTeamLoadout& loadout, int definitionIndex) noexcept {
    if (!ValidDefinition(definitionIndex)) return nullptr;
    for (const auto& item : loadout.weapons)
        if (item.definitionIndex == definitionIndex) return &item;
    return nullptr;
}

SkinItemSettings* SkinEnsureWeaponSettings(SkinTeamLoadout& loadout, int definitionIndex) noexcept {
    if (!ValidDefinition(definitionIndex)) return nullptr;
    for (auto& item : loadout.weapons)
        if (item.definitionIndex == definitionIndex) return &item;
    for (auto& item : loadout.weapons) {
        if (item.definitionIndex == 0) {
            item = {};
            item.definitionIndex = definitionIndex;
            return &item;
        }
    }
    return nullptr;
}

bool SkinSetCustomName(SkinItemSettings& settings, std::string_view name) noexcept {
    if (name.size() >= settings.customName.size()) return false;
    if (!name.empty() && (name.front() == ' ' || name.back() == ' ')) return false;
    for (const unsigned char character : name)
        if (character < 32 || character > 126 || character == '=' || character == '#' || character == ';') return false;
    std::array<char, 64> bounded{};
    std::copy(name.begin(), name.end(), bounded.begin());
    settings.customName = bounded;
    return true;
}

SkinItemSettings SkinSanitizeItemSettings(const SkinItemSettings& settings) noexcept {
    if (!ValidDefinition(settings.definitionIndex)) return {};
    auto result = settings;
    result.paintKit = std::max(0, result.paintKit);
    result.seed = std::clamp(result.seed, 0, 1000);
    result.wear = std::isfinite(result.wear) ? std::clamp(result.wear, 0.0f, 1.0f) : 0.0001f;
    result.statTrak = std::max(-1, result.statTrak);
    const auto end = std::find(settings.customName.begin(), settings.customName.end(), '\0');
    if (end == settings.customName.end() ||
        !SkinSetCustomName(result, {settings.customName.data(), static_cast<std::size_t>(end - settings.customName.begin())}))
        result.customName.fill('\0');
    return result;
}

SkinSettings SkinSanitizeSettings(const SkinSettings& settings) noexcept {
    auto result = settings;
    for (auto& team : result.teams) {
        for (std::size_t index = 0; index < team.weapons.size(); ++index) {
            auto& item = team.weapons[index];
            item = SkinSanitizeItemSettings(item);
            // First occurrence owns an identity; duplicate records cannot apply
            // conflicting overrides or consume capacity after loading a preset.
            for (std::size_t previous = 0; item.definitionIndex && previous < index; ++previous)
                if (team.weapons[previous].definitionIndex == item.definitionIndex) item = {};
        }
        team.knife = SkinSanitizeItemSettings(team.knife);
        team.gloves = SkinSanitizeItemSettings(team.gloves);
    }
    return result;
}

SkinSettings SkinGetSettings() noexcept {
    std::lock_guard<std::mutex> lock(settingsMutex);
    return currentSettings;
}
void SkinSetSettings(const SkinSettings& settings) noexcept {
    const auto sanitized = SkinSanitizeSettings(settings);
    std::lock_guard<std::mutex> lock(settingsMutex);
    currentSettings = sanitized;
}
void SkinResetSettings() noexcept { SkinSetSettings({}); }
SkinStatus SkinRuntimeStatus() noexcept {
    std::lock_guard<std::mutex> lock(settingsMutex);
    return runtimeStatus;
}
void SkinSetRuntimeStatus(const char* message) noexcept {
    SkinStatus bounded{};
    if (message)
        for (std::size_t index = 0; index + 1 < bounded.size() && message[index]; ++index) bounded[index] = message[index];
    std::lock_guard<std::mutex> lock(settingsMutex);
    runtimeStatus = bounded;
}
