#include "model_resource_validation.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
constexpr std::size_t MaxTree = 64 * 1024 * 1024;
constexpr std::size_t MaxReferenceBlock = 4 * 1024 * 1024;
constexpr std::size_t MaxResourceName = 1024;
constexpr std::string_view WorldGraph = "animation/graphs/worldmodel/worldmodel.vnmgraph";

template<class T> T Read(const std::vector<std::uint8_t>& data, std::size_t at) {
    if (at > data.size() || sizeof(T) > data.size() - at)
        throw std::runtime_error("Truncated resource metadata");
    T value{};
    std::memcpy(&value, data.data() + at, sizeof(value));
    return value;
}

std::vector<std::uint8_t> Bytes(std::ifstream& stream, std::uint64_t at, std::size_t count) {
    if (at > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
        throw std::runtime_error("Invalid metadata offset");
    std::vector<std::uint8_t> result(count);
    stream.seekg(static_cast<std::streamoff>(at));
    if (!stream || (count && !stream.read(reinterpret_cast<char*>(result.data()),
                                        static_cast<std::streamsize>(count))))
        throw std::runtime_error("Cannot read resource metadata");
    return result;
}

std::uint64_t FileSize(std::ifstream& stream) {
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    if (size < 0) throw std::runtime_error("Cannot read resource length");
    return static_cast<std::uint64_t>(size);
}

std::string Name(const std::vector<std::uint8_t>& bytes, std::size_t& at) {
    const auto start = at;
    while (at < bytes.size() && bytes[at] && at - start <= MaxResourceName) ++at;
    if (at >= bytes.size() || at - start > MaxResourceName)
        throw std::runtime_error("Invalid metadata string");
    std::string result(reinterpret_cast<const char*>(bytes.data() + start), at - start);
    ++at;
    return result;
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
        auto a = current->wstring(), b = part.wstring();
        std::transform(a.begin(), a.end(), a.begin(), [](wchar_t c) { return std::towlower(c); });
        std::transform(b.begin(), b.end(), b.begin(), [](wchar_t c) { return std::towlower(c); });
        if (a != b) return false;
        ++current;
    }
    return current != value.end();
}

bool Loose(const std::filesystem::path& root, const std::string& name) {
    std::error_code ec;
    const auto candidate = root / std::filesystem::path(name);
    if (!std::filesystem::is_regular_file(candidate, ec) || ec) return false;
    const auto resolved = std::filesystem::weakly_canonical(candidate, ec);
    return !ec && Within(resolved, root);
}

