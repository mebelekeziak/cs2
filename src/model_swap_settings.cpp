#include "model_swap_settings.h"
#include <algorithm>
#include <mutex>

namespace {
std::mutex settingsMutex;
ModelSwapSettings currentSettings;
ModelSwapStatus runtimeStatus{};
char Lower(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}
}

bool ModelSwapSetModel(ModelSwapSettings& settings, std::string_view path) noexcept {
    std::array<char, 260> normalized{};
    if (path.size() >= normalized.size()) return false;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const auto value = static_cast<unsigned char>(path[i]);
        if (value < 32 || value == 127 || path[i] == ':' || path[i] == '*' ||
            path[i] == '?' || path[i] == '"' || path[i] == '<' || path[i] == '>' || path[i] == '|') return false;
        normalized[i] = path[i] == '\\' ? '/' : Lower(path[i]);
    }
    const std::string_view resource(normalized.data(), path.size());
    if (!resource.empty()) {
        if (!resource.ends_with(".vmdl") || resource.front() == '/') return false;
        std::size_t start = 0;
        while (start < resource.size()) {
            const auto slash = resource.find('/', start);
            const auto part = resource.substr(start, slash == resource.npos ? resource.size() - start : slash - start);
            if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
            if (slash == resource.npos) break;
            start = slash + 1;
        }
        const auto slash = resource.find('/');
        if (slash == resource.npos) return false;
        const auto root = resource.substr(0, slash);
        if (root != "agents" && root != "characters" && root != "models" && root != "phase2") return false;
        const auto filename = resource.substr(resource.rfind('/') + 1);
        if (filename.size() <= 5) return false;
    }
    settings.model = normalized;
    return true;
}

ModelSwapSettings ModelSwapSanitizeSettings(const ModelSwapSettings& settings) noexcept {
    const auto end = std::find(settings.model.begin(), settings.model.end(), '\0');
    if (end == settings.model.end()) return {};
    ModelSwapSettings result;
    result.enabled = settings.enabled;
    if (!ModelSwapSetModel(result, {settings.model.data(), static_cast<std::size_t>(end - settings.model.begin())})) return {};
    return result;
}

ModelSwapSettings ModelSwapGetSettings() noexcept {
    std::lock_guard<std::mutex> lock(settingsMutex);
    return currentSettings;
}
void ModelSwapSetSettings(const ModelSwapSettings& settings) noexcept {
    const auto sanitized = ModelSwapSanitizeSettings(settings);
    std::lock_guard<std::mutex> lock(settingsMutex);
    currentSettings = sanitized;
}
void ModelSwapResetSettings() noexcept { ModelSwapSetSettings({}); }
ModelSwapStatus ModelSwapRuntimeStatus() noexcept {
    std::lock_guard<std::mutex> lock(settingsMutex);
    return runtimeStatus;
}
void ModelSwapSetRuntimeStatus(const char* message) noexcept {
    ModelSwapStatus bounded{};
    if (message) {
        for (std::size_t i = 0; i + 1 < bounded.size() && message[i]; ++i) bounded[i] = message[i];
    }
    std::lock_guard<std::mutex> lock(settingsMutex);
    runtimeStatus = bounded;
}
