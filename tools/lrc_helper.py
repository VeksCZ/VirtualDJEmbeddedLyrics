#!/usr/bin/env python3
"""Windowless helper the VirtualDJ plugins start for work that needs Python.

Built as LrcHelper.exe and installed next to the plugin DLLs, so the plugins need
no system Python, no mutagen and no particular LyricsTools location:

  LrcHelper.exe --lookup-lyrics MP3 RESULT_FILE          online lyrics search
  LrcHelper.exe --write-editor-text MP3 TEXT timed|untimed
  LrcHelper.exe --write-recording MP3 TIMING_FILE
"""

from __future__ import annotations

import sys

import lyrics_lookup
import lyrics_tag_converter


def main() -> int:
    if len(sys.argv) >= 2 and sys.argv[1] == "--lookup-lyrics":
        return lyrics_lookup.main(sys.argv[2:])
    if len(sys.argv) >= 2 and sys.argv[1] in ("--write-editor-text", "--write-recording"):
        return lyrics_tag_converter.main()
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
