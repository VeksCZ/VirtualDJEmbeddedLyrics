# Helper sources for the plugin build (`helper/`)

This folder holds only the Python that is built into the plugin package:

- `lrc_helper.py` is built into `LrcHelper` (Windows `.exe`, macOS binary). The plugins start it
  to save tags and search lyrics online without a system Python.
- `lyrics_tag_converter.py` is also shipped as `EmbeddedLyricsTagWriter.py`.
- `lyrics_lookup.py` and `lrc_tool.py` are their dependencies.

**These four Python files are a snapshot.** Their source of truth is `lyricstools/` in the
DJ Music Manager repository (VeksCZ/DJMusicManager). Change them there first, then copy them here with
`./sync-from-manager.ps1` (add `-Check` to only compare, which is what CI-style checks should use) and release the plugin. The lyrics tools (download, import, problems, restore, MyLists sync,
plugin installer, diagnostics) are part of DJ Music Manager, not of this repository.

The tests in `tests/test_lyrics_*.py` cover these four files and run in every build.
