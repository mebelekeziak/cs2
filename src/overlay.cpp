#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include <cstring>
#include <algorithm>
#include <cfloat>
#include "esp_draw.h"
#include "player_reader.h"
#include "grenade_visuals.h"
#include "grenade_effects.h"
#include "movement.h"
#include "silent_aim.h"
#include "aim_visualization.h"
#include "aim_console.h"
#include "console_control.h"
#include "shot_log_settings.h"
#include "shot_logging.h"
#include "anti_aim.h"
#include "third_person.h"
#include "third_person_settings.h"
#include "scoped_crosshair.h"
#include "scope_overlay.h"
#include "scoped_crosshair_draw.h"
#include "zoom.h"
#include "model_swap.h"
#include "model_swap_settings.h"
#include "skin_changer.h"
#include "skin_settings.h"
#include "silent_aim_settings.h"
#include "world_settings.h"
#include "world_postprocess.h"
#include "world_sky.h"
#include "world_materials.h"
#include "player_chams.h"
#include "world_lighting_switches.h"
#include "world_lighting.h"
#include "world_environment_lighting.h"
#if !CS2_DIAGNOSTIC_UI
#include "public_menu.h"
#include "menu_input.h"
#endif

#ifndef CS2_WORKSPACE_ROOT
#define CS2_WORKSPACE_ROOT L"C:\\Users\\dawid\\Documents\\ChatGPT\\cs2"
#endif

namespace {
using Microsoft::WRL::ComPtr;
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
PresentFn originalPresent = nullptr;
ResizeFn originalResize = nullptr;
void* presentCandidate = nullptr;
void* resizeCandidate = nullptr;
std::atomic<bool> stopRequested{false};
std::atomic<bool> stopped{false};
std::atomic<long> startState{0};
std::recursive_mutex renderMutex;
SRWLOCK logLock = SRWLOCK_INIT;
HANDLE logFile = INVALID_HANDLE_VALUE;
IDXGISwapChain* selectedChain = nullptr; // Identity only: do not retain a game-owned chain.
HWND selectedWindow = nullptr;
DWORD renderThread = 0;
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
ComPtr<ID3D11RenderTargetView> renderTarget;
ImGuiContext* imguiContext = nullptr;
bool backendInitialized = false;
bool deviceObjectsDirty = false;
bool visible = true;
bool insertDown = false;
bool endDown = false;
#if !CS2_DIAGNOSTIC_UI
bool menuMouseActive = false;
bool menuMouseArmed = false;
bool menuKeyboardActive = false;
bool menuKeyDown[256]{};
bool menuKeyArmed[256]{};
#endif
bool threadChangeLogged = false;
bool firstPresentReported = false;
IDXGISwapChain* lastRejectedChain = nullptr;
unsigned rendererGeneration = 0;
std::uint64_t frameCount = 0;
LARGE_INTEGER previousCounter{};
LARGE_INTEGER counterFrequency{};
UINT targetWidth = 0, targetHeight = 0;
// Dynamic Windows TLS avoids any dependency on loader-provided static TLS slots.
DWORD callbackTls = TLS_OUT_OF_INDEXES;
bool InCallback() { return TlsGetValue(callbackTls) != nullptr; }
void SetInCallback(bool active) { TlsSetValue(callbackTls, active ? reinterpret_cast<void*>(1) : nullptr); }

void RequestStop() {
    stopRequested.store(true, std::memory_order_release);
#if !CS2_DIAGNOSTIC_UI
    MenuInputStop();
#endif
}

void Log(const char* format, ...) {
    if (logFile == INVALID_HANDLE_VALUE) return;
    char buffer[2048];
    SYSTEMTIME time{};
    GetSystemTime(&time);
    int prefix = snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ [tid=%lu] ",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
        time.wMilliseconds, GetCurrentThreadId());
    if (prefix < 0) return;
    va_list args;
    va_start(args, format);
    vsnprintf(buffer + prefix, sizeof(buffer) - static_cast<size_t>(prefix) - 2, format, args);
    va_end(args);
    size_t length = strnlen_s(buffer, sizeof(buffer) - 2);
    buffer[length++] = '\r'; buffer[length++] = '\n';
    AcquireSRWLockExclusive(&logLock);
    DWORD written = 0;
    WriteFile(logFile, buffer, static_cast<DWORD>(length), &written, nullptr);
    // Appended records remain readable immediately through the OS file cache.
    // Forcing storage durability per line can stall input/render callbacks.
    ReleaseSRWLockExclusive(&logLock);
}

void FlushLog() {
    AcquireSRWLockExclusive(&logLock);
    if (logFile != INVALID_HANDLE_VALUE) FlushFileBuffers(logFile);
    ReleaseSRWLockExclusive(&logLock);
}

void EspLog(const char* message) noexcept {
    Log("%s", message);
    // Engine callbacks enqueue diagnostics; their render-owner drain reaches
    // this sink without performing console or file I/O in those callbacks.
    if (const auto console = ShotLogConsoleMessage(message, ShotLogGetSettings())) AimConsoleWrite(console);
}

