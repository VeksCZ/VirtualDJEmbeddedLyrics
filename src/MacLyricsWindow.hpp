#pragma once
#ifdef __APPLE__
#include <filesystem>
#include <string>

// macOS counterpart of EmbeddedLyricsEditor (Windows): what the MP3 holds now, an online
// search (LrcHelper: TIDAL then LRCLIB; built-in LRCLIB without it) and the Timed/Untimed
// editor. Saving goes through LrcHelper --write-editor-text. Must run on the main thread.
struct MacLyricsRequest {
    std::filesystem::path audioPath;
    std::filesystem::path helper;  // empty when LrcHelper is not installed
    double durationSeconds{};      // 0 when unknown
    double positionSeconds{-1};    // playback position; prefills "First line at" (-1 = unknown)
};

// Returns true when the MP3 was changed (the caller should reload its lyrics).
bool ShowMacLyricsWindow(const MacLyricsRequest& request);
#endif
