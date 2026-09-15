# LRC Lyrics for VirtualDJ 0.8.7

## Changes

- Fixed a race between the on-screen lyrics renderer and the embedded-lyrics editor: **Edit browser** could show or save lyrics against the wrong (currently playing) track while an edit dialog was open in the background. Editing a browsed file no longer touches the live renderer's state at all.
- Fixed the embedded-lyrics editor losing or mixing up text when switching between Timed and Untimed before saving.

## 0.8.6

- Timed and Untimed now keep separate contents in the embedded-lyrics editor. Switching type shows the actual corresponding MP3 tag, or an empty editor when that tag does not exist.
- Added **Edit browser** for preparing the MP3 selected in the VirtualDJ browser without changing the master output.
- Improved the Windows track intro: artist and title are white, and the intro joins the lyric time line so it scrolls away smoothly with the first lyrics.

## Windows download

Extract the complete LyricsTools-Windows-v0.8.7.zip, open LyricsTools.exe and choose Install / update plugin. Keep the _internal folder beside the EXE.

These new editor and track-intro improvements are currently Windows-specific.