bool OpenLog(const wchar_t* suppliedPath) {
    // The remote caller owns the allocation until OverlayStart's thread exits.
    if (!suppliedPath || wcsnlen_s(suppliedPath, 32768) >= 32768) return false;
    wchar_t full[32768], root[32768];
    DWORD length = GetFullPathNameW(suppliedPath, 32768, full, nullptr);
    DWORD rootLength = GetFullPathNameW(CS2_WORKSPACE_ROOT, 32768, root, nullptr);
    if (!length || length >= 32768 || !rootLength || rootLength >= 32767) return false;
    while (rootLength > 0 && root[rootLength - 1] == L'\\') --rootLength;
    root[rootLength++] = L'\\'; root[rootLength] = L'\0';
    if (_wcsnicmp(full, root, rootLength) != 0) return false;
    // Fail on missing directory. The loader creates the workspace diagnostics directory.
    logFile = CreateFileW(full, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return logFile != INVALID_HANDLE_VALUE;
}

bool VerifyMethod(void* method, const char* name) {
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(method, &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT)
        return false;
    const DWORD protection = memory.Protect & 0xff;
    if ((memory.Protect & PAGE_GUARD) || (protection != PAGE_EXECUTE && protection != PAGE_EXECUTE_READ &&
        protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)) return false;
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(method), &owner)) return false;
    wchar_t path[32768];
    if (!GetModuleFileNameW(owner, path, 32768)) return false;
    const wchar_t* basename = wcsrchr(path, L'\\');
    basename = basename ? basename + 1 : path;
    const auto rva = reinterpret_cast<std::uintptr_t>(method) - reinterpret_cast<std::uintptr_t>(owner);
    Log("Probe %s candidate=%p owner=%ls base=%p RVA=0x%llx protection=0x%lx", name, method,
        path, owner, static_cast<unsigned long long>(rva), memory.Protect);
    return _wcsicmp(basename, L"dxgi.dll") == 0 || _wcsicmp(basename, L"d3d11.dll") == 0;
}

LRESULT CALLBACK ProbeWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

bool ProbeMethods(void*& present, void*& resize) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    wchar_t className[96];
    swprintf_s(className, L"CS2OverlayProbe_%lu_%llu", GetCurrentProcessId(), GetTickCount64());
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = ProbeWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = className;
    if (!RegisterClassExW(&windowClass)) { Log("Probe RegisterClassEx failed=%lu", GetLastError()); return false; }
    HWND window = CreateWindowExW(0, className, L"D3D11 method discovery", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, nullptr, nullptr, instance, nullptr);
    bool success = false;
    if (window) {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = 64; desc.BufferDesc.Height = 64;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1; desc.OutputWindow = window;
        desc.Windowed = TRUE; desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        ComPtr<IDXGISwapChain> chain;
        ComPtr<ID3D11Device> probeDevice;
        ComPtr<ID3D11DeviceContext> probeContext;
        D3D_FEATURE_LEVEL feature{};
        HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &desc, &chain, &probeDevice, &feature, &probeContext);
        Log("In-DLL hidden D3D11 probe hr=0x%08lx feature=0x%x", result, feature);
        if (SUCCEEDED(result)) {
            void** methods = *reinterpret_cast<void***>(chain.Get());
            // IDXGISwapChain COM layout from the Windows SDK, not game offsets.
            present = methods[8]; resize = methods[13];
            success = VerifyMethod(present, "IDXGISwapChain::Present[8]") &&
                VerifyMethod(resize, "IDXGISwapChain::ResizeBuffers[13]");
            probeContext->ClearState(); probeContext->Flush();
        }
    } else Log("Probe CreateWindowEx failed=%lu", GetLastError());
    DestroyWindow(window);
    UnregisterClassW(className, instance);
    return success;
}

struct ScopedImGuiContext {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ScopedImGuiContext() { ImGui::SetCurrentContext(imguiContext); }
    ~ScopedImGuiContext() { ImGui::SetCurrentContext(previous); }
};

void ReleaseRendererOnOwner(const char* reason) {
    PlayerReaderInvalidate(reason);
    WorldPostprocessRelease();
#if !CS2_DIAGNOSTIC_UI
    MenuInputRequest(selectedWindow, false);
    menuMouseActive = false;
    menuMouseArmed = false;
    menuKeyboardActive = false;
    std::fill_n(menuKeyDown, 256, false);
    std::fill_n(menuKeyArmed, 256, false);
#endif
    if (imguiContext) {
        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imguiContext);
        if (backendInitialized) ImGui_ImplDX11_Shutdown();
        backendInitialized = false;
        ImGui::DestroyContext(imguiContext);
        ScopedCrosshairReset();
        if (previous != imguiContext) ImGui::SetCurrentContext(previous);
        imguiContext = nullptr;
    }
    renderTarget.Reset(); context.Reset(); device.Reset();
    deviceObjectsDirty = false;
    Log("Renderer resources released generation=%u reason=%s frames=%llu", rendererGeneration, reason,
        static_cast<unsigned long long>(frameCount));
}

void CleanupOnRenderThread(const char* reason) {
#if !CS2_DIAGNOSTIC_UI
    MenuInputStop();
#endif
    ScopeOverlayStop();
    ScopedCrosshairStop();
    ScopedCrosshairReset();
    MovementStop(reason);
    SilentAimStop(reason);
    ShotLoggingStop(reason);
    AntiAimStop(reason);
    ThirdPersonStop(reason);
    ZoomStop(reason);
    ModelSwapStop(reason);
    SkinChangerStop(reason);
    GrenadeEffectsStop();
    GrenadeVisualsStop();
    WorldSkyStop(reason);
    WorldMaterialsStop(reason);
    PlayerChamsStop(reason);
    WorldLightingSwitchesStop(reason);
    WorldLightingStop(reason);
    WorldEnvironmentLightingStop(reason);
    ShotLoggingFlushTelemetry();
    ConsoleControlStop();
    AimConsoleStop();
    ReleaseRendererOnOwner(reason);
    stopped.store(true, std::memory_order_release);
    Log("STOPPED_RESIDENT reason=%s frames=%llu. Hooks/trampolines/DLL retained; physical unload unavailable.",
        reason, static_cast<unsigned long long>(frameCount));
    FlushLog();
}

bool CreateRenderTarget(IDXGISwapChain* chain) {
    ComPtr<ID3D11Texture2D> buffer;
    HRESULT result = chain->GetBuffer(0, IID_PPV_ARGS(&buffer));
    if (FAILED(result)) { Log("GetBuffer failed hr=0x%08lx", result); return false; }
    D3D11_TEXTURE2D_DESC desc{};
    buffer->GetDesc(&desc);
    targetWidth = desc.Width; targetHeight = desc.Height;
    result = device->CreateRenderTargetView(buffer.Get(), nullptr, &renderTarget);
    Log("CreateRenderTargetView hr=0x%08lx dimensions=%ux%u format=%u", result, targetWidth, targetHeight, desc.Format);
    return SUCCEEDED(result);
}

