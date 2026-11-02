#pragma once

#include <string_view>

namespace Diagnostics {
void Info(std::wstring_view message);
void Error(std::wstring_view message);

// Crash forensics (Windows). While at least one plugin instance is alive, a passive
// vectored exception handler appends one line to the log for serious faults (access
// violation, heap corruption, fast-fail ...), naming the module the fault happened in
// (for example the NVIDIA driver). It never handles the exception, so VirtualDJ's own
// behaviour is unchanged. Every Acquire must be paired with a Release (plugin ctor/dtor);
// the handler is removed with the last instance, before the DLL can be unloaded.
void AcquireCrashLogger();
void ReleaseCrashLogger();
}
