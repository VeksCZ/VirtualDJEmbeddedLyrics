# LRC Lyrics for VirtualDJ

Display synchronized or plain lyrics from your own music files in the VirtualDJ
master video output. The project provides two Windows 64-bit video overlays:

- **LRC Master** displays the lyrics.
- **LRC BlackOut** adds an optional black background behind overlays.

## Download and install

1. Download the `LRC-Lyrics-VirtualDJ-vX.Y.Z.zip` asset from the
   [latest GitHub release](https://github.com/VeksCZ/VirtualDJEmbeddedLyrics/releases/latest).
2. Extract the complete ZIP. Do not run the installer from inside the ZIP preview.
3. Close VirtualDJ completely.
4. Double-click **Install.cmd**.
5. Start VirtualDJ again.

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

## Optional MP3 tools

The extracted `Tools` folder contains one **MP3 & Lyrics Tools** GUI with tabs
for:

- importing same-name LRC/TXT files into MP3 lyrics tags;
- scanning existing embedded lyrics and writing the portable Grouping marker;
- downloading or normalizing lyrics with structure-preserving LRC backups;
- restoring LRC sidecars from those backups.

Drag a music folder onto **MP3 & Lyrics Tools.cmd**, or start it and choose a
folder. The legacy **Import Lyrics.cmd** and **Mark Existing Lyrics.cmd** names
remain as shortcuts to the matching GUI tabs. Preview mode is enabled on first
use. Source LRC/TXT files are deleted only after successful verification and only
when you explicitly enable deletion.

These optional tools require Python 3, Mutagen and tidalapi. If Python is
installed but packages are missing, the launcher offers to install the required
versions for the current user.
Python is also required by **Record timing**, but not for normal lyrics display.

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
every private lyrics frame in a library. Use **Mark Existing Lyrics.cmd** for a
recursive MP3 scan.

## Update or uninstall

To update, close VirtualDJ, extract the new release and run its **Install.cmd**.
The operation is repeatable and keeps a backup of replaced files.

To remove the plugin, close VirtualDJ and run **Uninstall.cmd** from the extracted
release. Only files installed by this project are removed, and they are backed up
first. **Restore Backup.cmd** can restore one of the snapshots created before an
installation or uninstall operation.

## Troubleshooting

### The overlays are not listed

- Restart VirtualDJ after installation.
- Run `Install.cmd` again and confirm the displayed VirtualDJ home path.
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

Source builds, tests, SDK information and the unsupported experimental deck build
are documented in [DEVELOPMENT.md](DEVELOPMENT.md).
