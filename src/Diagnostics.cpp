#include "Diagnostics.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>

namespace {
std::mutex logMutex;

std::filesystem::path LogPath() {
    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData))) {
        std::filesystem::path path{localAppData};
        CoTaskMemFree(localAppData);
        path /= L"VirtualDJ";
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (!error) return path / L"EmbeddedLyrics.log";
    }
    wchar_t temporary[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, temporary);
    return length > 0 && length < MAX_PATH
        ? std::filesystem::path{temporary} / L"EmbeddedLyrics.log"
        : std::filesystem::path{L"EmbeddedLyrics.log"};
}

void Write(std::wstring_view level, std::wstring_view message) {
    std::scoped_lock lock{logMutex};
    std::wofstream stream{LogPath(), std::ios::app};
    if (!stream) return;
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &seconds);
    stream << std::put_time(&local, L"%Y-%m-%d %H:%M:%S") << L" [" << level << L"] "
           << message << L'\n';
}
}

void Diagnostics::Info(std::wstring_view message) { Write(L"INFO", message); }
void Diagnostics::Error(std::wstring_view message) { Write(L"ERROR", message); }
#else
void Diagnostics::Info(std::wstring_view) {}
void Diagnostics::Error(std::wstring_view) {}
#endif
