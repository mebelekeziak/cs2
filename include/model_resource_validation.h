#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>

// Disk-only discovery helper. Build once per catalog refresh, outside native
// callbacks/DllMain. VPK data archives and resource mesh payloads are not read.
// Expected metadata/I/O rejections return false; they must not use C++ throws
// as control flow inside the manually mapped payload.
class ModelResourceIndex {
public:
    bool Build(const std::filesystem::path& csgoRoot, std::string& error);
    bool Validate(const std::filesystem::path& fullModelPath, std::string& reason) const;

private:
    std::filesystem::path csgoRoot_;
    std::filesystem::path coreRoot_;
    std::unordered_set<std::string> packed_;
    bool ready_{};
};
