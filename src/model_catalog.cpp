#include "model_catalog.h"
#include "model_swap_settings.h"
#include "model_resource_validation.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <string_view>

namespace {
std::mutex catalogMutex;
std::shared_ptr<const ModelCatalog> currentCatalog;
std::string Utf8(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
std::string Lower(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}
bool ReparsePoint(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("Cannot inspect model catalog entry attributes.");
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}
bool HelperName(std::string_view name) {
    if (name.find("nohitbox") != name.npos || name.find("no_hitbox") != name.npos) return true;
    std::size_t start = 0;
    while (start < name.size()) {
        const auto end = name.find_first_of("/ _-.()[]", start);
        const auto token = name.substr(start, end == name.npos ? name.size() - start : end - start);
        if (token == "arm" || token == "arms" || token == "null" || token == "helper") return true;
        if (end == name.npos) break;
        start = end + 1;
    }
    return false;
}
bool Discover(const std::filesystem::path& root, std::string& error) {
    error.clear();
    if (!std::filesystem::is_directory(root) || ReparsePoint(root)) {
        error = "Model catalog root is not a regular directory.";
        return false;
    }
    auto found = std::make_shared<ModelCatalog>();
    ModelResourceIndex resources;
    if (!resources.Build(root, error)) return false;
    for (const auto* folder : {L"agents", L"characters", L"models", L"phase2"}) {
        const auto directory = root / folder;
        if (!std::filesystem::exists(directory)) continue;
        if (!std::filesystem::is_directory(directory) || ReparsePoint(directory)) continue;
        std::filesystem::recursive_directory_iterator iterator(directory), end;
        for (; iterator != end; ++iterator) {
            const auto& entry = *iterator;
            if (ReparsePoint(entry.path())) {
                iterator.disable_recursion_pending();
                continue;
            }
            if (!entry.is_regular_file() || Lower(Utf8(entry.path().extension())) != ".vmdl_c") continue;
            auto relative = Lower(Utf8(entry.path().lexically_relative(root)));
            if (HelperName(relative)) continue;
            relative.resize(relative.size() - 2); // Runtime resource names omit _c.
            ModelSwapSettings validated;
            if (!ModelSwapSetModel(validated, relative)) continue;
            std::string reason;
            if (!resources.Validate(entry.path(), reason)) continue;
            const auto slash = relative.rfind('/');
            auto label = relative.substr(slash + 1, relative.size() - slash - 6);
            for (auto& c : label) if (c == '_') c = ' ';
            found->push_back({std::move(relative), std::move(label)});
        }
    }
    std::sort(found->begin(), found->end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    found->erase(std::unique(found->begin(), found->end(), [](const auto& a, const auto& b) { return a.path == b.path; }), found->end());
    std::lock_guard<std::mutex> lock(catalogMutex);
    currentCatalog = std::move(found);
    return true;
}
}

bool ModelCatalogDiscover(const std::filesystem::path& csgoRoot, std::string& error) noexcept {
    try { return Discover(csgoRoot, error); }
    catch (const std::exception& exception) {
        try { error = std::string("Cannot discover loose player models: ") + exception.what(); } catch (...) {}
        return false;
    } catch (...) {
        try { error = "Cannot discover loose player models."; } catch (...) {}
        return false;
    }
}
std::shared_ptr<const ModelCatalog> ModelCatalogSnapshot() noexcept {
    std::lock_guard<std::mutex> lock(catalogMutex);
    return currentCatalog;
}
