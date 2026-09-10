# LRC Lyrics for VirtualDJ {{VERSION}}

## Highlights

- Standardized new MP3 lyrics storage on one ID3v2 `SYLT` frame for timed lyrics
  or one `USLT` frame for plain lyrics. Legacy custom TXXX duplicates remain
  readable but are removed when LyricsTools rewrites a track.
- Added an optional solid background to LRC Master's Advanced settings, with
  black and additional color choices; transparency remains the default and the
  top/bottom fade now remains active over solid backgrounds.
- Added a live font, color, backdrop, and background preview to Advanced settings.
  Custom appearance values are now stored independently from built-in presets.
- LRC Master now tags loaded synchronized lyrics as `#sylt` and untimed lyrics
  as `#uslt` in VirtualDJ User 1, replacing the older generic `#lrc` marker.
- LyricsTools now performs the same `#sylt/#uslt` update directly in VirtualDJ's
  per-volume databases after MP3-writing workflows, with backups and without
  discarding unrelated User 1 values or track analysis.
- Refined LyricsTools with a top-right settings dialog, action-specific run
  buttons, a Problems scrollbar, and color-coded Activity messages.
- Added a separate conservative option to delete redundant timed TXT sidecars
  only after a preferred same-name LRC was imported and verified.
- Added a backed-up **Reset video window layout** action that removes only
  VirtualDJ's remembered external-video window geometry.
- Unified plugin setup and all lyrics/library operations in one **LyricsTools**
  application. Plugin installation and update is now the first tab.
- Simplified release packages: Windows shows `LyricsTools.exe` at the root and
  keeps support files in `_internal`; macOS keeps them inside `LyricsTools.app`.
- Removed the separate LRC Plugin Setup application and downloads.
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
- Replaced the separate Windows PowerShell/CMD installer stack with the same
  native Python installation engine used by the packaged LyricsTools GUI. The
  release now contains only the application, a short README, version metadata,
  and the three required plugin payload files.

## Installation

- Windows: extract `LyricsTools-Windows-v{{VERSION}}.zip` and open
  `LyricsTools.exe`.
- macOS: extract the matching `LyricsTools-macOS-AppleSilicon` or
  `LyricsTools-macOS-Intel` ZIP and open `LyricsTools.app`.
- In LyricsTools, use the first **Plugin** tab to install or update the VirtualDJ
  plugin. The app offers to close VirtualDJ normally when required.

Restart VirtualDJ and enable LRC Master under Video Overlays. LRC BlackOut
remains optional.
