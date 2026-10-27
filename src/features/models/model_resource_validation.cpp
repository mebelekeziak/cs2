#include "model_resource_validation.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <exception>
#include <fstream>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
constexpr std::size_t MaxTree = 64 * 1024 * 1024;
constexpr std::size_t MaxReferenceBlock = 4 * 1024 * 1024;
constexpr std::size_t MaxResourceName = 1024;
constexpr std::string_view WorldGraph = "animation/graphs/worldmodel/worldmodel.vnmgraph";

bool Fail(std::string& error, const char* message) {
    error = message;
    return false;
}

// Rejected metadata is routine during discovery. Do not throw here: the payload
// can be manually mapped, where ordinary C++ exception image lookup is unavailable.
template<class T> bool Read(const std::vector<std::uint8_t>& data, std::size_t at,
                            T& value, std::string& error) {
    if (at > data.size() || sizeof(T) > data.size() - at)
        return Fail(error, "Truncated resource metadata");
    std::memcpy(&value, data.data() + at, sizeof(value));
    return true;
}

bool Bytes(std::ifstream& stream, std::uint64_t at, std::size_t count,
           std::vector<std::uint8_t>& result, std::string& error) {
    if (at > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        count > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        return Fail(error, "Invalid metadata offset");
    result.resize(count);
    stream.seekg(static_cast<std::streamoff>(at));
    if (!stream || (count && !stream.read(reinterpret_cast<char*>(result.data()),
                                        static_cast<std::streamsize>(count))))
        return Fail(error, "Cannot read resource metadata");
    return true;
}

bool FileSize(std::ifstream& stream, std::uint64_t& result, std::string& error) {
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    if (size < 0) return Fail(error, "Cannot read resource length");
    result = static_cast<std::uint64_t>(size);
    return true;
}

bool Name(const std::vector<std::uint8_t>& bytes, std::size_t& at,
          std::string& result, std::string& error) {
    const auto start = at;
    while (at < bytes.size() && bytes[at] && at - start <= MaxResourceName) ++at;
    if (at >= bytes.size() || at - start > MaxResourceName)
        return Fail(error, "Invalid metadata string");
    result.assign(reinterpret_cast<const char*>(bytes.data() + start), at - start);
    ++at;
    return true;
}

bool Normalize(std::string& value) {
    if (value.empty() || value.size() > MaxResourceName) return false;
    for (char& c : value) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 32 || byte == 127 || c == ':') return false;
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (value.front() == '/' || value.back() == '/') return false;
    std::size_t start{};
    while (start < value.size()) {
        const auto end = value.find('/', start);
        const auto part = std::string_view(value).substr(start,
            (end == std::string::npos ? value.size() : end) - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
            return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

bool Within(const std::filesystem::path& value, const std::filesystem::path& root) {
    auto current = value.begin();
    for (const auto& part : root) {
        if (current == value.end()) return false;
        auto a = current->native(), b = part.native();
        std::transform(a.begin(), a.end(), a.begin(), [](wchar_t c) { return std::towlower(c); });
        std::transform(b.begin(), b.end(), b.begin(), [](wchar_t c) { return std::towlower(c); });
        if (a != b) return false;
        ++current;
    }
    return current != value.end();
}

bool Loose(const std::filesystem::path& root, const std::string& name) {
    // Compiled resource names use UTF-8. Malformed names cannot reach a throwing
    // std::filesystem narrow-to-native conversion while checking dependencies.
    if (name.empty() || name.size() > MaxResourceName + 2) return false;
    const auto length = static_cast<int>(name.size());
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), length, nullptr, 0);
    if (!count) return false;
    std::wstring native(static_cast<std::size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), length, native.data(), count))
        return false;
    std::error_code ec;
    const auto candidate = root / std::filesystem::path(native);
    if (!std::filesystem::is_regular_file(candidate, ec) || ec) return false;
    const auto resolved = std::filesystem::weakly_canonical(candidate, ec);
    return !ec && Within(resolved, root);
}

