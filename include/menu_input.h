#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>

// Shared with command features, including standalone fixtures that do not link SDL.
inline std::atomic<bool> menuInputCapturing{false};
inline bool MenuInputCapturing() noexcept {
    return menuInputCapturing.load(std::memory_order_acquire);
}
using MenuInputLogCallback = void(*)(const char*) noexcept;
// Resolve loaded SDL exports and queue hooks after MH_Initialize, before ApplyQueued.
bool MenuInputPrepare(MenuInputLogCallback logger) noexcept;
void MenuInputRequest(HWND window, bool visible) noexcept;
void MenuInputDrainWheel(float& horizontal, float& vertical) noexcept;
// Restoration runs on the next SDL owner pump. Hooks and code remain resident.
void MenuInputStop() noexcept;

#ifdef CS2_MENU_INPUT_CHECKS
struct MenuInputTestApi {
    void (*pump)();
    void* (*focus)();
    std::uint32_t (*properties)(void*);
    void* (*pointerProperty)(std::uint32_t, const char*, void*);
    bool (*getRelative)(void*);
    bool (*setRelative)(void*, bool);
    bool (*getGrab)(void*);
    bool (*setGrab)(void*, bool);
    bool (*cursorVisible)();
    bool (*showCursor)();
    bool (*hideCursor)();
    void (*resetKeyboard)();
    int (*peep)(void*, int, int, std::uint32_t, std::uint32_t);
    bool (*poll)(void*);
    const bool* (*keyboard)(int*);
    std::uint32_t (*mouse)(float*, float*);
    void (*warpWindow)(void*, float, float);
    bool (*warpGlobal)(float, float);
    bool (*foreground)(HWND);
};
void MenuInputTestConfigure(const MenuInputTestApi&) noexcept;
void MenuInputTestPump() noexcept;
int MenuInputTestPeep(void*, int, int, std::uint32_t, std::uint32_t) noexcept;
bool MenuInputTestPoll(void*) noexcept;
const bool* MenuInputTestKeyboard(int*) noexcept;
std::uint32_t MenuInputTestMouse(float*, float*) noexcept;
bool MenuInputTestSetRelative(void*, bool) noexcept;
bool MenuInputTestSetGrab(void*, bool) noexcept;
bool MenuInputTestShowCursor(bool) noexcept;
void MenuInputTestWarp(void*, float, float) noexcept;
#endif
