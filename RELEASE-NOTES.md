# LRC Lyrics for VirtualDJ 0.8.9

## Changes

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

Extract the complete LyricsTools-Windows-v0.8.9.zip, open LyricsTools.exe and choose Install / update plugin. Keep the _internal folder beside the EXE.

These new editor and track-intro improvements are currently Windows-specific.