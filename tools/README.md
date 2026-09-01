# MP3 & Lyrics Tools

This optional Windows suite provides one GUI for importing LRC/TXT sidecars,
marking existing embedded lyrics, normalizing or retrieving lyrics, and restoring
structure-preserving backups. It is independent of normal plugin playback.

## Requirements

- Windows 10 or newer
- Python 3.10 or newer with `python` and `pythonw` available in `PATH`
- A TIDAL account when TIDAL lookup is enabled

From the repository root, install the pinned dependency range into the same
Python installation used by the launchers:

```powershell
python -m pip install --user -r requirements.txt
```

## Start the GUI

- Double-click `MP3 & Lyrics Tools.cmd` for visible diagnostics. The launcher can
  install missing Python packages after asking for permission.
- Double-click `MP3 & Lyrics Tools Silent.vbs` to run without a console window.
- Drag a music folder onto either launcher to select it immediately.
- From an Explorer address bar, enter the full path to either launcher while the
  desired music folder is open.

When a folder is supplied to a launcher, both the music library and its default
`_lrc_backup` folder are selected together. Saved settings never redirect a new
library into a previous library's backup.

The GUI starts in dry-run mode on first use. Review the activity log before
turning dry-run off.

## Tabs

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

Treat `tidal_session.json` as a password. Do not share or commit it. Delete it to
force a fresh TIDAL login.

## Command line

Embed or normalize without contacting TIDAL:

```powershell
python tools\embed_and_backup.py "D:\Music" "D:\Music\_lrc_backup" --no-tidal --dry-run
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
