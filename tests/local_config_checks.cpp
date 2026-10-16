#include "local_config.h"
#include "esp_draw.h"
#include "movement_settings.h"
#include "world_settings.h"
#include "silent_aim_settings.h"
#include "anti_aim_settings.h"
#include "third_person_settings.h"
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace {
int failures = 0;
void Expect(bool condition, const char* label) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", label);
    failures += !condition;
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}
}
int main() {
    const auto directory = LocalConfigDirectory();
    Expect(directory == std::filesystem::path(CS2_WORKSPACE_ROOT) / "configs", "workspace-local directory");
    const std::string prefix = "config-check-" + std::to_string(GetCurrentProcessId());
    const auto first = prefix + "-first";
    const auto second = prefix + "-second";
    const auto bad = prefix + "-bad";
    const auto firstPath = directory / (first + ".cfg");
    const auto secondPath = directory / (second + ".cfg");
    const auto badPath = directory / (bad + ".cfg");
    std::string error;
    const ThirdPersonSettings cameraDefaults;
    Expect(ThirdPersonGetSettings() == cameraDefaults, "third person starts disabled at default distance with collision");
    const auto boundedCamera = ThirdPersonSanitizeSettings({true, -1, -99, 99, false});
    Expect(boundedCamera == ThirdPersonSettings{true, 30, -60, 60, false}, "camera values clamp independently without changing toggles");
    ThirdPersonSetSettings({true, 999, 99, -99, false});
    Expect(ThirdPersonGetSettings() == ThirdPersonSettings{true, 300, 60, -60, false}, "camera publisher enforces bounds");
    ThirdPersonSetSettings({true, std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(), -std::numeric_limits<float>::infinity(), false});
    Expect(ThirdPersonGetSettings() == ThirdPersonSettings{true, 120, 0, 0, false}, "nonfinite camera values use defaults");
    ThirdPersonResetSettings();
    Expect(ThirdPersonGetSettings() == cameraDefaults, "camera reset restores every default including master and collision");
    Expect(!AntiAimGetSettings().enabled, "anti aim starts disabled");
    Expect(LocalConfigSave(first, error), "save defaults");
    const auto defaults = Read(firstPath);
    Expect(defaults.find("anti_aim.enabled=0\n") != std::string::npos, "default preset explicitly saves disabled anti aim");
    Expect(defaults.find("camera.enabled=0\ncamera.distance=120\ncamera.shoulder=0\ncamera.height=0\ncamera.collision=1\n") != std::string::npos,
        "default preset explicitly saves every camera control");
    EspSettings esp = EspGetSettings();
    esp.enabled = false; esp.names = false; esp.boxes = false; esp.distance = false;
    esp.health = false; esp.teammates = false; esp.armor = true; esp.teamColors = false;
    esp.boxStyle = 1; esp.boxThickness = 2.5f; esp.boxOutline = false; esp.boxFill = true;
    esp.nameUseBoxColor = false; esp.nameFont = 2; esp.nameSize = 18; esp.detailSize = 16;
    esp.textOutline = false; esp.healthGradient = true; esp.healthBarWidth = 5; esp.healthText = false;
    float* colors[]{esp.boxColor, esp.tColor, esp.ctColor, esp.fillColor, esp.nameColor,
        esp.detailColor, esp.outlineColor, esp.healthColor, esp.healthLowColor};
    for (unsigned c = 0; c < std::size(colors); ++c)
        for (unsigned i = 0; i < 4; ++i) colors[c][i] = (c + i + 1) / 16.0f;
    EspSetSettings(esp);
    MovementSetSettings({false, false});
    WorldSettings world{};
    world.enabled = true; world.exposureEv = -1.25f; world.contrast = 1.25f;
    world.saturation = 0.25f; world.gamma = 1.75f; world.vignette = 0.25f; world.sharpening = 0.75f;
    world.skyEnabled = true; world.skyBrightness = 2.5f;
    world.worldMaterialEnabled = true; world.flatTextures = true; world.lightingEnabled = true;
    world.disableLocalLights = true; world.localLightBrightness = 6.5f; world.sunLightBrightness = 7.5f;
    world.ambientLightEnabled = true; world.ambientLightBrightness = 1.5f; world.directionalLightmapsMode = 2;
    float* worldColors[]{world.tint, world.skyTint, world.skyLightingTint, world.localLightTint,
        world.sunLightTint, world.ambientLightTint};
    for (unsigned c = 0; c < std::size(worldColors); ++c)
        for (unsigned i = 0; i < 3; ++i) worldColors[c][i] = (c + i + 1) / 16.0f;
    WorldSetSettings(world);
    SilentAimSetSettings({true, 12.34f, AimHitbox::Pelvis});
    AntiAimSetSettings({true});
    const ThirdPersonSettings expectedCamera{true, 173.25f, -21.5f, 17.75f, false};
    ThirdPersonSetSettings(expectedCamera);
    const EspSettings expectedEsp = EspGetSettings();
    Expect(LocalConfigSave(second, error), "save second named preset");
    const auto custom = Read(secondPath);
    Expect(custom != defaults, "separate presets retain different options");
    Expect(custom.find("anti_aim.enabled=1\n") != std::string::npos, "custom preset saves enabled anti aim");
    Expect(LocalConfigLoad(first, error) && EspGetSettings() == EspSettings{} &&
        MovementGetSettings().bunnyhop && !WorldGetSettings().enabled && !SilentAimGetSettings().enabled &&
        !AntiAimGetSettings().enabled && ThirdPersonGetSettings() == cameraDefaults,
        "load defaults across all option groups");
    Expect(LocalConfigLoad(second, error), "load custom preset");
    Expect(EspGetSettings() == expectedEsp, "all ESP fields and array channels roundtrip");
    Expect(ThirdPersonGetSettings() == expectedCamera, "all camera fields roundtrip including fractional offsets and disabled collision");
    const auto loadedWorld = WorldGetSettings();
    bool sameWorld = loadedWorld.enabled == world.enabled && loadedWorld.exposureEv == world.exposureEv &&
        loadedWorld.contrast == world.contrast && loadedWorld.saturation == world.saturation &&
        loadedWorld.gamma == world.gamma && loadedWorld.vignette == world.vignette &&
        loadedWorld.sharpening == world.sharpening && loadedWorld.skyEnabled == world.skyEnabled &&
        loadedWorld.skyBrightness == world.skyBrightness && loadedWorld.worldMaterialEnabled == world.worldMaterialEnabled &&
        loadedWorld.flatTextures == world.flatTextures && loadedWorld.lightingEnabled == world.lightingEnabled &&
        loadedWorld.disableLocalLights == world.disableLocalLights &&
        loadedWorld.localLightBrightness == world.localLightBrightness &&
        loadedWorld.sunLightBrightness == world.sunLightBrightness &&
        loadedWorld.ambientLightEnabled == world.ambientLightEnabled &&
        loadedWorld.ambientLightBrightness == world.ambientLightBrightness &&
        loadedWorld.directionalLightmapsMode == world.directionalLightmapsMode;
    const float* loadedColors[]{loadedWorld.tint, loadedWorld.skyTint, loadedWorld.skyLightingTint,
        loadedWorld.localLightTint, loadedWorld.sunLightTint, loadedWorld.ambientLightTint};
    for (unsigned c = 0; c < std::size(worldColors); ++c)
        for (unsigned i = 0; i < 3; ++i) sameWorld &= loadedColors[c][i] == worldColors[c][i];
    Expect(sameWorld, "all world fields and array channels roundtrip");
    Expect(!MovementGetSettings().bunnyhop && !MovementGetSettings().autostrafe &&
        SilentAimGetSettings().enabled && SilentAimGetSettings().fov == 12.34f &&
        SilentAimGetSettings().hitbox == AimHitbox::Pelvis && AntiAimGetSettings().enabled,
        "movement, silent aim and anti aim roundtrip");
    Expect(LocalConfigSave(second, error) && Read(secondPath) == custom, "atomic overwrite preserves exact float roundtrip");
    std::vector<std::string> names;
    Expect(LocalConfigList(names, error) && std::is_sorted(names.begin(), names.end()) &&
        std::find(names.begin(), names.end(), first) != names.end() &&
        std::find(names.begin(), names.end(), second) != names.end(), "sorted named preset discovery");
    for (const std::string invalid : std::vector<std::string>{"", "../outside", "trailing ", "trailing.", "CON", "nul", "COM1", "LPT9", "bad/name", "bad\\name", "bad:name", "bad\nname", std::string(65, 'a')}) {
        Expect(!LocalConfigSave(invalid, error) && !error.empty() && !LocalConfigLoad(invalid, error), "reject unsafe preset name");
    }
    for (const std::string invalid : {
        "version=2\n", "esp.enabled=1\n", "version=1\nversion=1\n", "version=1\nesp.enabled=true\n",
        "version=1\nesp.boxThickness=nan\n", "version=1\nworld.tint.1=0.5junk\n",
        "version=1\naim.hitbox=1.2\n", "version=1\nmovement.bunnyhop=2\n",
        "version=1\nanti_aim.enabled=true\n", "version=1\nanti_aim.enabled=2\n",
        "version=1\ncamera.enabled=true\n", "version=1\ncamera.collision=2\n",
        "version=1\ncamera.distance=nan\n", "version=1\ncamera.shoulder=inf\n",
        "version=1\ncamera.height=12oops\n", "version=1\ncamera.distance=120\ncamera.distance=130\n",
        "version=1\nworld.enabled=0\nworld.enabled=1\n", "version=1\nbroken\n"}) {
        Write(badPath, invalid);
        Expect(!LocalConfigLoad(bad, error) && !error.empty(), "reject malformed config");
        Expect(LocalConfigSave(second, error) && Read(secondPath) == custom, "failed load applies no option changes");
    }
    Write(badPath, "version=1\nfuture.option=hello\nworld.exposureEv=999\nesp.boxThickness=-99\naim.fov=999\naim.hitbox=99\n");
    Expect(LocalConfigLoad(bad, error) && WorldGetSettings().exposureEv == 3 &&
        EspGetSettings().boxThickness == EspSanitizeSettings(EspSettings{.boxThickness = -99}).boxThickness &&
        SilentAimGetSettings().fov == 30 && SilentAimGetSettings().hitbox == AimHitbox::Pelvis,
        "unknown keys accepted and existing setters enforce bounds");
    Expect(!AntiAimGetSettings().enabled, "older presets without anti aim explicitly restore disabled default");
    Expect(ThirdPersonGetSettings() == cameraDefaults, "older presets without camera keys restore all camera defaults");
    Write(badPath, "version=1\ncamera.enabled=1\ncamera.distance=999\ncamera.shoulder=-999\ncamera.height=999\ncamera.collision=0\n");
    Expect(LocalConfigLoad(bad, error) && ThirdPersonGetSettings() == ThirdPersonSettings{true, 300, -60, 60, false},
        "camera config load clamps finite values through bounded settings publisher");
    Expect(LocalConfigLoad(second, error), "restore saved preset after parse checks");
    std::filesystem::remove(badPath);
    std::filesystem::create_directory(badPath);
    Expect(!LocalConfigSave(bad, error) && !LocalConfigLoad(bad, error), "directory cannot be treated as config file");
    Expect(LocalConfigSave(second, error) && Read(secondPath) == custom, "failed write leaves other preset intact");
    std::filesystem::remove(badPath);
    Expect(!LocalConfigLoad(bad, error), "missing preset gives error");
    Write(badPath, std::string(1024 * 1024 + 1, 'x'));
    Expect(!LocalConfigLoad(bad, error), "oversized config rejected");
    std::filesystem::remove(firstPath);
    std::filesystem::remove(secondPath);
    std::filesystem::remove(badPath);
    return failures ? 1 : 0;
}
