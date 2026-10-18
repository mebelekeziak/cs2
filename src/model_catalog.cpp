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
#include <exception>
#include <limits>
#include <mutex>
#include <string_view>

namespace {
std::mutex catalogMutex;
std::shared_ptr<const ModelCatalog> currentCatalog;
bool Fail(std::string& error, const char* message) {
    error = message;
    return false;
}
bool Utf8(const std::filesystem::path& path, std::string& result, std::string& error) {
    const auto& native = path.native();
    if (native.empty()) { result.clear(); return true; }
    if (native.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return Fail(error, "Model catalog entry path is too long.");
    const auto length = static_cast<int>(native.size());
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native.data(), length,
                                         nullptr, 0, nullptr, nullptr);
    if (!count) return Fail(error, "Cannot encode model catalog entry path.");
    result.resize(static_cast<std::size_t>(count));
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native.data(), length,
                             result.data(), count, nullptr, nullptr))
        return Fail(error, "Cannot encode model catalog entry path.");
    for (auto& c : result) if (c == '\\') c = '/';
    return true;
}
std::string Lower(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}
bool ReparsePoint(const std::filesystem::path& path, bool& result, std::string& error) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return Fail(error, "Cannot inspect model catalog entry attributes.");
    result = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    return true;
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
    std::error_code ec;
    bool reparse{};
    if (!std::filesystem::is_directory(root, ec) || ec)
        return Fail(error, "Model catalog root is not a regular directory.");
    if (!ReparsePoint(root, reparse, error)) return false;
    if (reparse) return Fail(error, "Model catalog root is not a regular directory.");
    auto found = std::make_shared<ModelCatalog>();
    ModelResourceIndex resources;
    if (!resources.Build(root, error)) return false;
    for (const auto* folder : {L"agents", L"characters", L"models", L"phase2"}) {
        const auto directory = root / folder;
        const bool exists = std::filesystem::exists(directory, ec);
        if (ec) return Fail(error, "Cannot inspect model catalog directory.");
        if (!exists) continue;
        const bool isDirectory = std::filesystem::is_directory(directory, ec);
        if (ec) return Fail(error, "Cannot inspect model catalog directory.");
        if (!isDirectory) continue;
        if (!ReparsePoint(directory, reparse, error)) return false;
        if (reparse) continue;
        std::filesystem::recursive_directory_iterator iterator(directory, ec), end;
        if (ec) return Fail(error, "Cannot enumerate model catalog directory.");
        for (; iterator != end; iterator.increment(ec)) {
            const auto& entry = *iterator;
            if (!ReparsePoint(entry.path(), reparse, error)) return false;
            if (reparse) {
                iterator.disable_recursion_pending();
                continue;
            }
            const bool regular = entry.is_regular_file(ec);
            if (ec) return Fail(error, "Cannot inspect model catalog file.");
            if (!regular) continue;
            std::string extension;
            if (!Utf8(entry.path().extension(), extension, error)) return false;
            if (Lower(std::move(extension)) != ".vmdl_c") continue;
            std::string relative;
            if (!Utf8(entry.path().lexically_relative(root), relative, error)) return false;
            relative = Lower(std::move(relative));
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
        if (ec) return Fail(error, "Cannot continue enumerating model catalog directory.");
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
