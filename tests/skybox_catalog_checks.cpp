#include "skybox_catalog.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace {
unsigned checks = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "skybox_catalog_checks: %s\n", message); std::exit(1); }
}
template<class T, std::size_t N> void Put(std::array<unsigned char, N>& bytes, unsigned at, T value) {
    std::memcpy(bytes.data() + at, &value, sizeof(value));
}
void Texture(const std::filesystem::path& path, unsigned flags = 16, unsigned width = 512, unsigned height = 512,
             unsigned depth = 1, bool badBlock = false) {
    std::array<unsigned char, 128> bytes{};
    Put(bytes, 0, std::uint32_t{128}); Put(bytes, 4, std::uint16_t{12}); Put(bytes, 6, std::uint16_t{1});
    Put(bytes, 8, std::uint32_t{8}); Put(bytes, 12, std::uint32_t{1});
    std::memcpy(bytes.data() + 16, "DATA", 4);
    Put(bytes, 20, std::uint32_t{badBlock ? 500u : 12u}); Put(bytes, 24, std::uint32_t{40});
    Put(bytes, 32, std::uint16_t{1}); Put(bytes, 34, static_cast<std::uint16_t>(flags));
    Put(bytes, 52, static_cast<std::uint16_t>(width)); Put(bytes, 54, static_cast<std::uint16_t>(height));
    Put(bytes, 56, static_cast<std::uint16_t>(depth)); bytes[58] = 19; bytes[59] = 8;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
}
int main(int argc, char** argv) {
    if (argc > 1) {
        // Read-only integration mode: check a real imported pack without copying
        // or removing any of its files. Optional second argument is cube count.
        std::string error;
        const bool discovered = SkyboxCatalogDiscover(std::filesystem::path(argv[1]), error);
        Check(discovered, error.c_str());
        const auto catalog = SkyboxCatalogSnapshot();
        Check(catalog != nullptr, "integration snapshot available");
        const auto custom = std::count_if(catalog->begin(), catalog->end(), [](const auto& entry) { return entry.custom; });
        if (argc > 2) {
            char* end = nullptr;
            const auto expected = std::strtoul(argv[2], &end, 10);
            Check(end && *end == '\0' && custom == static_cast<std::ptrdiff_t>(expected), "imported custom cube count");
        }
        std::printf("skybox_catalog_checks: %zu total, %zu builtins, %zu custom cubes\n", catalog->size(),
                    SkyboxCatalogBuiltins().size(), static_cast<std::size_t>(custom));
        return 0;
    }
    const auto root = std::filesystem::temp_directory_path() / (L"cs2-skybox-catalog-" + std::to_wstring(GetCurrentProcessId()));
    Check(!std::filesystem::exists(root), "unique temporary test directory");
    std::string error;
    auto initial = SkyboxCatalogSnapshot();
    Check(initial && initial->size() == 9, "builtins available before discovery");
    Check(SkyboxCatalogContains("materials/skybox/sky_de_dust2.vmat"), "builtin identity is a resource path");
    Check(SkyboxCatalogDiscover(root, error) && error.empty(), "missing custom folder preserves builtins");
    Texture(root / "materials/skies/pretty.vtex_c");
    Texture(root / "materials/skies/sub/night.vtex_c");
    Texture(root / "materials/skies/friendly_sky_cubemap_exr_12abcdef.vtex_c");
    Texture(root / "materials/skies/other_sky_cube_pfm_01abcde.vtex_c");
    Texture(root / "materials/skies/no_hash_tga.vtex_c");
    Texture(root / "materials/skies/meaningful_dead.vtex_c");
    Texture(root / "materials/skies/notcube.vtex_c", 0);
    Texture(root / "materials/skies/volume.vtex_c", 16 | 32);
    Texture(root / "materials/skies/array.vtex_c", 16 | 64);
    Texture(root / "materials/skies/depth.vtex_c", 16, 512, 512, 2);
    Texture(root / "materials/skies/nonsquare.vtex_c", 16, 512, 256);
    Texture(root / "materials/skies/invalid.vtex_c", 16, 512, 512, 1, true);
    Texture(root / "materials/elsewhere/other.vtex_c");
    Texture(root / "materials/greensky_png_c504d236.vtex_c");
    Texture(root / "materials/skies/unverified_shader.vmat_c");
    Check(SkyboxCatalogDiscover(root, error), "cube discovery succeeds");
    const auto catalog = SkyboxCatalogSnapshot();
    Check(catalog && catalog->size() == 17, "validated single cubes anywhere under materials are listed");
    Check(SkyboxCatalogContains("materials/skies/pretty.vtex"), "compiled suffix becomes logical resource path");
    Check(!SkyboxCatalogContains("materials/skies/notcube.vtex"), "flat textures rejected");
    Check(!SkyboxCatalogContains("materials/skies/unverified_shader.vmat"), "unverified material shader rejected");
    const auto label = [&](std::string_view path) -> std::string {
        const auto found = std::find_if(catalog->begin(), catalog->end(), [&](const auto& entry) { return entry.path == path; });
        return found == catalog->end() ? "" : found->label;
    };
    Check(std::is_sorted(catalog->begin() + 9, catalog->end(), [](const auto& a, const auto& b) { return a.path < b.path; }),
          "custom resource paths remain sorted");
    Check(label("materials/skies/pretty.vtex") == "Custom: pretty", "plain label retained");
    Check(label("materials/skies/friendly_sky_cubemap_exr_12abcdef.vtex") == "Custom: friendly sky",
          "compiled hash, source extension and cubemap suffix removed from label only");
    Check(label("materials/skies/other_sky_cube_pfm_01abcde.vtex") == "Custom: other sky",
          "seven-digit hash and PFM/cube suffix handled");
    Check(label("materials/skies/no_hash_tga.vtex") == "Custom: no hash", "TGA suffix removed without requiring hash");
    Check(label("materials/skies/meaningful_dead.vtex") == "Custom: meaningful dead", "short meaningful hexadecimal word retained");
    Check(label("materials/elsewhere/other.vtex") == "Custom: elsewhere/other", "nonstandard nested folder cube keeps a meaningful label");
    Check(label("materials/greensky_png_c504d236.vtex") == "Custom: greensky", "material-root cube included with PNG/hash suffix removed");
    Check(initial->size() == 9, "older shared snapshots remain immutable");
    const auto badRoot = root / "notdirectory";
    { std::ofstream file(badRoot); file << "x"; }
    Check(!SkyboxCatalogDiscover(badRoot, error) && !error.empty(), "invalid root reports error");
    Check(SkyboxCatalogSnapshot() == catalog, "failed discovery retains published snapshot");
    std::filesystem::remove(root / "materials/skies/pretty.vtex_c");
    Check(SkyboxCatalogDiscover(root, error) && !SkyboxCatalogContains("materials/skies/pretty.vtex"), "explicit refresh removes stale files");
    // Only remove the verified unique test directory that this process created.
    std::filesystem::remove_all(root);
    std::printf("skybox_catalog_checks: %u checks passed\n", checks);
    return 0;
}
