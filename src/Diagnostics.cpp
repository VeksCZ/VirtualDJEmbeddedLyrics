#include "Diagnostics.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

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

std::string ToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

void Write(std::wstring_view level, std::wstring_view message) {
    std::scoped_lock lock{logMutex};
    // A wofstream converts through the classic "C" locale's codecvt facet, which can only
    // represent ASCII; any non-ASCII character (e.g. a Czech diacritic in a file path or lyric
    // line) puts the stream into a fail state and silently stops that write mid-line, which is
    // what made log lines with accented text run together with no newline. Writing raw UTF-8
    // bytes through a narrow, binary stream sidesteps locale conversion entirely.
    std::ofstream stream{LogPath(), std::ios::app | std::ios::binary};
    if (!stream) return;
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &seconds);
    std::wostringstream line;
    line << std::put_time(&local, L"%Y-%m-%d %H:%M:%S") << L" [" << level << L"] "
         << message << L'\n';
    const auto utf8 = ToUtf8(line.str());
    stream.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
}
}

void Diagnostics::Info(std::wstring_view message) { Write(L"INFO", message); }
void Diagnostics::Error(std::wstring_view message) { Write(L"ERROR", message); }
#else
void Diagnostics::Info(std::wstring_view) {}
void Diagnostics::Error(std::wstring_view) {}
#endif
