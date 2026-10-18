#include "loader_support.h"
#include <shellapi.h>
#include <iostream>
#include <vector>

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void CheckStartupCompletion() {
    struct Event {
        HANDLE handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ~Event() { if (handle) CloseHandle(handle); }
    } completed;
    Require(completed.handle != nullptr, "Cannot create startup completion event");

    // Simulate the remote completion signal using a real kernel event. Fake
    // elapsed budgets exercise the old 15-second boundary without wall waits.
    std::vector<DWORD> budgets;
    unsigned notices = 0;
    auto delayedWait = [&](DWORD milliseconds) {
        budgets.push_back(milliseconds);
        if (budgets.size() == 2)
            Require(SetEvent(completed.handle) != FALSE, "Cannot signal delayed completion");
        return WaitForSingleObject(completed.handle, 0);
    };
    const auto delayed = loader::WaitForBootstrap(delayedWait, [&] { ++notices; });
    Require(delayed == WAIT_OBJECT_0, "Delayed startup was reported as timed out");
    Require(notices == 1 && budgets.size() == 2, "Delayed startup did not get one continuation");
    Require(budgets[0] == 15000 && budgets[1] == 45000
        && budgets[0] + budgets[1] == loader::kBootstrapWaitMilliseconds,
        "Startup wait exceeded or shortened its 60-second budget");

    budgets.clear();
    notices = 0;
    const auto immediate = loader::WaitForBootstrap(
        [&](DWORD milliseconds) { budgets.push_back(milliseconds); return WaitForSingleObject(completed.handle, 0); },
        [&] { ++notices; });
    Require(immediate == WAIT_OBJECT_0 && budgets.size() == 1 && notices == 0,
        "Immediate completion waited again or emitted a slow notice");

    Require(ResetEvent(completed.handle) != FALSE, "Cannot reset completion event");
    budgets.clear();
    const auto pending = loader::WaitForBootstrap(
        [&](DWORD milliseconds) { budgets.push_back(milliseconds); return WaitForSingleObject(completed.handle, 0); },
        [&] { ++notices; });
    Require(pending == WAIT_TIMEOUT && budgets.size() == 2 && notices == 1,
        "Unfinished startup did not stop waiting at its bounded deadline");
    // Timing out must leave the operation free to complete afterwards.
    Require(SetEvent(completed.handle) && WaitForSingleObject(completed.handle, 0) == WAIT_OBJECT_0,
        "Pending startup completion signal was destroyed or canceled");

    unsigned calls = 0;
    notices = 0;
    const auto failedWait = loader::WaitForBootstrap(
        [&](DWORD) { ++calls; return WAIT_FAILED; }, [&] { ++notices; });
    Require(failedWait == WAIT_FAILED && calls == 1 && notices == 0,
        "Failed wait was treated as a timeout or retried");
    calls = 0;
    const auto continuationFailure = loader::WaitForBootstrap(
        [&](DWORD) { return ++calls == 1 ? WAIT_TIMEOUT : WAIT_FAILED; }, [&] { ++notices; });
    Require(continuationFailure == WAIT_FAILED && calls == 2 && notices == 1,
        "Continuation wait failure did not propagate");

    Require(loader::BootstrapSucceeded(3, 3, ERROR_SUCCESS), "Completed native bootstrap was rejected");
    for (DWORD status : std::vector<DWORD>{0, 1, 2, 3})
        for (DWORD threadExit : std::vector<DWORD>{0, 1, 2, 3, STILL_ACTIVE, 0xC0000005u})
            for (DWORD startResult : std::vector<DWORD>{ERROR_SUCCESS, ERROR_FUNCTION_FAILED})
                Require(loader::BootstrapSucceeded(status, threadExit, startResult)
                    == (status == 3 && threadExit == 3 && startResult == ERROR_SUCCESS),
                    "Incomplete or failed native bootstrap was reported as success");

    using Completion = loader::Completion;
    Require(loader::MapperCompletion(true, 0) == Completion::Initialized, "Mapper success was lost");
    Require(loader::MapperCompletion(true, loader::kMapperPending) == Completion::Pending,
        "Mapper timeout was incorrectly reported as failure");
    for (DWORD code : {1u, 3u, 0xC0000005u})
        Require(loader::MapperCompletion(true, code) == Completion::Failed,
            "Mapper failure was reported as pending or success");
    Require(loader::MapperCompletion(true, loader::kMapperUnconfirmed) == Completion::Unconfirmed
        && loader::MapperCompletion(true, STILL_ACTIVE) == Completion::Unconfirmed
        && loader::MapperCompletion(false, 0) == Completion::Unconfirmed,
        "Unreadable or unfinished mapper was reported as success");
    const auto pendingMessage = loader::CompletionMessage(Completion::Pending);
    Require(pendingMessage.find(L"pending") != std::wstring::npos
        && pendingMessage.find(L"unconfirmed") != std::wstring::npos
        && pendingMessage.find(L"Restart CS2") != std::wstring::npos
        && pendingMessage.find(L"did not complete successfully") == std::wstring::npos,
        "Pending launcher text implies failure, success, or permission to remap");
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2) {
            try { loader::LoadReservation duplicate(argv[1]); }
            catch (const std::exception&) { return 0; }
            return 1;
        }
        CheckStartupCompletion();
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
        std::cout << "Bounded/delayed startup, authoritative result/status text, argument round-trip, concurrent reservation, rollback and cross-process persistence checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
