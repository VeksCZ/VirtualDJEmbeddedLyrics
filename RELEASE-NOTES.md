# LRC Lyrics for VirtualDJ {{VERSION}}

## Highlights

- Reordered the GUI so VirtualDJ setup is the first tab and folder-to-playlist
  synchronization is the second tab.
- Kept the tab bar at a stable vertical position and added matching manual
  VirtualDJ folder selection to both setup and synchronization.
- Upgraded folder synchronization to a full managed-root mirror that adds new
  tracks and removes playlist references that disappeared from the source tree.
- Added optional, add-only Search DB registration while preserving unrelated
  entries, existing metadata and VirtualDJ analysis.
- Correctly routes Search DB updates to VirtualDJ's database for each track's
  drive, including databases such as `D:\VirtualDJ\database.xml`.
- Added XML validation, atomic writes, per-database backups and transactional
  rollback. Preview scans avoid unnecessary metadata reads on large libraries.

## Installation

Download and extract `LRC-Lyrics-VirtualDJ-v{{VERSION}}.zip`, close VirtualDJ,
then run `Install.cmd`. Restart VirtualDJ and enable LRC Master under Video
Overlays. LRC BlackOut remains optional.
