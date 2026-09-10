# MP3 & Lyrics Tools

This Windows and macOS suite provides one `LyricsTools` GUI for installing the
plugin, importing LRC/TXT sidecars,
marking existing embedded lyrics, normalizing or retrieving lyrics, restoring
structure-preserving backups, mirroring folders into VirtualDJ lists, and
and managing the complete workflow from one application.

## Requirements

- Windows 10 or newer, or macOS 11 or newer
- A TIDAL account when TIDAL lookup is enabled

Release applications include their Python runtime. Python is required only when
running directly from a source checkout; contributors install:

```powershell
python -m pip install --user -r requirements.txt
```

## Start the GUI

- In a release, open `LyricsTools.exe/.app`; plugin management is its first tab.
- In a source checkout, contributors can run `python tools/lyrics_tools_gui.py`.
  End users should use the self-contained release application, which does not
  require Python or a command window.

When a music library is selected, its default `_lrc_backup` folder is selected
with it. Saved settings never redirect a new library into a previous library's
backup.

The GUI starts in dry-run mode on first use. Review the activity log before
turning dry-run off.

## Applications and tabs

### Plugin

Finds or accepts the active VirtualDJ home folder, installs the proper plugin
payload, and supports uninstall and newest-backup restore. The top panel reports
the path and a green, orange, or red plugin state. If VirtualDJ is running, the
installer offers to ask it to close normally. VirtualDJ must be closed for
changes. Installation and uninstall create timestamped snapshots under
`LRC Lyrics Backups`. Update checking is network-silent until requested;
downloads are matched to the application and platform and SHA-256 verified.

The shared top panel shows the same VirtualDJ path and plugin state. Areas are
named **Playlists**, **Local lyrics**, **Online sources**, **Problem queue**, and
**Recovery**. Simple mode contains common synchronization, local tag tools, and
the problem queue. The top-right settings button reveals Advanced mode,
TIDAL/normalization and sidecar restore, update checking, and diagnostics. The
footer shows the last operation, links to its backup when one was created, and
names the exact action for the active tab. Activity messages use distinct colors
for successful work, warnings/skips, and errors. Diagnostic ZIPs exclude music,
tags, credentials, and absolute user paths.

### Sync folders to VDJ

This is the second tab. It creates a direct one-way mirror from the selected
music directory into a named root under VirtualDJ `MyLists`. Both the music
folder and VirtualDJ home folder can be selected manually. Each leaf directory
becomes a list containing its audio files. A directory containing both audio
files and child directories gets an additional `_ Tracks in this folder` list.

Repeated synchronization adds new files and removes playlist references for
files or folders that no longer exist in the source tree. The tool owns only the
selected managed root and does not modify other VirtualDJ lists. If a root with
the same name already exists but was not created by the tool, replacement is
refused unless the adoption option is explicitly enabled.

The optional Search DB setting is add-only. It registers current source tracks
and can reactivate them if VirtualDJ previously marked them hidden or missing.
It does not delete unrelated or now-missing database entries and preserves
existing track metadata and analysis. Newly registered files are not analyzed;
use VirtualDJ if you also want BPM, waveform, stem, or other analysis data.
VirtualDJ keeps a separate database for each drive or volume. The tool routes
Windows tracks to `Drive:\VirtualDJ\database.xml`, macOS external tracks to
`/Volumes/<name>/VirtualDJ/database.xml`, and tracks on the system volume to the
home database. Every database changed by a run is included in the backup and
rollback transaction.

Preview mode scans and compares without writing anything. A real sync requires
VirtualDJ to be closed, writes through a staging directory, keeps a timestamped
snapshot under `Folder Sync Backups`, and rolls back the previous MyLists tree,
root order, and Search DB if replacement fails.

### Import LRC / TXT

