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
- The unsupported experimental LRC Deck is not included.

OPTIONAL TOOLS
--------------

The Tools folder can import LRC/TXT into MP3 tags or mark MP3 files that already
contain lyrics. Drag a music folder onto the relevant CMD file. These optional
tools require Python 3 and Mutagen; the launcher can install Mutagen when needed.

UPDATE / REMOVE
---------------

Run Install.cmd again to update. Close VirtualDJ first.
Run Uninstall.cmd to remove the installed files safely.
Run Restore Backup.cmd to select and restore a previous installer snapshot.

Full documentation:
https://github.com/VeksCZ/VirtualDJEmbeddedLyrics
