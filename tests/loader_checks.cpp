#include "loader_support.h"
#include <shellapi.h>
#include <iostream>
#include <vector>

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2) {
            try { loader::LoadReservation duplicate(argv[1]); }
            catch (const std::exception&) { return 0; }
            return 1;
        }
        // Round-trip through Windows' actual parser, not a duplicate algorithm.
        const std::vector<std::wstring> expected{
            L"loader.exe", L"", L"C:\\Users\\Test User\\overlay.dll", L"C:\\folder\\",
            L"a\\\"quoted\" value", L"C:\\Users\\Dawid \u0141\\diagnostics.log", L"plain"
        };
        std::wstring command;
        for (const auto& value : expected) {
            if (!command.empty()) command += L' ';
            command += loader::Quote(value);
        }
        int count = 0;
        auto parsed = CommandLineToArgvW(command.c_str(), &count);
        Require(parsed != nullptr, "Cannot parse test command line");
        bool matches = static_cast<std::size_t>(count) == expected.size();
        for (int i = 0; matches && i < count; ++i) matches = parsed[i] == expected[static_cast<std::size_t>(i)];
        LocalFree(parsed);
        Require(matches, "Quoted command line changed an argument");

        const auto directory = std::filesystem::path(CS2_WORKSPACE_ROOT) / L"logs" / L"loader-checks";
        std::filesystem::create_directories(directory);
        const auto state = directory / (L"check-" + std::to_wstring(GetCurrentProcessId()) + L".state");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() { DeleteFileW(path.c_str()); }
        } cleanup{state};
        {
            loader::LoadReservation attempt(state);
            attempt.Save();
            bool rejected = false;
            try { loader::LoadReservation duplicate(state); }
            catch (const std::exception&) { rejected = true; }
            Require(rejected, "Concurrent reservation was accepted");
        }
        Require(!std::filesystem::exists(state), "Pre-dispatch failure did not allow retry");
        {
            loader::LoadReservation retry(state);
            retry.Save();
            retry.Dispatched();
        }
        Require(std::filesystem::exists(state), "Dispatched reservation was lost when the launcher closed");
        // Separate process must honor the record after the first launcher exits.
        std::vector<wchar_t> executable(32768);
        Require(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())) != 0, "Cannot locate check executable");
        auto childCommand = loader::Quote(executable.data()) + L" " + loader::Quote(state.wstring());
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        Require(CreateProcessW(executable.data(), childCommand.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child) != FALSE, "Cannot start independent duplicate check");
        CloseHandle(child.hThread);
        const auto wait = WaitForSingleObject(child.hProcess, 10000);
        DWORD result = 1;
        const bool completed = wait == WAIT_OBJECT_0 && GetExitCodeProcess(child.hProcess, &result);
        CloseHandle(child.hProcess);
        Require(completed && result == 0, "Reopened loader did not reject a prior dispatch");
        std::cout << "Argument round-trip, concurrent reservation, rollback and cross-process persistence checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