Imports same-name sidecars using the existing verified converter. Timed LRC/TXT
is written to one standard `SYLT` frame; plain TXT is written to one standard
`USLT` frame. Obsolete custom TXXX lyric duplicates are removed when lyrics are
rewritten. Optional deletion happens only after the destination tag has
been reopened and verified. A separate option deletes a lower-priority timed TXT
only when a same-name LRC was imported and verified; plain and unverified skipped
TXT files remain untouched. A real run also writes `#sylt` or `#uslt` to the
track's VirtualDJ User 1 field.

### Mark existing lyrics

Scans embedded lyrics and writes `Lyrics: Synced` or `Lyrics: Unsynced` to the
portable ID3 Grouping field. Existing unrelated Grouping content is preserved.
It canonicalizes recognized lyrics to exactly one standard `SYLT` or `USLT`,
removes legacy custom lyric duplicates, then mirrors that classification to
VirtualDJ User 1 as `#sylt` or `#uslt`.
The TIDAL/normalize workflow performs the same synchronization after writing.
VirtualDJ database updates preserve other User 1 values and analysis, register
missing tracks, require VirtualDJ to be closed, and are backed up under
`Lyrics Tag Backups`.

### TIDAL / normalize

For each MP3, the tool tries:

1. A same-name `.lrc` sidecar next to the MP3.
2. TIDAL lookup using the MP3's ISRC (`TSRC`) tag.
3. TIDAL search using artist, title, and duration.
4. Existing embedded USLT frames, when normalization is enabled.

The selected text is stored as USLT. When timestamps are present and the SYLT
option is enabled, a synchronized SYLT frame is also created. Obsolete SYLT
frames are removed whenever USLT is replaced.

### Restore sidecars

Restores LRC files from the structure-preserving backup. See the safety rules
below for handling of older flat backups.

## Backups and safety

Backups preserve the music library's relative directory structure. For example:

```text
Music\Album A\song.mp3  ->  _lrc_backup\Album A\song.lrc
Music\Album B\song.mp3  ->  _lrc_backup\Album B\song.lrc
```

This prevents tracks with the same filename from overwriting one another. The
backup is written completely before the MP3 tag is changed. A source sidecar is
deleted only after both backup and tag writes succeed.

The restore tab uses structured backups. It can also read backups made by the
older flat format, but only when the matching MP3 basename is unique throughout
the library. Ambiguous legacy backups are skipped.

### Problem queue

Scans locally for MP3 files without lyrics, invalid LRC files, unreadable tags,
and LRC/TXT sidecars without a matching MP3. Results are not sent online and can
be exported with paths relative to the selected library.

In a source checkout, a successful release build leaves only a ZIP and checksum.
The GUI extracts only the three verified plugin payload files from that ZIP into
`%LOCALAPPDATA%\VirtualDJEmbeddedLyrics\InstallerPayload`; it does not recreate
a duplicate release tree inside the repository.

Dry-run does not write MP3 files, sidecars, backups, OAuth credentials, or the
CSV report. GUI preferences are still saved as application settings.

## Runtime data

GUI state is stored outside the repository under:

```text
%LOCALAPPDATA%\VirtualDJEmbeddedLyrics\LyricsTools
```

This directory contains:

- `gui_settings.json` — GUI preferences and last selected folders
- `lyrics_report.csv` — the latest non-dry-run processing report
- `tidal_session.json` — OAuth access and refresh credentials

Installer payload files used by a source checkout are stored separately under
`%LOCALAPPDATA%\VirtualDJEmbeddedLyrics\InstallerPayload`.

Treat `tidal_session.json` as a password. Do not share or commit it. Delete it to
force a fresh TIDAL login.

## Limitations

`tidalapi` is an unofficial TIDAL client and may stop working when TIDAL changes
its services. Search results are filtered by artist, title, and duration, but any
downloaded lyrics should still be reviewed. Per-file failures are recorded and
do not stop the remainder of a batch.

VirtualDJ does not provide a supported external API for adding files to Search
DB. This tool therefore validates the XML, requires VirtualDJ to be closed,
writes atomically, and backs up every changed per-drive database. Keep the Search
DB option disabled if you prefer to add files from inside VirtualDJ itself.
