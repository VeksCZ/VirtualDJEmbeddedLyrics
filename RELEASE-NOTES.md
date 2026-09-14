# LRC Lyrics for VirtualDJ 0.8.5

## Changes

- Reworked the Windows track intro into two clean lines: artist, then song title.
- Added an in-plugin **Edit lyrics** window for loaded MP3 tracks; it preloads the current lyrics and updates the video output immediately after saving.
- The editor writes timed LRC-style entries as a standard ID3v2 `SYLT` frame or plain entries as a standard `USLT` frame after the track is unloaded from every deck.
- Saving replaces obsolete embedded lyric variants, validates the selected format first, and updates VirtualDJ User 1 with `#sylt` or `# - uslt`.

## Windows download

Extract the complete LyricsTools-Windows-v0.8.5.zip, open LyricsTools.exe and choose Install / update plugin. Keep the _internal folder beside the EXE.

The in-plugin lyric editor and track intro in this release are currently Windows-specific.