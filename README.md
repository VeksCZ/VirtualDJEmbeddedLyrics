# LRC Lyrics for VirtualDJ

Windows 64-bit plugins that display embedded or sidecar lyrics in VirtualDJ.

## Plugins

- **LRC Master** — final master overlay. It follows `video_crossfader` by default;
  enable **Upfaders** to use `get_crossfader_result` instead.
- **LRC Deck** — source for audio-only tracks. VirtualDJ also lists visualisation
  sources among master overlays; this duplicate menu entry is normal VDJ behaviour.
- **LRC BlackOut** — black master background processed before later overlays,
  slideshows, shaders and other visualisations.

## Install

1. Close VirtualDJ.
2. Run `build-release.ps1` when building from source.
3. Run `Install.cmd`.
4. Restart VirtualDJ.

The installer uses these required VirtualDJ categories:

- `Plugins64/VideoOverlay`: `LRC Master.dll`, `LRC BlackOut.dll`
- `Plugins64/Visualisations`: `LRC Deck.dll`

It also installs `EmbeddedLyricsTagWriter.py` beside Master and Deck for deferred
manual-timing writes. Python 3 and Mutagen are required only for tag writing and
the standalone importer (`py -m pip install mutagen`). Lyrics display itself does
not require Python.

## Lyrics lookup order

1. Timestamped `TXXX:USLT`, `TXXX:LYRICS` or `TXXX:SYNCEDLYRICS`.
2. ID3v2 `SYLT`.
3. `TXXX:UNSYNCEDLYRICS`, then standard `USLT`.
4. Same-name `.lrc`.
5. Same-name `.txt`.

Timestamped text found in nominally unsynchronised tags or TXT is treated as
timed lyrics. Missing lyrics are shown as `...`.

## Display and controls

- Continuous timed and untimed scrolling with independent line counts (5–12).
- Smooth word/line highlight timing and persistent pause rows.
- Long intro and inter-verse pauses show their real duration in the queue;
  waiting countdown rows use the read color and active countdowns use highlight.
- Adjustable font size, vertical position and strong adaptive black outline.
- Clickable Windows color pickers for text, highlight and read colors.
- `Next` and `Prev` move untimed lyrics one line at a time.
- `Record timing` timestamps the line that becomes active, waits until the MP3 is
  unloaded from decks 1–4, then writes both `SYLT` and `TXXX:SYNCEDLYRICS`.
- `Edit TXT` creates/opens the current track's same-name TXT and reloads saved edits.

Example mappings:

```text
deck 1 effect 'LRC Deck' button 7
deck 1 effect 'LRC Deck' button 8
```

## Import LRC/TXT into MP3 tags

Preview recursively:

```powershell
py tools/lyrics_tag_converter.py "D:/Music"
```

Write tags:

```powershell
py tools/lyrics_tag_converter.py "D:/Music" --write
```

`Import-Lyrics-Here.bat` is a disposable launcher intended to be copied into a
music folder. It calls the central tool, passes that folder, and deletes only
the launcher itself afterward. Verified source `.lrc`/`.txt` deletion remains an
explicit prompt.

## Build and test

The public VirtualDJ SDK headers remain vendored under
`third_party/VirtualDJ8_SDK_20211003`.

```powershell
./build-release.ps1
```

This builds the three DLLs, runs both C++ tests and all Python `unittest` tests,
then creates `dist/full`.
