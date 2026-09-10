"""Local-only scanner for music files that need lyrics attention."""

from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import lrc_tool
import lyrics_tag_converter


@dataclass(frozen=True)
class LyricsIssue:
    kind: str
    path: Path
    detail: str


def has_embedded_lyrics(mp3: Path) -> bool:
    _encoding, ID3, ID3NoHeaderError, _sylt, _tit1, _txxx, _uslt = lyrics_tag_converter.load_mutagen()
    try:
        tags = ID3(mp3)
    except ID3NoHeaderError:
        return False
    if lyrics_tag_converter.embedded_lyrics_kind(tags):
        return True
    return any(
        str(getattr(frame, "desc", "")).upper() in {"LYRICS", "USLT"}
        and any(str(value).strip() for value in getattr(frame, "text", []))
        for frame in tags.getall("TXXX")
    )


def scan_library(root: Path, log: Callable[[object], None] = print) -> list[LyricsIssue]:
    root = root.expanduser().resolve()
    if not root.is_dir():
        raise ValueError(f"Music library does not exist: {root}")
    issues: list[LyricsIssue] = []
    pairs, unmatched = lyrics_tag_converter.discover_pairs(root)
    pair_by_mp3 = {mp3.resolve(): (lrc_path, txt_path) for mp3, lrc_path, txt_path in pairs}
    for sidecar in unmatched:
        issues.append(LyricsIssue("Unmatched sidecar", sidecar, "No same-name MP3 file was found."))
    if root.is_file():
        mp3_files = [root] if root.suffix.casefold() == ".mp3" else []
    else:
        mp3_files = sorted(
            (path for path in root.rglob("*") if path.is_file() and path.suffix.casefold() == ".mp3"),
            key=lambda path: str(path).casefold(),
        )
    for mp3 in mp3_files:
        lrc_path, txt_path = pair_by_mp3.get(mp3.resolve(), (None, None))
        _artist, _title, _isrc, _duration, error = lrc_tool.read_tags(mp3)
        if error:
            issues.append(LyricsIssue("Unreadable MP3", mp3, error))
            continue
        if lrc_path is not None:
            try:
                if not lyrics_tag_converter.parse_lrc(lrc_path):
                    issues.append(LyricsIssue("Invalid LRC", lrc_path, "No valid timestamped lines were found."))
            except Exception as exc:
                issues.append(LyricsIssue("Invalid LRC", lrc_path, str(exc)))
        try:
            embedded = has_embedded_lyrics(mp3)
        except Exception as exc:
            issues.append(LyricsIssue("Unreadable lyrics tags", mp3, str(exc)))
            continue
        if not embedded and lrc_path is None and txt_path is None:
            issues.append(LyricsIssue("Missing lyrics", mp3, "No embedded lyrics or LRC/TXT sidecar was found."))
    issues.sort(key=lambda item: (item.kind.casefold(), str(item.path).casefold()))
    log(f"Problem scan complete: {len(issues)} item(s) need attention.")
    return issues


def write_report(path: Path, issues: list[LyricsIssue], root: Path) -> Path:
    path = path.expanduser().resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    root = root.expanduser().resolve()
    temporary = path.with_name(f".{path.name}.tmp")
    try:
        with temporary.open("w", encoding="utf-8-sig", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(("Problem", "File", "Detail"))
            for issue in issues:
                try:
                    display = issue.path.resolve().relative_to(root)
                except ValueError:
                    display = issue.path.name
                writer.writerow((issue.kind, str(display), issue.detail))
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)
    return path