bool InitializeOnRenderThread(IDXGISwapChain* chain) {
    ++rendererGeneration;
    frameCount = 0;
    firstPresentReported = false;
    HRESULT result = chain->GetDevice(IID_PPV_ARGS(&device));
    if (FAILED(result)) {
        Log("Real chain GetDevice(ID3D11Device) failed hr=0x%08lx; stop requested", result);
        RequestStop(); return false;
    }
    device->GetImmediateContext(&context);
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC adapterDesc{};
    if (SUCCEEDED(device.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) adapter->GetDesc(&adapterDesc);
    Log("REAL_D3D11 chain=%p hwnd=%p owner_tid=%lu device=%p context=%p feature=0x%x adapter=%ls vendor=0x%x deviceid=0x%x",
        chain, selectedWindow, renderThread, device.Get(), context.Get(), device->GetFeatureLevel(),
        adapterDesc.Description, adapterDesc.VendorId, adapterDesc.DeviceId);
    ImGuiContext* previous = ImGui::GetCurrentContext();
    IMGUI_CHECKVERSION();
    imguiContext = ImGui::CreateContext();
    ScopedCrosshairReset();
    ImGui::SetCurrentContext(imguiContext);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
#if CS2_DIAGNOSTIC_UI
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard;
    ImGui::StyleColorsDark();
#else
    ConfigurePublicMenuStyle();
#endif
    EspConfigureFonts();
    backendInitialized = ImGui_ImplDX11_Init(device.Get(), context.Get());
    bool objects = backendInitialized && ImGui_ImplDX11_CreateDeviceObjects();
    ImGui::SetCurrentContext(previous);
    QueryPerformanceFrequency(&counterFrequency);
    QueryPerformanceCounter(&previousCounter);
    if (!objects || !CreateRenderTarget(chain)) {
        Log("Renderer initialization incomplete; cooperative stop requested");
        RequestStop(); return false;
    }
    return true;
}

bool VerifyRealChain(IDXGISwapChain* chain, DXGI_SWAP_CHAIN_DESC& desc,
    ComPtr<ID3D11Device>& actualDevice) {
    DWORD pid = 0;
    if (FAILED(chain->GetDesc(&desc)) || !desc.OutputWindow || !IsWindowVisible(desc.OutputWindow)) return false;
    GetWindowThreadProcessId(desc.OutputWindow, &pid);
    if (pid != GetCurrentProcessId() || FAILED(chain->GetDevice(IID_PPV_ARGS(&actualDevice)))) return false;
    void** methods = *reinterpret_cast<void***>(chain);
    bool match = methods[8] == presentCandidate && methods[13] == resizeCandidate;
    if (!match && lastRejectedChain != chain) {
        Log("Real chain rejected=%p Present=%p Resize=%p; probe=%p/%p. Both callback paths must match before retaining back buffer.",
            chain, methods[8], methods[13], presentCandidate, resizeCandidate);
        lastRejectedChain = chain;
    }
    return match;
}

// ImGui v1.91.9b backs up most draw state, but does not preserve HS/DS/CS,
// OM targets/UAVs, or release GS class-instance references. Save those here;
// clear GS before invoking its backend so the backend never acquires its instances.
// Save all SRVs too: binding the back buffer can implicitly unbind aliasing SRVs.
struct SupplementalState {
    ID3D11DeviceContext* ctx;
    ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11UnorderedAccessView* omUavs[D3D11_1_UAV_SLOT_COUNT]{};
    ID3D11UnorderedAccessView* csUavs[D3D11_1_UAV_SLOT_COUNT]{};
    ID3D11ShaderResourceView* srvs[6][D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    ID3D11GeometryShader* gs = nullptr;
    ID3D11HullShader* hs = nullptr;
    ID3D11DomainShader* ds = nullptr;
    ID3D11ComputeShader* cs = nullptr;
    ID3D11ClassInstance* instances[4][256]{};
    UINT counts[4]{256, 256, 256, 256};
    UINT uavSlots;
    SupplementalState(ID3D11DeviceContext* c, D3D_FEATURE_LEVEL feature) : ctx(c),
        uavSlots(feature >= D3D_FEATURE_LEVEL_11_1 ? D3D11_1_UAV_SLOT_COUNT : D3D11_PS_CS_UAV_REGISTER_COUNT) {
        ctx->OMGetRenderTargets(8, rtvs, &dsv);
        ctx->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, uavSlots, omUavs);
        ctx->CSGetUnorderedAccessViews(0, uavSlots, csUavs);
        ctx->VSGetShaderResources(0, 128, srvs[0]); ctx->PSGetShaderResources(0, 128, srvs[1]);
        ctx->GSGetShaderResources(0, 128, srvs[2]); ctx->HSGetShaderResources(0, 128, srvs[3]);
        ctx->DSGetShaderResources(0, 128, srvs[4]); ctx->CSGetShaderResources(0, 128, srvs[5]);
        ctx->GSGetShader(&gs, instances[0], &counts[0]);
        ctx->HSGetShader(&hs, instances[1], &counts[1]);
        ctx->DSGetShader(&ds, instances[2], &counts[2]);
        ctx->CSGetShader(&cs, instances[3], &counts[3]);
        ctx->GSSetShader(nullptr, nullptr, 0);
        UINT preserve[D3D11_1_UAV_SLOT_COUNT];
        std::fill_n(preserve, D3D11_1_UAV_SLOT_COUNT, UINT(-1));
        ID3D11UnorderedAccessView* empty[D3D11_1_UAV_SLOT_COUNT]{};
        ctx->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, uavSlots, empty, preserve);
        ctx->CSSetUnorderedAccessViews(0, uavSlots, empty, preserve);
    }
    ~SupplementalState() {
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        UINT preserve[D3D11_1_UAV_SLOT_COUNT];
        std::fill_n(preserve, D3D11_1_UAV_SLOT_COUNT, UINT(-1));
        UINT rtvCount = 0;
        for (UINT i = 0; i < 8; ++i) if (rtvs[i]) rtvCount = i + 1;
        ctx->OMSetRenderTargetsAndUnorderedAccessViews(rtvCount, rtvs, dsv, rtvCount,
            uavSlots - rtvCount, omUavs + rtvCount, preserve + rtvCount);
        ctx->CSSetUnorderedAccessViews(0, uavSlots, csUavs, preserve);
        ctx->VSSetShaderResources(0, 128, srvs[0]); ctx->PSSetShaderResources(0, 128, srvs[1]);
        ctx->GSSetShaderResources(0, 128, srvs[2]); ctx->HSSetShaderResources(0, 128, srvs[3]);
        ctx->DSSetShaderResources(0, 128, srvs[4]); ctx->CSSetShaderResources(0, 128, srvs[5]);
        ctx->GSSetShader(gs, instances[0], counts[0]); ctx->HSSetShader(hs, instances[1], counts[1]);
        ctx->DSSetShader(ds, instances[2], counts[2]); ctx->CSSetShader(cs, instances[3], counts[3]);
        if (gs) gs->Release(); if (hs) hs->Release(); if (ds) ds->Release(); if (cs) cs->Release();
        for (auto& stage : instances) for (auto* instance : stage) if (instance) instance->Release();
        for (auto& stage : srvs) for (auto* srv : stage) if (srv) srv->Release();
        for (auto* rtv : rtvs) if (rtv) rtv->Release();
        for (auto* uav : omUavs) if (uav) uav->Release();
        for (auto* uav : csUavs) if (uav) uav->Release();
        if (dsv) dsv->Release();
    }
};

