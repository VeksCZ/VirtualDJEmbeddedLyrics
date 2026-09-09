# LRC Lyrics for VirtualDJ {{VERSION}}

## Highlights

- Added native Metal builds of LRC Master and LRC BlackOut for macOS.
- Added clearly named macOS packages for Apple Silicon and Intel. Both contain
  universal VirtualDJ plugin bundles; the standalone GUI runtime matches the
  architecture in the ZIP name.
- Added a native macOS installer with automatic `PluginsArm`/`Plugins64`
  selection, timestamped backups, uninstall, and restore.
- Added **LyricsTools.app**, which opens the unified GUI without a Terminal
  window and supports lyrics maintenance, managed MyLists synchronization, and
  add-only Search DB registration on macOS.
- Kept the Windows package and workflow unchanged apart from an explicit
  `Windows` label in its filename.

## Installation

- Windows: extract `LRC-Lyrics-VirtualDJ-Windows-v{{VERSION}}.zip`, close
  VirtualDJ, and run `Install.cmd`.
- macOS: extract the `macOS-AppleSilicon` or `macOS-Intel` ZIP, close VirtualDJ,
  and open `LRCPluginSetup.app`.

Restart VirtualDJ and enable LRC Master under Video Overlays. LRC BlackOut
remains optional.
