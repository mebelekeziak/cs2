#pragma once

#include <array>
#include <string_view>

// The runtime resource name is relative to game/csgo, with forward slashes and
// the source resource suffix (.vmdl), never the on-disk compiled suffix.
struct ModelSwapSettings {
    bool enabled = false;
    std::array<char, 260> model{};

    bool operator==(const ModelSwapSettings&) const = default;
};

// Empty clears the selection. Invalid input leaves the supplied value intact.
bool ModelSwapSetModel(ModelSwapSettings& settings, std::string_view path) noexcept;
ModelSwapSettings ModelSwapSanitizeSettings(const ModelSwapSettings& settings) noexcept;
ModelSwapSettings ModelSwapGetSettings() noexcept;
void ModelSwapSetSettings(const ModelSwapSettings& settings) noexcept;
void ModelSwapResetSettings() noexcept;
using ModelSwapStatus = std::array<char, 256>;
ModelSwapStatus ModelSwapRuntimeStatus() noexcept;
void ModelSwapSetRuntimeStatus(const char* message) noexcept;