#if !CS2_DIAGNOSTIC_UI
void PollMenuMouse(bool foreground) {
    ImGuiIO& io = ImGui::GetIO();
    POINT position{};
    RECT client{};
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool active = foreground && visible && GetCursorPos(&position) &&
        ScreenToClient(selectedWindow, &position) && GetClientRect(selectedWindow, &client) &&
        client.right > 0 && client.bottom > 0;
    // Reopening or refocusing cannot turn an already-held game click into a menu click.
    if (!active || !menuMouseActive) menuMouseArmed = false;
    if (active && !down) menuMouseArmed = true;
    io.AddFocusEvent(active);
    if (active) {
        io.AddMousePosEvent(static_cast<float>(position.x) * io.DisplaySize.x / static_cast<float>(client.right),
            static_cast<float>(position.y) * io.DisplaySize.y / static_cast<float>(client.bottom));
    } else {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    io.AddMouseButtonEvent(0, active && menuMouseArmed && down);
    float horizontal = 0, vertical = 0;
    MenuInputDrainWheel(horizontal, vertical);
    if (active) io.AddMouseWheelEvent(horizontal, vertical);
    // SDL shows the native cursor on its event owner; avoid a second software cursor.
    io.MouseDrawCursor = false;
    menuMouseActive = active;
}

void PollMenuKeyboard(bool foreground) {
    ImGuiIO& io = ImGui::GetIO();
    const bool active = foreground && visible;
    bool pressed[256]{};
    bool accepted[256]{};
    for (int key = 0; key < 256; ++key) {
        const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        // A key held during reopening/refocusing must be released before use.
        if (!active || !menuKeyboardActive) menuKeyArmed[key] = false;
        if (active && !down) menuKeyArmed[key] = true;
        accepted[key] = active && menuKeyArmed[key] && down;
        pressed[key] = accepted[key] && !menuKeyDown[key];
        menuKeyDown[key] = down;
    }
    const bool shift = accepted[VK_SHIFT];
    const bool ctrl = accepted[VK_CONTROL];
    const bool alt = accepted[VK_MENU];
    const bool super = accepted[VK_LWIN] || accepted[VK_RWIN];
    io.AddKeyEvent(ImGuiMod_Shift, shift);
    io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
    io.AddKeyEvent(ImGuiMod_Alt, alt);
    io.AddKeyEvent(ImGuiMod_Super, super);
    for (int key = 'A'; key <= 'Z'; ++key)
        io.AddKeyEvent(static_cast<ImGuiKey>(ImGuiKey_A + key - 'A'), accepted[key]);
    const struct { int vk; ImGuiKey imgui; } editingKeys[] = {
        {VK_BACK, ImGuiKey_Backspace}, {VK_DELETE, ImGuiKey_Delete},
        {VK_LEFT, ImGuiKey_LeftArrow}, {VK_RIGHT, ImGuiKey_RightArrow},
        {VK_HOME, ImGuiKey_Home}, {VK_RETURN, ImGuiKey_Enter},
        {VK_ESCAPE, ImGuiKey_Escape}
    };
    for (const auto& key : editingKeys) io.AddKeyEvent(key.imgui, accepted[key.vk]);
    // Config names deliberately use a small ASCII alphabet. WantTextInput is
    // from the previous frame, so gameplay keys never queue text without a field.
    // Check physical modifiers as well, including those suppressed on refocus.
    if (active && io.WantTextInput && !(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
        !(GetAsyncKeyState(VK_MENU) & 0x8000) &&
        !(GetAsyncKeyState(VK_LWIN) & 0x8000) && !(GetAsyncKeyState(VK_RWIN) & 0x8000)) {
        const bool uppercase = shift != ((GetKeyState(VK_CAPITAL) & 1) != 0);
        for (int key = 'A'; key <= 'Z'; ++key)
            if (pressed[key]) io.AddInputCharacter(static_cast<unsigned>(uppercase ? key : key + 'a' - 'A'));
        for (int key = '0'; key <= '9'; ++key)
            if (pressed[key]) io.AddInputCharacter(static_cast<unsigned>(key));
        if (pressed[VK_SPACE]) io.AddInputCharacter(' ');
        if (pressed[VK_OEM_MINUS]) io.AddInputCharacter(shift ? '_' : '-');
    }
    menuKeyboardActive = active;
}
#endif

void DrawFrame(IDXGISwapChain* chain) {
    ConsoleControlFlushRequests();
    SilentAimFlushTelemetry();
    ShotLoggingFlushTelemetry();
    AntiAimFlushTelemetry();
    ThirdPersonFlushTelemetry();
    ZoomFlushTelemetry();
    ModelSwapFlushTelemetry();
    SkinChangerFlushTelemetry();
    MovementFlushTelemetry();
    WorldSkyFlushTelemetry();
    WorldMaterialsFlushTelemetry();
    PlayerChamsFlushTelemetry();
    WorldLightingSwitchesFlushTelemetry();
    WorldLightingFlushTelemetry();
    WorldEnvironmentLightingFlushTelemetry();
    ScopedImGuiContext scopedContext;
    if (deviceObjectsDirty) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        if (!ImGui_ImplDX11_CreateDeviceObjects()) {
            Log("Device object recreation failed; stop requested"); RequestStop(); return;
        }
        deviceObjectsDirty = false;
    }
    if (!renderTarget && !CreateRenderTarget(chain)) { RequestStop(); return; }
    const bool foreground = GetForegroundWindow() == selectedWindow;
    EspPollControls(foreground);
    MovementPollControls(foreground);
    SilentAimPollControls(foreground);
    ThirdPersonPollControls(foreground);
    const bool insertNow = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    const bool endNow = (GetAsyncKeyState(VK_END) & 0x8000) != 0;
    if (foreground && insertNow && !insertDown) { visible = !visible; Log("Overlay visibility=%s", visible ? "shown" : "hidden"); }
    if (foreground && endNow && !endDown) { RequestStop(); Log("End requested cooperative shutdown"); }
    insertDown = insertNow; endDown = endNow;
#if !CS2_DIAGNOSTIC_UI
    MenuInputRequest(selectedWindow, visible && foreground && !stopRequested.load());
#endif
    if (stopRequested.load()) return;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(targetWidth), static_cast<float>(targetHeight));
    io.DeltaTime = std::clamp(static_cast<float>(now.QuadPart - previousCounter.QuadPart) /
        static_cast<float>(counterFrequency.QuadPart), 0.001f, 0.25f);
    previousCounter = now;
#if !CS2_DIAGNOSTIC_UI
    PollMenuMouse(foreground);
    PollMenuKeyboard(foreground);
#endif
    ImGui_ImplDX11_NewFrame(); ImGui::NewFrame();
#if !CS2_DIAGNOSTIC_UI
    if (visible) DrawPublicMenu();
#endif
    const auto players = PlayerReaderCapture();
    ImDrawList* espList = ImGui::GetBackgroundDrawList();
    const int beforeEsp = espList->VtxBuffer.Size;
    const auto espStats = EspDraw(players, io.DisplaySize.x, io.DisplaySize.y);
    const int espVertices = espList->VtxBuffer.Size - beforeEsp;
    GrenadeVisualsDraw(players.worldToClip, players.matrixValid, io.DisplaySize.x, io.DisplaySize.y);
    const auto aimSettings = SilentAimGetSettings();
    if (foreground && aimSettings.enabled)
        AimVisualizationDraw(AimVisualizationCapture(GetTickCount64(), &aimSettings), players,
            io.DisplaySize.x, io.DisplaySize.y, AimVisualizationGetSettings());
    const auto crosshair = ScopedCrosshairCaptureForDraw();
    ScopedCrosshairDraw({foreground && crosshair.visible, crosshair.spread, crosshair.inaccuracy, crosshair.sourceKey},
        io.DisplaySize.x, io.DisplaySize.y, io.DeltaTime);
#if CS2_DIAGNOSTIC_UI
    if (visible) {
        ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.90f);
        ImGui::Begin("CS2 player ESP", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
        ImGui::TextUnformatted("D3D11 / Dear ImGui");
        ImGui::Text("Back buffer: %u x %u", targetWidth, targetHeight);
        ImGui::Text("Render thread: %lu", renderThread);
        ImGui::Text("Frame: %llu", static_cast<unsigned long long>(frameCount + 1));
        ImGui::Text("Reader: %s", PlayerReaderStatus());
        ImGui::Text("Movement: %s", MovementStatus());
        ImGui::Text("Silent aim: %s", SilentAimStatus());
        ImGui::Text("Anti aim: %s", AntiAimStatus());
        ImGui::Text("Camera: %s", ThirdPersonStatus());
        ImGui::Text("Scoped zoom: %s", ZoomStatus());
        ImGui::Text("Scope overlay: %s", ScopeOverlayStatus());
        ImGui::Text("Player model: %s", ModelSwapRuntimeStatus().data());
        ImGui::Text("Skins: %s", SkinRuntimeStatus().data());
        ImGui::Text("F10 silent aim: %s", SilentAimGetSettings().enabled ? "ON" : "OFF");
        ImGui::Text("Remove spread: %s", aimSettings.removeSpread ? "ON" : "OFF");
        ImGui::Text("Hitchance sampler: %s%s", aimSettings.nativeHitchance ? "Native RNG experiment" : "Baseline",
            aimSettings.removeSpread ? " (bypassed by Remove spread)" : "");
        ImGui::Text("Grenade removal: %s", GrenadeEffectsStatus());
        ImGui::Text("Grenade borders: %s", GrenadeVisualsStatus());
        ImGui::Text("Sky: %s", WorldSkyStatus());
        ImGui::Text("World surfaces: %s", WorldMaterialsStatus());
        ImGui::Text("Player chams: %s", PlayerChamsStatus());
        ImGui::Text("Local light switches: %s", WorldLightingSwitchesStatus());
        ImGui::Text("Local light color: %s", WorldLightingStatus());
        ImGui::Text("Sun/lightmaps: %s", WorldEnvironmentLightingStatus());
        ImGui::Text("Screen color: %s", WorldPostprocessStatus());
        ImGui::Text("Players: %zu | drawing: %zu | matrix: %s", players.playerCount,
            espStats.drawn, players.matrixValid ? "valid" : "unavailable");
        const auto& settings = EspGetSettings();
        ImGui::Text("F1 ESP: %s | range: unlimited", settings.enabled ? "ON" : "OFF");
        ImGui::Text("F2 names: %s | F3 boxes: %s", settings.names ? "ON" : "OFF", settings.boxes ? "ON" : "OFF");
        ImGui::Text("F4 distance: %s | F5 health: %s", settings.distance ? "ON" : "OFF", settings.health ? "ON" : "OFF");
        ImGui::Text("F6 third person: %s | F7 armor: %s", ThirdPersonGetSettings().enabled ? "ON" : "OFF", settings.armor ? "ON" : "OFF");
        const auto movement = MovementGetSettings();
        ImGui::Text("F8 bunnyhop: %s | F9 autostrafe: %s | hold Space", movement.bunnyhop ? "ON" : "OFF", movement.autostrafe ? "ON" : "OFF");
        ImGui::Text("Jumpbug: %s | hold Space", movement.jumpbug ? "ON" : "OFF");
        if (!players.distanceAvailable) ImGui::TextUnformatted("Distance unavailable: awaiting local live pawn.");
        ImGui::Separator();
        ImGui::TextUnformatted("Insert: panel   End: stop");
        ImGui::TextUnformatted("Stopped code remains resident until game exit.");
        ImGui::End();
    }
#endif
    ImGui::Render();
    PlayerReaderReportDraw(espStats, espVertices, ImGui::GetDrawData()->TotalVtxCount, io.DisplaySize.x, io.DisplaySize.y);
    // The game image (including its HUD) is graded before our ESP/menu draw.
    const auto world = WorldGetSettings();
    if (WorldPostprocessIsActive(world)) {
        ComPtr<ID3D11Texture2D> backbuffer;
        if (SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))))
            WorldPostprocessRender(device.Get(), context.Get(), backbuffer.Get(), renderTarget.Get(), world);
    } else WorldPostprocessRender(device.Get(), context.Get(), nullptr, renderTarget.Get(), world);
    if (ImGui::GetDrawData()->TotalVtxCount > 0) {
        SupplementalState state(context.Get(), device->GetFeatureLevel());
        ID3D11RenderTargetView* target = renderTarget.Get();
        context->OMSetRenderTargets(1, &target, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (!frameCount) Log("FIRST_OVERLAY_DRAW submitted vertices=%d indices=%d (display confirmation requires observation)",
            ImGui::GetDrawData()->TotalVtxCount, ImGui::GetDrawData()->TotalIdxCount);
        ++frameCount;
    }
}

HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* chain, UINT syncInterval, UINT flags) {
    if (InCallback() || stopped.load(std::memory_order_acquire) || (flags & DXGI_PRESENT_TEST))
        return originalPresent(chain, syncInterval, flags);
    SetInCallback(true);
    {
        std::lock_guard lock(renderMutex);
        if (!selectedChain && !stopRequested.load()) {
            DXGI_SWAP_CHAIN_DESC desc{};
            ComPtr<ID3D11Device> actualDevice;
            if (VerifyRealChain(chain, desc, actualDevice)) {
                void** methods = *reinterpret_cast<void***>(chain);
                Log("REAL_CHAIN_METHODS chain=%p Present[8]=%p matches_probe=yes Resize[13]=%p matches_probe=yes hwnd=%p",
                    chain, methods[8], methods[13], desc.OutputWindow);
                selectedChain = chain; selectedWindow = desc.OutputWindow; renderThread = GetCurrentThreadId();
                InitializeOnRenderThread(chain);
            }
        } else if (selectedChain != chain && !stopRequested.load() && !stopped.load()) {
            DXGI_SWAP_CHAIN_DESC desc{};
            ComPtr<ID3D11Device> actualDevice;
            if (VerifyRealChain(chain, desc, actualDevice) && desc.OutputWindow == selectedWindow) {
                if (GetCurrentThreadId() == renderThread) {
                    Log("Swapchain identity changed old=%p new=%p; same window and owner thread, rebuilding renderer", selectedChain, chain);
                    ReleaseRendererOnOwner("swapchain replacement");
                    selectedChain = chain;
                    InitializeOnRenderThread(chain);
                } else {
                    RequestStop();
                    Log("Replacement swapchain arrived on tid=%lu owner=%lu; stop pending original thread", GetCurrentThreadId(), renderThread);
                }
            }
        }
        if (selectedChain == chain && !stopped.load()) {
            if (renderThread != GetCurrentThreadId()) {
                RequestStop();
                if (!threadChangeLogged) {
                    Log("Render ownership changed old=%lu current=%lu; rendering halted. Cleanup awaits original render thread; DLL resident.",
                        renderThread, GetCurrentThreadId());
                    threadChangeLogged = true;
                }
            } else {
                DXGI_SWAP_CHAIN_DESC currentDesc{};
                ComPtr<ID3D11Device> currentDevice;
                if (FAILED(chain->GetDesc(&currentDesc)) || currentDesc.OutputWindow != selectedWindow ||
                    !IsWindow(selectedWindow) || FAILED(chain->GetDevice(IID_PPV_ARGS(&currentDevice)))) {
                    Log("Selected swapchain/window lifecycle no longer valid; stop requested");
                    RequestStop();
                } else if (device && currentDevice.Get() != device.Get()) {
                    Log("Device identity changed for chain=%p old=%p new=%p; rebuilding on owner thread", chain, device.Get(), currentDevice.Get());
                    ReleaseRendererOnOwner("device replacement/address reuse");
                    InitializeOnRenderThread(chain);
                }
                if (stopRequested.load()) CleanupOnRenderThread("requested or invalid lifecycle");
                else {
                    DrawFrame(chain);
                    if (stopRequested.load()) CleanupOnRenderThread("requested during render");
                }
            }
        } else if (!selectedChain && stopRequested.load() && !stopped.load()) {
            ConsoleControlStop();
            MovementStop("stopped before selecting render chain");
            SilentAimStop("stopped before selecting render chain");
            ShotLoggingStop("stopped before selecting render chain");
            AntiAimStop("stopped before selecting render chain");
            ThirdPersonStop("stopped before selecting render chain");
            ScopeOverlayStop();
            ScopedCrosshairStop();
            ZoomStop("stopped before selecting render chain");
            ModelSwapStop("stopped before selecting render chain");
            SkinChangerStop("stopped before selecting render chain");
            GrenadeEffectsStop();
            GrenadeVisualsStop();
            WorldSkyStop("stopped before selecting render chain");
            WorldMaterialsStop("stopped before selecting render chain");
            PlayerChamsStop("stopped before selecting render chain");
            WorldLightingSwitchesStop("stopped before selecting render chain");
            WorldLightingStop("stopped before selecting render chain");
            WorldEnvironmentLightingStop("stopped before selecting render chain");
            stopped.store(true);
            Log("STOPPED_RESIDENT before selecting a real chain; hooks/DLL retained.");
            FlushLog();
        }
    }
    HRESULT result = originalPresent(chain, syncInterval, flags);
    {
        std::lock_guard lock(renderMutex);
        if (chain == selectedChain && frameCount && !firstPresentReported) {
            Log("FIRST_PRESENT_AFTER_DRAW hr=0x%08lx generation=%u sync=%u flags=0x%x; visible output requires observation",
                result, rendererGeneration, syncInterval, flags);
            firstPresentReported = true;
        }
    }
    if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) {
        std::lock_guard lock(renderMutex);
        if (chain == selectedChain && !stopped.load()) {
            Log("Present device loss hr=0x%08lx removal_reason=0x%08lx; stop requested", result,
                device ? device->GetDeviceRemovedReason() : result);
            RequestStop();
            if (GetCurrentThreadId() == renderThread) CleanupOnRenderThread("device loss");
        }
    }
    SetInCallback(false);
    return result;
}

