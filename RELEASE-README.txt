LRC Lyrics for VirtualDJ {{VERSION}} - Windows 64-bit
=====================================================

QUICK INSTALL
-------------

1. Extract the complete ZIP.
2. Close VirtualDJ completely.
3. Double-click LRCPluginSetup.exe.
4. Confirm the VirtualDJ folder and click Install / update.
5. Start VirtualDJ.
6. Enable LRC Master under Video Overlays. Enable LRC BlackOut only when a
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

INCLUDED APPLICATIONS
---------------------

LRCPluginSetup.exe handles plugin installation, update, removal and backup
restore. LyricsTools.exe provides tabs to import LRC/TXT,
mark existing lyrics in ID3 Grouping, retrieve or normalize lyrics with safe
structured backups, and restore LRC sidecars.
It can also mirror a music directory hierarchy directly into one isolated
VirtualDJ MyLists root, so Serato crates are not required. The folder sync starts
in preview mode, protects unrelated lists, and creates a backup before changes.
Both applications display the detected VirtualDJ path and plugin status. They
also provide on-demand update checks and privacy-safe diagnostic ZIP files.
LyricsTools starts in Simple mode; enable Advanced for TIDAL/normalization and
restore tools.

Preview mode is enabled on first use. Both applications include their runtime;
Python is not required.

UPDATE / REMOVE
---------------

Close VirtualDJ and use LRCPluginSetup.exe to update, remove, or restore.

Full documentation:
https://github.com/VeksCZ/VirtualDJEmbeddedLyrics
