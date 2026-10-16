#include "local_config.h"
#include "esp_draw.h"
#include "movement_settings.h"
#include "world_settings.h"
#include "silent_aim_settings.h"
#include "anti_aim_settings.h"
#include "third_person_settings.h"
#include "zoom_settings.h"
#include <Windows.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <type_traits>

namespace {
struct Snapshot {
    EspSettings esp;
    MovementSettings movement;
    WorldSettings world;
    SilentAimSettings aim;
    AntiAimSettings anti_aim;
    ThirdPersonSettings camera;
    ZoomSettings zoom;
};
// One explicit schema drives saving and loading, including every color channel.
template<class Visitor> void Fields(Snapshot& s, Visitor visit) {
#define FIELD(group, member) visit(#group "." #member, s.group.member)
#define COLOR(group, member) for (std::size_t i = 0; i < std::size(s.group.member); ++i) visit(std::string(#group "." #member ".") + std::to_string(i), s.group.member[i])
    FIELD(esp, enabled); FIELD(esp, names); FIELD(esp, boxes); FIELD(esp, distance);
    FIELD(esp, health); FIELD(esp, teammates); FIELD(esp, armor); FIELD(esp, teamColors);
    COLOR(esp, boxColor); COLOR(esp, tColor); COLOR(esp, ctColor);
    FIELD(esp, boxStyle); FIELD(esp, boxThickness); FIELD(esp, boxOutline); FIELD(esp, boxFill);
    COLOR(esp, fillColor); FIELD(esp, nameUseBoxColor); COLOR(esp, nameColor);
    FIELD(esp, nameFont); FIELD(esp, nameSize); FIELD(esp, detailSize); COLOR(esp, detailColor);
    FIELD(esp, textOutline); COLOR(esp, outlineColor); COLOR(esp, healthColor);
    FIELD(esp, healthGradient); COLOR(esp, healthLowColor); FIELD(esp, healthBarWidth); FIELD(esp, healthText);
    FIELD(movement, bunnyhop); FIELD(movement, autostrafe);
    FIELD(world, enabled); FIELD(world, exposureEv); FIELD(world, contrast);
    FIELD(world, saturation); FIELD(world, gamma); COLOR(world, tint);
    FIELD(world, vignette); FIELD(world, sharpening); FIELD(world, skyEnabled);
    COLOR(world, skyTint); COLOR(world, skyLightingTint); FIELD(world, skyBrightness);
    FIELD(world, worldMaterialEnabled); FIELD(world, flatTextures); FIELD(world, lightingEnabled);
    FIELD(world, disableLocalLights); COLOR(world, localLightTint); FIELD(world, localLightBrightness);
    COLOR(world, sunLightTint); FIELD(world, sunLightBrightness); FIELD(world, ambientLightEnabled);
    COLOR(world, ambientLightTint); FIELD(world, ambientLightBrightness); FIELD(world, directionalLightmapsMode);
    FIELD(aim, enabled); FIELD(aim, fov); FIELD(aim, hitbox);
    FIELD(anti_aim, enabled);
    FIELD(camera, enabled); FIELD(camera, distance); FIELD(camera, shoulder);
    FIELD(camera, height); FIELD(camera, collision);
    FIELD(zoom, enabled); FIELD(zoom, zoom1); FIELD(zoom, zoom2);
#undef COLOR
#undef FIELD
}
bool ValidName(std::string_view name) {
    if (name.empty() || name.size() > 64 || name.back() == ' ') return false;
    std::string upper;
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ')) return false;
        upper += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL") return false;
    return !(upper.size() == 4 && (upper.starts_with("COM") || upper.starts_with("LPT")) && upper[3] >= '1' && upper[3] <= '9');
}
std::string_view Trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == value.npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}
template<class T> bool Parse(std::string_view text, T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        if (text != "0" && text != "1") return false;
        value = text == "1";
        return true;
    } else if constexpr (std::is_enum_v<T>) {
        int parsed{};
        if (!Parse(text, parsed)) return false;
        value = static_cast<T>(parsed);
        return true;
    } else {
        T parsed{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
        if constexpr (std::is_floating_point_v<T>) if (!std::isfinite(parsed)) return false;
        value = parsed;
        return true;
    }
}
bool PrepareDirectory(bool create, std::string& error) {
    const auto directory = LocalConfigDirectory();
    std::error_code ec;
    if (create) std::filesystem::create_directories(directory, ec);
    if (ec) { error = "Cannot create config directory: " + ec.message(); return false; }
    const auto status = std::filesystem::symlink_status(directory, ec);
    if (ec == std::errc::no_such_file_or_directory && !create) return true;
    if (ec || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) {
        error = "Config path is not a regular directory."; return false;
    }
    return true;
}
bool FilePath(std::string_view name, std::filesystem::path& path, std::string& error) {
    if (!ValidName(name)) { error = "Use 1-64 letters, digits, spaces, '-' or '_'; no trailing spaces or Windows device names."; return false; }
    path = LocalConfigDirectory() / (std::string(name) + ".cfg");
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory) return true;
    if (ec || (std::filesystem::exists(status) && !std::filesystem::is_regular_file(status))) {
        error = "Config path is not a regular file."; return false;
    }
    return true;
}
}