HRESULT STDMETHODCALLTYPE HookResize(IDXGISwapChain* chain, UINT count, UINT width, UINT height,
    DXGI_FORMAT format, UINT flags) {
    if (InCallback() || stopped.load()) return originalResize(chain, count, width, height, format, flags);
    SetInCallback(true);
    HRESULT result;
    {
        std::lock_guard lock(renderMutex);
        if (chain == selectedChain) {
            // COM resource Release is thread-safe. ImGui/context work is deferred to the owning Present thread.
            renderTarget.Reset();
            WorldPostprocessRelease();
            deviceObjectsDirty = true;
            Log("ResizeBuffers begin requested=%ux%u buffers=%u format=%u render_tid=%lu", width, height, count, format, renderThread);
        }
        result = originalResize(chain, count, width, height, format, flags);
        if (chain == selectedChain) {
            Log("ResizeBuffers result=0x%08lx; target recreation deferred to next Present", result);
            if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) RequestStop();
        }
    }
    SetInCallback(false);
    return result;
}
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI OverlayStart(const wchar_t* absoluteLogPath) {
    long expected = 0;
    if (!startState.compare_exchange_strong(expected, 1)) return 1;
    if (!OpenLog(absoluteLogPath)) { startState.store(-1); return 2; }
    Log("OverlayStart pid=%lu x64=%s ImGui=%s MinHook=1.3.4; no initialization in DllMain",
        GetCurrentProcessId(), sizeof(void*) == 8 ? "yes" : "no", IMGUI_VERSION);
    Log("UI_MODE %s", CS2_DIAGNOSTIC_UI ? "diagnostic" : "public");
    const bool aimConsole = AimConsoleInitialize();
    EspLog(aimConsole ? "AIM_CONSOLE_READY native developer console and workspace session log" :
        "AIM_CONSOLE_UNAVAILABLE native export/build verification failed; workspace session log remains available");
    EspSetLogCallback(&EspLog);
    PlayerReaderSetLogCallback(&EspLog);
    MovementSetLogCallback(&EspLog);
    WorldPostprocessSetLogCallback(&EspLog);
    callbackTls = TlsAlloc();
    if (callbackTls == TLS_OUT_OF_INDEXES) { Log("Dynamic TLS allocation failed=%lu", GetLastError()); startState.store(-1); return 7; }
    void* present = nullptr;
    void* resize = nullptr;
    if (!ProbeMethods(present, resize)) { Log("Method discovery/owner verification failed; no hooks installed"); startState.store(-1); return 3; }
    presentCandidate = present; resizeCandidate = resize;
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) { Log("MH_Initialize failed=%s", MH_StatusToString(status)); startState.store(-1); return 4; }
    status = MH_CreateHook(present, reinterpret_cast<LPVOID>(&HookPresent), reinterpret_cast<LPVOID*>(&originalPresent));
    if (status == MH_OK) status = MH_CreateHook(resize, reinterpret_cast<LPVOID>(&HookResize), reinterpret_cast<LPVOID*>(&originalResize));
    if (status != MH_OK) { Log("MH_CreateHook failed=%s; no enabled hooks, resident allocations retained", MH_StatusToString(status)); startState.store(-1); return 5; }
    const bool movementPrepared = MovementPrepare(&EspLog);
