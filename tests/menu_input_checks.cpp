#include "menu_input.h"
#include <array>
#include <vector>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <thread>
#include <MinHook.h>

namespace {
struct alignas(8) Event { std::uint32_t type = 0; unsigned char data[124]{}; };
std::vector<Event> queue;
HWND game = nullptr;
void* const sdlWindow = reinterpret_cast<void*>(0x1000);
void* const otherWindow = reinterpret_cast<void*>(0x2000);
bool foreground = true, relative = true, grab = true, cursor = false;
bool wrongProperty = false, pumpPress = false, failRelative = false;
unsigned pumpCalls = 0, resets = 0, warps = 0, nativeMessages = 0, focusCalls = 0;
std::array<bool, 512> keys{};
unsigned checks = 0, failures = 0;
void Expect(bool value, const char* label) { ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", label); } }
LRESULT CALLBACK Native(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    ++nativeMessages;
    return DefWindowProcW(window, message, wparam, lparam);
}
void Pump() { ++pumpCalls; if (pumpPress) keys[4] = true; }
void* Focus() { ++focusCalls; return sdlWindow; }
std::uint32_t Properties(void* window) { return window == sdlWindow ? 7 : 0; }
void* Property(std::uint32_t id, const char* name, void* fallback) {
    return !wrongProperty && id == 7 && !std::strcmp(name, "SDL.window.win32.hwnd") ? game : fallback;
}
bool GetRelative(void*) { return relative; }
bool SetRelative(void*, bool value) { if (failRelative) return false; relative = value; return true; }
bool GetGrab(void*) { return grab; }
bool SetGrab(void*, bool value) { grab = value; return true; }
bool Cursor() { return cursor; }
bool Show() { cursor = true; return true; }
bool Hide() { cursor = false; return true; }
void ResetKeyboard() { ++resets; keys.fill(false); }
int Peep(void* output, int count, int action, std::uint32_t low, std::uint32_t high) {
    auto* events = static_cast<Event*>(output);
    if (action == 0) { for (int i = 0; i < count; ++i) queue.push_back(events[i]); return count; }
    if (!events) return static_cast<int>(std::count_if(queue.begin(), queue.end(), [&](auto& e) { return e.type >= low && e.type <= high; }));
    int found = 0;
    for (std::size_t i = 0; i < queue.size() && found < count;) {
        if (queue[i].type < low || queue[i].type > high) { ++i; continue; }
        events[found++] = queue[i];
        if (action == 2) queue.erase(queue.begin() + static_cast<std::ptrdiff_t>(i)); else ++i;
    }
    return found;
}
bool Poll(void* event) { return event ? Peep(event, 1, 2, 0, 0xffff) > 0 : !queue.empty(); }
const bool* Keyboard(int* count) { if (count) *count = 512; return keys.data(); }
std::uint32_t Mouse(float* x, float* y) { if (x) *x = 12; if (y) *y = 34; return 3; }
void Warp(void*, float, float) { ++warps; }
bool WarpGlobal(float, float) { ++warps; return true; }
bool Foreground(HWND window) { return foreground && window == game; }
Event Make(std::uint32_t type) { Event event; event.type = type; return event; }
Event Wheel(float x, float y, std::uint32_t direction = 0) {
    Event event = Make(0x403);
    std::memcpy(reinterpret_cast<unsigned char*>(&event) + 24, &x, 4);
    std::memcpy(reinterpret_cast<unsigned char*>(&event) + 28, &y, 4);
    std::memcpy(reinterpret_cast<unsigned char*>(&event) + 32, &direction, 4);
    return event;
}
}
int main(int argc, char** argv) {
    if (argc == 2 && !std::strcmp(argv[1], "--installed-sdl")) {
        const auto library = LoadLibraryExW(L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Counter-Strike Global Offensive\\game\\bin\\win64\\SDL3.dll",
            nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        Expect(library != nullptr, "installed SDL3 loads in standalone test process");
        if (!library) return 1;
        Expect(MH_Initialize() == MH_OK, "native export smoke initializes MinHook");
        Expect(MenuInputPrepare(nullptr), "all installed SDL export stubs support hook creation and queueing");
        Expect(MH_ApplyQueued() == MH_OK, "all installed SDL export hooks activate");
        using Peep = int(*)(void*, int, int, std::uint32_t, std::uint32_t);
        using Keyboard = const bool*(*)(int*);
        const auto nativePeep = reinterpret_cast<Peep>(GetProcAddress(library, "SDL_PeepEvents"));
        const auto nativeKeyboard = reinterpret_cast<Keyboard>(GetProcAddress(library, "SDL_GetKeyboardState"));
        Event event{};
        Expect(nativePeep(&event, 1, 2, 0, 0xffff) <= 0, "inactive real PeepEvents trampoline forwards uninitialized queue result");
        int scancodes = 0;
        Expect(nativeKeyboard(&scancodes) != nullptr && scancodes == 512, "inactive real keyboard trampoline reports verified SDL scancode count");
        MenuInputStop();
        // Exercise the hook through the installed export. SDL_PumpEvents before
        // video init is a no-op; the test creates no game or SDL video window.
        reinterpret_cast<void(*)()>(GetProcAddress(library, "SDL_PumpEvents"))();
        std::printf("installed SDL smoke: %u checks, %u failures; callbacks remain resident until test exit\n", checks, failures);
        return failures ? 1 : 0;
    }
    WNDCLASSW wc{}; wc.lpfnWndProc = Native; wc.lpszClassName = L"MenuInputFixture"; wc.hInstance = GetModuleHandleW(nullptr);
    Expect(RegisterClassW(&wc) != 0, "fixture window class registered");
    game = CreateWindowW(wc.lpszClassName, L"input fixture", 0, 0, 0, 320, 240, nullptr, nullptr, wc.hInstance, nullptr);
    Expect(game != nullptr, "synthetic HWND created without a game process");
    MenuInputTestConfigure({Pump, Focus, Properties, Property, GetRelative, SetRelative, GetGrab, SetGrab,
        Cursor, Show, Hide, ResetKeyboard, Peep, Poll, Keyboard, Mouse, Warp, WarpGlobal, Foreground});
    MenuInputRequest(game, false); MenuInputTestPump();
    Expect(relative && grab && !cursor && !MenuInputCapturing(), "hidden menu leaves native cursor policy unchanged");
    MenuInputRequest(game, true);
    std::thread firstForeignPump([] { MenuInputTestPump(); }); firstForeignPump.join();
    Expect(focusCalls == 0 && relative && !MenuInputCapturing(), "first foreign pump does not invoke main-thread SDL video getters");
    MenuInputTestPump();
    Expect(!relative && !grab && cursor && MenuInputCapturing(), "opening releases relative mode and grab and shows cursor");
    Expect(resets >= 2 && nativeMessages >= 5, "takeover releases native held keyboard and mouse state");
    pumpPress = true; MenuInputTestPump(); pumpPress = false;
    Expect(!keys[4], "cached SDL keyboard-state pointer is cleared after native pump");
    int count = 0; keys[4] = true;
    Expect(!MenuInputTestKeyboard(&count)[4] && count == 512, "synchronous keyboard getter masks input while captured");
    float x = 0, y = 0;
    Expect(MenuInputTestMouse(&x, &y) == 0 && x == 12 && y == 34, "global mouse getter preserves cursor coordinates and masks buttons");
    MenuInputTestSetRelative(sdlWindow, true); MenuInputTestSetGrab(sdlWindow, true); MenuInputTestShowCursor(false);
    Expect(!relative && !grab && cursor, "native recapture and hide requests are deferred");
    MenuInputTestWarp(sdlWindow, 10, 20);
    Expect(warps == 0, "captured window cannot recenter mouse");
    MenuInputTestWarp(otherWindow, 10, 20);
    Expect(warps == 1, "other SDL windows retain native warp behavior");
    queue = {Make(0x300), Wheel(2, -3), Make(0x301), Make(0x202), Make(0x401), Make(0x402)};
    std::array<Event, 8> events{};
    Expect(MenuInputTestPeep(events.data(), 8, 1, 0, 0xffff) == 3 && events[0].type == 0x301 && events[1].type == 0x202 && events[2].type == 0x402,
        "peek filters presses and wheel while preserving key/button releases and lifecycle events");
    Expect(queue.size() == 6, "peek never removes queue entries");
    MenuInputDrainWheel(x, y); Expect(x == 0 && y == 0, "peek never delivers wheel input");
    Expect(MenuInputTestPeep(events.data(), 8, 2, 0, 0xffff) == 3 && queue.empty(), "GET compacts and refills until original queue empty");
    MenuInputDrainWheel(x, y); Expect(x == -2 && y == -3, "GET delivers wheel once with ImGui horizontal sign");
    MenuInputDrainWheel(x, y); Expect(x == 0 && y == 0, "wheel drain clears consumed delta");
    queue = {Wheel(0.001f, -0.002f)};
    Expect(!MenuInputTestPoll(events.data()), "fractional wheel is consumed by menu");
    MenuInputDrainWheel(x, y); Expect(x == -0.001f && y == -0.002f, "precision wheel deltas below one Windows wheel tick remain intact");
    queue = {Make(0x300), Make(0x400), Make(0x401), Make(0x301), Make(0x202), Make(0x402)};
    Expect(MenuInputTestPeep(events.data(), 2, 2, 0, 0xffff) == 2 && events[0].type == 0x301 && events[1].type == 0x202 && queue.size() == 1,
        "GET refills requested count after multiple filtered batches and preserves remainder");
    queue.clear();
    queue = {Make(0x300), Wheel(1, 2, 1), Make(0x100)};
    Expect(MenuInputTestPoll(nullptr) && queue.size() == 3, "PollEvent null query does not consume events");
    Expect(MenuInputTestPoll(events.data()) && events[0].type == 0x100 && queue.empty(), "poll skips captured input and forwards quit");
    MenuInputDrainWheel(x, y); Expect(x == 1 && y == -2, "flipped wheel direction normalized");
    queue = {Make(0x400), Make(0x401), Make(0x302), Make(0x303), Make(0x307)};
    Expect(!MenuInputTestPoll(events.data()) && queue.empty(), "motion clicks and text never reach native event consumers");
    Event add = Make(0x300); Expect(MenuInputTestPeep(&add, 1, 0, 0, 0xffff) == 1 && queue.size() == 1, "ADD event operation is preserved");
    queue.clear();
    MenuInputRequest(game, false); MenuInputTestPump();
    Expect(relative && grab && !cursor && !MenuInputCapturing(), "closing restores most recent native desired cursor modes");
    keys[4] = true;
    Expect(MenuInputTestKeyboard(&count) == keys.data() && MenuInputTestMouse(nullptr, nullptr) == 3, "hidden menu restores native state pointers and buttons");
    queue = {Make(0x300)};
    Expect(MenuInputTestPoll(events.data()) && events[0].type == 0x300, "hidden menu forwards key presses");
    MenuInputRequest(game, true); MenuInputTestPump();
    SendMessageW(game, WM_KILLFOCUS, 0, 0);
    Expect(!MenuInputCapturing() && relative && grab && !cursor, "focus loss message immediately restores cursor and shared capture gate");
    MenuInputTestPump();
    std::thread offOwner([] { MenuInputTestPump(); }); offOwner.join();
    Expect(MenuInputCapturing() && !relative && cursor, "other pump threads cannot mutate SDL video owner state");
    foreground = false; MenuInputTestPump();
    Expect(relative && grab && !cursor && !MenuInputCapturing(), "focus loss restores cursor policy");
    foreground = true; MenuInputTestPump();
    Expect(!relative && cursor && MenuInputCapturing(), "focus return reacquires visible menu");
    MenuInputTestSetRelative(sdlWindow, false); MenuInputTestSetGrab(sdlWindow, false); MenuInputTestShowCursor(true);
    MenuInputRequest(game, false); MenuInputTestPump();
    Expect(!relative && !grab && cursor, "latest native requests supersede original snapshot on restoration");
    relative = grab = true; cursor = false;
    failRelative = true; MenuInputRequest(game, true); MenuInputTestPump();
    Expect(relative && !MenuInputCapturing(), "SDL cursor API failure leaves capture inactive");
    keys[4] = true; queue = {Make(0x300)};
    Expect(MenuInputTestKeyboard(nullptr) == keys.data() && MenuInputTestPoll(events.data()), "failed cursor release never swallows game input");
    failRelative = false;
    wrongProperty = true; MenuInputRequest(game, true); MenuInputTestPump();
    Expect(relative && grab && !cursor && !MenuInputCapturing(), "SDL HWND mismatch prevents cursor ownership assumptions");
    wrongProperty = false; MenuInputTestPump();
    Expect(!relative && cursor, "matching HWND can acquire after mismatched focus");
    MenuInputStop();
    Expect(!MenuInputCapturing(), "cooperative stop disarms gameplay capture immediately");
    MenuInputTestPump();
    Expect(relative && grab && !cursor, "stop restoration runs on native SDL owner pump");
    MenuInputRequest(game, true); MenuInputTestPump();
    Expect(relative && !MenuInputCapturing(), "stopped capture cannot be rearmed");
    DestroyWindow(game);
    Expect(pumpCalls > 0, "every owner hook forwards native pump");
    std::printf("menu_input_checks: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
