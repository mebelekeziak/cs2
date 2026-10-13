#pragma once
#include <Windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace loader {
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