#if !CS2_DIAGNOSTIC_UI
    const bool menuInputPrepared = MenuInputPrepare(&EspLog);
    EspLog(menuInputPrepared ? "MENU_INPUT_READY cursor/input capture queued on SDL event owner" :
        "MENU_INPUT_UNAVAILABLE native cursor/input hooks could not be prepared");
#endif
    const bool consoleControlsPrepared = aimConsole && ConsoleControlPrepare();
    const bool aimPrepared = SilentAimPrepare(&EspLog);
    const bool shotLoggingPrepared = ShotLoggingPrepare(&EspLog);
    const bool antiAimPrepared = AntiAimPrepare(&EspLog);
    const bool thirdPersonPrepared = ThirdPersonPrepare(&EspLog);
    const bool zoomPrepared = ZoomPrepare(&EspLog);
    const bool crosshairPrepared = ScopedCrosshairPrepare();
    EspLog(crosshairPrepared ? "SCOPED_CROSSHAIR_READY scoped third-person or replaced first-person views; copied native accuracy cone" :
        "SCOPED_CROSSHAIR_UNAVAILABLE accuracy/scope contract verification failed");
    const bool scopeOverlayPrepared = crosshairPrepared && ScopeOverlayPrepare(&EspLog);
    const bool modelSwapPrepared = ModelSwapPrepare(&EspLog);
    const bool skinsPrepared = SkinChangerPrepare(&EspLog);
    const bool grenadeEffectsPrepared = GrenadeEffectsPrepare(&EspLog);
    const bool grenadeVisualsPrepared = GrenadeVisualsPrepare(&EspLog);
    const bool skyPrepared = WorldSkyPrepare(&EspLog);
    const bool materialsPrepared = WorldMaterialsPrepare(&EspLog);
    const bool playerChamsPrepared = PlayerChamsPrepare(&EspLog);
    const bool lightingSwitchesPrepared = WorldLightingSwitchesPrepare(&EspLog);
    const bool lightingPrepared = WorldLightingPrepare(&EspLog);
    const bool environmentLightingPrepared = WorldEnvironmentLightingPrepare(&EspLog);
    status = MH_QueueEnableHook(present);
    if (status == MH_OK) status = MH_QueueEnableHook(resize);
    if (status == MH_OK) status = MH_ApplyQueued();
    if (status != MH_OK) {
#if !CS2_DIAGNOSTIC_UI
        MenuInputStop();
#endif
        ConsoleControlStop();
        MovementStop("partial hook activation failure");
        SilentAimStop("partial hook activation failure");
        ShotLoggingStop("partial hook activation failure");
        AntiAimStop("partial hook activation failure");
        ThirdPersonStop("partial hook activation failure");
        ScopeOverlayStop();
        ScopedCrosshairStop();
        ZoomStop("partial hook activation failure");
        ModelSwapStop("partial hook activation failure");
        SkinChangerStop("partial hook activation failure");
        GrenadeEffectsStop();
        GrenadeVisualsStop();
        WorldSkyStop("partial hook activation failure");
        WorldMaterialsStop("partial hook activation failure");
        PlayerChamsStop("partial hook activation failure");
        WorldLightingSwitchesStop("partial hook activation failure");
        WorldLightingStop("partial hook activation failure");
        WorldEnvironmentLightingStop("partial hook activation failure");
        RequestStop();
        Log("Hook activation failed=%s; stop requested; retain all code/trampolines (partial activation possible)", MH_StatusToString(status));
        startState.store(-1); return 6;
    }
    if (movementPrepared) MovementActivate();
    if (consoleControlsPrepared) ConsoleControlActivate();
    EspLog(consoleControlsPrepared ? "AIM_CONSOLE_CONTROLS_READY clear/replay and project-only native console filter available" :
        "AIM_CONSOLE_CONTROLS_UNAVAILABLE engine console contract verification or hook preparation failed");
    if (aimPrepared) SilentAimActivate(movementPrepared);
    if (shotLoggingPrepared && movementPrepared) ShotLoggingActivate();
    if (antiAimPrepared) AntiAimActivate(movementPrepared);
    if (thirdPersonPrepared) ThirdPersonActivate();
    if (zoomPrepared) ZoomActivate();
    if (scopeOverlayPrepared) ScopeOverlayActivate();
    if (modelSwapPrepared) ModelSwapActivate();
    if (skinsPrepared) SkinChangerActivate();
    if (grenadeEffectsPrepared) GrenadeEffectsActivate();
    if (grenadeVisualsPrepared) GrenadeVisualsActivate();
    if (skyPrepared) WorldSkyActivate();
    if (materialsPrepared) WorldMaterialsActivate();
    if (playerChamsPrepared) PlayerChamsActivate();
    if (lightingSwitchesPrepared) WorldLightingSwitchesActivate();
    if (lightingPrepared) WorldLightingActivate();
    if (environmentLightingPrepared) WorldEnvironmentLightingActivate();
    startState.store(2);
    Log("HOOKS_INSTALLED; waiting for an observed real D3D11 Present. Discovery alone does not confirm game rendering.");
    return 0;
}

