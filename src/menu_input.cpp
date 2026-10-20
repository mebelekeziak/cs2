#include "menu_input.h"
#include <MinHook.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
// Public SDL3 C ABI, verified against the installed 3.5.0 exports and SDL headers.
// Opaque windows and the fixed public event union require no engine offsets.
struct alignas(8) Event { std::uint32_t type; unsigned char rest[124]; };
static_assert(sizeof(Event) == 128 && alignof(Event) == 8);
using PumpFn = void(*)();
using FocusFn = void*(*)();
using PropertiesFn = std::uint32_t(*)(void*);
using PropertyFn = void*(*)(std::uint32_t, const char*, void*);
using WindowBoolFn = bool(*)(void*);
using SetWindowBoolFn = bool(*)(void*, bool);
using BoolFn = bool(*)();
using PeepFn = int(*)(void*, int, int, std::uint32_t, std::uint32_t);
using PollFn = bool(*)(void*);
using KeyboardFn = const bool*(*)(int*);
using MouseFn = std::uint32_t(*)(float*, float*);
using WarpWindowFn = void(*)(void*, float, float);
using WarpGlobalFn = bool(*)(float, float);
PumpFn pump = nullptr, resetKeyboard = nullptr;
FocusFn focus = nullptr;
PropertiesFn properties = nullptr;
PropertyFn pointerProperty = nullptr;
WindowBoolFn getRelative = nullptr, getGrab = nullptr;
SetWindowBoolFn setRelative = nullptr, setGrab = nullptr;
BoolFn cursorVisible = nullptr, showCursor = nullptr, hideCursor = nullptr;
PeepFn peep = nullptr;
PollFn poll = nullptr;
KeyboardFn keyboard = nullptr;
MouseFn globalMouse = nullptr, mouse = nullptr, relativeMouse = nullptr;
WarpWindowFn warpWindow = nullptr;
WarpGlobalFn warpGlobal = nullptr;
std::atomic<HWND> requestedWindow{nullptr};
std::atomic<bool> requested{false}, prepared{false}, permanentlyStopped{false};
std::atomic<float> wheelX{0}, wheelY{0};
std::atomic<void*> capturedWindow{nullptr};
DWORD ownerThread = 0;
HWND boundWindow = nullptr;
WNDPROC previousProc = nullptr;
bool captured = false, applying = false;
bool savedRelative = false, savedGrab = false, savedCursor = false;
#ifdef CS2_MENU_INPUT_CHECKS
bool (*testForeground)(HWND) = nullptr;
#endif
bool Foreground(HWND window) noexcept {
#ifdef CS2_MENU_INPUT_CHECKS
    if (testForeground) return testForeground(window);
#endif
    return window && IsWindow(window) && GetForegroundWindow() == window;
}
bool WantsCapture() noexcept {
    return prepared.load(std::memory_order_acquire) && !permanentlyStopped.load(std::memory_order_acquire) &&
        requested.load(std::memory_order_acquire) && Foreground(requestedWindow.load(std::memory_order_acquire));
}
bool FiltersInput() noexcept {
    return menuInputCapturing.load(std::memory_order_acquire) && WantsCapture();
}
bool Captures(void* window) noexcept {
    return !applying && captured && window == capturedWindow.load(std::memory_order_acquire) && WantsCapture();
}
void RestoreOwner() noexcept {
    menuInputCapturing.store(false, std::memory_order_release);
    if (!captured) return;
    applying = true;
    // Never dereference a window whose HWND has been destroyed/replaced.
    void* window = capturedWindow.load(std::memory_order_acquire);
    if (boundWindow && IsWindow(boundWindow) && window) {
        setRelative(window, savedRelative);
        setGrab(window, savedGrab);
    }
    if (savedCursor) showCursor(); else hideCursor();
    capturedWindow.store(nullptr, std::memory_order_release);
    captured = false;
    applying = false;
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    const auto native = previousProc;
    if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !wparam)) RestoreOwner();
    const LRESULT result = CallWindowProcW(native, window, message, wparam, lparam);
    if (message == WM_NCDESTROY) {
        // SDL owns destruction; discard identity before another pump can use it.
        captured = false;
        capturedWindow.store(nullptr, std::memory_order_release);
        menuInputCapturing.store(false, std::memory_order_release);
        boundWindow = nullptr;
        previousProc = nullptr;
        requested.store(false, std::memory_order_release);
    }
    return result;
}
bool BindOwner(HWND window) noexcept {
    DWORD pid = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &pid);
    if (!thread || thread != GetCurrentThreadId() || pid != GetCurrentProcessId()) return false;
    if (boundWindow == window) return true;
    if (boundWindow) return false; // Retain any existing resident subclass chain.
    SetLastError(ERROR_SUCCESS);
    const auto native = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&WindowProc)));
    if (!native && GetLastError() != ERROR_SUCCESS) return false;
    previousProc = native;
    boundWindow = window;
    ownerThread = thread;
    return true;
}
void SyncOwner() noexcept {
    const bool wants = WantsCapture();
    if (!wants) { RestoreOwner(); return; }
    HWND target = requestedWindow.load(std::memory_order_acquire);
    if (GetWindowThreadProcessId(target, nullptr) != GetCurrentThreadId()) return;
    void* window = focus();
    if (!window || pointerProperty(properties(window), "SDL.window.win32.hwnd", nullptr) != target ||
        !BindOwner(target)) {
        RestoreOwner();
        menuInputCapturing.store(false, std::memory_order_release);
        return;
    }
    if (captured && capturedWindow.load(std::memory_order_acquire) != window) RestoreOwner();
    if (!captured) {
        savedRelative = getRelative(window);
        savedGrab = getGrab(window);
        savedCursor = cursorVisible();
        capturedWindow.store(window, std::memory_order_release);
        captured = true;
        // Publish releases to the native queue before filtering downs. SDL has no
        // public mouse reset; route ordinary button-up messages through its own WndProc.
        resetKeyboard();
        for (const UINT release : {WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP})
            CallWindowProcW(previousProc, target, release, 0, 0);
        CallWindowProcW(previousProc, target, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), 0);
        CallWindowProcW(previousProc, target, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), 0);
    }
    applying = true;
    const bool relativeOk = setRelative(window, false);
    const bool grabOk = setGrab(window, false);
    const bool cursorOk = showCursor();
    applying = false;
    if (!relativeOk || !grabOk || !cursorOk) {
        RestoreOwner();
        menuInputCapturing.store(false, std::memory_order_release);
    } else {
        menuInputCapturing.store(true, std::memory_order_release);
    }
}
void HookPump() {
    if (ownerThread && GetCurrentThreadId() != ownerThread) { pump(); return; }
    // SDL video APIs execute only on the same main/window owner that pumps events.
    SyncOwner();
    pump();
    if (captured && WantsCapture()) resetKeyboard();
    SyncOwner();
}
bool HookSetRelative(void* window, bool enabled) {
    if (GetCurrentThreadId() == ownerThread && Captures(window)) { savedRelative = enabled; return true; }
    return setRelative(window, enabled);
}
bool HookSetGrab(void* window, bool enabled) {
    if (GetCurrentThreadId() == ownerThread && Captures(window)) { savedGrab = enabled; return true; }
    return setGrab(window, enabled);
}
bool HookShowCursor() {
    if (GetCurrentThreadId() == ownerThread && !applying && captured && WantsCapture()) savedCursor = true;
    return showCursor();
}
bool HookHideCursor() {
    if (GetCurrentThreadId() == ownerThread && !applying && captured && WantsCapture()) { savedCursor = false; return true; }
    return hideCursor();
}
void HookWarpWindow(void* window, float x, float y) {
    if (Captures(window)) return;
    warpWindow(window, x, y);
}
bool HookWarpGlobal(float x, float y) {
    if (!applying && captured && WantsCapture()) return true;
    return warpGlobal(x, y);
}
void Accumulate(std::atomic<float>& destination, float value) noexcept {
    if (!std::isfinite(value)) return;
    const float delta = std::clamp(value, -1000.0f, 1000.0f);
    float old = destination.load(std::memory_order_relaxed);
    while (!destination.compare_exchange_weak(old, std::clamp(old + delta, -1000.0f, 1000.0f), std::memory_order_relaxed)) {}
}
bool Filter(Event& event, bool consume) noexcept {
    if (!FiltersInput()) return false;
    // Preserve releases, window/focus/device/lifecycle and quit events. Filtering
    // the output handles SDL's buffered raw input as well as legacy Win32 events.
    switch (event.type) {
    case 0x300: // SDL_EVENT_KEY_DOWN
    case 0x302: // SDL_EVENT_TEXT_EDITING
    case 0x303: // SDL_EVENT_TEXT_INPUT
    case 0x307: // SDL_EVENT_TEXT_EDITING_CANDIDATES
    case 0x400: // SDL_EVENT_MOUSE_MOTION
    case 0x401: // SDL_EVENT_MOUSE_BUTTON_DOWN
        return true;
    case 0x403: { // SDL_EVENT_MOUSE_WHEEL: public SDL3 layout, no engine offsets.
        if (consume) {
            float x = 0, y = 0;
            std::uint32_t direction = 0;
            std::memcpy(&x, reinterpret_cast<unsigned char*>(&event) + 24, sizeof(x));
            std::memcpy(&y, reinterpret_cast<unsigned char*>(&event) + 28, sizeof(y));
            std::memcpy(&direction, reinterpret_cast<unsigned char*>(&event) + 32, sizeof(direction));
            if (direction == 1) { x = -x; y = -y; }
            Accumulate(wheelX, x); Accumulate(wheelY, y);
        }
        return true;
    }
    default: return false;
    }
}
int HookPeep(void* raw, int count, int action, std::uint32_t minimum, std::uint32_t maximum) {
    if (!FiltersInput() || !raw || count <= 0 || action == 0)
        return peep(raw, count, action, minimum, maximum);
    auto* events = static_cast<Event*>(raw);
    if (action == 1) { // PEEK must not remove events or deliver a wheel more than once.
        const int available = peep(nullptr, 0, action, minimum, maximum);
        if (available <= 0) return available;
        if (available > 1048576) return 0; // Refuse unreasonable allocation without leaking input.
        auto* scratch = static_cast<Event*>(HeapAlloc(GetProcessHeap(), 0, sizeof(Event) * static_cast<SIZE_T>(available)));
        if (!scratch) return 0;
        const int found = peep(scratch, available, action, minimum, maximum);
        int output = 0;
        for (int i = 0; i < found && output < count; ++i)
            if (!Filter(scratch[i], false)) events[output++] = scratch[i];
        HeapFree(GetProcessHeap(), 0, scratch);
        return found < 0 ? found : output;
    }
    if (action != 2) return peep(raw, count, action, minimum, maximum);
    int output = 0;
    while (output < count) {
        const int found = peep(events + output, count - output, action, minimum, maximum);
        if (found <= 0) return output ? output : found;
        const int start = output;
        for (int i = 0; i < found; ++i)
            if (!Filter(events[start + i], true)) events[output++] = events[start + i];
    }
    return output;
}
bool HookPoll(void* raw) {
    if (!raw || !FiltersInput()) return poll(raw);
    while (poll(raw)) if (!Filter(*static_cast<Event*>(raw), true)) return true;
    return false;
}
const bool* HookKeyboard(int* count) {
    const bool* native = keyboard(count);
    static const bool released[512]{}; // SDL_SCANCODE_COUNT in the public SDL3 ABI.
    if (!FiltersInput()) return native;
    if (count) *count = 512;
    return released;
}
std::uint32_t HookGlobalMouse(float* x, float* y) {
    const auto buttons = globalMouse(x, y);
    return FiltersInput() ? 0 : buttons;
}
std::uint32_t HookMouse(float* x, float* y) {
    const auto buttons = mouse(x, y);
    return FiltersInput() ? 0 : buttons;
}
std::uint32_t HookRelativeMouse(float* x, float* y) {
    const auto buttons = relativeMouse(x, y);
    if (!FiltersInput()) return buttons;
    if (x) *x = 0; if (y) *y = 0;
    return 0;
}
template<typename T> bool Resolve(HMODULE module, const char* name, T& value) noexcept {
    value = reinterpret_cast<T>(GetProcAddress(module, name));
    return value != nullptr;
}
template<typename T, typename D> bool Queue(T& function, D detour) noexcept {
    const auto target = reinterpret_cast<void*>(function);
    return MH_CreateHook(target, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(&function)) == MH_OK &&
        MH_QueueEnableHook(target) == MH_OK;
}
}
bool MenuInputPrepare(MenuInputLogCallback logger) noexcept {
    HMODULE sdl = GetModuleHandleW(L"SDL3.dll");
    const bool resolved = sdl &&
        Resolve(sdl, "SDL_PumpEvents", pump) && Resolve(sdl, "SDL_GetKeyboardFocus", focus) &&
        Resolve(sdl, "SDL_GetWindowProperties", properties) && Resolve(sdl, "SDL_GetPointerProperty", pointerProperty) &&
        Resolve(sdl, "SDL_GetWindowRelativeMouseMode", getRelative) && Resolve(sdl, "SDL_SetWindowRelativeMouseMode", setRelative) &&
        Resolve(sdl, "SDL_GetWindowMouseGrab", getGrab) && Resolve(sdl, "SDL_SetWindowMouseGrab", setGrab) &&
        Resolve(sdl, "SDL_CursorVisible", cursorVisible) && Resolve(sdl, "SDL_ShowCursor", showCursor) &&
        Resolve(sdl, "SDL_HideCursor", hideCursor) && Resolve(sdl, "SDL_ResetKeyboard", resetKeyboard) &&
        Resolve(sdl, "SDL_PeepEvents", peep) && Resolve(sdl, "SDL_PollEvent", poll) &&
        Resolve(sdl, "SDL_GetKeyboardState", keyboard) && Resolve(sdl, "SDL_GetGlobalMouseState", globalMouse) &&
        Resolve(sdl, "SDL_GetMouseState", mouse) && Resolve(sdl, "SDL_GetRelativeMouseState", relativeMouse) &&
        Resolve(sdl, "SDL_WarpMouseInWindow", warpWindow) && Resolve(sdl, "SDL_WarpMouseGlobal", warpGlobal);
    const bool queued = resolved && Queue(pump, &HookPump) && Queue(setRelative, &HookSetRelative) &&
        Queue(setGrab, &HookSetGrab) && Queue(showCursor, &HookShowCursor) && Queue(hideCursor, &HookHideCursor) &&
        Queue(peep, &HookPeep) && Queue(poll, &HookPoll) && Queue(keyboard, &HookKeyboard) &&
        Queue(globalMouse, &HookGlobalMouse) && Queue(mouse, &HookMouse) && Queue(relativeMouse, &HookRelativeMouse) &&
        Queue(warpWindow, &HookWarpWindow) && Queue(warpGlobal, &HookWarpGlobal);
    prepared.store(queued, std::memory_order_release);
    if (logger) logger(queued ? "MENU_INPUT_READY SDL3 owner-pump cursor release and keyboard/mouse capture queued" :
        "MENU_INPUT_UNAVAILABLE SDL3 exports or hook preparation failed; input takeover unavailable");
    return queued;
}
void MenuInputRequest(HWND window, bool visible) noexcept {
    requestedWindow.store(window, std::memory_order_release);
    requested.store(visible, std::memory_order_release);
    const bool active = WantsCapture();
    if (!active) menuInputCapturing.store(false, std::memory_order_release);
    if (!active) { wheelX.store(0); wheelY.store(0); }
}
void MenuInputDrainWheel(float& horizontal, float& vertical) noexcept {
    const float x = wheelX.exchange(0), y = wheelY.exchange(0);
    horizontal = FiltersInput() ? -x : 0;
    vertical = FiltersInput() ? y : 0;
}
void MenuInputStop() noexcept {
    permanentlyStopped.store(true, std::memory_order_release);
    requested.store(false, std::memory_order_release);
    menuInputCapturing.store(false, std::memory_order_release);
    wheelX.store(0); wheelY.store(0);
}
#ifdef CS2_MENU_INPUT_CHECKS
void MenuInputTestConfigure(const MenuInputTestApi& api) noexcept {
    pump = api.pump; focus = api.focus; properties = api.properties; pointerProperty = api.pointerProperty;
    getRelative = api.getRelative; setRelative = api.setRelative; getGrab = api.getGrab; setGrab = api.setGrab;
    cursorVisible = api.cursorVisible; showCursor = api.showCursor; hideCursor = api.hideCursor;
    resetKeyboard = api.resetKeyboard; peep = api.peep; poll = api.poll; keyboard = api.keyboard;
    globalMouse = mouse = relativeMouse = api.mouse; warpWindow = api.warpWindow; warpGlobal = api.warpGlobal;
    testForeground = api.foreground;
    prepared.store(true); permanentlyStopped.store(false); requested.store(false); menuInputCapturing.store(false);
    wheelX.store(0); wheelY.store(0);
}
void MenuInputTestPump() noexcept { HookPump(); }
int MenuInputTestPeep(void* events, int count, int action, std::uint32_t low, std::uint32_t high) noexcept { return HookPeep(events, count, action, low, high); }
bool MenuInputTestPoll(void* event) noexcept { return HookPoll(event); }
const bool* MenuInputTestKeyboard(int* count) noexcept { return HookKeyboard(count); }
std::uint32_t MenuInputTestMouse(float* x, float* y) noexcept { return HookGlobalMouse(x, y); }
bool MenuInputTestSetRelative(void* window, bool enabled) noexcept { return HookSetRelative(window, enabled); }
bool MenuInputTestSetGrab(void* window, bool enabled) noexcept { return HookSetGrab(window, enabled); }
bool MenuInputTestShowCursor(bool shown) noexcept { return shown ? HookShowCursor() : HookHideCursor(); }
void MenuInputTestWarp(void* window, float x, float y) noexcept { HookWarpWindow(window, x, y); }
#endif
