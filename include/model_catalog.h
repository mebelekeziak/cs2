#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct ModelCatalogEntry {
    std::string path;
    std::string label;
};
using ModelCatalog = std::vector<ModelCatalogEntry>;

// Call outside DllMain. Reads loose .vmdl_c files only, without following links
// or Windows reparse directories. A failed refresh preserves the last catalog.
bool ModelCatalogDiscover(const std::filesystem::path& csgoRoot, std::string& error) noexcept;
// Immutable shared snapshots avoid copying thousands of strings each UI frame.
// Null before the first successful discovery. Filenames do not verify rig fit.
std::shared_ptr<const ModelCatalog> ModelCatalogSnapshot() noexcept;
