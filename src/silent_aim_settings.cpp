#include "silent_aim_settings.h"
#include <Windows.h>
#include <atomic>
#include <algorithm>
#include <cmath>

namespace {
// Hundredths of a degree, with the enable bit above the entire FOV range.
constexpr unsigned enableBit = 1u << 16;
std::atomic<unsigned> settings{500 | (static_cast<unsigned>(AimHitbox::Chest) << 17)};
bool wasDown = false; // Render owner only.
}

SilentAimSettings SilentAimGetSettings() noexcept {
    const unsigned value = settings.load(std::memory_order_acquire);
    return {(value & enableBit) != 0, static_cast<float>(value & 0xffff) / 100.0f,
        static_cast<AimHitbox>((value >> 17) & 3)};
}
void SilentAimSetSettings(const SilentAimSettings& value) noexcept {
    const float fov = std::isfinite(value.fov) ? std::clamp(value.fov, 0.1f, 30.0f) : 5.0f;
    const auto hitbox = static_cast<unsigned>(std::clamp(static_cast<int>(value.hitbox), 0, 3));
    settings.store(static_cast<unsigned>(std::lround(fov * 100)) |
        (value.enabled ? enableBit : 0) | (hitbox << 17), std::memory_order_release);
}
void SilentAimPollControls(bool foreground) noexcept {
    const bool down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    if (foreground && down && !wasDown) settings.fetch_xor(enableBit, std::memory_order_acq_rel);
    wasDown = down;
}
