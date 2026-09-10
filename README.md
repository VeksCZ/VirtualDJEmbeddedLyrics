# LRC Lyrics for VirtualDJ

Display synchronized or plain lyrics from your own music files in the VirtualDJ
master video output. The project provides two video overlays for Windows and macOS:

- **LRC Master** displays the lyrics.
- **LRC BlackOut** adds an optional black background behind overlays.

## Download and install

1. Download **LyricsTools** matching your system: `Windows`,
   `macOS-AppleSilicon`, or `macOS-Intel`, from the
   [latest GitHub release](https://github.com/VeksCZ/VirtualDJEmbeddedLyrics/releases/latest).
2. Extract the complete ZIP. Do not run the application from inside the ZIP preview.
3. Open **LyricsTools.exe** on Windows or **LyricsTools.app** on macOS.
4. Select the first **Plugin** tab and confirm the detected VirtualDJ folder.
5. Choose **Install / update plugin**. If VirtualDJ is running, LyricsTools can
   ask it to close normally after you save your work.
6. Start VirtualDJ again.

LyricsTools detects the active VirtualDJ home folder from the VirtualDJ
registry setting, the current `%LOCALAPPDATA%\VirtualDJ` location, or the legacy
`Documents\VirtualDJ` location. If it cannot choose safely, it asks for the
folder. In VirtualDJ, **Settings > Options > cog button** opens the active home
folder so you can copy its path.

No administrator access, CMake, Visual Studio, Python or source checkout is
required to display lyrics. Existing plugin files and known legacy versions are
backed up under `LRC Lyrics Backups` in the selected VirtualDJ home folder.

## First use

1. Open the Video effects/overlays list in VirtualDJ.
2. Enable **LRC Master**.
3. Optionally enable **LRC BlackOut** when you want a solid black background.
4. Load and play a track containing supported lyrics.

LRC Master follows the video crossfader by default. Enable its **Upfaders**
option if you prefer the effective audio-fader result. A track with no lyrics
displays `...`.

These are master overlays. VirtualDJ's public plugin interface does not provide
this project with an independent audio-only lyric source for every deck preview.

## Supported lyrics

LRC Master reads, in priority order:

1. Timestamped `TXXX:USLT`, `TXXX:LYRICS` or `TXXX:SYNCEDLYRICS`.
2. Standard ID3v2 `SYLT`.
3. `TXXX:UNSYNCEDLYRICS` or standard `USLT`.
4. A same-name `.lrc` file.
5. A same-name `.txt` file.

Timestamped content found in TXT or nominally unsynchronized fields is displayed
as synchronized lyrics.

LyricsTools writes new synchronized lyrics as one standard ID3v2 `SYLT` frame
and new plain lyrics as one standard `USLT` frame. When it rewrites lyrics, it
removes the older duplicate custom TXXX lyric frames. The plugin continues to
read those legacy fields for existing libraries. A standard SYLT frame retains
each lyric line and its millisecond timestamp, so it can later be exported back
to an LRC file; nonstandard LRC metadata may not be recoverable.

## LRC Master controls

- **Font size**, **Timed lines**, **Untimed lines** and **Vertical position**
  control the layout.
- **Advanced** opens presets, font selection, outline/shadow options, colors,
  and the optional solid video background. Its live sample previews those choices
  before they are applied. The Custom preset is stored separately and survives
  trying or applying a built-in preset. The top/bottom lyrics fade remains active
  with either a transparent or solid background.
- **Next** and **Prev** move through plain untimed lyrics.
- **Edit TXT** creates or opens the current track's same-name TXT file.
- **Record timing** timestamps plain lyrics and writes synchronized MP3 tags
  after the track has been unloaded from all decks.
- **Auto-tag lyrics** adds `#sylt` for synchronized lyrics or `#uslt` for plain
  untimed lyrics to VirtualDJ **User 1** after lyrics have been loaded. It also
  replaces the older `#lrc` marker and preserves unrelated User 1 content.

`User 1` is stored in the VirtualDJ database, not in the MP3. LyricsTools writes
the standard ID3 `Grouping` field for the portable markers `Lyrics: Synced` and
`Lyrics: Unsynced`; after a real Import, Mark, or TIDAL/normalize run it also
updates `#sylt/#uslt` directly in the appropriate VirtualDJ database. Missing
database entries are added, while unrelated tags and existing analysis are kept.
VirtualDJ must be closed for that direct database update, and a backup is created.

## Included application

Each platform has one no-console **LyricsTools** download. Its first tab handles
plugin installation, update, uninstall, recovery, and video-window reset. The
remaining tabs provide music, lyrics, MyLists, Search DB, and problem-queue tools.
On Windows, `LyricsTools.exe` is prominent at the package root and support files
are contained in `_internal`. On macOS the support files are inside the app.

LyricsTools includes its runtime; end users do not need to install Python. It
opens in Simple mode and exposes TIDAL/normalization and
restore tools after enabling **Advanced**. Its tabs cover:

- fully mirroring a music directory tree and its tracks into an isolated
  VirtualDJ MyLists root, with optional add-only Search DB registration;
- importing same-name LRC/TXT files into MP3 lyrics tags;
- scanning existing embedded lyrics and writing the portable Grouping marker;
- downloading or normalizing lyrics with structure-preserving LRC backups;
- restoring LRC sidecars from those backups.
- finding missing lyrics, invalid LRC, unreadable tags, and unmatched sidecars
  in a local problem queue with CSV export.

Start **LyricsTools** and choose a folder. Preview mode is enabled on first use.
Source LRC/TXT files are deleted only after successful verification and only
when you explicitly enable deletion. A separate conservative option can remove
a timed TXT that was ignored because a same-name LRC was successfully imported
and verified. Plain or otherwise skipped TXT files are retained.

Python is required only by the Windows plugin's optional **Record timing** helper,
not by either packaged GUI application or normal lyrics display.

On macOS, extract the complete ZIP and open **LyricsTools.app**. If
Gatekeeper blocks a locally signed
app on first launch, Control-click it, choose **Open**, and confirm once. The GUI
detects both `~/Library/Application Support/VirtualDJ` and the legacy
`~/Documents/VirtualDJ` home. Music on external drives is registered in that
volume's `/Volumes/<name>/VirtualDJ/database.xml`.

Each macOS package contains universal LRC Master and LRC BlackOut `.bundle`
plugins inside the architecture-specific LyricsTools app. The Plugin tab selects
the proper VirtualDJ plugin location automatically. Folder/MyLists synchronization, Search DB
registration, and MP3 lyrics maintenance are available on both platforms.

The **Plugin** tab validates the selected folder, shows a color-coded installation
state, keeps replaced files in `LRC Lyrics Backups`, and never bypasses the check
that VirtualDJ is closed. If VirtualDJ is running, it offers to request a normal
shutdown and never force-kills it. LyricsTools can create a privacy-safe
diagnostic ZIP. Update checks run only when requested; an update can then be
downloaded, SHA-256 verified, extracted, and launched automatically.

The **Sync folders to VDJ** tab is a one-way, full mirror. It rebuilds only the
named managed root: new files appear, and playlist references to files or folders
that are no longer in the selected source tree disappear. Unrelated manual lists
remain untouched. It can also add current source tracks to Search DB without
deleting any other database entry or existing analysis. Tracks are routed to
VirtualDJ's correct per-drive database (for example `D:\VirtualDJ\database.xml`).
Both the music folder and VirtualDJ home can be selected manually. Preview mode
performs no writes. Real synchronization requires VirtualDJ to be closed and
creates a timestamped backup first.

Full tool documentation is in [tools/README.md](tools/README.md). The tools are
not required to run the VirtualDJ plugins.

## VirtualDJScript helpers

The plugin automatically distinguishes loaded lyrics in VirtualDJ User 1:

- `#sylt` means synchronized lyrics and automatic line progression.
- `#uslt` means untimed lyrics and manual page/line control.

VirtualDJ's microphone icon is not evidence that a same-name LRC exists next to
the audio file. It can also reflect embedded MP3 lyrics or lyrics already known
to VirtualDJ's database/cache.

Add a generic marker for the track loaded on the current deck when VirtualDJ
detects any lyrics (useful only if the plugin has not loaded it yet):

```text
has_lyrics ? get_loaded_song 'User 1' & param_contains '#lyrics' ? nothing : loaded_song_hashtag 'user 1' '#lyrics' : nothing
```

After **Reload Tags**, copy the portable Grouping marker for the browsed track:

```text
get_browsed_song 'Grouping' & param_contains 'Lyrics: Synced' ? get_browsed_song 'User 1' & param_contains '#sylt' ? nothing : browsed_song_hashtag 'user 1' '#sylt' : get_browsed_song 'Grouping' & param_contains 'Lyrics: Unsynced' ? get_browsed_song 'User 1' & param_contains '#uslt' ? nothing : browsed_song_hashtag 'user 1' '#uslt' : nothing
```

VDJScript handles one loaded or browsed track here; it does not iterate through
every private lyrics frame in a library. Use the **Mark existing lyrics** GUI tab
for a recursive MP3 scan.

## Update or uninstall

To update the application, use the top-right settings menu or extract a newer
LyricsTools release. To update the plugin, use its first **Plugin** tab. The
operation is repeatable and keeps a backup of replaced files.

To remove or restore the plugin, use the **Plugin** tab. Only
managed files are removed, and they are backed up first.

If VirtualDJ has remembered an unwanted external-video window size or monitor,
use **Reset video window layout** in the Plugin tab. It backs up `settings.xml`
and forgets only the saved video-window geometry; VirtualDJ chooses its default
again and can remember the new position for the current monitor arrangement.

## Troubleshooting

### The overlays are not listed

- Restart VirtualDJ after installation.
- Open LyricsTools' **Plugin** tab and confirm the displayed VirtualDJ home path.
- If asked for a path, open it using **Settings > Options > cog button** in
  VirtualDJ.

### A track displays `...`

The selected track has no lyrics supported by this plugin. Verify its embedded
tags, or place a same-name LRC/TXT file beside the audio file. The diagnostic log
is `%LOCALAPPDATA%\VirtualDJ\EmbeddedLyrics.log`.

### Record timing or the MP3 tools do not work

Install current Python 3 from [python.org](https://www.python.org/downloads/windows/)
and enable **Add Python to PATH** during setup. Then run:

```powershell
python -m pip install --user -r requirements.txt
```

## Development

Source builds, tests and SDK information are documented in
[DEVELOPMENT.md](DEVELOPMENT.md).
