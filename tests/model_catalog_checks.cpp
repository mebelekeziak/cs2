#include "model_catalog.h"
#include "model_swap_settings.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <type_traits>

namespace {
int failures = 0;
void Expect(bool condition, const char* label) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", label);
    failures += !condition;
}
void Put32(std::vector<char>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<char>(value >> (i * 8));
}
void Put64(std::vector<char>& bytes, std::size_t offset, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) bytes[offset + i] = static_cast<char>(value >> (i * 8));
}
void Model(const std::filesystem::path& path, std::string_view reference = "animation/graphs/worldmodel/worldmodel.vnmgraph") {
    std::filesystem::create_directories(path.parent_path());
    std::vector<char> bytes(52 + reference.size() + 1);
    Put32(bytes, 0, static_cast<std::uint32_t>(bytes.size()));
    bytes[4] = 12; bytes[6] = 1;
    Put32(bytes, 8, 8); Put32(bytes, 12, 1);
    std::copy_n("RERL", 4, bytes.begin() + 16);
    Put32(bytes, 20, 8); Put32(bytes, 24, static_cast<std::uint32_t>(bytes.size() - 28));
    Put32(bytes, 28, 8); Put32(bytes, 32, 1); Put64(bytes, 36, 1); Put64(bytes, 44, 8);
    std::copy(reference.begin(), reference.end(), bytes.begin() + 52);
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wcscmp(argv[1], L"--scan") == 0) {
        std::string error;
        if (!ModelCatalogDiscover(std::filesystem::path(argv[2]), error)) {
            std::printf("FAIL installed catalog: %s\n", error.c_str());
            return 1;
        }
        const auto catalog = ModelCatalogSnapshot();
        std::printf("catalog_entries=%zu\n", catalog ? catalog->size() : 0);
        if (catalog) {
            for (const auto& entry : *catalog)
                if (entry.path.starts_with("agents/models/hmn_ag2/zenless_zone_zero/"))
                    std::printf("zenless_model=%s\n", entry.path.c_str());
        }
        return catalog && !catalog->empty() ? 0 : 1;
    }
    if (argc != 1) {
        std::fprintf(stderr, "Usage: model_catalog_checks [--scan <game/csgo directory>]\n");
        return 1;
    }
    static_assert(std::is_trivially_copyable_v<ModelSwapSettings>);
    Expect(ModelSwapGetSettings() == ModelSwapSettings{}, "model swap defaults disabled with an empty selection");
    ModelSwapSettings settings;
    settings.enabled = true;
    Expect(ModelSwapSetModel(settings, "Agents\\Example\\My_Model.VMDL") &&
        std::string_view(settings.model.data()) == "agents/example/my_model.vmdl" && settings.enabled,
        "runtime paths normalize slash and ASCII case without changing the toggle");
    const auto saved = settings;
    for (const auto& invalid : std::vector<std::string>{
        "../agents/bad.vmdl", "/agents/bad.vmdl", "C:/agents/bad.vmdl", "//server/agents/bad.vmdl",
        "agents/../bad.vmdl", "agents/./bad.vmdl", "agents//bad.vmdl", "agents/bad.vmdl_c", "bad.vmdl",
        "materials/bad.vmdl", "agents/bad?.vmdl", "agents/bad\n.vmdl", "agents/trailing./bad.vmdl",
        "agents/trailing /bad.vmdl", "agents/.vmdl", std::string("agents/bad\0suffix.vmdl", 22),
        "agents/" + std::string(260, 'x') + ".vmdl"}) {
        Expect(!ModelSwapSetModel(settings, invalid) && settings == saved, "invalid runtime model leaves the prior selection intact");
    }
    ModelSwapSetSettings(settings);
    Expect(ModelSwapGetSettings() == saved, "settings publish a bounded POD copy");
    auto malformed = saved;
    malformed.model.fill('x');
    ModelSwapSetSettings(malformed);
    Expect(ModelSwapGetSettings() == ModelSwapSettings{}, "unterminated settings reset instead of reading outside the buffer");
    malformed = saved;
    std::copy_n("../bad.vmdl", 12, malformed.model.begin());
    Expect(ModelSwapSanitizeSettings(malformed) == ModelSwapSettings{}, "malformed settings disable the override");
    Expect(ModelSwapSetModel(settings, "") && !settings.model[0] && settings.enabled,
        "empty selection clears the resource and preserves the explicit toggle");
    ModelSwapResetSettings();
    Expect(ModelSwapGetSettings() == ModelSwapSettings{}, "reset clears both override and selection");
    const std::string longStatus(400, 'x');
    ModelSwapSetRuntimeStatus(longStatus.c_str());
    Expect(ModelSwapRuntimeStatus().back() == '\0' && ModelSwapRuntimeStatus()[254] == 'x', "status copies are bounded and terminated");
    ModelSwapSetRuntimeStatus(nullptr);
    Expect(!ModelSwapRuntimeStatus()[0], "null runtime status clears the message");

    const auto root = std::filesystem::path(CS2_WORKSPACE_ROOT) / L"logs" / (L"model-catalog-check-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const auto graph = root / L"animation/graphs/worldmodel/worldmodel.vnmgraph_c";
    std::filesystem::create_directories(graph.parent_path());
    std::ofstream(graph).put('x');
    Model(root / "agents/zeta/zeta.vmdl_c");
    Model(root / "agents/alpha/alpha.vmdl_c");
    Model(root / "characters/models/valid/valid.vmdl_c");
    Model(root / "models/custom/model_one.vmdl_c");
    Model(root / "phase2/taiga/taiga.vmdl_c");
    Model(root / "agents/alpha/alpha_arms.vmdl_c");
    Model(root / "agents/arm/arm.vmdl_c");
    Model(root / "agents/helper/helper.vmdl_c");
    Model(root / "agents/alpha/alpha_nohitbox.vmdl_c");
    Model(root / "agents/null/null.vmdl_c");
    Model(root / "models/custom/legacy.vmdl_c", "animation/graphs/player.vanmgrph");
    Model(root / "models/custom/broken.vmdl_c", "animation/kxnrl/graphs/worldmodel.vnmgraph");
    Model(root / "materials/particle_helper.vmdl_c");
    std::ofstream(root / "agents/bad.vmdl_c").put('x');
    std::ofstream(root / "agents/source_only.vmdl").put('x');
    std::string error;
    Expect(ModelCatalogDiscover(root, error) && error.empty(), "catalog discovers validated loose models without needing installed VPKs");
    const auto catalog = ModelCatalogSnapshot();
    Expect(catalog && catalog->size() == 5, "catalog excludes arms, helper resources, malformed models, unsupported graphs and unrelated folders");
    Expect(catalog && std::is_sorted(catalog->begin(), catalog->end(), [](const auto& a, const auto& b) { return a.path < b.path; }),
        "catalog order is deterministic");
    Expect(catalog && !catalog->empty() && catalog->front().path == "agents/alpha/alpha.vmdl" && catalog->front().label == "alpha",
        "catalog publishes source resource names and readable labels");
    const auto oldSnapshot = catalog;
    Expect(!ModelCatalogDiscover(root / "missing", error) && !error.empty() && ModelCatalogSnapshot() == oldSnapshot,
        "failed discovery retains the complete previous snapshot");
    const auto external = root / "outside";
    Model(external / "escaped.vmdl_c");
    // Junction/reparse creation is available without developer-mode symlinks.
    const auto link = root / "agents/linked";
    const auto command = L"cmd /c mklink /J \"" + link.wstring() + L"\" \"" + external.wstring() + L"\" >nul";
    const bool linked = _wsystem(command.c_str()) == 0;
    Expect(linked, "create directory-reparse fixture");
    if (linked) {
        Expect(ModelCatalogDiscover(root, error) && ModelCatalogSnapshot()->size() == 5,
            "catalog never traverses directory reparse points");
        std::filesystem::remove(link);
    }
    Model(root / "agents/new/new.vmdl_c");
    Expect(ModelCatalogDiscover(root, error) && ModelCatalogSnapshot()->size() == 6 && oldSnapshot && oldSnapshot->size() == 5,
        "refresh replaces catalog atomically while existing readers keep a stable snapshot");
    std::filesystem::remove_all(root);
    return failures ? 1 : 0;
}
