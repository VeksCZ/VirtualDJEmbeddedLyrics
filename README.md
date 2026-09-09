# LRC Lyrics for VirtualDJ

Display synchronized or plain lyrics from your own music files in the VirtualDJ
master video output. The project provides two Windows 64-bit video overlays:

- **LRC Master** displays the lyrics.
- **LRC BlackOut** adds an optional black background behind overlays.

## Download and install

1. Download the asset matching your system: `Windows`, `macOS-AppleSilicon`, or
   `macOS-Intel`, from the
   [latest GitHub release](https://github.com/VeksCZ/VirtualDJEmbeddedLyrics/releases/latest).
2. Extract the complete ZIP. Do not run the installer from inside the ZIP preview.
3. Close VirtualDJ completely.
4. Open **LRCPluginSetup.exe** on Windows or **LRCPluginSetup.app** on macOS.
5. Confirm the detected VirtualDJ folder and choose **Install / update**.
6. Start VirtualDJ again.

The installer detects the active VirtualDJ home folder from the VirtualDJ
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

## LRC Master controls

- **Font size**, **Timed lines**, **Untimed lines** and **Vertical position**
  control the layout.
- **Advanced** opens presets, font selection, outline/shadow options and colors.
- **Next** and **Prev** move through plain untimed lyrics.
- **Edit TXT** creates or opens the current track's same-name TXT file.
- **Record timing** timestamps plain lyrics and writes synchronized MP3 tags
  after the track has been unloaded from all decks.
- **Auto-tag #lrc** adds `#lrc` to VirtualDJ **User 1** after lyrics have been
  verified. Existing User 1 content is preserved.

`User 1` is stored in the VirtualDJ database, not in the MP3. The optional tools
use the standard ID3 `Grouping` field for the portable markers
`Lyrics: Synced` and `Lyrics: Unsynced`.

## Included applications

Both release packages contain two separate no-console applications:

- **LRCPluginSetup** installs, updates, removes, or restores the VirtualDJ plugin.
- **LyricsTools** manages music files, lyrics, MyLists, and Search DB.

Both applications include their runtime; end users do not need to install
Python. LyricsTools opens in Simple mode and exposes TIDAL/normalization and
restore tools after enabling **Advanced**. Its tabs cover:

- fully mirroring a music directory tree and its tracks into an isolated
  VirtualDJ MyLists root, with optional add-only Search DB registration;
- importing same-name LRC/TXT files into MP3 lyrics tags;
- scanning existing embedded lyrics and writing the portable Grouping marker;
- downloading or normalizing lyrics with structure-preserving LRC backups;
- restoring LRC sidecars from those backups.

Start **LyricsTools** and choose a folder. Preview mode is enabled on first use.
Source LRC/TXT files are deleted
only after successful verification and only when you explicitly enable deletion.

Python is required only by the Windows plugin's optional **Record timing** helper,
not by either packaged GUI application or normal lyrics display.

On macOS, extract the complete ZIP and open **LRCPluginSetup.app** first. If
Gatekeeper blocks a locally signed
app on first launch, Control-click it, choose **Open**, and confirm once. The GUI
detects both `~/Library/Application Support/VirtualDJ` and the legacy
`~/Documents/VirtualDJ` home. Music on external drives is registered in that
volume's `/Volumes/<name>/VirtualDJ/database.xml`.

Each macOS package contains universal LRC Master and LRC BlackOut `.bundle`
plugins plus GUI applications native to the architecture named in the ZIP. The
installer selects the proper plugin location automatically. LyricsTools,
folder/MyLists synchronization, Search DB
registration, and MP3 lyrics maintenance are available on both platforms.

**LRCPluginSetup** validates the selected folder, shows a color-coded installation
state, keeps replaced files in `LRC Lyrics Backups`, and never bypasses the check
that VirtualDJ is closed. Both applications can create a privacy-safe diagnostic
ZIP and check GitHub for updates only when the user presses the relevant button.

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

Add `#lrc` for the track loaded on the current deck when VirtualDJ detects lyrics:

```text
has_lyrics ? get_loaded_song 'User 1' & param_contains '#lrc' ? nothing : loaded_song_hashtag 'user 1' '#lrc' : nothing
```

After **Reload Tags**, copy the portable Grouping marker for the browsed track:

```text
get_browsed_song 'Grouping' & param_contains 'Lyrics:' ? get_browsed_song 'User 1' & param_contains '#lrc' ? nothing : browsed_song_hashtag 'user 1' '#lrc' : nothing
```

VDJScript handles one loaded or browsed track here; it does not iterate through
every private lyrics frame in a library. Use the **Mark existing lyrics** GUI tab
for a recursive MP3 scan.

## Update or uninstall

To update, close VirtualDJ, extract the new release and open **LRCPluginSetup**.
The operation is repeatable and keeps a backup of replaced files.

To remove or restore the plugin, close VirtualDJ and use **LRCPluginSetup**. Only
managed files are removed, and they are backed up first.

## Troubleshooting

### The overlays are not listed

- Restart VirtualDJ after installation.
- Open `LRCPluginSetup` and confirm the displayed VirtualDJ home path.
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
