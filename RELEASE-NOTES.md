# LRC Lyrics for VirtualDJ {{VERSION}}

## Highlights

- Added one unified **MP3 & Lyrics Tools** GUI with separate tabs for importing
  LRC/TXT, marking existing lyrics, TIDAL retrieval/normalization and restoring
  sidecars.
- Added structure-preserving LRC backups, atomic writes and safe compatibility
  handling for older flat backups.
- Added correct TIDAL ISRC lookup and stricter artist/title/duration matching.
- Added a real no-write preview mode, stale-SYLT cleanup and support for multiple
  LRC timestamps on one line.
- Added persistent GUI settings, browser-assisted OAuth login, runtime data
  outside the release folder and automatic dependency setup in the main launcher.
- Kept the previous Import Lyrics and Mark Existing Lyrics launcher names as
  shortcuts to the appropriate GUI tabs.

## Installation

Download and extract `LRC-Lyrics-VirtualDJ-v{{VERSION}}.zip`, close VirtualDJ,
then run `Install.cmd`. Restart VirtualDJ and enable LRC Master under Video
Overlays. LRC BlackOut remains optional.

The unsupported experimental LRC Deck is not included in this release.
