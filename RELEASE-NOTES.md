# LRC Lyrics for VirtualDJ 0.8.3

## Changes

- Windows Advanced settings now include timed/untimed line counts (1–12), font size and vertical position with a live preview, plus wide playback checkboxes.
- Fixed line-count rendering while preserving the opaque background fix from 0.8.2.
- Added a timed lyric offset from -2000 to +2000 ms in 10 ms steps. Negative values show lyrics earlier; the offset resets on track changes.
- Added optional whole-line highlighting and a saved countdown pause threshold from 3 to 10 seconds (default 5).
- Untimed lyrics now use `#-uslt` in VirtualDJ User 1. Automatic tagging replaces legacy `#uslt` without removing unrelated tags.
- Added explicit green Done messages, clearer running/warning/error colors, and the complete README and tool guide in release archives.
- Shortened the manual navigation label to Next line.

Includes the LRCLIB fallback and standard SYLT/USLT storage changes from 0.8.2.

## Windows download

Extract the complete LyricsTools-Windows-v0.8.3.zip, open LyricsTools.exe and choose Install / update plugin. Keep the _internal folder beside the EXE.

The Advanced settings changes above are Windows-specific. macOS receives the untimed marker and shared tool/documentation updates.
