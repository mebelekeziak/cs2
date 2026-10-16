#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Render/menu owner only, outside DllMain. No automatic load or engine access.
// Names: 1-64 ASCII letters/digits, spaces, '-' and '_'; no trailing space or
// reserved Windows device names. Files live under workspace/configs/*.cfg.
std::filesystem::path LocalConfigDirectory();
bool LocalConfigList(std::vector<std::string>& names, std::string& error);
bool LocalConfigSave(std::string_view name, std::string& error);
bool LocalConfigLoad(std::string_view name, std::string& error);
