# VirtualDJ Embedded Lyrics

Runtime diagnostics are written to `%LOCALAPPDATA%\VirtualDJ\EmbeddedLyrics.log`.
Track metadata and sidecar lyrics are loaded asynchronously so disk access does
not block VirtualDJ's video-render thread. Release verification includes parser
tests and DirectX 11 rendering tests on the software WARP adapter.

Windows 64-bit VirtualDJ Video FX plugins for synchronized lyrics.

Release artifacts are split into `dist/full` and `dist/basic`. The Basic edition
contains only synchronous lyrics loading, the overlay renderer and Next/Previous
page controls; it intentionally omits diagnostics, background loading, TXT editing,
file watching, sliders, active-line controls and Blackout.

For a portable release, unzip the package and double-click `Install-Full.cmd`
or `Install-Basic.cmd`. VirtualDJ itself loads the DLL files copied into its
`Plugins64/VideoEffect` directory; the ZIP is only a transport package.

- **LRC Deck** is an audio-only visualisation source for each deck.
  It renders lyrics over a black background before VirtualDJ performs its video
  transition.
- **LRC Master** draws over the final master output and selects the
  audible left/right source from `get_crossfader_result`. A small hysteresis
  keeps the selected deck stable around the midpoint during transitions.
  Its **Upfaders** switch selects between `get_crossfader_result` (enabled, default off) and
  `video_crossfader` (disabled), so both behaviours can be tested live.

## Lookup order

1. Timestamped `TXXX:USLT`, `TXXX:LYRICS`, or `TXXX:SYNCEDLYRICS`, including
   the format written by TIDAL-GUI-NG.
2. A synchronized ID3v2 `SYLT` frame.
3. `TXXX:UNSYNCEDLYRICS`, then standard ID3v2 `USLT`.
4. A UTF-8 `.lrc` file with the same basename in the same directory.
5. A UTF-8 `.txt` file with the same basename in the same directory.

Unsynchronized `USLT` and TXT are displayed as manually controlled pages with
3–15 logical lines. Long lines wrap automatically.

## Current status

- ID3v2.3 and ID3v2.4 `SYLT` parser with millisecond timestamps.
- Timestamped embedded text parser, including `(00:12.32) Line` as found in
  real Tidal-tagged MP3 files.
- Standard and enhanced LRC parser.
- Embedded-first, sidecar-second loader.
- DX11/GDI text texture generation with line-progress highlighting.
- Parser tests runnable on Windows or Linux.
- Manual pages for untimed lyrics with a highlighted active line. Page and
  active-line navigation are exposed as separate FX buttons.
- `Edit TXT` creates/opens the current track's same-name `.txt` file in Notepad;
  saved changes are detected and reloaded automatically.
- Adjustable font size and vertical position, centered by default.
- Separate line counts for synchronized lyrics (1–6, default 4) and untimed
  pages (3–15, default 10).
- DX11 overlay renderer with alpha blending and graphics-state restoration.
- Continuous lyric ribbon with three previous lines and top/bottom alpha fades.
- Independent highlight timing with continuous scrolling on lyric rows. Long
  pauses stop on persistent timeline
  rows that enter from below, count down in yellow from five or ten to one
  depending on pause length, and leave above.
- Timed and untimed line counts are independently adjustable from 5 to 12.
- Full untimed lyrics use smooth Next/Previous line scrolling instead of pages.
  Optional `Record timing` captures each newly highlighted line, waits until
  the MP3 is unloaded from decks 1–4, then writes both ID3 `SYLT` and
  `TXXX:SYNCEDLYRICS`. Provenance is stored in the SYLT descriptor and an
  ignored `[re:...]` metadata line; no LRC is created.
- Independent per-deck timing through `get_plugindeck` in the deck variant.
- Stable selection of the audible left/right deck using `get_crossfader_result`
  in the master variant.
- Deck variants are advertised as visualisations for audio-only tracks and
  generate their own black background.
- `LRC BlackOut` is a Master-only, process-first background effect included only in
  the Full edition, allowing later Master effects to render above it.
- Full HD responsive typography with automatic wrapping of long lines.

## Import LRC and TXT into MP3 tags

Install Python 3 and Mutagen:

```powershell
py -m pip install mutagen
```

Preview changes without modifying files:

```powershell
py tools/lyrics_tag_converter.py "D:/Music"
```

The input can also be one `.lrc`, `.txt`, or `.mp3` file. For every LRC/TXT
sidecar, the converter looks for a same-name MP3 in the same folder
case-insensitively. LRC is written as synchronized `SYLT`; TXT as unsynchronized
`USLT`. When both sidecars exist, both frames are handled in one pass.

When the converter is copied into the root of a music folder, the path argument
can be omitted. It recursively scans the directory containing the script and all
of its subdirectories:

```powershell
py lyrics_tag_converter.py          # dry-run
py lyrics_tag_converter.py --write  # write tags
```

Write tags into the working copy of the music files:

```powershell
py tools/lyrics_tag_converter.py "D:/Music" --write

# Or drag a folder onto Import-LRC-Folder.cmd for dry-run, confirmation,
# dual-tag writing, verification, and optional verified LRC deletion.
```

LRC is written to standard `SYLT`; TXT is written to standard `USLT`. Existing
target frames are skipped unless `--overwrite` is supplied.

## Build parser tests

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Build the plugin

The required public VirtualDJ SDK headers are included under
`third_party/VirtualDJ8_SDK_20211003`.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

On Windows with Visual Studio 2022, `build-release.ps1` builds both x64 DLLs and
runs tests. `install-plugin.ps1` installs it into the detected VirtualDJ
`Plugins64/VideoEffect` directory. Restart VirtualDJ, open the master Video FX
list, and activate **LRC Master**, or activate **Embedded Lyrics
Deck** in the Video FX slot of each deck.

For untimed lyrics on the deck variant, map next/previous independently, for
example:

```text
deck 1 effect 'LRC Deck' button 7
deck 1 effect 'LRC Deck' button 8
```

Use the corresponding deck number for other decks.
