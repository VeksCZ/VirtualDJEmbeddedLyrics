# LRC Lyrics for VirtualDJ

Display synchronized or plain lyrics from your own music files in the VirtualDJ
master video output. The project provides two video overlays for Windows and macOS:

- **LRC Master** displays the lyrics.
- **LRC BlackOut** adds an optional black background behind overlays.
- **LRC Deck** displays each deck's own lyrics inside that deck's video,
  so the deck previews show exactly what the audience will see.

## Download and install

The plugin is a separate, small download. There are two ways to install it.

**With DJ Music Manager (recommended).** Open the **VirtualDJ** tab and press **LRC plugin**. It
downloads the latest release for your system (Windows, macOS Apple Silicon or macOS Intel),
keeps a backup of replaced files and installs everything in the right folders. VirtualDJ must be
closed. Start VirtualDJ again afterwards.

**By hand.**

1. Download the plugin package for your system from the
   [latest GitHub release](https://github.com/VeksCZ/VirtualDJEmbeddedLyrics/releases/latest):
   `LRC-Plugins-Windows`, `LRC-Plugins-macOS-AppleSilicon` or `LRC-Plugins-macOS-Intel`.
2. Close VirtualDJ, extract the ZIP and follow `INSTALL.txt` inside it (copy the files from its
   `Plugins` folder into your VirtualDJ plugin folders).
3. Start VirtualDJ.

Release ZIPs include this complete README as both `README.md` and `README.txt`, plus
`INSTALL.txt`. They match the source revision used to build the release.

In VirtualDJ, **Settings > Options > cog button** opens the active home folder so you can copy
its path.

## First use

1. Open the Video effects/overlays list in VirtualDJ.
2. Enable **LRC Master**.
3. Optionally enable **LRC BlackOut** when you want a solid black background.
4. Load and play a track containing supported lyrics.

LRC Master follows the video crossfader by default. Enable its **Upfaders**
option if you prefer the effective audio-fader result. A track with no lyrics
displays `...`.

## Lyrics in the deck previews (LRC Deck)

**LRC Deck** is an ordinary video effect for a single deck's FX chain. It draws
the deck's picture (or the black canvas of an audio-only track) and then that
deck's lyrics on top, so each deck preview shows its own lyrics and the master
receives them through the video crossfader.

VirtualDJ lists every deck-capable video effect under **transforms** in the
master effects list as well; LRC Deck is meant for decks only, so don't enable it
there.

To turn it on automatically for every loaded track, add a toggle pad and a
keyboard-mapping event (Settings > Controllers > Keyboard > Advanced editor):

- Pad action (any pad page):
  ```text
  toggle '@$lrcdeck' & var '@$lrcdeck' ? deck 1 effect_active 'LRCDeck' on & deck 2 effect_active 'LRCDeck' on : deck 1 effect_active 'LRCDeck' off & deck 2 effect_active 'LRCDeck' off
  ```
- `ONSONGLOAD` action (runs on the deck the track was loaded on):
  ```text
  var '@$lrcdeck' ? effect_active 'LRCDeck' on : nothing
  ```

Deck effects are processed before master effects, so a master Slideshow or
shader covers LRC Deck's text. To keep the lyrics on top of those as well, keep
**LRC Master** enabled on the master at the same time: it draws the identical
text over the master output, so normally only one copy is visible (a second one
can briefly show through while the crossfader blends two decks).

LRC Master and LRC Deck share all appearance and Settings-panel options (stored
in `%LOCALAPPDATA%\VirtualDJ\LRC Advanced.ini` on Windows and in the VirtualDJ
preferences on macOS); a change saved in either one is picked up by the others
immediately. The lyrics timing offset stays per deck. On macOS the installer puts
`LRCDeck.bundle` into the `VideoEffect` folder next to `VideoOverlay`, and the same
pad and `ONSONGLOAD` actions work there.

## Supported lyrics

LRC Master reads, in priority order:

1. Timestamped `TXXX:USLT`, `TXXX:LYRICS` or `TXXX:SYNCEDLYRICS`.
2. Standard ID3v2 `SYLT`.
3. `TXXX:UNSYNCEDLYRICS` or standard `USLT`.
4. A same-name `.lrc` file.
5. A same-name `.txt` file.

Timestamped content found in TXT or nominally unsynchronized fields is displayed
as synchronized lyrics.

DJ Music Manager and the plugin write new synchronized lyrics as one standard ID3v2 `SYLT` frame
and new plain lyrics as one standard `USLT` frame. When it rewrites lyrics, it
removes the older duplicate custom TXXX lyric frames. The plugin continues to
read those legacy fields for existing libraries. A standard SYLT frame retains
each lyric line and its millisecond timestamp, so it can later be exported back
to an LRC file; nonstandard LRC metadata may not be recoverable.

## LRC Master controls

- On Windows, **Advanced** contains **Font size**, **Timed lines**, **Untimed
  lines** and **Vertical position**, with a live timed/untimed preview. Line counts
  select 1–12 logical lines total, including the current line. Long lyrics can wrap.
  Large line counts reduce the maximum font size to fit the video area.
- Windows **Timing offset** is a main effect control ranging from −5000 to
  +5000 ms in 10 ms steps, centered at 0 ms. For example, −10 ms displays and
  highlights lyrics 10 ms earlier. The correction resets when the active track
  changes and does not modify lyric timestamps.
- Windows playback options use wide checkboxes in **Advanced** for following
  audio upfaders, adding lyrics tags to User 1, and recording timing.
- The main **Whole line** switch changes the default progressive highlighting to
  highlighting the entire active line at its timestamp. **Countdown gap** selects
  the minimum estimated pause from 3 to 10 seconds (default 5). A pause equal to
  the threshold also shows the countdown. Untimed navigation is unchanged.
- **Advanced** opens presets, font selection, outline/shadow options, colors,
  and the optional solid video background. Its live sample previews those choices
  before they are applied. The Custom preset is stored separately and survives
  trying or applying a built-in preset. The top/bottom lyrics fade remains active
  with either a transparent or solid background.
- **Next line** and **Previous line** move through plain untimed lyrics.
- **Edit lyrics** / **Edit browsed** open the **LRC lyrics** window for the loaded
  or browsed MP3. It shows which lyrics the MP3 already has, lets you paste or type
  Timed/Untimed text, and **Search TIDAL / LRCLIB** finds lyrics online (TIDAL by
  ISRC, then by artist/title/length, then LRCLIB). The window reports where the
  lyrics were found, what they matched and whether they are timed; nothing is
  written until **Save to MP3 tag**. TIDAL needs your own TIDAL account, signed in
  once in DJ Music Manager (it asks while downloading lyrics); LRCLIB needs no account. Without the bundled
  helper the window still searches LRCLIB on its own. On macOS the same window
  opens from LRC Master's **Edit lyrics** / **Edit browsed** buttons.
- **Start here** fixes timed lyrics that start at the wrong moment: press it where the vocals
  begin and all timestamps move so the first line starts there (saved to the MP3 tag). The
  LRC lyrics window has the same as *First line at* + *Shift timing*.
- **Record timing** timestamps plain lyrics and writes synchronized MP3 tags
  after the track has been unloaded from all decks.
- **Auto-tag lyrics** adds `#sylt` for synchronized lyrics or `# - uslt` for plain
  untimed lyrics to VirtualDJ **User 1** after lyrics have been loaded. It also
  replaces the older `#lrc` marker and preserves unrelated User 1 content.

`User 1` is stored in the VirtualDJ database, not in the MP3. DJ Music Manager writes
the standard ID3 `Grouping` field for the portable markers `Lyrics: Synced` and
`Lyrics: Unsynced`; after a real Import, Mark, or TIDAL/normalize run it also
updates `#sylt / # - uslt` directly in the appropriate VirtualDJ database. Missing
database entries are added, while unrelated tags and existing analysis are kept.
VirtualDJ must be closed for that direct database update, and a backup is created.

The **Mark existing lyrics** action also cleans existing MP3 files: it keeps or
creates exactly one standard `SYLT` for timed lyrics, otherwise exactly one
standard `USLT` for plain lyrics, and removes recognized legacy lyric duplicates.
Unrelated ID3 frames are not removed.

## Companion tools

The plugin package contains the plugins and **LrcHelper**, a small windowless program the
plugins start to save tags and to search lyrics online. It includes its own runtime, so no system
Python is needed.

The tools around the lyrics are part of **DJ Music Manager**, which is a separate program:

- downloading lyrics (TIDAL first, then LRCLIB; LRCLIB synchronized lyrics are preferred over
  plain ones) with a preview, and lyrics overview of a folder;
- **Edit lyrics** (timed SYLT and untimed USLT of one MP3) and **Lyrics problems** (missing lyrics,
  invalid LRC, unreadable tags, unmatched sidecars, with CSV export);
- restoring `.lrc` files from backups, and plugin diagnostics (a privacy-safe ZIP);
- installing and updating this plugin, and mirroring folders into VirtualDJ MyLists;
- on startup it warns when the plugin logged a serious fault (`[FAULT]` in the plugin log).

DJ Music Manager runs on Windows and macOS. The plugin works without it.

Python is required only by the Windows plugin's optional **Record timing** helper, not by normal
lyrics display.

On macOS, locally signed plugins may need to be allowed once; see `INSTALL.txt`. Each macOS
package contains universal LRC Master, LRC Deck and LRC BlackOut `.bundle` plugins.

## VirtualDJScript helpers

The plugin automatically distinguishes loaded lyrics in VirtualDJ User 1:

- `#sylt` means synchronized lyrics and automatic line progression.
- `# - uslt` means untimed lyrics and manual page/line control.

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
get_browsed_song 'Grouping' & param_contains 'Lyrics: Synced' ? get_browsed_song 'User 1' & param_contains '#sylt' ? nothing : browsed_song_hashtag 'user 1' '#sylt' : get_browsed_song 'Grouping' & param_contains 'Lyrics: Unsynced' ? get_browsed_song 'User 1' & param_contains '# - uslt' ? nothing : browsed_song_hashtag 'user 1' '# - uslt' : nothing
```

VDJScript handles one loaded or browsed track here; it does not iterate through
every private lyrics frame in a library. Use DJ Music Manager's lyrics overview for a
recursive MP3 scan.

## Update or uninstall

To update the plugin, press **LRC plugin** in DJ Music Manager again, or copy the files from a
newer release package over the old ones (VirtualDJ closed). The operation is repeatable; DJ Music
Manager keeps a backup of replaced files.

To remove the plugin, close VirtualDJ and delete the files listed in `INSTALL.txt`.

If VirtualDJ has remembered an unwanted external-video window size or monitor, close VirtualDJ
and delete the `<videoWindowPosition>` block from its `settings.xml` (make a copy first);
VirtualDJ then chooses its default again and remembers the new position.

## Troubleshooting

### The overlays are not listed

- Restart VirtualDJ after installation.
- Check that the files were copied into the VirtualDJ home folder that VirtualDJ really uses.
- If asked for a path, open it using **Settings > Options > cog button** in
  VirtualDJ.

### A track displays `...`

The selected track has no lyrics supported by this plugin. Verify its embedded
tags, or place a same-name LRC/TXT file beside the audio file. The diagnostic log
is `%LOCALAPPDATA%\VirtualDJ\EmbeddedLyrics.log`.

### Saving lyrics from the plugin does not work

`LrcHelper.exe` sits next to the plugins; it saves tags and searches
online without any system Python. Reinstall the plugin (DJ Music Manager or `INSTALL.txt`) if it is
missing. Only without it does the plugin fall back to a system Python with Mutagen:

Install current Python 3 from [python.org](https://www.python.org/downloads/windows/)
and enable **Add Python to PATH** during setup. Then run:

```powershell
python -m pip install --user -r requirements.txt
```

## Development

Source builds, tests and SDK information are documented in
[DEVELOPMENT.md](DEVELOPMENT.md).
