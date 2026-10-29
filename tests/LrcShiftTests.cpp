#include "LrcShift.hpp"

#include <cstdio>
#include <string>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}
}  // namespace

int main() {
    std::wstring text = L"[00:10.50] First\r\n[00:12.00] Second\r\n[01:05.25] Third";
    std::int64_t delta = 0;
    Check(ShiftLrcTimestamps(text, 23400, &delta), "shift succeeds");
    Check(delta == 12900, "delta is target - first");
    Check(text == L"[00:23.40] First\r\n[00:24.90] Second\r\n[01:18.15] Third", "all timestamps moved");

    std::wstring earlier = L"[00:05.00] A\n[00:06.00] B";
    Check(ShiftLrcTimestamps(earlier, 1000), "shift earlier");
    Check(earlier == L"[00:01.00] A\n[00:02.00] B", "earlier result");

    std::wstring clamp = L"[00:01.00] A\n[00:03.00] B";
    ShiftLrcTimestamps(clamp, 0);
    Check(clamp == L"[00:00.00] A\n[00:02.00] B", "never below zero");

    std::wstring threeDigits = L"[00:10.500] A";
    ShiftLrcTimestamps(threeDigits, 11000);
    Check(threeDigits == L"[00:11.00] A", "three-digit fraction");

    std::wstring none = L"no timestamps";
    Check(!ShiftLrcTimestamps(none, 1000) && none == L"no timestamps", "untimed text unchanged");

    Check(ParseLyricsTime(L"1:23.45") == 83450, "parse m:ss.xx");
    Check(ParseLyricsTime(L"83.4") == 83400, "parse seconds");
    Check(ParseLyricsTime(L"0:05") == 5000, "parse m:ss");
    Check(ParseLyricsTime(L"abc") == -1, "parse invalid");
    Check(FormatLyricsTime(83450) == L"1:23.45", "format");

    if (!failures) std::puts("lrcshift_tests: all passed");
    return failures ? 1 : 0;
}