std::filesystem::path LocalConfigDirectory() {
    return std::filesystem::path(CS2_WORKSPACE_ROOT) / L"configs";
}
static bool LocalConfigListImpl(std::vector<std::string>& names, std::string& error) {
    names.clear(); error.clear();
    if (!PrepareDirectory(false, error)) return false;
    std::error_code ec;
    std::filesystem::directory_iterator iterator(LocalConfigDirectory(), ec);
    if (ec == std::errc::no_such_file_or_directory) return true;
    if (ec) { error = "Cannot list configs: " + ec.message(); return false; }
    const std::filesystem::directory_iterator end;
    for (; iterator != end; iterator.increment(ec)) {
        if (ec) break;
        const auto& entry = *iterator;
        const auto status = entry.symlink_status(ec);
        if (ec) break;
        if (!std::filesystem::is_regular_file(status) || entry.path().extension() != L".cfg") continue;
        const auto name = entry.path().stem().string();
        if (ValidName(name)) names.push_back(name);
    }
    if (ec) { names.clear(); error = "Cannot list configs: " + ec.message(); return false; }
    std::sort(names.begin(), names.end());
    return true;
}
static bool LocalConfigSaveImpl(std::string_view name, std::string& error) {
    error.clear();
    std::filesystem::path path;
    if (!FilePath(name, path, error) || !PrepareDirectory(true, error)) return false;
    Snapshot snapshot{EspGetSettings(), MovementGetSettings(), WorldGetSettings(), SilentAimGetSettings(), AntiAimGetSettings(), ThirdPersonGetSettings(), ZoomGetSettings()};
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(std::numeric_limits<float>::max_digits10) << "version=1\n";
    Fields(snapshot, [&](const auto& key, const auto& value) {
        text << key << '=';
        if constexpr (std::is_enum_v<std::remove_cvref_t<decltype(value)>>) text << static_cast<int>(value);
        else text << value;
        text << '\n';
    });
    // CREATE_NEW never follows a pre-existing temporary link.
    auto temp = path;
    temp += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
    const auto content = text.str();
    const HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = "Cannot create temporary config (Windows error " + std::to_string(GetLastError()) + ")."; return false; }
    DWORD written = 0;
    const bool saved = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) &&
        written == content.size() && FlushFileBuffers(file);
    DWORD failure = saved ? ERROR_SUCCESS : GetLastError();
    if (!saved && failure == ERROR_SUCCESS) failure = ERROR_WRITE_FAULT;
    if (!CloseHandle(file) && saved) failure = GetLastError();
    if (failure == ERROR_SUCCESS && !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) failure = GetLastError();
    if (failure != ERROR_SUCCESS) {
        DeleteFileW(temp.c_str());
        error = "Cannot save config (Windows error " + std::to_string(failure) + ").";
        return false;
    }
    return true;
}
static bool LocalConfigLoadImpl(std::string_view name, std::string& error) {
    error.clear();
    std::filesystem::path path;
    if (!FilePath(name, path, error) || !PrepareDirectory(false, error)) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) { error = "Cannot open config."; return false; }
    std::error_code sizeError;
    if (std::filesystem::file_size(path, sizeError) > 1024 * 1024 || sizeError) {
        error = "Cannot read config or config exceeds 1 MiB."; return false;
    }
    Snapshot snapshot{}; // Missing keys use documented defaults.
    std::set<std::string> seen;
    std::string line;
    bool versionFound = false;
    std::size_t bytes = 0;
    while (std::getline(file, line)) {
        bytes += line.size() + 1;
        if (bytes > 1024 * 1024) { error = "Config exceeds 1 MiB."; return false; }
        const auto trimmed = Trim(line);
        if (trimmed.empty() || trimmed.front() == '#') continue;
        const auto equals = trimmed.find('=');
        if (equals == trimmed.npos) { error = "Malformed config line: expected key=value."; return false; }
        const std::string key(Trim(trimmed.substr(0, equals)));
        const auto value = Trim(trimmed.substr(equals + 1));
        if (key.empty() || !seen.insert(key).second) { error = "Empty or duplicate config key: " + key; return false; }
        if (key == "version") {
            int version{};
            if (!Parse(value, version) || version != 1) { error = "Unsupported config version."; return false; }
            versionFound = true;
            continue;
        }
        bool valid = true;
        Fields(snapshot, [&](const auto& candidate, auto& field) { if (key == candidate) valid = Parse(value, field); });
        if (!valid) { error = "Invalid config value: " + key; return false; }
    }
    if (file.bad() || !versionFound) { error = file.bad() ? "Cannot read config." : "Missing config version."; return false; }
    // Publish only after the entire file parses, through the existing bounded setters.
    EspSetSettings(snapshot.esp);
    MovementSetSettings(snapshot.movement);
    WorldSetSettings(snapshot.world);
    SilentAimSetSettings(snapshot.aim);
    AntiAimSetSettings(snapshot.anti_aim);
    ThirdPersonSetSettings(snapshot.camera);
    ZoomSetSettings(snapshot.zoom);
    return true;
}

// UI callbacks must receive errors instead of unwinding through the render hook.
bool LocalConfigList(std::vector<std::string>& names, std::string& error) {
    try { return LocalConfigListImpl(names, error); }
    catch (const std::exception& ex) { names.clear(); error = ex.what(); return false; }
}
bool LocalConfigSave(std::string_view name, std::string& error) {
    try { return LocalConfigSaveImpl(name, error); }
    catch (const std::exception& ex) { error = ex.what(); return false; }
}
bool LocalConfigLoad(std::string_view name, std::string& error) {
    try { return LocalConfigLoadImpl(name, error); }
    catch (const std::exception& ex) { error = ex.what(); return false; }
}
