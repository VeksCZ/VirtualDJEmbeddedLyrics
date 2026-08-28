#!/usr/bin/env python3
"""Find same-name MP3/LRC/TXT files and import lyrics into standard ID3 frames."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


LINE_TIMESTAMP = re.compile(r"\[(?P<minutes>\d{1,3}):(?P<seconds>\d{2})[.:](?P<fraction>\d{1,3})\]")
WORD_TIMESTAMP = re.compile(r"<\d{1,3}:\d{2}[.:]\d{1,3}>")
UNTIMED_TIMESTAMP = re.compile(
    r"^\s*(?:[\[(]\d{1,3}:\d{2}(?:[.:]\d{1,3})?[\])]\s*)+"
)
LRC_METADATA = re.compile(r"^\s*\[[a-zA-Z]{1,8}:.*]\s*$")


@dataclass(frozen=True)
class TimedLine:
    text: str
    time_ms: int


def decode_text_file(path: Path) -> str:
    data = path.read_bytes()
    for encoding in ("utf-8-sig", "utf-16", "cp1250"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", errors="replace")


def sanitize_untimed_text(text: str) -> str:
    """Remove LRC timing/metadata while preserving ordinary lyric line breaks."""
    lines: list[str] = []
    for raw_line in text.splitlines():
        if LRC_METADATA.match(raw_line):
            continue
        line = UNTIMED_TIMESTAMP.sub("", raw_line)
        line = WORD_TIMESTAMP.sub("", line).strip()
        lines.append(line)
    return "\n".join(lines).strip()


def timestamp_ms(match: re.Match[str]) -> int:
    fraction = match.group("fraction")
    milliseconds = int(fraction.ljust(3, "0")[:3])
    return (int(match.group("minutes")) * 60 + int(match.group("seconds"))) * 1000 + milliseconds


def parse_lrc(path: Path) -> list[TimedLine]:
    result: list[TimedLine] = []
    for raw_line in decode_text_file(path).splitlines():
        matches = list(LINE_TIMESTAMP.finditer(raw_line))
        if not matches:
            continue
        text = WORD_TIMESTAMP.sub("", raw_line[matches[-1].end():]).strip()
        if text:
            result.extend(TimedLine(text, timestamp_ms(match)) for match in matches)
    result.sort(key=lambda line: line.time_ms)
    return result


def find_sidecar(mp3_path: Path, extension: str) -> Path | None:
    expected_name = mp3_path.stem.casefold() + extension.casefold()
    for candidate in mp3_path.parent.iterdir():
        if candidate.is_file() and candidate.name.casefold() == expected_name:
            return candidate
    return None


def find_matching_mp3(sidecar_path: Path) -> Path | None:
    expected_stem = sidecar_path.stem.casefold()
    for candidate in sidecar_path.parent.iterdir():
        if (candidate.is_file() and candidate.suffix.casefold() == ".mp3"
                and candidate.stem.casefold() == expected_stem):
            return candidate
    return None


def discover_pairs(root: Path) -> tuple[list[tuple[Path, Path | None, Path | None]], list[Path]]:
    """Return MP3 targets with matching sidecars and sidecars without an MP3."""
    if root.is_file() and root.suffix.casefold() == ".mp3":
        lrc = find_sidecar(root, ".lrc")
        txt = find_sidecar(root, ".txt")
        return ([(root, lrc, txt)] if lrc or txt else []), []

    if root.is_file() and root.suffix.casefold() in (".lrc", ".txt"):
        mp3 = find_matching_mp3(root)
        if not mp3:
            return [], [root]
        return [(mp3, find_sidecar(mp3, ".lrc"), find_sidecar(mp3, ".txt"))], []

    sidecars = sorted(
        (path for path in root.rglob("*")
         if path.is_file() and path.suffix.casefold() in (".lrc", ".txt")),
        key=lambda path: str(path).casefold(),
    )
    pairs: dict[Path, tuple[Path | None, Path | None]] = {}
    unmatched: list[Path] = []
    for sidecar in sidecars:
        mp3 = find_matching_mp3(sidecar)
        if not mp3:
            unmatched.append(sidecar)
            continue
        lrc, txt = pairs.get(mp3, (None, None))
        if sidecar.suffix.casefold() == ".lrc":
            lrc = sidecar
        else:
            txt = sidecar
        pairs[mp3] = (lrc, txt)
    return [(mp3, sidecars[0], sidecars[1]) for mp3, sidecars in sorted(
        pairs.items(), key=lambda item: str(item[0]).casefold())], unmatched


def load_mutagen():
    try:
        from mutagen.id3 import Encoding, ID3, ID3NoHeaderError, SYLT, TXXX, USLT
    except ImportError:
        print("Missing dependency. Install it with: py -m pip install mutagen", file=sys.stderr)
        raise SystemExit(2)
    return Encoding, ID3, ID3NoHeaderError, SYLT, TXXX, USLT


def write_frames(mp3_path: Path, lrc_path: Path | None, txt_path: Path | None,
                 language: str, overwrite: bool, delete_lrc: bool = False,
                 delete_sidecars: bool = False) -> tuple[str, bool]:
    Encoding, ID3, ID3NoHeaderError, SYLT, TXXX, USLT = load_mutagen()
    try:
        tags = ID3(mp3_path)
    except ID3NoHeaderError:
        tags = ID3()
    changed = False
    lrc_written = False
    txt_written = False
    messages: list[str] = []
    if lrc_path:
        timed_lines = parse_lrc(lrc_path)
        if not timed_lines:
            messages.append("LRC has no timed lines")
        else:
            synced_txxx = [frame for frame in tags.getall("TXXX")
                           if frame.desc.upper() == "SYNCEDLYRICS"
                           and any(str(value).strip() for value in frame.text)]
            if (tags.getall("SYLT") or synced_txxx) and not overwrite:
                messages.append("synchronized lyrics exist (skipped)")
            else:
                if overwrite:
                    tags.delall("SYLT")
                    tags.delall("TXXX:SYNCEDLYRICS")
                descriptor = "Imported from LRC by VirtualDJ Embedded Lyrics"
                tags.add(SYLT(encoding=Encoding.UTF16, lang=language, format=2, type=1,
                              desc=descriptor,
                              text=[(line.text, line.time_ms) for line in timed_lines]))
                formatted = ["[re:VirtualDJ Embedded Lyrics - imported from LRC]"]
                for line in timed_lines:
                    minutes, remainder = divmod(line.time_ms, 60000)
                    seconds, millis = divmod(remainder, 1000)
                    formatted.append(f"[{minutes:02d}:{seconds:02d}.{millis:03d}]{line.text}")
                tags.add(TXXX(encoding=Encoding.UTF16, desc="SYNCEDLYRICS",
                              text=["\n".join(formatted)]))
                changed = True
                lrc_written = True
                messages.append(f"SYLT + SYNCEDLYRICS {len(timed_lines)} lines")
    if txt_path:
        plain_text = sanitize_untimed_text(decode_text_file(txt_path))
        matching_uslt = [frame for frame in tags.getall("USLT") if frame.lang == language]
        matching_txxx = [frame for frame in tags.getall("TXXX")
                         if frame.desc.upper() == "UNSYNCEDLYRICS"
                         and any(str(value).strip() for value in frame.text)]
        if not plain_text:
            messages.append("TXT is empty")
        elif (matching_uslt or matching_txxx) and not overwrite:
            messages.append("unsynchronized lyrics exist (skipped)")
        else:
            if overwrite:
                tags.delall("USLT")
                tags.delall("TXXX:UNSYNCEDLYRICS")
            tags.add(USLT(encoding=Encoding.UTF16, lang=language,
                          desc="Imported from TXT", text=plain_text))
            tags.add(TXXX(encoding=Encoding.UTF16, desc="UNSYNCEDLYRICS",
                          text=[plain_text]))
            changed = True
            txt_written = True
            messages.append("USLT + UNSYNCEDLYRICS from TXT")
    if changed:
        version = tags.version[1] if tags.version and tags.version[1] in (3, 4) else 3
        tags.save(mp3_path, v2_version=version)
    if changed:
        verify = ID3(mp3_path)
        if lrc_written:
            has_sylt = bool(verify.getall("SYLT"))
            has_txxx = any(frame.desc.upper() == "SYNCEDLYRICS" and frame.text
                           for frame in verify.getall("TXXX"))
            if not has_sylt or not has_txxx:
                raise RuntimeError("synchronized lyrics verification failed")
        if txt_written:
            has_uslt = any(frame.lang == language and frame.text.strip()
                           for frame in verify.getall("USLT"))
            has_unsynced_txxx = any(frame.desc.upper() == "UNSYNCEDLYRICS" and frame.text
                                    for frame in verify.getall("TXXX"))
            if not has_uslt or not has_unsynced_txxx:
                raise RuntimeError("unsynchronized lyrics verification failed")
        messages.append("verified")
        if lrc_written and (delete_lrc or delete_sidecars):
            lrc_path.unlink()
            messages.append("LRC deleted")
        if txt_written and delete_sidecars:
            txt_path.unlink()
            messages.append("TXT deleted")
    return "; ".join(messages), changed


def write_recording(mp3_path: Path, timing_path: Path) -> int:
    try:
        from mutagen.id3 import Encoding, ID3, ID3NoHeaderError, SYLT, TXXX
    except ImportError:
        return 2
    try:
        tags = ID3(mp3_path)
    except ID3NoHeaderError:
        tags = ID3()
    synced_txxx = [frame for frame in tags.getall("TXXX")
                   if frame.desc.upper() in ("SYNCEDLYRICS", "USLT", "LYRICS")
                   and any(str(value).strip() for value in frame.text)]
    if tags.getall("SYLT") or synced_txxx:
        return 3
    entries: list[tuple[str, int]] = []
    for raw in timing_path.read_text(encoding="utf-8").splitlines():
        timestamp, separator, text = raw.partition("\t")
        if separator and text and int(timestamp) >= 0:
            entries.append((text, int(timestamp)))
    if not entries:
        return 4
    descriptor = "Manually timed in VirtualDJ Embedded Lyrics"
    tags.add(SYLT(encoding=Encoding.UTF16, lang="und", format=2, type=1,
                  desc=descriptor, text=entries))
    formatted = ["[re:VirtualDJ Embedded Lyrics - manual timing]"]
    for text, milliseconds in entries:
        minutes, remainder = divmod(milliseconds, 60000)
        seconds, millis = divmod(remainder, 1000)
        formatted.append(f"[{minutes:02d}:{seconds:02d}.{millis:03d}]{text}")
    tags.add(TXXX(encoding=Encoding.UTF16, desc="SYNCEDLYRICS", text=["\n".join(formatted)]))
    version = tags.version[1] if tags.version and tags.version[1] in (3, 4) else 3
    tags.save(mp3_path, v2_version=version)
    verify = ID3(mp3_path)
    if not verify.getall("SYLT") or not any(frame.desc.upper() == "SYNCEDLYRICS" for frame in verify.getall("TXXX")):
        return 5
    timing_path.unlink(missing_ok=True)
    return 0


def main() -> int:
    if len(sys.argv) == 4 and sys.argv[1] == "--write-recording":
        return write_recording(Path(sys.argv[2]), Path(sys.argv[3]))
    parser = argparse.ArgumentParser(
        description=("Find same-name MP3/LRC/TXT files; write LRC to SYLT + "
                     "SYNCEDLYRICS and TXT to USLT + UNSYNCEDLYRICS."))
    parser.add_argument(
        "root", nargs="?", type=Path, default=Path(__file__).resolve().parent,
        help="MP3, LRC, TXT, or library directory; default: the script's own directory",
    )
    parser.add_argument("--write", action="store_true", help="Modify MP3 files (default is dry-run)")
    parser.add_argument("--overwrite", action="store_true", help="Replace existing target lyrics frames")
    parser.add_argument("--delete-lrc", action="store_true",
                        help="Delete LRC only after both synchronized tags are verified")
    parser.add_argument("--delete-sidecars", action="store_true",
                        help="Delete each LRC/TXT only after its own tags are written and verified")
    parser.add_argument("--language", default="und", help="Three-letter ID3 language code (default: und)")
    args = parser.parse_args()
    if len(args.language) != 3 or not args.language.isascii():
        parser.error("--language must be a three-letter ASCII code such as ces, eng, or und")
    root = args.root.expanduser().resolve()
    if not root.exists():
        parser.error(f"Path does not exist: {root}")
    if root.is_file() and root.suffix.casefold() not in (".mp3", ".lrc", ".txt"):
        parser.error("file must have an .mp3, .lrc, or .txt extension")
    pairs, unmatched = discover_pairs(root)
    changed = errors = 0
    for sidecar in unmatched:
        print(f"SKIP     {sidecar} (no same-name MP3)")
    for mp3_path, lrc_path, txt_path in pairs:
        sources = ", ".join(path.name for path in (lrc_path, txt_path) if path)
        if not args.write:
            print(f"DRY-RUN  {mp3_path} <- {sources}")
            continue
        try:
            message, did_change = write_frames(mp3_path, lrc_path, txt_path,
                                                args.language, args.overwrite, args.delete_lrc,
                                                args.delete_sidecars)
            changed += int(did_change)
            print(f"WRITE    {mp3_path}: {message}")
        except Exception as exc:
            errors += 1
            print(f"ERROR    {mp3_path}: {exc}", file=sys.stderr)
    print(f"Summary: matched={len(pairs)}, unmatched={len(unmatched)}, changed={changed}, errors={errors}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
