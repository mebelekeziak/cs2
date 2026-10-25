#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct SkyboxCatalogEntry {
    std::string path; // Logical Source 2 .vmat or .vtex resource, never a menu index.
    std::string label;
    bool custom = false;
};
using SkyboxCatalog = std::vector<SkyboxCatalogEntry>;
std::filesystem::path SkyboxCatalogDirectory();
const SkyboxCatalog& SkyboxCatalogBuiltins() noexcept;
// Explicit refresh only. Reads loose compiled resources under root/materials;
// never follows symlinks/reparse points. Failure retains the previous snapshot.
bool SkyboxCatalogDiscover(const std::filesystem::path& root, std::string& error) noexcept;
// Always nonnull, including before the first refresh. Immutable copied ownership.
std::shared_ptr<const SkyboxCatalog> SkyboxCatalogSnapshot() noexcept;
bool SkyboxCatalogContains(std::string_view logicalPath) noexcept;
