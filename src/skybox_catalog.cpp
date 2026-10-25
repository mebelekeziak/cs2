#include "skybox_catalog.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>

#ifndef CS2_WORKSPACE_ROOT
#define CS2_WORKSPACE_ROOT L"."
#endif

namespace {
SkyboxCatalog builtins;
std::once_flag builtinOnce;
void InitializeBuiltins() {
    builtins = {
    {"materials/skybox/sky_de_dust2.vmat", "Dust II"},
    {"materials/skybox/sky_de_mirage.vmat", "Mirage"},
    {"materials/skybox/sky_de_nuke.vmat", "Nuke"},
    {"materials/skybox/sky_de_vertigo.vmat", "Vertigo"},
    {"materials/skybox/sky_de_overpass_01.vmat", "Overpass"},
    {"materials/skybox/sky_de_train03.vmat", "Train"},
    {"materials/skybox/sky_csgo_cloudy01.vmat", "Cloudy"},
    {"materials/skybox/sky_rain_night_01.vmat", "Rainy night"},
    {"materials/skybox/sky_black.vmat", "Black"}
    };
}
std::mutex mutex;
std::shared_ptr<const SkyboxCatalog> snapshot;
bool Fail(std::string& error, const char* message) { error = message; return false; }
bool Reparse(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}
std::string Logical(const std::filesystem::path& path) {
    // Native resources in this catalog deliberately use portable ASCII names.
    std::string text;
    for (const auto c : path.generic_wstring()) {
        if (c < 32 || c > 126 || c == ':' || c == '"' || c == '?' || c == '*') return {};
        text.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c));
    }
    return text;
}
template<class T> bool Read(std::ifstream& file, std::uint64_t at, T& result) {
    file.seekg(static_cast<std::streamoff>(at));
    return static_cast<bool>(file.read(reinterpret_cast<char*>(&result), sizeof(result)));
}
bool Resource(const std::filesystem::path& path) {
    std::error_code ec;
    const auto length = std::filesystem::file_size(path, ec);
    if (ec || length < 16 || length > 256ULL * 1024 * 1024) return false;
    std::ifstream file(path, std::ios::binary);
    std::uint32_t size = 0, blockOffset = 0, count = 0;
    std::uint16_t headerVersion = 0, resourceVersion = 0;
    if (!Read(file, 0, size) || !Read(file, 4, headerVersion) || !Read(file, 6, resourceVersion) ||
        !Read(file, 8, blockOffset) || !Read(file, 12, count) || headerVersion != 12 ||
        size < 16 || size > length || count == 0 || count > 64 ||
        static_cast<std::uint64_t>(blockOffset) + 8 + count * 12 > size) return false;
    bool data = false;
    for (unsigned i = 0; i < count; ++i) {
        const std::uint64_t at = 8ULL + blockOffset + i * 12;
        std::array<char, 4> kind{};
        std::uint32_t offset = 0, bytes = 0;
        if (!Read(file, at, kind) || !Read(file, at + 4, offset) || !Read(file, at + 8, bytes)) return false;
        const auto begin = at + 4 + offset;
        if (begin > size || bytes > size - begin) return false;
        if (std::memcmp(kind.data(), "DATA", 4)) continue;
        if (data) return false;
        data = true;
        // Source2Viewer Texture.Read / VTexFlags: modern version-1 single cube.
        // https://github.com/ValveResourceFormat/ValveResourceFormat/blob/master/ValveResourceFormat/Resource/ResourceTypes/Texture.cs
        std::uint16_t version = 0, flags = 0, width = 0, height = 0, depth = 0;
        unsigned char mips = 0;
        if (!resourceVersion || bytes < 40 || !Read(file, begin, version) || !Read(file, begin + 2, flags) ||
            !Read(file, begin + 20, width) || !Read(file, begin + 22, height) || !Read(file, begin + 24, depth) ||
            !Read(file, begin + 27, mips) || version != 1 || (flags & 0x10) == 0 ||
            (flags & 0x60) != 0 || width == 0 || width != height || width > 16384 || depth != 1 || mips == 0 || mips > 15)
            return false;
    }
    return data;
}
std::string FriendlyLabel(std::string path) {
    std::size_t prefix = 10; // Keep subfolders for nonstandard material roots.
    for (const std::string_view folder : {"materials/skies/", "materials/skybox/"})
        if (path.starts_with(folder)) prefix = folder.size();
    auto label = path.substr(prefix, path.size() - prefix - 5);
    if (label.ends_with(".generated")) label.resize(label.size() - 10);
    const auto underscore = label.rfind('_');
    if (underscore != label.npos) {
        const auto hash = std::string_view(label).substr(underscore + 1);
        if (hash.size() >= 6 && hash.size() <= 16 && std::all_of(hash.begin(), hash.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })) label.resize(underscore);
    }
    bool removed = true;
    while (removed) {
        removed = false;
        for (const std::string_view suffix : {"_exr", "_pfm", "_tga", "_png", "_cubemap", "_cube"}) {
            if (label.size() > suffix.size() && label.ends_with(suffix)) {
                label.resize(label.size() - suffix.size());
                removed = true;
                break;
            }
        }
    }
    std::replace(label.begin(), label.end(), '_', ' ');
    return "Custom: " + label;
}
bool Discover(const std::filesystem::path& root, std::string& error) {
    error.clear();
    std::call_once(builtinOnce, InitializeBuiltins);
    auto result = std::make_shared<SkyboxCatalog>(builtins);
    std::error_code ec;
    const bool exists = std::filesystem::exists(root, ec);
    if (ec) return Fail(error, "Cannot inspect custom skybox folder.");
    if (exists) {
        if (!std::filesystem::is_directory(root, ec) || ec || Reparse(root))
            return Fail(error, "Custom skybox folder must be a regular directory.");
        const auto materials = root / L"materials";
        const bool hasMaterials = std::filesystem::exists(materials, ec);
        if (ec) return Fail(error, "Cannot inspect skybox materials folder.");
        if (hasMaterials) {
            if (Reparse(materials) || !std::filesystem::is_directory(materials, ec) || ec)
                return Fail(error, "Skybox materials folder must be a regular directory.");
            std::set<std::string> known;
            for (const auto& entry : builtins) known.insert(entry.path);
            unsigned visited = 0;
            std::filesystem::recursive_directory_iterator it(materials, ec), end;
            if (ec) return Fail(error, "Cannot list custom skyboxes.");
            for (; it != end; it.increment(ec)) {
                if (ec) return Fail(error, "Cannot finish listing custom skyboxes.");
                if (++visited > 8192 || it.depth() > 16) return Fail(error, "Custom skybox catalog exceeds its scan limit.");
                if (Reparse(it->path())) { it.disable_recursion_pending(); continue; }
                if (!it->is_regular_file(ec)) { if (ec) return Fail(error, "Cannot inspect skybox file."); continue; }
                auto path = Logical(it->path().lexically_relative(root));
                const bool texture = path.ends_with(".vtex_c");
                // The workspace asset root is dedicated to skyboxes; accept
                // metadata-validated cubes anywhere under its materials/ tree.
                // Community cubemaps use our verified sky.vfx recipe, while
                // arbitrary material shaders need a separate native contract.
                if (!texture || path.size() >= 256 || !Resource(it->path())) continue;
                path.resize(path.size() - 2);
                if (!known.insert(path).second) continue;
                auto label = FriendlyLabel(path);
                result->push_back({std::move(path), std::move(label), true});
                if (result->size() > 1024) return Fail(error, "Too many custom skyboxes (limit 1015).");
            }
            if (ec) return Fail(error, "Cannot finish listing custom skyboxes.");
            std::sort(result->begin() + builtins.size(), result->end(), [](const auto& a, const auto& b) { return a.path < b.path; });
        }
    }
    std::lock_guard<std::mutex> lock(mutex);
    snapshot = std::move(result);
    return true;
}
}
std::filesystem::path SkyboxCatalogDirectory() { return std::filesystem::path(CS2_WORKSPACE_ROOT) / L"skyboxes"; }
const SkyboxCatalog& SkyboxCatalogBuiltins() noexcept {
    try { std::call_once(builtinOnce, InitializeBuiltins); } catch (...) {}
    return builtins;
}
bool SkyboxCatalogDiscover(const std::filesystem::path& root, std::string& error) noexcept {
    try { return Discover(root, error); }
    catch (...) { error = "Cannot refresh custom skybox catalog."; return false; }
}
std::shared_ptr<const SkyboxCatalog> SkyboxCatalogSnapshot() noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    if (!snapshot) {
        try { snapshot = std::make_shared<const SkyboxCatalog>(SkyboxCatalogBuiltins()); } catch (...) {}
    }
    return snapshot;
}
bool SkyboxCatalogContains(std::string_view logicalPath) noexcept {
    const auto current = SkyboxCatalogSnapshot();
    return current && std::any_of(current->begin(), current->end(), [&](const auto& entry) { return entry.path == logicalPath; });
}
