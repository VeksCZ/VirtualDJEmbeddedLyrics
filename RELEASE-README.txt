LRC Lyrics for VirtualDJ 0.4.0 (Windows 64-bit)
================================================

Installation
------------

1. Close VirtualDJ.
2. Open %LOCALAPPDATA%\VirtualDJ\Plugins64.
3. Create the VideoOverlay folder if needed.
4. Copy into VideoOverlay:
   - LRC Master.dll
   - LRC BlackOut.dll
   - EmbeddedLyricsTagWriter.py
5. Start VirtualDJ.

Usage
-----

- LRC Master and LRC BlackOut are in the Overlays section.
- The experimental LRC Deck audio-only visualisation is disabled in this
  release because VirtualDJ hosts that source in a shared master slot rather
  than as an independent crossfade source for every deck. The installer removes
  older LRC Deck files and resets that Audio Only source selection to None.
- Text has a black outline for readability over photos and video.
- The Advanced button opens the LRC Presets dialog with font, outline/shadow
  and text color options.

Lyrics are read from SYLT, SYNCEDLYRICS, UNSYNCEDLYRICS and USLT tags, as well
as same-name .lrc and .txt files. Timestamps found in TXT or unsynchronized
tags are used as timed lyrics.

Writing manually tapped timing requires Python 3 and Mutagen:
  py -m pip install mutagen
