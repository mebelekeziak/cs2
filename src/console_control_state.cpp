#include "console_control.h"
#include <atomic>

namespace {
std::atomic<bool> available{false}, onlyOwn{false};
// 0 idle, 1 requested, 2 queued, 3 completed. Repeated clicks coalesce.
std::atomic<unsigned> clearState{0};
}
bool ConsoleControlAvailable() noexcept { return available.load(std::memory_order_acquire); }
bool ConsoleControlOnlyOwn() noexcept { return onlyOwn.load(std::memory_order_acquire); }
void ConsoleControlSetOnlyOwn(bool enabled) noexcept {
    if (!ConsoleControlAvailable()) return;
    const bool previous = onlyOwn.exchange(enabled, std::memory_order_acq_rel);
    if (enabled && !previous) ConsoleControlRequestClear();
}
void ConsoleControlRequestClear() noexcept {
    if (!ConsoleControlAvailable()) return;
    unsigned state = clearState.load(std::memory_order_acquire);
    while (state != 1 && state != 2 && !clearState.compare_exchange_weak(state, 1)) {}
}
bool ConsoleControlTakeRequest() noexcept {
    unsigned state = 1;
    return clearState.compare_exchange_strong(state, 2);
}
void ConsoleControlCompleteClear() noexcept { clearState.store(3, std::memory_order_release); }
void ConsoleControlSetAvailable(bool value) noexcept {
    available.store(value, std::memory_order_release);
    if (!value) { onlyOwn.store(false); clearState.store(0); }
}
const char* ConsoleControlStatus() noexcept {
    if (!ConsoleControlAvailable()) return "Console controls unavailable.";
    switch (clearState.load(std::memory_order_acquire)) {
    case 1: case 2: return "Clear requested; waiting for the native console.";
    case 3: return "Console cleared; recent project logs restored.";
    default: return "Clear keeps the latest 256 project log lines.";
    }
}
