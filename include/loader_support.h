#pragma once
#include <Windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace loader {
inline constexpr DWORD kBootstrapInitialWaitMilliseconds = 15000;
inline constexpr DWORD kBootstrapWaitMilliseconds = 60000;
inline constexpr int kMapperPending = 2;
inline constexpr int kMapperUnconfirmed = 4;

// Keep a bounded wait while allowing expensive startup discovery to finish.
// The wait/notice callbacks also let tests exercise a delayed completion without
// sleeping or starting a remote thread. Only timeout permits a second wait.
template <typename Wait, typename SlowNotice>
DWORD WaitForBootstrap(Wait wait, SlowNotice slowNotice) {
    const auto initial = wait(kBootstrapInitialWaitMilliseconds);
    if (initial != WAIT_TIMEOUT) return initial;
    slowNotice();
    return wait(kBootstrapWaitMilliseconds - kBootstrapInitialWaitMilliseconds);
}

inline bool BootstrapSucceeded(DWORD status, DWORD threadExit, DWORD startResult) noexcept {
    return status == 3 && threadExit == 3 && startResult == ERROR_SUCCESS;
}

enum class Completion { Initialized, Pending, Failed, Unconfirmed };

inline Completion MapperCompletion(bool resultRead, DWORD exitCode) noexcept {
    if (!resultRead || exitCode == kMapperUnconfirmed || exitCode == STILL_ACTIVE)
        return Completion::Unconfirmed;
    if (exitCode == ERROR_SUCCESS) return Completion::Initialized;
    if (exitCode == kMapperPending) return Completion::Pending;
    return Completion::Failed;
}

inline std::wstring CompletionMessage(Completion completion) {
    switch (completion) {
    case Completion::Initialized:
        return L"Overlay initialized. Check CS2 for visible output.\r\n\r\nInsert: toggle panel. End: cooperative stop.\r\nRestart CS2 before loading a rebuilt DLL or loading again.";
    case Completion::Pending:
        return L"Overlay startup is still pending after 60 seconds.\r\nIt may finish in CS2 after this window stops waiting.\r\nCheck CS2 and the matching logs; completion is unconfirmed.\r\nRestart CS2 normally before another load attempt.";
    case Completion::Failed:
        return L"Loading did not complete successfully. Check the mapper log below.\r\nRestart CS2 normally before another attempt.";
    case Completion::Unconfirmed:
        return L"Could not confirm overlay startup completion.\r\nThe overlay may still be initializing; check CS2 and the matching logs.\r\nRestart CS2 normally before another load attempt.";
    }
    return {};
}

inline std::wstring Quote(const std::wstring& argument) {
    // Windows CRT quoting, including quotes and trailing backslashes.
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto c : argument) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}

class LoadReservation {
    std::filesystem::path path_;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    bool dispatched_ = false;
public:
    explicit LoadReservation(std::filesystem::path path) : path_(std::move(path)) {
        file_ = CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            if (error == ERROR_FILE_EXISTS)
                throw std::runtime_error("This loader has already attempted loading into this CS2 process. Check logs; restart CS2 normally before loading again. Stopped code remains resident until process exit.");
            throw std::runtime_error("Cannot reserve loader state (Win32 " + std::to_string(error) + ")");
        }
    }
    ~LoadReservation() {
        if (file_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_);
            if (!dispatched_) DeleteFileW(path_.c_str());
        }
    }
    LoadReservation(const LoadReservation&) = delete;
    LoadReservation& operator=(const LoadReservation&) = delete;
    void Save() {
        const std::string record = "Load attempt reserved. Consult matching mapper/overlay logs. Restart CS2 before another attempt.\r\n";
        DWORD written = 0;
        if (!WriteFile(file_, record.data(), static_cast<DWORD>(record.size()), &written, nullptr)
            || written != record.size() || !FlushFileBuffers(file_))
            throw std::runtime_error("Cannot save loader state (Win32 " + std::to_string(GetLastError()) + ")");
    }
    // Keep the record once a child ran, even on failure or early UI closure.
    void Dispatched() noexcept { dispatched_ = true; }
};
} // namespace loader