bool ReadVpk(const std::filesystem::path& path, std::unordered_set<std::string>& names,
             std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) return Fail(error, "Cannot inspect VPK directory");
        return true; // A loose-only fixture or installation does not need a VPK.
    }
    std::ifstream stream(path, std::ios::binary);
    std::uint64_t size{};
    std::vector<std::uint8_t> header;
    std::uint32_t signature{}, version{}, treeSize{};
    if (!FileSize(stream, size, error) || !Bytes(stream, 0, 12, header, error) ||
        !Read(header, 0, signature, error) || !Read(header, 4, version, error) ||
        !Read(header, 8, treeSize, error)) return false;
    if (signature != 0x55aa1234 || (version != 1 && version != 2))
        return Fail(error, "Unsupported VPK directory header");
    const std::uint64_t headerSize = version == 1 ? 12 : 28;
    if (!treeSize || treeSize > MaxTree || headerSize > size || treeSize > size - headerSize)
        return Fail(error, "Invalid VPK directory size");
    auto inlineEnd = size;
    if (version == 2) {
        std::vector<std::uint8_t> extended;
        std::uint32_t dataSize{};
        if (!Bytes(stream, 0, 28, extended, error) || !Read(extended, 12, dataSize, error)) return false;
        if (dataSize > size - headerSize - treeSize)
            return Fail(error, "Invalid VPK inline data size");
        inlineEnd = headerSize + treeSize + dataSize;
    }
    std::vector<std::uint8_t> tree;
    if (!Bytes(stream, headerSize, treeSize, tree, error)) return false;
    std::size_t at{}, count{};
    std::unordered_map<std::uint16_t, std::uint64_t> archiveSizes;
    archiveSizes.emplace(std::uint16_t{0x7fff}, inlineEnd);
    for (;;) {
        std::string extension;
        if (!Name(tree, at, extension, error)) return false;
        if (extension.empty()) break;
        for (;;) {
            std::string folder;
            if (!Name(tree, at, folder, error)) return false;
            if (folder.empty()) break;
            for (;;) {
                std::string base;
                if (!Name(tree, at, base, error)) return false;
                if (base.empty()) break;
                if (++count > 1000000 || at > tree.size() || 18 > tree.size() - at)
                    return Fail(error, "Invalid VPK directory entry");
                std::uint16_t preload{}, archive{}, terminator{};
                std::uint32_t offset{}, length{};
                if (!Read(tree, at + 4, preload, error) || !Read(tree, at + 6, archive, error) ||
                    !Read(tree, at + 8, offset, error) || !Read(tree, at + 12, length, error) ||
                    !Read(tree, at + 16, terminator, error)) return false;
                if (terminator != 0xffff) return Fail(error, "Invalid VPK entry terminator");
                at += 18;
                if (preload > tree.size() - at) return Fail(error, "Truncated VPK preload");
                at += preload;
                auto name = (folder == " " ? "" : folder + '/') + base +
                            (extension == " " ? "" : '.' + extension);
                if (!Normalize(name)) return Fail(error, "Invalid VPK resource path");
                if (!length && preload) {
                    names.insert(std::move(name));
                    continue;
                }
                // An index entry alone does not establish the data archive exists.
                std::filesystem::path archivePath = path;
                std::uint64_t dataStart = headerSize + treeSize;
                if (archive != 0x7fff) {
                    auto filename = path.filename().native();
                    const auto suffix = filename.rfind(L"_dir.vpk");
                    if (suffix == std::wstring::npos) return Fail(error, "Invalid VPK index name");
                    auto number = std::to_wstring(archive);
                    while (number.size() < 3) number.insert(number.begin(), L'0');
                    filename.replace(suffix, 8, L'_' + number + L".vpk");
                    archivePath = path.parent_path() / filename;
                    dataStart = 0;
                }
                auto cached = archiveSizes.find(archive);
                if (cached == archiveSizes.end()) {
                    const auto measured = std::filesystem::file_size(archivePath, ec);
                    cached = archiveSizes.emplace(archive, ec ? 0 : measured).first;
                    ec.clear();
                }
                const auto archiveSize = cached->second;
                if (archiveSize && dataStart <= archiveSize && offset <= archiveSize - dataStart &&
                    length <= archiveSize - dataStart - offset) names.insert(std::move(name));
            }
        }
    }
    if (at != tree.size()) return Fail(error, "Unexpected VPK directory tail");
    return true;
}

