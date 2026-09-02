LRC Lyrics for VirtualDJ {{VERSION}} - Windows 64-bit
=====================================================

QUICK INSTALL
-------------

1. Extract the complete ZIP.
2. Close VirtualDJ completely.
3. Double-click Install.cmd.
4. Start VirtualDJ.
5. Enable LRC Master under Video Overlays. Enable LRC BlackOut only when a
   solid black background is wanted.

The installer supports current AppData installations, legacy Documents
installations and custom VirtualDJ HomeFolder locations. If it asks for the
folder, open VirtualDJ and use Settings > Options > cog button to locate the
active home folder, then close VirtualDJ and paste that path into the installer.

No administrator access, build tools or Python are required for lyrics display.
Replaced files are backed up inside the selected VirtualDJ home folder.

BEHAVIOR
--------

- LRC Master reads embedded SYLT, synchronized/unsynchronized TXXX and USLT,
  plus same-name .lrc and .txt files.
- Tracks without supported lyrics display ... in the master output.
- Auto-tag #lrc adds #lrc to VirtualDJ User 1 without replacing existing data.
- LRC BlackOut is an optional black background processed before later overlays.

OPTIONAL TOOLS
--------------

Run LyricsTools.cmd from the extracted release root to open one GUI with
tabs to import LRC/TXT,
mark existing lyrics in ID3 Grouping, retrieve or normalize lyrics with safe
structured backups, restore LRC sidecars, and manage the VirtualDJ plugin files.
It can also mirror a music directory hierarchy directly into one isolated
VirtualDJ MyLists root, so Serato crates are not required. The folder sync starts
in preview mode, protects unrelated lists, and creates a backup before changes.
The VirtualDJ setup tab can detect or select the active home folder and run the
same verified install, uninstall and backup-restore operations as the root
launchers. Drag a music folder onto LyricsTools.cmd or start it normally
and choose a folder. The Tools directory contains implementation files and does
not contain additional launchers.

Preview mode is enabled on first use. The optional tools require Python 3; their
launcher can install the required Mutagen and tidalapi packages after asking for
permission. Python is not required for normal plugin playback.

UPDATE / REMOVE
---------------

Run Install.cmd again to update. Close VirtualDJ first.
Run Uninstall.cmd to remove the installed files safely.
Run Restore-Backup.cmd to select and restore a previous installer snapshot.

Full documentation:
https://github.com/VeksCZ/VirtualDJEmbeddedLyrics
