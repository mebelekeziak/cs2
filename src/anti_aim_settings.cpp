#include "anti_aim_settings.h"
#include <atomic>

namespace {
std::atomic<bool> enabled{false};
}

AntiAimSettings AntiAimGetSettings() noexcept {
    return {enabled.load(std::memory_order_acquire)};
}

void AntiAimSetSettings(const AntiAimSettings& settings) noexcept {
    enabled.store(settings.enabled, std::memory_order_release);
}
