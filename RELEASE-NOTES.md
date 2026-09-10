# LRC Lyrics for VirtualDJ {{VERSION}}

## Highlights

- Added an optional solid background to LRC Master's Advanced settings, with
  black and additional color choices; transparency remains the default and the
  top/bottom fade now remains active over solid backgrounds.
- LRC Master now tags loaded synchronized lyrics as `#sylt` and untimed lyrics
  as `#uslt` in VirtualDJ User 1, replacing the older generic `#lrc` marker.
- Refined LyricsTools with a top-right settings dialog, action-specific run
  buttons, a Problems scrollbar, and color-coded Activity messages.
- Added a separate conservative option to delete redundant timed TXT sidecars
  only after a preferred same-name LRC was imported and verified.
- Added a backed-up **Reset video window layout** action that removes only
  VirtualDJ's remembered external-video window geometry.
- Split releases into dedicated **LRC Plugin Setup** and **LyricsTools**
  downloads for every supported platform and architecture.
- Added opt-in automatic updates with SHA-256 verification and safe extraction.
- Added a local problem queue with CSV export.
- Added an option to ask a running VirtualDJ instance to close normally before
  installation; the installer never force-terminates it.
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

- Windows: extract `LRC-Plugin-Setup-Windows-v{{VERSION}}.zip` and open
  `LRCPluginSetup.exe`.
- macOS: extract the matching `LRC-Plugin-Setup-macOS-AppleSilicon` or
  `LRC-Plugin-Setup-macOS-Intel` ZIP, close VirtualDJ,
  and open `LRCPluginSetup.app`.

Restart VirtualDJ and enable LRC Master under Video Overlays. LRC BlackOut
remains optional.
