#pragma once

#ifdef _WIN32
#include "Diagnostics.hpp"

#include <windows.h>

#include <cwchar>
#include <exception>
#include <string>

// Every callback VirtualDJ makes into the plugin runs inside this guard. A C++ exception
// or a hardware fault (access violation, illegal instruction ...) inside our code must
// never unwind into the host: with a full dance floor that would end the whole set.
namespace CrashGuard {

enum class Outcome { Completed, Exception, Fault };

using Thunk = HRESULT (*)(void*);

inline std::wstring HexCode(DWORD code) {
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"%08lX", static_cast<unsigned long>(code));
    return buffer;
}

// Deliberately free of objects with destructors so __try/__except is legal here.
inline Outcome RunProtected(Thunk thunk, void* context, HRESULT* result, DWORD* code) {
    __try {
        *result = thunk(context);
        return Outcome::Completed;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return Outcome::Fault;
    }
}

template <class Function>
Outcome Run(const wchar_t* where, Function&& function, HRESULT* result) noexcept {
    struct Context { Function* function; } context{&function};
    DWORD code = 0;
    *result = E_FAIL;
    try {
        const auto outcome = RunProtected(
            [](void* raw) -> HRESULT { return (*static_cast<Context*>(raw)->function)(); },
            &context, result, &code);
        if (outcome == Outcome::Fault) {
            *result = E_FAIL;
            Diagnostics::Error(std::wstring{L"Hardware fault 0x"} + HexCode(code) + L" in " + where);
        }
        return outcome;
    } catch (const std::exception& error) {
        *result = E_FAIL;
        Diagnostics::Error(std::wstring{L"Exception in "} + where + L": " +
                           std::wstring(error.what(), error.what() + std::char_traits<char>::length(error.what())));
        return Outcome::Exception;
    } catch (...) {
        *result = E_FAIL;
        Diagnostics::Error(std::wstring{L"Unknown exception in "} + where);
        return Outcome::Exception;
    }
}

}  // namespace CrashGuard
#endif
