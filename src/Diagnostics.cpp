#include "Diagnostics.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <system_error>
#include <string>
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

// A failure that repeats every frame (60x per second) must not hammer the disk from the
// render thread: identical messages are collapsed and the log is size-limited.
constexpr std::uintmax_t kMaxLogBytes = 1024 * 1024;
constexpr auto kRepeatWindow = std::chrono::seconds(10);
std::wstring lastMessage;
std::chrono::steady_clock::time_point lastWritten{};
unsigned suppressed = 0;

// A wofstream converts through the classic "C" locale's codecvt facet, which can only
// represent ASCII; any non-ASCII character (e.g. a Czech diacritic in a file path or lyric
// line) puts the stream into a fail state and silently stops that write mid-line. Writing raw
// UTF-8 bytes through a narrow, binary stream sidesteps locale conversion entirely.
void Append(std::ofstream& stream, std::wstring_view level, std::wstring_view message) {
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

void Write(std::wstring_view level, std::wstring_view message) {
    try {
        std::scoped_lock lock{logMutex};
        const auto nowSteady = std::chrono::steady_clock::now();
        const std::wstring key = std::wstring{level} + L'|' + std::wstring{message};
        if (key == lastMessage && nowSteady - lastWritten < kRepeatWindow) { ++suppressed; return; }
        const auto path = LogPath();
        std::error_code error;
        if (std::filesystem::file_size(path, error) > kMaxLogBytes && !error) {
            auto old = path;
            old += L".old";
            std::filesystem::remove(old, error);
            std::filesystem::rename(path, old, error);
        }
        std::ofstream stream{path, std::ios::app | std::ios::binary};
        if (!stream) return;
        if (suppressed) {
            Append(stream, L"INFO", L"Previous message repeated " + std::to_wstring(suppressed) +
                                    L" more times");
            suppressed = 0;
        }
        Append(stream, level, message);
        lastMessage = key;
        lastWritten = nowSteady;
    } catch (...) {
        // Logging must never take VirtualDJ down.
    }
}

// ---- crash forensics -------------------------------------------------------------------
// The handler runs inside a possibly corrupted process: no heap allocation, no locks, no
// C++ runtime; only a preformatted path, a fixed buffer and raw Win32 calls.
wchar_t crashLogPath[MAX_PATH * 2]{};
std::atomic<int> crashLoggerUsers{0};
std::atomic<int> crashLinesWritten{0};
PVOID crashHandler = nullptr;
std::mutex crashInstallMutex;

bool IsSeriousFault(DWORD code) noexcept {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case 0xC0000374:  // STATUS_HEAP_CORRUPTION
        case 0xC0000409:  // STATUS_STACK_BUFFER_OVERRUN (fast fail)
            return true;
        default:
            return false;
    }
}

LONG CALLBACK CrashLogHandler(EXCEPTION_POINTERS* info) noexcept {
    if (!info || !info->ExceptionRecord || !IsSeriousFault(info->ExceptionRecord->ExceptionCode))
        return EXCEPTION_CONTINUE_SEARCH;
    // Benign first-chance faults exist; never flood the log.
    if (crashLinesWritten.fetch_add(1, std::memory_order_relaxed) >= 25)
        return EXCEPTION_CONTINUE_SEARCH;
    const auto* record = info->ExceptionRecord;
    wchar_t module[MAX_PATH]{};
    HMODULE owner = nullptr;
    DWORD_PTR offset = 0;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(record->ExceptionAddress), &owner) && owner) {
        GetModuleFileNameW(owner, module, MAX_PATH);
        offset = reinterpret_cast<DWORD_PTR>(record->ExceptionAddress) -
                 reinterpret_cast<DWORD_PTR>(owner);
    }
    const wchar_t* name = module;
    for (const wchar_t* p = module; *p; ++p)
        if (*p == L'\\' || *p == L'/') name = p + 1;
    ULONG_PTR accessType = 9, accessAddress = 0;
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        accessType = record->ExceptionInformation[0];
        accessAddress = record->ExceptionInformation[1];
    }
    wchar_t line[512]{};
    const int length = swprintf_s(line,
        L"[FAULT] code=0x%08lX module=%ls+0x%llX thread=%lu access=%llu target=0x%llX\n",
        static_cast<unsigned long>(record->ExceptionCode), *name ? name : L"<unknown>",
        static_cast<unsigned long long>(offset), GetCurrentThreadId(),
        static_cast<unsigned long long>(accessType),
        static_cast<unsigned long long>(accessAddress));
    if (length <= 0 || !*crashLogPath) return EXCEPTION_CONTINUE_SEARCH;
    char utf8[1024]{};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, length, utf8, sizeof(utf8), nullptr, nullptr);
    HANDLE file = CreateFileW(crashLogPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        if (bytes > 0) WriteFile(file, utf8, static_cast<DWORD>(bytes), &written, nullptr);
        FlushFileBuffers(file);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;  // purely observational
}
}

void Diagnostics::AcquireCrashLogger() {
    try {
        std::scoped_lock lock{crashInstallMutex};
        if (crashLoggerUsers.fetch_add(1) != 0) return;
        const auto path = LogPath().wstring();
        if (path.size() < std::size(crashLogPath)) {
            std::copy(path.begin(), path.end(), crashLogPath);
            crashLogPath[path.size()] = L'\0';
        }
        crashLinesWritten = 0;
        crashHandler = AddVectoredExceptionHandler(0, CrashLogHandler);  // 0 = last in the chain
    } catch (...) {}
}

void Diagnostics::ReleaseCrashLogger() {
    try {
        std::scoped_lock lock{crashInstallMutex};
        if (crashLoggerUsers.fetch_sub(1) != 1) return;
        if (crashHandler) RemoveVectoredExceptionHandler(crashHandler);
        crashHandler = nullptr;
    } catch (...) {}
}

void Diagnostics::Info(std::wstring_view message) { Write(L"INFO", message); }
void Diagnostics::Error(std::wstring_view message) { Write(L"ERROR", message); }
#else
void Diagnostics::Info(std::wstring_view) {}
void Diagnostics::Error(std::wstring_view) {}
void Diagnostics::AcquireCrashLogger() {}
void Diagnostics::ReleaseCrashLogger() {}
#endif
