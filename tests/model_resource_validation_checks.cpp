#include "model_resource_validation.h"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
int checks{}, failures{};
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", message); }
}
template<class T> void Put(Bytes& data, std::size_t at, T value) {
    if (at + sizeof(value) > data.size()) data.resize(at + sizeof(value));
    std::memcpy(data.data() + at, &value, sizeof(value));
}
void String(Bytes& data, const std::string& value) {
    data.insert(data.end(), value.begin(), value.end());
    data.push_back(0);
}
void Write(const std::filesystem::path& path, const Bytes& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}
Bytes Model(const std::vector<std::string>& refs) {
    Bytes data(36 + refs.size() * 16);
    Put(data, 4, std::uint16_t{12}); Put(data, 6, std::uint16_t{1});
    Put(data, 8, std::uint32_t{8}); Put(data, 12, std::uint32_t{1});
    std::memcpy(data.data() + 16, "RERL", 4);
    Put(data, 20, std::uint32_t{8});
    Put(data, 28, std::uint32_t{8}); Put(data, 32, static_cast<std::uint32_t>(refs.size()));
    for (std::size_t i = 0; i < refs.size(); ++i) {
        const auto field = 36 + i * 16 + 8;
        Put(data, field, static_cast<std::int64_t>(data.size() - field));
        String(data, refs[i]);
    }
    Put(data, 0, static_cast<std::uint32_t>(data.size()));
    Put(data, 24, static_cast<std::uint32_t>(data.size() - 28));
    return data;
}
Bytes Vpk(unsigned version = 2, std::uint16_t archive = 0x7fff, unsigned length = 1,
          bool preloadOnly = false) {
    Bytes tree;
    String(tree, "vnmgraph_c"); String(tree, "animation/graphs/worldmodel"); String(tree, "worldmodel");
    const auto record = tree.size(); tree.resize(record + 18);
    Put(tree, record + 4, static_cast<std::uint16_t>(preloadOnly ? 1 : 0));
    Put(tree, record + 6, archive); Put(tree, record + 12, static_cast<std::uint32_t>(length));
    Put(tree, record + 16, std::uint16_t{0xffff});
    if (preloadOnly) tree.push_back(17);
    tree.insert(tree.end(), {0, 0, 0});
    Bytes out(version == 1 ? 12 : 28);
    Put(out, 0, std::uint32_t{0x55aa1234}); Put(out, 4, static_cast<std::uint32_t>(version));
    Put(out, 8, static_cast<std::uint32_t>(tree.size()));
    if (version == 2) Put(out, 12, std::uint32_t{1});
    out.insert(out.end(), tree.begin(), tree.end());
    out.push_back(19);
    return out;
}
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--scan") {
        ModelResourceIndex index; std::string error;
        const std::filesystem::path root(argv[2]);
        if (!index.Build(root, error)) { std::printf("INDEX FAILED %s\n", error.c_str()); return 1; }
        unsigned scanned{}, accepted{};
        for (const auto* folder : {"agents", "characters", "models", "phase2"}) {
            for (const auto& item : std::filesystem::recursive_directory_iterator(root / folder)) {
                if (item.is_regular_file() && item.path().extension() == ".vmdl_c") {
                    ++scanned;
                    if (index.Validate(item.path(), error)) ++accepted;
                }
            }
        }
        std::printf("Compiled model direct-reference scan: %u accepted / %u scanned\n", accepted, scanned);
        return 0;
    }
    const auto fixtureParent = std::filesystem::absolute(std::filesystem::current_path());
    const auto fixture = fixtureParent / ("model-resource-fixture-" + std::to_string(GetCurrentProcessId()));
    const auto root = fixture / "csgo";
    const auto core = fixture / "core";
    const auto model = root / "agents/check/check.vmdl_c";
    const std::string graph = "animation/graphs/worldmodel/worldmodel.vnmgraph";
    const std::string material = "agents/check/check.vmat";
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(core);
    ModelResourceIndex index;
    std::string reason;
    Check(!index.Validate(model, reason), "validation requires built index");
    Check(!index.Build(fixture / "missing", reason), "missing root fails");
    Check(index.Build(root, reason), "loose-only index builds");
    Write(model, Model({graph, material}));
    Check(!index.Validate(model, reason), "missing graph rejected");
    Write(root / (graph + "_c"), {1});
    Check(!index.Validate(model, reason) && reason.find(material) != std::string::npos,
          "missing material is diagnosed");
    Write(core / (material + "_c"), {1});
    Check(index.Validate(model, reason) && reason.empty(), "loose resources resolve in csgo and core");
    Write(model, Model({"animation/kxnrl/graphs/worldmodel.vnmgraph"}));
    Check(!index.Validate(model, reason), "custom unverified AG2 graph rejected");
    Write(model, Model({"characters/models/shared/animgraphs/player_ct.vanmgrph"}));
    Check(!index.Validate(model, reason), "legacy graph rejected");
    Write(model, Model({graph, "../escape.vmat"}));
    Check(!index.Validate(model, reason), "dependency traversal rejected");
    Write(model, Model({graph, "C:/outside.vmat"}));
    Check(!index.Validate(model, reason), "absolute dependency rejected");
    Write(model, Model({graph}));
    Write(fixture / "outside.vmdl_c", Model({graph}));
    Check(!index.Validate(fixture / "outside.vmdl_c", reason), "model outside root rejected");
    auto malformed = Model({graph});
    Put(malformed, 4, std::uint16_t{11}); Write(model, malformed);
    Check(!index.Validate(model, reason), "unsupported resource header rejected");
    malformed = Model({graph}); Put(malformed, 12, std::uint32_t{0xffffffff}); Write(model, malformed);
    Check(!index.Validate(model, reason), "oversized block count rejected");
    malformed = Model({graph}); Put(malformed, 20, std::uint32_t{0xffffffff}); Write(model, malformed);
    Check(!index.Validate(model, reason), "block outside file rejected");
    malformed = Model({graph}); Put(malformed, 32, std::uint32_t{0xffffffff}); Write(model, malformed);
    Check(!index.Validate(model, reason), "oversized reference count rejected");
    malformed = Model({graph}); Put(malformed, 44, std::int64_t{-9999}); Write(model, malformed);
    Check(!index.Validate(model, reason), "negative overflowing string offset rejected");
    malformed = Model({graph}); malformed.back() = 1; Write(model, malformed);
    Check(!index.Validate(model, reason), "unterminated reference rejected");
    Write(model, {1, 2, 3});
    Check(!index.Validate(model, reason), "truncated header rejected");
    Write(model, Model({graph}));
    auto tailed = Model({graph}); tailed.insert(tailed.end(), {0xdd, 0xee, 0xff});
    Write(model, tailed);
    Check(index.Validate(model, reason), "declared resource bounds permit trailing pack data");
    Put(tailed, 0, static_cast<std::uint32_t>(tailed.size() + 1)); Write(model, tailed);
    Check(!index.Validate(model, reason), "declared resource larger than file rejected");
    Write(model, Model({graph}));
    std::filesystem::remove(root / (graph + "_c"));
    Write(root / "pak01_dir.vpk", Vpk());
    Check(index.Build(root, reason) && index.Validate(model, reason), "inline VPK v2 graph resolves");
    Write(root / "pak01_dir.vpk", Vpk(1));
    Check(index.Build(root, reason) && index.Validate(model, reason), "inline VPK v1 graph resolves");
    Write(root / "pak01_dir.vpk", Vpk(2, 0, 1));
    Check(index.Build(root, reason) && !index.Validate(model, reason), "missing numbered archive rejects reference");
    Write(root / "pak01_000.vpk", {1});
    Check(index.Build(root, reason) && index.Validate(model, reason), "numbered VPK archive resolves");
    Write(root / "pak01_dir.vpk", Vpk(2, 0, 2));
    Check(index.Build(root, reason) && !index.Validate(model, reason), "truncated numbered archive rejected");
    Write(root / "pak01_dir.vpk", Vpk(2, 11, 0, true));
    Check(index.Build(root, reason) && index.Validate(model, reason), "preload-only VPK entry needs no archive");
    std::filesystem::remove(root / "pak01_dir.vpk");
    Write(core / "pak01_dir.vpk", Vpk());
    Check(index.Build(root, reason) && index.Validate(model, reason), "core VPK references resolve");
    malformed = Vpk(); Put(malformed, 8, std::uint32_t{0xffffffff});
    Write(core / "pak01_dir.vpk", malformed);
    Check(!index.Build(root, reason), "oversized VPK tree rejected");
    Check(!index.Validate(model, reason), "failed rebuild invalidates prior index");
    malformed = Vpk(); Put(malformed, 12, std::uint32_t{0xffffffff});
    Write(core / "pak01_dir.vpk", malformed);
    Check(!index.Build(root, reason), "oversized VPK inline section rejected");
    Write(core / "pak01_dir.vpk", {1, 2, 3});
    Check(!index.Build(root, reason), "truncated VPK header rejected");
    // Only remove the unique directory created immediately above this test.
    if (fixture.parent_path() == fixtureParent && fixture.filename().wstring().starts_with(L"model-resource-fixture-"))
        std::filesystem::remove_all(fixture);
    std::printf("Model resource validation: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
