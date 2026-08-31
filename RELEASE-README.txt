LRC Lyrics for VirtualDJ 0.4.0 (Windows 64-bit)
================================================

Installation
------------

1. Close VirtualDJ.
2. Open %LOCALAPPDATA%\VirtualDJ\Plugins64.
3. Create the VideoOverlay, Visualisations and VideoEffect folders if needed.
4. Copy into VideoOverlay:
   - LRC Master.dll
   - LRC BlackOut.dll
   - EmbeddedLyricsTagWriter.py
5. Copy into Visualisations:
   - LRC Deck.dll
   - EmbeddedLyricsTagWriter.py
6. Copy into VideoEffect:
   - LRC Deck FX.dll
   - EmbeddedLyricsTagWriter.py
7. Start VirtualDJ.

Usage
-----

- LRC Master and LRC BlackOut are in the Overlays section.
- Select LRC Deck as the Source for audio-only tracks. VirtualDJ provides one
  shared automatic videoAudioOnlyVisualisation slot, not a separate slot for
  every deck.
- For independent lyrics on two audio-only decks, add LRC Deck FX to each
  deck's own Video FX chain. Deck FX replaces that deck's picture with its
  black lyrics canvas, so disable it before playing a real video.
- Text has a black outline for readability over photos and video.
- The Advanced button opens the LRC Presets dialog with font, outline/shadow
  and text color options.

Lyrics are read from SYLT, SYNCEDLYRICS, UNSYNCEDLYRICS and USLT tags, as well
as same-name .lrc and .txt files. Timestamps found in TXT or unsynchronized
tags are used as timed lyrics.

Writing manually tapped timing requires Python 3 and Mutagen:
  py -m pip install mutagen
