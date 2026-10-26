# LRC Lyrics for VirtualDJ 0.12.1

## Changes

- **Much faster lyrics rendering (Windows):** highlighting and scrolling no longer redraw every line with GDI on each frame (40-190 ms per 1080p frame, which made VirtualDJ's video stutter, worst with a strong outline/shadow and with LRC Master and LRC Deck together). Each line is now rendered once on a background thread and frames are composed from these cached lines in a reused texture: about 5-9 ms in the worst frame, with the same look. A newly loaded track shows its lyrics a frame or two later instead of blocking the video.

## 0.12.0

- **macOS now has everything the Windows plugins have:**
  - **LRC Deck** for macOS (lyrics inside one deck's video and its preview), installed by LyricsTools into `VideoEffect` next to `VideoOverlay`.
  - Full **Advanced** settings in LRC Master / LRC Deck: font, outline/shadow and strength, text/highlight/read colors, solid background, timed and untimed line counts, font size, vertical position, upfaders, auto-tag and **Record timing**.
  - Settings-panel options **Whole line**, **Countdown gap**, **Timing offset** and **Show name**, plus the "Artist - Title" heading, countdowns in pauses and the progressive highlight.
  - Settings are shared between LRC Master and LRC Deck and apply immediately, as on Windows.
- The macOS Advanced window has no live preview yet; changes show in the video output after **Apply**.
- The macOS build is verified by CI (Apple Silicon and Intel) but hasn't been tested in VirtualDJ on a real Mac yet. Please report anything that looks different from Windows.

## 0.11.0

- **macOS:** LRC Master now has **Edit lyrics** and **Edit browsed** with the same **LRC lyrics** window as Windows: what the MP3 already contains, **Search TIDAL / LRCLIB** with the source/match/timed report, the Timed/Untimed editor and **Save to MP3 tag**. LyricsTools installs `LrcHelper` next to the macOS plugin bundles for saving and TIDAL search; without it the window still searches LRCLIB on its own.
- The built-in LRCLIB fallback is now shared C++ for Windows and macOS (WinHTTP / NSURLSession), with its matching tests running on both platforms.

## 0.10.0

- New **LRC lyrics** window (Edit lyrics / Edit browsed): shows what the MP3 already contains, searches lyrics online with **Search TIDAL / LRCLIB** and reports where they were found, what they matched and whether they are timed. Found lyrics go into the editor for review; nothing is saved until **Save to MP3 tag**. Manual pasting/typing stays in the same window.
- New `LrcHelper.exe`, installed next to the plugins: saving tags and online search no longer need a system Python, Mutagen or a particular LyricsTools location. Without it the plugin still searches LRCLIB on its own.
- Better online matching (plugin and LyricsTools Online tab): titles such as "Guess featuring billie eilish" and tags listing several artists or a slightly misspelled one now match. TIDAL also retries with the first artist and the bare title; LRCLIB retries once when busy (HTTP 5xx) and with the first artist / free text.

## 0.9.0

- New **LRC Deck** video effect (Windows): shows each deck's own lyrics inside that deck's video, so the deck previews in the skin show exactly what the audience will see. It works on audio-only tracks too and is installed by LyricsTools into `Plugins64\VideoEffect`. See the README for an optional toggle pad and the `ONSONGLOAD` mapping that switches it on automatically for each loaded track.
- LRC Master and LRC Deck now share all appearance and Settings-panel options (highlight whole line, countdown gap, show name included). A change saved in either one is applied to the others immediately, without restarting VirtualDJ.
- Removed the leftover once-a-second watch for external edits of same-name `.txt` lyrics files (and the unused Notepad editor code). `.txt` files are still read as the last lyrics source.

## 0.8.11

- Internal: deduplicated embedded ID3 lyrics parsing (shared tag/frame-walking helpers), and hardened the `lyrics_core` build with `/W4 /WX` and an optional AddressSanitizer target. No user-facing changes.

## 0.8.10

- Fixed **"Edit browsed"** not finding the song selected in the browser: it was reading `get_browsed_song 'filepath'`, which returned only the containing folder instead of the file, so the button looked broken even with a song correctly selected. Now uses the dedicated `get_browsed_filepath` lookup.
- **"Edit browsed"** now shows a message instead of silently doing nothing when nothing is actually selected in the browser (e.g. a folder or playlist node instead of a song row).
- The embedded-lyrics editor now shows which song you're editing, both in its title bar and above the text box.
- Embedded-lyrics edits now write to the MP3 immediately when the file isn't in use, instead of silently waiting for VirtualDJ's video window to be open and the track to be unloaded from a deck.
- The Advanced settings preview now shows a placeholder "Artist - Title" banner matching the real on-screen heading, and uses a 16:9 aspect ratio to better match actual video output.

## 0.8.9

- Fixed the diagnostics log silently truncating and merging lines whenever a message contained a non-ASCII character (e.g. a Czech file path or artist name).
- Clarified the "Edit browsed" Settings-panel button (was labeled "Edit browser", which read as if it edited the file browser itself).
- LyricsTools now checks for its own updates shortly after startup by default (silently, unless one is actually available) and shows a status badge for it in the header, matching the plugin one. Toggle in Settings: "Automatically check for updates on startup".
- `build-release.ps1 -Publish` now tags, pushes, creates the GitHub Release and attaches both the Windows and macOS packages automatically, so a release can no longer end up tagged with no corresponding GitHub Release.
- LyricsTools GUI polish: a colored primary "Run" button, a progress indicator while any operation is running, a colored status pill on the Problems tab, and shorter checkbox labels with a smaller explanation underneath instead of one long wrapped sentence.

## 0.8.8

- Replaced the track name/artist intro with an independent banner that no longer interacts with the lyrics scroll at all, fixing several bugs it used to cause (wrong highlight color, broken countdown, unstable sizing).
- Fixed lyrics scrolling and the top/bottom fade mask jumping whenever a line wrapped onto two rows or the active line changed: the anchor and fade band are now derived purely from the configured line count and font settings, never from which lines happen to be on screen.
- Fixed the background color plate: it now stays solid across the full frame height, with only the text fading near the top/bottom edges (previously the plate itself faded, or left an untinted gap at the edges).
- Added a **Show name** toggle (track name and artist) to VirtualDJ's Settings panel.
- **Edit browsed lyrics** now checks that an actual MP3 is selected in the browser, instead of silently failing when a folder was selected.
- The embedded-lyrics tag writer now logs its real exit code and any error output, so a failed save is no longer indistinguishable from a successful one.

## 0.8.7

- Fixed a race between the on-screen lyrics renderer and the embedded-lyrics editor: **Edit browser** could show or save lyrics against the wrong (currently playing) track while an edit dialog was open in the background. Editing a browsed file no longer touches the live renderer's state at all.
- Fixed the embedded-lyrics editor losing or mixing up text when switching between Timed and Untimed before saving.

## 0.8.6

- Timed and Untimed now keep separate contents in the embedded-lyrics editor. Switching type shows the actual corresponding MP3 tag, or an empty editor when that tag does not exist.
- Added **Edit browser** for preparing the MP3 selected in the VirtualDJ browser without changing the master output.
- Improved the Windows track intro: artist and title are white, and the intro joins the lyric time line so it scrolls away smoothly with the first lyrics.

## Windows download

Extract the complete LyricsTools-Windows-v0.8.11.zip, open LyricsTools.exe and choose Install / update plugin. Keep the _internal folder beside the EXE.

These editor, preview, and lyrics-writing improvements are currently Windows-specific.