void ReadVpk(const std::filesystem::path& path, std::unordered_set<std::string>& names) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) throw std::runtime_error("Cannot inspect VPK directory");
        return; // A loose-only fixture or installation does not need a VPK.
    }
    std::ifstream stream(path, std::ios::binary);
    const auto size = FileSize(stream);
    const auto header = Bytes(stream, 0, 12);
    const auto version = Read<std::uint32_t>(header, 4);
    const auto treeSize = Read<std::uint32_t>(header, 8);
    if (Read<std::uint32_t>(header, 0) != 0x55aa1234 || (version != 1 && version != 2))
        throw std::runtime_error("Unsupported VPK directory header");
    const std::uint64_t headerSize = version == 1 ? 12 : 28;
    if (!treeSize || treeSize > MaxTree || headerSize > size || treeSize > size - headerSize)
        throw std::runtime_error("Invalid VPK directory size");
    auto inlineEnd = size;
    if (version == 2) {
        const auto extended = Bytes(stream, 0, 28);
        const auto dataSize = Read<std::uint32_t>(extended, 12);
        if (dataSize > size - headerSize - treeSize)
            throw std::runtime_error("Invalid VPK inline data size");
        inlineEnd = headerSize + treeSize + dataSize;
    }
    const auto tree = Bytes(stream, headerSize, treeSize);
    std::size_t at{}, count{};
    std::unordered_map<std::uint16_t, std::uint64_t> archiveSizes;
    archiveSizes.emplace(std::uint16_t{0x7fff}, inlineEnd);
    for (;;) {
        const auto extension = Name(tree, at);
        if (extension.empty()) break;
        for (;;) {
            const auto folder = Name(tree, at);
            if (folder.empty()) break;
            for (;;) {
                const auto base = Name(tree, at);
                if (base.empty()) break;
                if (++count > 1000000 || at > tree.size() || 18 > tree.size() - at)
                    throw std::runtime_error("Invalid VPK directory entry");
                const auto preload = Read<std::uint16_t>(tree, at + 4);
                const auto archive = Read<std::uint16_t>(tree, at + 6);
                const auto offset = Read<std::uint32_t>(tree, at + 8);
                const auto length = Read<std::uint32_t>(tree, at + 12);
                if (Read<std::uint16_t>(tree, at + 16) != 0xffff)
                    throw std::runtime_error("Invalid VPK entry terminator");
                at += 18;
                if (preload > tree.size() - at) throw std::runtime_error("Truncated VPK preload");
                at += preload;
                auto name = (folder == " " ? "" : folder + '/') + base +
                            (extension == " " ? "" : '.' + extension);
                if (!Normalize(name)) throw std::runtime_error("Invalid VPK resource path");
                if (!length && preload) {
                    names.insert(std::move(name));
                    continue;
                }
                // An index entry alone does not establish the data archive exists.
                std::filesystem::path archivePath = path;
                std::uint64_t dataStart = headerSize + treeSize;
                if (archive != 0x7fff) {
                    auto filename = path.filename().string();
                    const auto suffix = filename.rfind("_dir.vpk");
                    if (suffix == std::string::npos) throw std::runtime_error("Invalid VPK index name");
                    auto number = std::to_string(archive);
                    while (number.size() < 3) number.insert(number.begin(), '0');
                    filename.replace(suffix, 8, '_' + number + ".vpk");
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
    if (at != tree.size()) throw std::runtime_error("Unexpected VPK directory tail");
}

std::vector<std::string> References(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    const auto size = FileSize(stream);
    const auto header = Bytes(stream, 0, 16);
    const auto declaredSize = Read<std::uint32_t>(header, 0);
    const auto blockOffset = Read<std::uint32_t>(header, 8);
    const auto count = Read<std::uint32_t>(header, 12);
    if (declaredSize < 16 || declaredSize > size || Read<std::uint16_t>(header, 4) != 12 ||
        Read<std::uint16_t>(header, 6) != 1 || !count || count > 512)
        throw std::runtime_error("Unsupported compiled model header");
    const std::uint64_t tableAt = 8ull + blockOffset;
    if (tableAt < 16 || tableAt > declaredSize || count * 12ull > declaredSize - tableAt)
        throw std::runtime_error("Invalid compiled model block table");
    const auto table = Bytes(stream, tableAt, count * 12);
    bool found{};
    std::vector<std::string> result;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto at = static_cast<std::size_t>(i) * 12;
        const auto blockAt = tableAt + at + 4 + Read<std::uint32_t>(table, at + 4);
        const auto blockSize = Read<std::uint32_t>(table, at + 8);
        if (blockAt > declaredSize || blockSize > declaredSize - blockAt)
            throw std::runtime_error("Compiled model block outside file");
        if (std::memcmp(table.data() + at, "RERL", 4) != 0) continue;
        if (found || blockSize > MaxReferenceBlock)
            throw std::runtime_error("Invalid compiled model reference block");
        found = true;
        const auto data = Bytes(stream, blockAt, blockSize);
        const auto entries = Read<std::uint32_t>(data, 0);
        const auto references = Read<std::uint32_t>(data, 4);
        if (entries < 8 || entries > data.size() || !references || references > 4096 ||
            references * 16ull > data.size() - entries)
            throw std::runtime_error("Invalid compiled model reference table");
        for (std::uint32_t j = 0; j < references; ++j) {
            const auto field = static_cast<std::size_t>(entries) + j * 16 + 8;
            const auto relative = Read<std::int64_t>(data, field);
            if (relative < -static_cast<std::int64_t>(field) ||
                relative >= static_cast<std::int64_t>(data.size() - field))
                throw std::runtime_error("Invalid compiled model reference offset");
            auto stringAt = static_cast<std::size_t>(static_cast<std::int64_t>(field) + relative);
            auto name = Name(data, stringAt);
            if (!Normalize(name)) throw std::runtime_error("Invalid compiled model reference path");
            result.push_back(std::move(name));
        }
    }
    if (!found) throw std::runtime_error("Compiled model has no external references");
    return result;
}
}

bool ModelResourceIndex::Build(const std::filesystem::path& csgoRoot, std::string& error) {
    ready_ = false;
    packed_.clear();
    error.clear();
    try {
        std::error_code ec;
        csgoRoot_ = std::filesystem::weakly_canonical(csgoRoot, ec);
        if (ec || !std::filesystem::is_directory(csgoRoot_))
            throw std::runtime_error("CS2 csgo directory is unavailable");
        coreRoot_ = std::filesystem::weakly_canonical(csgoRoot_.parent_path() / "core", ec);
        if (ec) throw std::runtime_error("Cannot resolve CS2 core directory");
        ReadVpk(csgoRoot_ / "pak01_dir.vpk", packed_);
        ReadVpk(coreRoot_ / "pak01_dir.vpk", packed_);
        ready_ = true;
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        packed_.clear();
        return false;
    }
}

bool ModelResourceIndex::Validate(const std::filesystem::path& fullModelPath, std::string& reason) const {
    reason.clear();
    try {
        if (!ready_) throw std::runtime_error("Model resource index is unavailable");
        std::error_code ec;
        const auto path = std::filesystem::weakly_canonical(fullModelPath, ec);
        if (ec || !Within(path, csgoRoot_) || !std::filesystem::is_regular_file(path))
            throw std::runtime_error("Model is outside the discovered CS2 directory");
        const auto refs = References(path);
        if (std::find(refs.begin(), refs.end(), WorldGraph) == refs.end())
            throw std::runtime_error("Model does not reference the installed AG2 world graph");
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
