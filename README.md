# LRC Lyrics for VirtualDJ

Windows 64-bit plugins that display embedded or sidecar lyrics in VirtualDJ.

## Plugins

- **LRC Master** — final master overlay. It follows `video_crossfader` by default;
  enable **Upfaders** to use `get_crossfader_result` instead.
- **LRC Deck** — automatic audio-only video source. Select it in VirtualDJ's
  **Source for audio-only tracks** section.
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
- Adjustable font size and vertical position.
- The standard VirtualDJ parameter panel includes an `Advanced` button. It
  opens the **LRC Presets** dialog with Default, Photos and Clean presets,
  font selection, outline/shadow style and strength, plus visible color
  swatches for text, highlight and read lines.
- `Next` and `Prev` move untimed lyrics one line at a time.
- `Record timing` timestamps the line that becomes active, waits until the MP3 is
  unloaded from decks 1–4, then writes both `SYLT` and `TXXX:SYNCEDLYRICS`.
- `Edit TXT` creates/opens the current track's same-name TXT and reloads saved edits.

Example mappings:

```text
deck 1 effect 'LRC Deck' button 7
deck 1 effect 'LRC Deck' button 8
```

Enable LRC Deck in **Source for audio-only tracks**. This is VirtualDJ's single
automatic `videoAudioOnlyVisualisation` slot. VirtualDJ may run that instance
on the master when both video sides contain audio-only tracks; the public SDK
does not expose separate automatic audio-only source slots for each deck.

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

Successfully imported MP3 files also receive a VirtualDJ-visible ID3
`Grouping` marker: `Lyrics: Synced` or `Lyrics: Unsynced`. Existing Grouping
text is preserved.

`Mark-Lyrics-Here.bat` is the separate scanner for files that already contain
embedded lyrics. Copy it into a music folder and run it there. It recursively
reads only MP3 tags (it does not look for LRC/TXT files), previews the changes,
and after confirmation writes the same Grouping marker. In VirtualDJ use
**Reload Tags**, then enable the **Grouping** browser column.

## Build and test

The public VirtualDJ SDK headers remain vendored under
`third_party/VirtualDJ8_SDK_20211003`.

```powershell
./build-release.ps1
```

This builds the three DLLs, runs both C++ tests and all Python `unittest` tests,
then creates `dist/full`.
