#include "shot_log_settings.h"
#include <atomic>
#include <cstring>

namespace { std::atomic<unsigned> settings{1}; }
ShotLogSettings ShotLogGetSettings() noexcept {
    const auto value = settings.load(std::memory_order_acquire);
    return {(value & 1) != 0, (value & 2) != 0};
}
void ShotLogSetSettings(const ShotLogSettings& value) noexcept {
    settings.store((value.enabled ? 1u : 0u) | (value.verbose ? 2u : 0u), std::memory_order_release);
}
const char* ShotLogConsoleMessage(const char* message, const ShotLogSettings& value) noexcept {
    if (!message) return nullptr;
    const char* body = message;
    if (std::strncmp(body, "[move_tid=", 10) == 0) {
        const char* cursor = body + 10;
        const auto digits = [](const char*& p) {
            if (*p < '0' || *p > '9') return false;
            while (*p >= '0' && *p <= '9') ++p;
            return true;
        };
        if (!digits(cursor) || std::strncmp(cursor, " created_ms=", 12)) return nullptr;
        cursor += 12;
        if (!digits(cursor) || std::strncmp(cursor, "] ", 2)) return nullptr;
        body = cursor + 2;
    }
    if (std::strncmp(body, "AIM_", 4)) return nullptr;
    if (!std::strncmp(body, "AIM_CONSOLE_", 12)) return body;
    if (!std::strncmp(body, "AIM_SHOT cmd #", 14))
        return value.verbose ? body : nullptr;
    if (!std::strncmp(body, "AIM_SHOT ", 9) || !std::strncmp(body, "AIM_HIT ", 8) || !std::strncmp(body, "AIM_SHOT_LOG_", 13))
        return value.enabled ? body : nullptr;
    return value.verbose ? body : nullptr;
}
