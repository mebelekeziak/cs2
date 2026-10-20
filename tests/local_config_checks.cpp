#include "local_config.h"
#include "esp_draw.h"
#include "movement_settings.h"
#include "world_settings.h"
#include "silent_aim_settings.h"
#include "anti_aim_settings.h"
#include "third_person_settings.h"
#include "zoom_settings.h"
#include "shot_log_settings.h"
#include "model_swap_settings.h"
#include "player_chams_settings.h"
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
    const PlayerChamsSettings chamsDefaults;
    Expect(PlayerChamsGetSettings() == chamsDefaults && chamsDefaults.throughWalls,
        "player chams start disabled with flat material, independent target toggles and through-walls rendering selected");
    auto invalidChams = chamsDefaults;
    invalidChams.enabled = true;
    invalidChams.teammates = true;
    invalidChams.localPlayer = true;
    invalidChams.throughWalls = false;
    invalidChams.style = static_cast<PlayerChamsStyle>(99);
    invalidChams.color[0] = -1;
    invalidChams.color[1] = 2;
    invalidChams.color[2] = std::numeric_limits<float>::quiet_NaN();
    invalidChams.color[3] = std::numeric_limits<float>::infinity();
    invalidChams.iridescentColor[0] = std::numeric_limits<float>::quiet_NaN();
    invalidChams.iridescentColor[1] = -std::numeric_limits<float>::infinity();
    invalidChams.iridescentColor[2] = -2;
    invalidChams.iridescentColor[3] = 2;
    PlayerChamsSetSettings(invalidChams);
    const auto boundedChams = PlayerChamsGetSettings();
    Expect(boundedChams.enabled && boundedChams.teammates && boundedChams.localPlayer && !boundedChams.throughWalls &&
        boundedChams.style == PlayerChamsStyle::Flat && boundedChams.color[0] == 0 && boundedChams.color[1] == 1 &&
        boundedChams.color[2] == chamsDefaults.color[2] && boundedChams.color[3] == chamsDefaults.color[3] &&
        boundedChams.iridescentColor[0] == chamsDefaults.iridescentColor[0] &&
        boundedChams.iridescentColor[1] == chamsDefaults.iridescentColor[1] &&
        boundedChams.iridescentColor[2] == 0 && boundedChams.iridescentColor[3] == 1,
        "player chams reject unknown style, clamp finite RGBA and default nonfinite channels independently");
    PlayerChamsResetSettings();
    Expect(PlayerChamsGetSettings() == chamsDefaults, "player chams reset restores every field");
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
    const ZoomSettings zoomDefaults;
    Expect(ZoomGetSettings() == zoomDefaults && zoomDefaults.sensitivity == 100,
        "scoped zoom starts disabled with weapon zoom and native scoped sensitivity defaults");
    Expect(ZoomSanitizeSettings({true, -1, 999, -1}) == ZoomSettings{true, 0, 200, 0},
        "scoped zoom levels and sensitivity clamp independently including unscoped sensitivity");
    ZoomSetSettings({true, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()});
    Expect(ZoomGetSettings() == ZoomSettings{true, 100, 100, 100}, "nonfinite scoped zoom and sensitivity values use native defaults");
    Expect(ZoomSanitizeSettings({true, 25, 75, std::numeric_limits<float>::infinity()}).sensitivity == 100 &&
        ZoomSanitizeSettings({true, 25, 75, -std::numeric_limits<float>::infinity()}).sensitivity == 100,
        "both infinite sensitivities restore native scoped sensitivity");
    ZoomSetSettings({true, -999, 999, 999});
    Expect(ZoomGetSettings() == ZoomSettings{true, 0, 200, 100}, "scoped zoom publisher enforces distinct zoom and sensitivity bounds");
    ZoomSetSettings({true, 100, 100, 0});
    ZoomResetSettings();
    Expect(ZoomGetSettings() == zoomDefaults, "scoped zoom reset restores both levels and native sensitivity and disables override");
    Expect(!AntiAimGetSettings().enabled, "anti aim starts disabled");
    Expect(ShotLogGetSettings()==ShotLogSettings{true,false}, "shot logs start enabled with verbose diagnostics disabled");
    Expect(ModelSwapGetSettings() == ModelSwapSettings{}, "model swap starts disabled with no selected model");
    Expect(LocalConfigSave(first, error), "save defaults");
    const auto defaults = Read(firstPath);
    Expect(defaults.find("anti_aim.enabled=0\n") != std::string::npos, "default preset explicitly saves disabled anti aim");
    Expect(defaults.find("aim.autofire=0\naim.hitchance=50\naim.samples=256\naim.quickstop=0\naim.min_damage=1\n") != std::string::npos,
        "default preset saves autofire and quickstop off, hitchance controls and minimum damage");
    Expect(defaults.find("camera.enabled=0\ncamera.distance=120\ncamera.shoulder=0\ncamera.height=0\ncamera.collision=1\n") != std::string::npos,
        "default preset explicitly saves every camera control");
    Expect(defaults.find("zoom.enabled=0\nzoom.zoom1=100\nzoom.zoom2=100\nzoom.sensitivity=100\n") != std::string::npos,
        "default preset explicitly saves both scoped zoom levels and native sensitivity");
    Expect(defaults.find("shot_log.enabled=1\nshot_log.verbose=0\n") != std::string::npos,
        "default preset explicitly saves human shot logs enabled and verbose console diagnostics disabled");
    Expect(defaults.find("model_swap.enabled=0\nmodel_swap.model=\n") != std::string::npos,
        "default preset saves disabled player model and an empty path");
    Expect(defaults.find("player_chams.enabled=0\nplayer_chams.teammates=0\nplayer_chams.localPlayer=0\nplayer_chams.style=0\n") != std::string::npos &&
        defaults.find("player_chams.color.3=1\n") != std::string::npos &&
        defaults.find("player_chams.iridescentColor.3=1\n") != std::string::npos &&
        defaults.find("player_chams.throughWalls=1\n") != std::string::npos,
        "default preset explicitly saves player chams modes, target toggles, RGBA channels and through-walls selection");
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
    const SilentAimSettings expectedAim{true, 172.34f, AimHitbox::Pelvis, true, 73.25f, 512, true, 67,
        AimHitgroupBit(AimHitgroup::Head) | AimHitgroupBit(AimHitgroup::Chest) | AimHitgroupBit(AimHitgroup::Legs)};
    SilentAimSetSettings(expectedAim);
    const AntiAimSettings expectedAntiAim{true, AntiAimYawMode::Jitter, AntiAimYawBase::World,
        -123.25f, 213.5f, 13, -17.75f, AntiAimPitchMode::Jitter, 32.25f, -51.5f};
    AntiAimSetSettings(expectedAntiAim);
    const ThirdPersonSettings expectedCamera{true, 173.25f, -21.5f, 17.75f, false};
    ThirdPersonSetSettings(expectedCamera);
    const ZoomSettings expectedZoom{true, 0, 137.25f, 38.25f};
    ZoomSetSettings(expectedZoom);
    const ShotLogSettings expectedShotLog{false,true};
    ShotLogSetSettings(expectedShotLog);
    ModelSwapSettings expectedModel;
    expectedModel.enabled = true;
    Expect(ModelSwapSetModel(expectedModel, "agents/models/hmn_ag2/zenless_zone_zero/ellen/ellen.vmdl"), "prepare selected custom model");
    ModelSwapSetSettings(expectedModel);
    PlayerChamsSettings expectedChams{true, true, true, PlayerChamsStyle::Iridescent};
    expectedChams.throughWalls = false;
    for (int channel = 0; channel < 4; ++channel) {
        expectedChams.color[channel] = (channel + 1) / 8.0f;
        expectedChams.iridescentColor[channel] = (channel + 4) / 8.0f;
    }
    PlayerChamsSetSettings(expectedChams);
    const EspSettings expectedEsp = EspGetSettings();
    Expect(LocalConfigSave(second, error), "save second named preset");
    const auto custom = Read(secondPath);
    Expect(custom != defaults, "separate presets retain different options");
    Expect(custom.find("anti_aim.enabled=1\n") != std::string::npos, "custom preset saves enabled anti aim");
    Expect(LocalConfigLoad(first, error) && EspGetSettings() == EspSettings{} &&
        MovementGetSettings().bunnyhop && !WorldGetSettings().enabled && !SilentAimGetSettings().enabled &&
        !AntiAimGetSettings().enabled && ThirdPersonGetSettings() == cameraDefaults && ZoomGetSettings() == zoomDefaults &&
        ShotLogGetSettings()==ShotLogSettings{true,false} && ModelSwapGetSettings() == ModelSwapSettings{} &&
        PlayerChamsGetSettings() == chamsDefaults,
        "load defaults across all option groups");
    Expect(LocalConfigLoad(second, error), "load custom preset");
    Expect(EspGetSettings() == expectedEsp, "all ESP fields and array channels roundtrip");
    Expect(SilentAimGetSettings() == expectedAim && custom.find("aim.min_damage=67\n") != std::string::npos &&
        custom.find("aim.hitgroups=67\n") != std::string::npos,
        "aim, autofire, hitchance, sample count, minimum damage and independent hitgroups roundtrip");
    Expect(AntiAimGetSettings() == expectedAntiAim, "all anti aim modes, interval and fractional angle controls roundtrip");
    Expect(ThirdPersonGetSettings() == expectedCamera, "all camera fields roundtrip including fractional offsets and disabled collision");
    Expect(ZoomGetSettings() == expectedZoom && custom.find("zoom.sensitivity=38.25\n") != std::string::npos,
        "scoped zoom master, no zoom, fractional second level and blended sensitivity roundtrip");
    Expect(ModelSwapGetSettings() == expectedModel, "local player model toggle and normalized resource path roundtrip");
    Expect(PlayerChamsGetSettings() == expectedChams && custom.find("player_chams.throughWalls=0\n") != std::string::npos,
        "player chams master, teammate and local targets, iridescent style, RGBA and disabled through-walls selection roundtrip");
    Expect(ShotLogGetSettings()==expectedShotLog && custom.find("shot_log.enabled=0\nshot_log.verbose=1\n")!=std::string::npos,
        "human results and verbose diagnostics independently roundtrip in named presets");
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
        SilentAimGetSettings().enabled && SilentAimGetSettings().fov == expectedAim.fov &&
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
        "version=1\naim.hitgroups=-1\n", "version=1\naim.hitgroups=1.5\n",
        "version=1\naim.hitgroups=nan\n", "version=1\naim.hitgroups=4294967296\n",
        "version=1\naim.hitgroups=1\naim.hitgroups=2\n",
        "version=1\naim.autofire=2\n", "version=1\naim.hitchance=nan\n", "version=1\naim.samples=1.2\n",
        "version=1\naim.quickstop=2\n", "version=1\naim.quickstop=true\n",
        "version=1\naim.quickstop=0\naim.quickstop=1\n",
        "version=1\naim.min_damage=1.5\n", "version=1\naim.min_damage=nan\n",
        "version=1\naim.min_damage=35junk\n", "version=1\naim.min_damage=99999999999999\n",
        "version=1\naim.min_damage=1\naim.min_damage=100\n",
        "version=1\naim.hitchance=25\naim.hitchance=75\n",
        "version=1\nanti_aim.enabled=true\n", "version=1\nanti_aim.enabled=2\n",
        "version=1\nanti_aim.yawMode=1.5\n", "version=1\nanti_aim.yawBase=world\n",
        "version=1\nanti_aim.yawOffset=nan\n", "version=1\nanti_aim.jitterRange=inf\n",
        "version=1\nanti_aim.jitterInterval=1.5\n", "version=1\nanti_aim.spinSpeed=-inf\n",
        "version=1\nanti_aim.pitchMode=custom\n", "version=1\nanti_aim.pitch=nan\n",
        "version=1\nanti_aim.pitchAlternate=89junk\n",
        "version=1\nanti_aim.yawOffset=10\nanti_aim.yawOffset=20\n",
        "version=1\ncamera.enabled=true\n", "version=1\ncamera.collision=2\n",
        "version=1\ncamera.distance=nan\n", "version=1\ncamera.shoulder=inf\n",
        "version=1\ncamera.height=12oops\n", "version=1\ncamera.distance=120\ncamera.distance=130\n",
        "version=1\nzoom.enabled=2\n", "version=1\nzoom.zoom1=nan\n", "version=1\nzoom.zoom2=inf\n",
        "version=1\nzoom.zoom1=1oops\n", "version=1\nzoom.zoom2=50\nzoom.zoom2=150\n",
        "version=1\nzoom.sensitivity=nan\n", "version=1\nzoom.sensitivity=inf\n",
        "version=1\nzoom.sensitivity=-inf\n", "version=1\nzoom.sensitivity=50oops\n",
        "version=1\nzoom.sensitivity=25\nzoom.sensitivity=75\n",
        "version=1\nshot_log.enabled=true\n", "version=1\nshot_log.verbose=2\n",
        "version=1\nshot_log.enabled=0\nshot_log.enabled=1\n",
        "version=1\nshot_log.verbose=0\nshot_log.verbose=1\n",
        "version=1\nshot_log.enabled=1\naim.enabled=0\nshot_log.verbose=invalid\n",
        "version=1\nmodel_swap.enabled=2\n", "version=1\nmodel_swap.model=../escape.vmdl\n",
        "version=1\nmodel_swap.enabled=0\naim.enabled=0\nmodel_swap.model=C:/outside.vmdl\n",
        "version=1\nmodel_swap.model=agents/bad.vmdl_c\n",
        "version=1\nmodel_swap.model=agents/first.vmdl\nmodel_swap.model=agents/second.vmdl\n",
        "version=1\nplayer_chams.enabled=2\n", "version=1\nplayer_chams.teammates=true\n",
        "version=1\nplayer_chams.localPlayer=2\n", "version=1\nplayer_chams.style=1.5\n",
        "version=1\nplayer_chams.color.0=nan\n", "version=1\nplayer_chams.color.3=inf\n",
        "version=1\nplayer_chams.iridescentColor.2=-inf\n", "version=1\nplayer_chams.iridescentColor.3=0.5oops\n",
        "version=1\nplayer_chams.style=0\nplayer_chams.style=1\n",
        "version=1\nplayer_chams.throughWalls=2\n", "version=1\nplayer_chams.throughWalls=true\n",
        "version=1\nplayer_chams.throughWalls=0\nplayer_chams.throughWalls=1\n",
        "version=1\naim.enabled=0\nplayer_chams.enabled=0\nplayer_chams.color.0=nan\n",
        "version=1\nworld.enabled=0\nworld.enabled=1\n", "version=1\nbroken\n"}) {
        Write(badPath, invalid);
        Expect(!LocalConfigLoad(bad, error) && !error.empty(), "reject malformed config");
        Expect(LocalConfigSave(second, error) && Read(secondPath) == custom, "failed load applies no option changes");
    }
    Write(badPath, "version=1\nfuture.option=hello\nworld.exposureEv=999\nesp.boxThickness=-99\naim.fov=999\naim.hitbox=99\n");
    Expect(LocalConfigLoad(bad, error) && WorldGetSettings().exposureEv == 3 &&
        EspGetSettings().boxThickness == EspSanitizeSettings(EspSettings{.boxThickness = -99}).boxThickness &&
        SilentAimGetSettings().fov == 180 && SilentAimGetSettings().hitbox == AimHitbox::Pelvis,
        "unknown keys accepted and existing setters enforce bounds");
    Expect(AntiAimGetSettings() == AntiAimSettings{}, "older presets without anti aim restore every disabled default");
    Expect(!SilentAimGetSettings().autofire && SilentAimGetSettings().hitchance == 50 &&
        SilentAimGetSettings().samples == 256 && !SilentAimGetSettings().quickstop && SilentAimGetSettings().minDamage == 1,
        "older presets restore autofire and quickstop off, hitchance defaults and minimum damage 1");
    Expect(ThirdPersonGetSettings() == cameraDefaults, "older presets without camera keys restore all camera defaults");
    Expect(ZoomGetSettings() == zoomDefaults, "older presets without scoped zoom keys restore default levels and native sensitivity disabled");
    Expect(ShotLogGetSettings()==ShotLogSettings{true,false}, "older presets without logging keys restore human results on and verbose diagnostics off");
    Expect(ModelSwapGetSettings() == ModelSwapSettings{}, "older presets without player model keys clear the selection and disable override");
    Expect(PlayerChamsGetSettings() == chamsDefaults, "older presets without player chams restore every disabled default");
    for (int legacy = 0; legacy < 4; ++legacy) {
        Write(badPath, "version=1\naim.hitbox=" + std::to_string(legacy) + "\n");
        Expect(LocalConfigLoad(bad, error) && SilentAimGetSettings().hitgroups == (1u << legacy),
            "legacy single hitbox migrates to exactly the previously selected group");
    }
    for (const auto& content : {"version=1\naim.hitbox=0\naim.hitgroups=192\n",
                                "version=1\naim.hitgroups=192\naim.hitbox=0\n"}) {
        Write(badPath, content);
        Expect(LocalConfigLoad(bad, error) && SilentAimGetSettings().hitgroups == 192,
            "explicit multi-hitgroup mask wins over legacy field independently of key order");
    }
    Write(badPath, "version=1\naim.hitbox=0\naim.hitgroups=0\n");
    Expect(LocalConfigLoad(bad, error) && SilentAimGetSettings().hitgroups == 0 &&
        LocalConfigSave(bad, error) && LocalConfigLoad(bad, error) && SilentAimGetSettings().hitgroups == 0,
        "empty hitgroup selection survives saving and loading without legacy fallback");
    Write(badPath, "version=1\naim.hitgroups=4294967295\n");
    Expect(LocalConfigLoad(bad, error) && SilentAimGetSettings().hitgroups == AimAllHitgroups,
        "preset mask strips unknown high bits and retains all supported groups");
    Write(badPath, "version=1\n");
    Expect(LocalConfigLoad(bad, error) && SilentAimGetSettings().hitgroups == AimHitgroupBit(AimHitgroup::Chest),
        "presets with no targeting fields preserve the default chest selection");
    Write(badPath, "version=1\naim.min_damage=-999\n");
    Expect(LocalConfigLoad(bad, error) && SilentAimGetSettings().minDamage == 1,
        "config bounds minimum damage to 1 HP without enabling aim");
    Write(badPath, "version=1\naim.min_damage=999\n");
    Expect(LocalConfigLoad(bad, error) && SilentAimGetSettings().minDamage == 100 && !SilentAimGetSettings().enabled,
        "config bounds maximum minimum damage to 100 HP without enabling aim");
    Write(badPath, "version=1\nplayer_chams.enabled=1\nplayer_chams.style=-99\nplayer_chams.color.0=-2\nplayer_chams.color.3=2\nplayer_chams.iridescentColor.2=-2\n");
    auto expectedBoundedChams = chamsDefaults;
    expectedBoundedChams.enabled = true;
    expectedBoundedChams.color[0] = 0;
    expectedBoundedChams.color[3] = 1;
    expectedBoundedChams.iridescentColor[2] = 0;
    Expect(LocalConfigLoad(bad, error) && PlayerChamsGetSettings() == expectedBoundedChams,
        "player chams config bounds both colors and falls back to flat for unknown style while defaulting missing targets");
    Expect(PlayerChamsGetSettings().enabled && PlayerChamsGetSettings().throughWalls,
        "older enabled player chams presets without through-walls key inherit enabled depth override");
    Write(badPath, "version=1\nanti_aim.enabled=1\n");
    Expect(LocalConfigLoad(bad, error) && AntiAimGetSettings() == AntiAimSettings{true},
        "older enabled-only anti aim preset preserves static backwards yaw and downward pitch");
    Write(badPath, "version=1\nanti_aim.enabled=1\nanti_aim.yawMode=99\nanti_aim.yawBase=-99\n"
        "anti_aim.yawOffset=-999\nanti_aim.jitterRange=999\nanti_aim.jitterInterval=0\nanti_aim.spinSpeed=-999\n"
        "anti_aim.pitchMode=99\nanti_aim.pitch=999\nanti_aim.pitchAlternate=-999\n");
    Expect(LocalConfigLoad(bad, error) && AntiAimGetSettings() == AntiAimSettings{true,
        AntiAimYawMode::Static, AntiAimYawBase::View, -180, 360, 1, -180, AntiAimPitchMode::Down, 89, -89},
        "anti aim config loader bounds numeric values and restores invalid mode defaults through publisher");
    Write(badPath, "version=1\nmodel_swap.enabled=1\nmodel_swap.model=Agents\\Example\\Example.VMDL\n");
    Expect(LocalConfigLoad(bad, error) && ModelSwapGetSettings().enabled &&
        std::string_view(ModelSwapGetSettings().model.data()) == "agents/example/example.vmdl",
        "config loading normalizes valid model resource names");
    Write(badPath,"version=1\nshot_log.enabled=0\n");
    Expect(LocalConfigLoad(bad,error) && ShotLogGetSettings()==ShotLogSettings{false,false},
        "preset with only human logging key uses independent verbose default");
    Write(badPath,"version=1\nshot_log.verbose=1\n");
    Expect(LocalConfigLoad(bad,error) && ShotLogGetSettings()==ShotLogSettings{true,true} && !SilentAimGetSettings().enabled,
        "preset with only verbose key retains enabled human logging default without enabling aim");
    Write(badPath, "version=1\nzoom.enabled=1\nzoom.zoom1=-999\nzoom.zoom2=999\n");
    Expect(LocalConfigLoad(bad, error) && ZoomGetSettings() == ZoomSettings{true, 0, 200},
        "older zoom preset clamps both levels and defaults missing sensitivity to native scoped value");
    Write(badPath, "version=1\nzoom.enabled=1\nzoom.sensitivity=-999\n");
    Expect(LocalConfigLoad(bad, error) && ZoomGetSettings() == ZoomSettings{true, 100, 100, 0},
        "scoped sensitivity config clamps below zero to unscoped sensitivity");
    Write(badPath, "version=1\nzoom.enabled=0\nzoom.zoom1=137\nzoom.sensitivity=999\n");
    Expect(LocalConfigLoad(bad, error) && ZoomGetSettings() == ZoomSettings{false, 137, 100, 100},
        "scoped sensitivity config clamps above one hundred to native scoped sensitivity independently of zoom");
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
