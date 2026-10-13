#include <Windows.h>
#include <TlHelp32.h>
#include <shellapi.h>
#include "loader_support.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kTitle[] = L"CS2 Overlay Loader";
constexpr int kOpenLogs = 100;
constexpr int kClose = 101;
const std::filesystem::path kLogs = std::filesystem::path(CS2_WORKSPACE_ROOT) / L"logs";
HANDLE gMapper = nullptr;
HWND gStatus = nullptr;
std::filesystem::path gOverlayLog, gMapperLog;

struct Handle {
    HANDLE value;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

[[noreturn]] void Fail(const char* operation) {
    throw std::runtime_error(std::string(operation) + " (Win32 " + std::to_string(GetLastError()) + ")");
}

std::filesystem::path ExecutableDirectory() {
    std::vector<wchar_t> path(32768);
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) Fail("Cannot locate loader executable");
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
}

DWORD FindClient() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE) Fail("Cannot list processes");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    if (!Process32FirstW(snapshot.value, &entry)) Fail("Cannot read process list");
    do {
        if (_wcsicmp(entry.szExeFile, L"cs2.exe") != 0) continue;
        if (pid) throw std::runtime_error("Multiple CS2 clients are running. Leave only the intended client open, then reopen this loader.");
        pid = entry.th32ProcessID;
    } while (Process32NextW(snapshot.value, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) Fail("Cannot finish process list");
    if (!pid) throw std::runtime_error("CS2 is not running. Start it at the disconnected main menu or in offline Practice, then double-click the loader again.");
    return pid;
}

void LaunchMapper(HWND window) {
    const auto directory = ExecutableDirectory();
    const auto mapper = directory / L"mapper.exe";
    const auto dll = directory / L"cs2_overlay.dll";
    if (!std::filesystem::is_regular_file(mapper) || !std::filesystem::is_regular_file(dll))
        throw std::runtime_error("mapper.exe and cs2_overlay.dll must be beside cs2_loader.exe. Build the project's Release configuration first.");

    const auto pid = FindClient();
    Handle client(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
    if (!client.value) Fail("Cannot inspect CS2 process");
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(client.value, &created, &exited, &kernel, &user)) Fail("Cannot read CS2 start time");
    const auto creation = (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    const auto identity = std::to_wstring(pid) + L"-" + std::to_wstring(creation);
    Handle mutex(CreateMutexW(nullptr, FALSE, (L"Local\\CS2OverlayLoader-" + identity).c_str()));
    const auto mutexError = GetLastError();
    if (!mutex.value) Fail("Cannot create loader lock");
    if (mutexError == ERROR_ALREADY_EXISTS)
        throw std::runtime_error("Another loader is handling this CS2 process. Check its window and the project logs.");

    std::filesystem::create_directories(kLogs);
    const auto marker = kLogs / (L"loader-" + identity + L".state");
    gOverlayLog = kLogs / (L"overlay-" + identity + L".log");
    gMapperLog = kLogs / (L"mapper-" + identity + L".log");
    // Record before dispatch. A crash, failed bootstrap or closed window cannot
    // authorize a second mapping into code that might already be resident.
    loader::LoadReservation reservation(marker);
    reservation.Save();

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle output(CreateFileW(gMapperLog.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (output.value == INVALID_HANDLE_VALUE || input.value == INVALID_HANDLE_VALUE) Fail("Cannot open mapper diagnostics");

    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<unsigned char> attributes(attributeBytes);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = output.value;
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributeBytes)) Fail("Cannot prepare mapper startup");
    struct AttributeCleanup {
        LPPROC_THREAD_ATTRIBUTE_LIST list;
        ~AttributeCleanup() { DeleteProcThreadAttributeList(list); }
    } cleanup{startup.lpAttributeList};
    HANDLE inherited[] = {input.value, output.value};
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherited, sizeof(inherited), nullptr, nullptr)) Fail("Cannot configure mapper output");
    if (WaitForSingleObject(client.value, 0) != WAIT_TIMEOUT)
        throw std::runtime_error("CS2 exited before loading. Start a new client and reopen the loader.");

    auto command = loader::Quote(mapper.wstring()) + L" --pid " + std::to_wstring(pid)
        + L" --dll " + loader::Quote(dll.wstring()) + L" --log " + loader::Quote(gOverlayLog.wstring());
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(mapper.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, directory.c_str(), &startup.StartupInfo, &process)) {
        Fail("Cannot start mapper");
    }
    reservation.Dispatched();
    CloseHandle(process.hThread);
    gMapper = process.hProcess;
    const auto status = L"Loading overlay into CS2 (PID " + std::to_wstring(pid)
        + L")...\r\n\r\nDiagnostics: " + gMapperLog.wstring()
        + L"\r\n\r\nClosing this window does not stop the mapper or the overlay.";
    SetWindowTextW(gStatus, status.c_str());
    if (!SetTimer(window, 1, 100, nullptr)) Fail("Cannot monitor mapper completion");
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND) {
        if (LOWORD(wParam) == kClose) DestroyWindow(window);
        else if (LOWORD(wParam) == kOpenLogs) {
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", kLogs.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                MessageBoxW(window, L"Could not open the logs folder.", kTitle, MB_OK | MB_ICONERROR);
        }
        return 0;
    }
    if (message == WM_TIMER && gMapper) {
        if (WaitForSingleObject(gMapper, 0) == WAIT_TIMEOUT) return 0;
        KillTimer(window, 1);
        DWORD result = 0;
        const bool readResult = GetExitCodeProcess(gMapper, &result) != FALSE;
        CloseHandle(gMapper);
        gMapper = nullptr;
        const std::wstring status = readResult && result == 0
            ? L"Overlay initialized. Check CS2 for visible output.\r\n\r\nInsert: toggle panel. End: cooperative stop.\r\nRestart CS2 before loading a rebuilt DLL or loading again.\r\n\r\nOverlay log: " + gOverlayLog.wstring()
            : L"Loading did not complete successfully. Check the mapper log below.\r\nRestart CS2 normally before another attempt.\r\n\r\nMapper log: " + gMapperLog.wstring();
        SetWindowTextW(gStatus, status.c_str());
        return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = L"CS2ProjectOneClickLoader";
        if (!RegisterClassW(&windowClass)) Fail("Cannot register loader window");
        const auto window = CreateWindowExW(0, windowClass.lpszClassName, kTitle,
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, 650, 330, nullptr, nullptr, instance, nullptr);
        if (!window) Fail("Cannot create loader window");
        gStatus = CreateWindowExW(0, L"STATIC", L"Preparing loader...", WS_CHILD | WS_VISIBLE,
            20, 20, 590, 220, window, nullptr, instance, nullptr);
        const auto open = CreateWindowExW(0, L"BUTTON", L"Open logs", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            20, 250, 110, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenLogs)), instance, nullptr);
        const auto close = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            510, 250, 110, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kClose)), instance, nullptr);
        if (!gStatus || !open || !close) Fail("Cannot create loader controls");
        for (const auto control : {gStatus, open, close})
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        ShowWindow(window, show);
        UpdateWindow(window);
        LaunchMapper(window);
        MSG message{};
        BOOL received = 0;
        while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (gMapper) CloseHandle(gMapper); // Child continues; never terminate it.
        return received == -1 ? 1 : 0;
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "CS2 Overlay Loader", MB_OK | MB_ICONERROR);
        if (gMapper) CloseHandle(gMapper);
        return 1;
    }
}
