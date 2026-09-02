# MP3 & Lyrics Tools

This optional Windows suite provides one GUI for importing LRC/TXT sidecars,
marking existing embedded lyrics, normalizing or retrieving lyrics, restoring
structure-preserving backups, mirroring folders into VirtualDJ lists, and
managing the VirtualDJ plugin installation.

## Requirements

- Windows 10 or newer
- Python 3.10 or newer with `python` and `pythonw` available in `PATH`
- A TIDAL account when TIDAL lookup is enabled

From the repository root, install the pinned dependency range into the same
Python installation used by the launcher:

```powershell
python -m pip install --user -r requirements.txt
```

## Start the GUI

- Double-click the single `LyricsTools.cmd` in the repository or extracted
  release root. It can install missing Python packages after asking permission.
- Drag a music folder onto that launcher to select it immediately.
- From an Explorer address bar, enter its full path while the desired music
  folder is open.

When a folder is supplied to a launcher, both the music library and its default
`_lrc_backup` folder are selected together. Saved settings never redirect a new
library into a previous library's backup.

The GUI starts in dry-run mode on first use. Review the activity log before
turning dry-run off.

## Tabs

### VirtualDJ setup

This is the first tab. It finds the active VirtualDJ home folder from
VirtualDJ's registry setting and the standard current or legacy locations. You
can also type a path or browse to a custom folder; the GUI validates it before
enabling an operation.

The tab can install or update the bundled LRC Master and LRC BlackOut DLLs,
uninstall them, or restore the newest installer backup. It calls the same
PowerShell scripts as the root `Install.cmd`, `Uninstall.cmd`, and
`Restore-Backup.cmd` launchers. VirtualDJ must be completely closed. Install and
uninstall operations create a timestamped snapshot under `LRC Lyrics Backups`
before changing files.

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
VirtualDJ keeps a separate database for each drive, so the tool routes tracks on
additional drives to `Drive:\VirtualDJ\database.xml` and tracks on the home drive
to the home database. Every database changed by a run is included in the backup
and rollback transaction.

Preview mode scans and compares without writing anything. A real sync requires
VirtualDJ to be closed, writes through a staging directory, keeps a timestamped
snapshot under `Folder Sync Backups`, and rolls back the previous MyLists tree,
root order, and Search DB if replacement fails.

### Import LRC / TXT

Imports same-name sidecars using the existing verified converter. Timed LRC/TXT
is written to SYLT and `SYNCEDLYRICS`; plain TXT is written to USLT and
`UNSYNCEDLYRICS`. Optional deletion happens only after the destination tags have
been reopened and verified.

### Mark existing lyrics

Scans embedded lyrics and writes `Lyrics: Synced` or `Lyrics: Unsynced` to the
portable ID3 Grouping field. Existing unrelated Grouping content is preserved.

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

## Command line

Embed or normalize without contacting TIDAL:

```powershell
python tools\lrc_tool.py "D:\Music" "D:\Music\_lrc_backup" --no-tidal --dry-run
```

Restore sidecars in preview mode:

```powershell
python tools\restore_lrc.py "D:\Music" "D:\Music\_lrc_backup" --dry-run
```

Remove `--dry-run` only after checking the output. Use `--help` to see all
options.

## Limitations

`tidalapi` is an unofficial TIDAL client and may stop working when TIDAL changes
its services. Search results are filtered by artist, title, and duration, but any
downloaded lyrics should still be reviewed. Per-file failures are recorded and
do not stop the remainder of a batch.

VirtualDJ does not provide a supported external API for adding files to Search
DB. This tool therefore validates the XML, requires VirtualDJ to be closed,
writes atomically, and backs up every changed per-drive database. Keep the Search
DB option disabled if you prefer to add files from inside VirtualDJ itself.
