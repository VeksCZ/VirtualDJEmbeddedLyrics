#pragma once

#include <string_view>

namespace Diagnostics {
void Info(std::wstring_view message);
void Error(std::wstring_view message);
}
