#include "movement_settings.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>

namespace {
std::atomic<unsigned> settingsBits{3};
std::atomic<MovementLogCallback> logger{nullptr};
bool keyDown[2]{}; // Render thread only.
void Report(unsigned bits) noexcept {
    if (const auto callback = logger.load(std::memory_order_acquire)) {
        char text[128]{};
        snprintf(text, sizeof(text), "MOVE_SETTINGS bunnyhop=%u autostrafe=%u", bits & 1, (bits >> 1) & 1);
        callback(text);
    }
}
}

MovementSettings MovementGetSettings() noexcept {
    const unsigned bits = settingsBits.load(std::memory_order_acquire);
    return { (bits & 1) != 0, (bits & 2) != 0 };
}
void MovementSetSettings(const MovementSettings& settings) noexcept {
    const unsigned bits = (settings.bunnyhop ? 1u : 0u) | (settings.autostrafe ? 2u : 0u);
    if (settingsBits.exchange(bits, std::memory_order_acq_rel) != bits) Report(bits);
}
void MovementSetLogCallback(MovementLogCallback callback) noexcept {
    logger.store(callback, std::memory_order_release);
    Report(settingsBits.load(std::memory_order_acquire));
}
void MovementPollControls(bool foreground) noexcept {
    const int keys[2]{ VK_F8, VK_F9 };
    for (unsigned i = 0; i < 2; ++i) {
        const bool down = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
        if (foreground && down && !keyDown[i]) {
            const unsigned next = settingsBits.fetch_xor(1u << i, std::memory_order_acq_rel) ^ (1u << i);
            Report(next);
        }
        keyDown[i] = down;
    }
}