extern "C" __declspec(dllexport) DWORD WINAPI OverlayRequestStop(void*) {
    RequestStop();
    ConsoleControlStop();
    MovementStop("explicit overlay stop request");
    SilentAimStop("explicit overlay stop request");
    ShotLoggingStop("explicit overlay stop request");
    AntiAimStop("explicit overlay stop request");
    ThirdPersonStop("explicit overlay stop request");
    ScopeOverlayStop();
    ScopedCrosshairStop();
    ZoomStop("explicit overlay stop request");
    ModelSwapStop("explicit overlay stop request");
    SkinChangerStop("explicit overlay stop request");
    GrenadeEffectsStop();
    GrenadeVisualsStop();
    WorldSkyStop("explicit overlay stop request");
    WorldMaterialsStop("explicit overlay stop request");
    PlayerChamsStop("explicit overlay stop request");
    WorldLightingSwitchesStop("explicit overlay stop request");
    WorldLightingStop("explicit overlay stop request");
    WorldEnvironmentLightingStop("explicit overlay stop request");
    Log("STOP_REQUESTED; cleanup needs next Present on original render thread. No thread forced/physical unload.");
    return stopped.load(std::memory_order_acquire) ? 0 : 1;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    // No hook installation, renderer initialization, thread work, or teardown under loader lock.
    return TRUE;
}