bool References(const std::filesystem::path& path, std::vector<std::string>& result,
                std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    std::uint64_t size{};
    std::vector<std::uint8_t> header;
    std::uint32_t declaredSize{}, blockOffset{}, count{};
    std::uint16_t headerVersion{}, resourceVersion{};
    if (!FileSize(stream, size, error) || !Bytes(stream, 0, 16, header, error) ||
        !Read(header, 0, declaredSize, error) || !Read(header, 4, headerVersion, error) ||
        !Read(header, 6, resourceVersion, error) || !Read(header, 8, blockOffset, error) ||
        !Read(header, 12, count, error)) return false;
    if (declaredSize < 16 || declaredSize > size || headerVersion != 12 || resourceVersion != 1 ||
        !count || count > 512) return Fail(error, "Unsupported compiled model header");
    const std::uint64_t tableAt = 8ull + blockOffset;
    if (tableAt < 16 || tableAt > declaredSize || count * 12ull > declaredSize - tableAt)
        return Fail(error, "Invalid compiled model block table");
    std::vector<std::uint8_t> table;
    if (!Bytes(stream, tableAt, count * 12, table, error)) return false;
    bool found{};
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto at = static_cast<std::size_t>(i) * 12;
        std::uint32_t relativeBlock{}, blockSize{};
        if (!Read(table, at + 4, relativeBlock, error) || !Read(table, at + 8, blockSize, error)) return false;
        const auto blockAt = tableAt + at + 4 + relativeBlock;
        if (blockAt > declaredSize || blockSize > declaredSize - blockAt)
            return Fail(error, "Compiled model block outside file");
        if (std::memcmp(table.data() + at, "RERL", 4) != 0) continue;
        if (found || blockSize > MaxReferenceBlock)
            return Fail(error, "Invalid compiled model reference block");
        found = true;
        std::vector<std::uint8_t> data;
        if (!Bytes(stream, blockAt, blockSize, data, error)) return false;
        std::uint32_t entries{}, references{};
        if (!Read(data, 0, entries, error) || !Read(data, 4, references, error)) return false;
        if (entries < 8 || entries > data.size() || !references || references > 4096 ||
            references * 16ull > data.size() - entries)
            return Fail(error, "Invalid compiled model reference table");
        for (std::uint32_t j = 0; j < references; ++j) {
            const auto field = static_cast<std::size_t>(entries) + j * 16 + 8;
            std::int64_t relative{};
            if (!Read(data, field, relative, error)) return false;
            if (relative < -static_cast<std::int64_t>(field) ||
                relative >= static_cast<std::int64_t>(data.size() - field))
                return Fail(error, "Invalid compiled model reference offset");
            auto stringAt = static_cast<std::size_t>(static_cast<std::int64_t>(field) + relative);
            std::string name;
            if (!Name(data, stringAt, name, error)) return false;
            if (!Normalize(name)) return Fail(error, "Invalid compiled model reference path");
            result.push_back(std::move(name));
        }
    }
    if (!found) return Fail(error, "Compiled model has no external references");
    return true;
}
}

bool ModelResourceIndex::Build(const std::filesystem::path& csgoRoot, std::string& error) {
    ready_ = false;
    packed_.clear();
    error.clear();
    try {
        std::error_code ec;
        csgoRoot_ = std::filesystem::weakly_canonical(csgoRoot, ec);
        if (ec || !std::filesystem::is_directory(csgoRoot_, ec) || ec)
            return Fail(error, "CS2 csgo directory is unavailable");
        coreRoot_ = std::filesystem::weakly_canonical(csgoRoot_.parent_path() / L"core", ec);
        if (ec) return Fail(error, "Cannot resolve CS2 core directory");
        if (!ReadVpk(csgoRoot_ / L"pak01_dir.vpk", packed_, error) ||
            !ReadVpk(coreRoot_ / L"pak01_dir.vpk", packed_, error)) {
            packed_.clear();
            return false;
        }
        ready_ = true;
        return true;
    } catch (const std::exception& ex) {
        // Allocation failures remain exceptional; malformed files and expected
        // filesystem failures have already returned an explicit failure status.
        error = ex.what();
        packed_.clear();
        return false;
    }
}

bool ModelResourceIndex::Validate(const std::filesystem::path& fullModelPath, std::string& reason) const {
    reason.clear();
    try {
        if (!ready_) return Fail(reason, "Model resource index is unavailable");
        std::error_code ec;
        const auto path = std::filesystem::weakly_canonical(fullModelPath, ec);
        if (ec || !Within(path, csgoRoot_) || !std::filesystem::is_regular_file(path, ec) || ec)
            return Fail(reason, "Model is outside the discovered CS2 directory");
        std::vector<std::string> refs;
        if (!References(path, refs, reason)) return false;
        if (std::find(refs.begin(), refs.end(), WorldGraph) == refs.end())
            return Fail(reason, "Model does not reference the installed AG2 world graph");
        for (const auto& ref : refs) {
            const auto compiled = ref + "_c";
            if (!packed_.contains(compiled) && !Loose(csgoRoot_, compiled) && !Loose(coreRoot_, compiled)) {
                reason = "Missing model dependency: " + ref;
                return false;
            }
        }
        return true;
    } catch (const std::exception& ex) {
        reason = ex.what();
        return false;
    }
}
