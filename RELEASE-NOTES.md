# LRC Lyrics for VirtualDJ {{VERSION}}

## Highlights

- Added direct, preview-first mirroring of a music directory into one protected
  VirtualDJ MyLists root. New files are added, removed files disappear, and
  unrelated lists remain untouched.
- Added a VirtualDJ setup tab that detects custom and standard home folders and
  runs the verified install, update, uninstall and backup-restore operations.
- Reduced the optional tools to one obvious `LyricsTools.cmd` launcher and one
  tabbed GUI. Duplicate launchers and wrappers were removed.
- Renamed the plugin files to `LRCMaster.dll` and `LRCBlackOut.dll`. The
  installer backs up and removes the previous spaced filenames during update.
- Moved the VirtualDJ SDK under `tools/sdk`, consolidated the regression tests,
  and removed the unused experimental LRC Deck build path.
- Release builds now remove compiler and staging output automatically and leave
  only the ZIP and its SHA-256 checksum.

## Installation

Download and extract `LRC-Lyrics-VirtualDJ-v{{VERSION}}.zip`, close VirtualDJ,
then run `Install.cmd`. Restart VirtualDJ and enable LRC Master under Video
Overlays. LRC BlackOut remains optional.
