# LRC Lyrics for VirtualDJ {{VERSION}}

## Highlights

- Added an optional solid background directly to LRC Master, with black and
  additional color choices; transparency remains the default.
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
