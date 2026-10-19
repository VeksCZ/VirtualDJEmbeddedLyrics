#!/usr/bin/env python3
"""Headless single-track lyrics lookup used by the VirtualDJ plugin's lyrics dialog.

Sources are tried in the same order as the LyricsTools "Online" tab: TIDAL by ISRC,
TIDAL search by artist/title/duration, then LRCLIB. Nothing is written to the MP3;
the plugin shows the result and lets the user decide whether to save it.

The result file is UTF-8: ``key=value`` header lines, one blank line, then the
lyrics text. Header values never contain line breaks.
"""

from __future__ import annotations

import sys
from collections.abc import Callable
from pathlib import Path

import lrc_tool


def _single_line(value: object) -> str:
    return " ".join(str(value or "").split())


def _format_duration(seconds: float | None) -> str:
    if seconds is None:
        return ""
    total = int(round(float(seconds)))
    return f"{total // 60}:{total % 60:02d}"


def lookup(mp3_path: Path, *, session_file: Path, duration_tolerance: float = 3.0,
           tidal_factory: Callable[..., object] | None = None,
           lrclib_factory: Callable[[], object] | None = None) -> dict[str, str]:
    """Find lyrics for one MP3 and describe where they came from."""
    result = {"status": "not_found", "source": "", "synchronized": "0", "matched": "",
              "query": "", "message": "", "text": ""}
    artist, title, isrc, duration, error = lrc_tool.read_tags(mp3_path)
    if error:
        result.update(status="error", message=error)
        return result
    query = " - ".join(part for part in (artist, title) if part)
    if duration:
        query += f" ({_format_duration(duration)})"
    if isrc:
        query += f", ISRC {isrc}"
    result["query"] = query
    if not title:
        result.update(status="error", message="The MP3 has no title tag to search for.")
        return result

    notes: list[str] = []
    text, source, matched = None, "", ""
    make_tidal = tidal_factory or (lambda: lrc_tool.TidalClient(
        session_file, interactive=False, log=lambda _message: None))
    try:
        tidal = make_tidal()
    except lrc_tool.TidalClient.LoginRequired as exc:
        tidal = None
        result["status"] = "login_required"
        notes.append(str(exc))
    except Exception as exc:  # network, tidalapi missing, broken session...
        tidal = None
        notes.append(f"TIDAL unavailable: {exc}")

    if tidal is not None:
        track = tidal.find_by_isrc(isrc) if isrc else None
        source = "TIDAL (ISRC)" if track else ""
        if not track:
            track = tidal.find_by_search(artist, title, duration, duration_tolerance)
            source = "TIDAL (search)" if track else ""
        if track:
            matched = (f"{tidal.candidate_artist(track)} - {getattr(track, 'name', '')}"
                       f" ({_format_duration(getattr(track, 'duration', None))})")
            text = tidal.fetch_lrc(track)
            if not text:
                notes.append("TIDAL has the track but no lyrics"
                             + (f" ({tidal.last_lyrics_error})" if tidal.last_lyrics_error else ""))
        else:
            notes.append("TIDAL: track not found")

    if not text:
        lrclib = (lrclib_factory or lrc_tool.LrclibClient)()
        text, lrclib_status = lrclib.find_lyrics(artist, title, duration, duration_tolerance)
        if text:
            source, matched = lrclib_status, ""
        elif getattr(lrclib, "last_error", ""):
            notes.append(f"LRCLIB unavailable ({lrclib.last_error})")
        else:
            notes.append("LRCLIB: not found")

    if text:
        text = lrc_tool.strip_lang_prefix(text).strip()
        result.update(status="found", source=source, matched=matched, text=text,
                      synchronized="1" if lrc_tool.LRC_TIMESTAMP_RE.search(text) else "0")
    result["message"] = "; ".join(notes)
    return result


def write_result(path: Path, result: dict[str, str]) -> None:
    header = "".join(f"{key}={_single_line(result.get(key, ''))}\n"
                     for key in ("status", "source", "synchronized", "matched", "query", "message"))
    lrc_tool.atomic_write_text(path, header + "\n" + result.get("text", ""))


def main(arguments: list[str]) -> int:
    if len(arguments) != 2:
        print("usage: lyrics_lookup.py MP3 RESULT_FILE", file=sys.stderr)
        return 2
    mp3_path, result_path = Path(arguments[0]), Path(arguments[1])
    session_file = lrc_tool.default_runtime_dir() / "tidal_session.json"
    try:
        result = lookup(mp3_path, session_file=session_file)
    except Exception as exc:  # never leave the plugin waiting without an answer
        result = {"status": "error", "message": f"{type(exc).__name__}: {exc}"}
    write_result(result_path, result)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
