#include "silent_aim_settings.h"
#include <Windows.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
// Hundredths of a degree, with the enable bit above the entire FOV range.
constexpr std::uint64_t enableBit = 1ull << 16;
constexpr std::uint64_t autofireBit = 1ull << 19;
std::atomic<std::uint64_t> settings{500 | (1ull << 17) | (5000ull << 20) | (256ull << 34)};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
bool wasDown = false; // Render owner only.
}

SilentAimSettings SilentAimGetSettings() noexcept {
    const auto value = settings.load(std::memory_order_acquire);
    return {(value & enableBit) != 0, static_cast<float>(value & 0xffff) / 100.0f,
        static_cast<AimHitbox>((value >> 17) & 3), (value & autofireBit) != 0,
        static_cast<float>((value >> 20) & 0x3fff) / 100.0f, static_cast<int>((value >> 34) & 0x7ff)};
}
void SilentAimSetSettings(const SilentAimSettings& value) noexcept {
    const float fov = std::isfinite(value.fov) ? std::clamp(value.fov, 0.1f, 180.0f) : 5.0f;
    const auto hitbox = static_cast<unsigned>(std::clamp(static_cast<int>(value.hitbox), 0, 3));
    const float chance = std::isfinite(value.hitchance) ? std::clamp(value.hitchance, 0.0f, 100.0f) : 50.0f;
    settings.store(static_cast<std::uint64_t>(std::lround(fov * 100)) |
        (value.enabled ? enableBit : 0) | (static_cast<std::uint64_t>(hitbox) << 17) |
        (value.autofire ? autofireBit : 0) | (static_cast<std::uint64_t>(std::lround(chance * 100)) << 20) |
        (static_cast<std::uint64_t>(std::clamp(value.samples, 64, 1024)) << 34), std::memory_order_release);
}
void SilentAimPollControls(bool foreground) noexcept {
    const bool down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    if (foreground && down && !wasDown) settings.fetch_xor(enableBit, std::memory_order_acq_rel);
    wasDown = down;
}
