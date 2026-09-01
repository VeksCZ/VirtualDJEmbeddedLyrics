#!/usr/bin/env python3
"""Embed LRC lyrics into MP3 files and keep restorable sidecar backups.

Sources are tried in this order: a same-name sidecar, TIDAL by ISRC, TIDAL
search by artist/title/duration, and finally existing USLT frames. Backups
preserve the library's relative directory structure so duplicate filenames in
different albums cannot overwrite one another.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import difflib
import json
import os
import re
import tempfile
from collections.abc import Callable
from pathlib import Path

from mutagen.id3 import ID3, ID3NoHeaderError, SYLT, USLT, Encoding
from mutagen.mp3 import MP3


LogFunction = Callable[[object], None]
LRC_TIMESTAMP_RE = re.compile(
    r"\[(?P<minutes>\d+):(?P<seconds>[0-5]\d)(?:[.:](?P<fraction>\d{1,3}))?\]"
)
LANG_PREFIX_RE = re.compile(r"^\ufeff?\s*[a-zA-Z]{2,3}\|\|")
BACKUP_FORMAT_MARKER = ".lrc-backup-format"


def default_runtime_dir() -> Path:
    base = Path(os.environ.get("LOCALAPPDATA", Path.home()))
    return base / "VirtualDJEmbeddedLyrics" / "LyricsTools"


def atomic_write_text(path: Path, text: str, *, encoding: str = "utf-8") -> None:
    """Replace a text file only after its complete contents are on disk."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    temp_path = Path(temp_name)
    try:
        with os.fdopen(fd, "w", encoding=encoding, newline="") as handle:
            handle.write(text)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temp_path, path)
    finally:
        temp_path.unlink(missing_ok=True)


def atomic_write_report(path: Path, rows: list[list[object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    temp_path = Path(temp_name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8-sig", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(["file", "artist", "title", "isrc", "status", "note"])
            writer.writerows(rows)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temp_path, path)
    finally:
        temp_path.unlink(missing_ok=True)


def is_within(path: Path, directory: Path) -> bool:
    try:
        path.relative_to(directory)
        return True
    except ValueError:
        return False


def validate_directories(library_dir: Path, backup_dir: Path) -> tuple[Path, Path]:
    library_dir = library_dir.expanduser().resolve()
    backup_dir = backup_dir.expanduser().resolve()
    if not library_dir.is_dir():
        raise ValueError(f"Music library does not exist or is not a directory: {library_dir}")
    if library_dir == backup_dir:
        raise ValueError("The backup directory must not be the music library itself.")
    if is_within(library_dir, backup_dir):
        raise ValueError("The backup directory must not contain the music library.")
    return library_dir, backup_dir


def iter_mp3_files(library_dir: Path, backup_dir: Path) -> list[Path]:
    backup_inside_library = is_within(backup_dir, library_dir)
    return sorted(
        path
        for path in library_dir.rglob("*.mp3")
        if not (backup_inside_library and is_within(path, backup_dir))
    )


def backup_path_for(mp3_path: Path, library_dir: Path, backup_dir: Path) -> Path:
    relative = mp3_path.resolve().relative_to(library_dir.resolve())
    return backup_dir / relative.with_suffix(".lrc")


def read_lrc_file(path: Path) -> str:
    data = path.read_bytes()
    encodings = ["utf-8-sig"]
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        encodings.insert(0, "utf-16")
    encodings.extend(["cp1250", "cp1252"])
    for encoding in encodings:
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", errors="replace")


def read_tags(mp3_path: Path):
    """Return artist, title, ISRC, duration, and an optional read error."""
    try:
        audio = MP3(mp3_path, ID3=ID3)
    except Exception as exc:
        return None, None, None, None, f"Unable to read MP3: {exc}"

    tags = audio.tags
    artist = title = isrc = None
    if tags is not None:
        if "TPE1" in tags:
            artist = str(tags["TPE1"].text[0])
        if "TIT2" in tags:
            title = str(tags["TIT2"].text[0])
        if "TSRC" in tags:
            isrc = str(tags["TSRC"].text[0]).strip()
    duration = audio.info.length if audio.info else None
    return artist, title, isrc, duration, None


def parse_lrc(lrc_text: str) -> list[tuple[int, str]]:
    """Parse every leading timestamp on each LRC line."""
    pairs: list[tuple[int, str]] = []
    for raw_line in lrc_text.splitlines():
        line = raw_line.strip()
        matches = list(LRC_TIMESTAMP_RE.finditer(line))
        if not matches or matches[0].start() != 0:
            continue

        end = 0
        timestamps = []
        for match in matches:
            if match.start() != end:
                break
            timestamps.append(match)
            end = match.end()

        lyric = line[end:].strip()
        for match in timestamps:
            fraction = match.group("fraction") or ""
            milliseconds = int(match.group("minutes")) * 60_000
            milliseconds += int(match.group("seconds")) * 1_000
            if fraction:
                milliseconds += int(fraction.ljust(3, "0")[:3])
            pairs.append((milliseconds, lyric))
    pairs.sort(key=lambda item: item[0])
    return pairs


def normalize_title(text: str | None) -> str:
    if not text:
        return ""
    normalized = text.casefold()
    noise = [
        r"\(feat\.?[^)]*\)", r"\[feat\.?[^]]*\]", r"\(with[^)]*\)",
        r"-?\s*remaster(ed)?[^-(\[]*", r"\(remaster(ed)?[^)]*\)",
        r"\(live[^)]*\)", r"-?\s*live version", r"\(radio edit\)",
        r"-?\s*radio edit", r"\(explicit\)", r"\(clean\)",
        r"\(deluxe[^)]*\)", r"\(bonus track\)",
    ]
    for pattern in noise:
        normalized = re.sub(pattern, "", normalized, flags=re.IGNORECASE)
    normalized = re.sub(r"[^\w\s]", " ", normalized)
    return re.sub(r"\s+", " ", normalized).strip()


def normalize_artist(text: str | None) -> str:
    if not text:
        return ""
    normalized = text.casefold().replace("&", " and ")
    normalized = re.sub(r"\b(feat|featuring|ft)\.?\b.*$", "", normalized)
    normalized = re.sub(r"[^\w\s]", " ", normalized)
    return re.sub(r"\s+", " ", normalized).strip()


def similarity(left: str, right: str) -> float:
    return difflib.SequenceMatcher(None, left, right).ratio() if left and right else 0.0


class TidalClient:
    """Small tidalapi wrapper with reusable OAuth credentials and safe logging."""

    def __init__(self, session_file: Path, *, persist_session: bool = True, log: LogFunction = print):
        import tidalapi  # Imported only when TIDAL access is requested.

        self.tidalapi = tidalapi
        self.session_file = session_file.expanduser().resolve()
        self.persist_session = persist_session
        self.log = log
        self.session = tidalapi.Session()
        self._load_or_login()

    @staticmethod
    def parse_expiry(value):
        if not value or isinstance(value, dt.datetime):
            return value
        if isinstance(value, str):
            return dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
        return None

    def load_saved_session(self) -> bool:
        if not self.session_file.exists():
            return False
        try:
            data = json.loads(self.session_file.read_text(encoding="utf-8"))
            return bool(self.session.load_oauth_session(
                data["token_type"], data["access_token"], data.get("refresh_token"),
                self.parse_expiry(data.get("expiry_time")), bool(data.get("is_pkce", False)),
            ))
        except Exception as exc:
            self.log(f"[WARNING] Saved TIDAL session could not be loaded: {exc}")
            return False

    def save_session(self) -> None:
        if not self.persist_session:
            return
        expiry = self.session.expiry_time
        payload = {
            "token_type": self.session.token_type,
            "access_token": self.session.access_token,
            "refresh_token": self.session.refresh_token,
            "expiry_time": expiry.isoformat() if hasattr(expiry, "isoformat") else expiry,
            "is_pkce": bool(getattr(self.session, "is_pkce", False)),
        }
        atomic_write_text(self.session_file, json.dumps(payload, indent=2))
        try:
            self.session_file.chmod(0o600)
        except OSError:
            pass

    def _load_or_login(self) -> None:
        if self.load_saved_session():
            return
        self.log("TIDAL login is required. Opening the device authorization page...")
        login, future = self.session.login_oauth()
        raw_url = str(login.verification_uri_complete)
        url = raw_url if "://" in raw_url else f"https://{raw_url}"
        self.log(f"Open this URL if the browser does not start: {url}")
        self.log(f"The login code expires in {login.expires_in} seconds.")
        try:
            import webbrowser
            webbrowser.open(url)
        except Exception as exc:
            self.log(f"[WARNING] The browser could not be opened automatically: {exc}")
        future.result()
        if not self.session.check_login():
            raise RuntimeError("TIDAL login did not complete successfully.")
        self.save_session()

    def find_by_isrc(self, isrc: str):
        if not isrc:
            return None
        try:
            tracks = self.session.get_tracks_by_isrc(isrc)
            return tracks[0] if tracks else None
        except Exception as exc:
            self.log(f"[WARNING] TIDAL ISRC lookup failed for {isrc}: {exc}")
            return None

    @staticmethod
    def candidate_artist(candidate) -> str:
        artist = getattr(candidate, "artist", None)
        name = getattr(artist, "name", None)
        if name:
            return str(name)
        artists = getattr(candidate, "artists", None) or []
        return " ".join(str(getattr(item, "name", "")) for item in artists).strip()

    def find_by_search(self, artist, title, duration, tolerance):
        query = f"{artist or ''} {title or ''}".strip()
        if not query or not title:
            return None
        try:
            results = self.session.search(query, models=[self.tidalapi.media.Track])
            candidates = results.get("tracks", []) if isinstance(results, dict) else getattr(results, "tracks", [])
        except Exception as exc:
            self.log(f"[WARNING] TIDAL search failed for {query!r}: {exc}")
            return None

        target_title = normalize_title(title)
        target_artist = normalize_artist(artist)
        best, best_score = None, 0.0
        for candidate in candidates:
            candidate_duration = getattr(candidate, "duration", None)
            if duration is not None and candidate_duration is not None:
                if abs(float(candidate_duration) - float(duration)) > tolerance:
                    continue
            title_score = similarity(target_title, normalize_title(getattr(candidate, "name", "")))
            if title_score < 0.65:
                continue
            if target_artist:
                artist_score = similarity(target_artist, normalize_artist(self.candidate_artist(candidate)))
                if artist_score < 0.5:
                    continue
                score = title_score * 0.7 + artist_score * 0.3
            else:
                score = title_score
            if score > best_score:
                best, best_score = candidate, score
        return best if best_score >= 0.72 else None

    def fetch_lrc(self, track) -> str | None:
        try:
            lyrics = track.lyrics()
        except Exception as exc:
            self.log(f"[WARNING] TIDAL lyrics request failed: {exc}")
            return None
        text = getattr(lyrics, "subtitles", None) or getattr(lyrics, "text", None)
        return str(text).strip() if text else None


def read_existing_uslt(mp3_path: Path):
    try:
        tags = ID3(mp3_path)
    except ID3NoHeaderError:
        return []
    frames = tags.getall("USLT")
    return [(str(frame.lang or "").strip().lower(), frame.desc, str(frame.text)) for frame in frames]


def strip_lang_prefix(text: str) -> str:
    return LANG_PREFIX_RE.sub("", text, count=1)


def choose_best_existing_uslt(frames):
    if not frames:
        return None, False
    texts = [strip_lang_prefix(text) for _, _, text in frames]
    unique = list(dict.fromkeys(text.strip() for text in texts if text.strip()))
    if not unique:
        return None, False
    if len(unique) == 1:
        return unique[0], False
    return max(unique, key=lambda text: (len(parse_lrc(text)), len(text))), True


ENGLISH_COMMON_WORDS = {
    "the", "and", "you", "your", "yours", "i'm", "im", "don't", "dont",
    "love", "night", "heart", "time", "know", "never", "baby", "like",
    "want", "feel", "life", "world", "way", "going", "said", "still",
    "right", "back", "come", "more", "when", "what", "with", "that",
    "this", "from", "have", "will", "just", "into", "over", "been",
    "were", "they", "them", "some", "then", "than", "only", "most",
    "much", "many", "need", "take", "give", "make", "keep", "hold",
    "could", "would", "should", "about", "again", "always", "before",
    "after", "around", "together", "forever", "yeah", "gonna", "wanna",
    "gotta", "cause", "because", "under", "without", "inside", "outside",
    "everything", "everybody", "nothing", "somebody", "someone", "little",
    "these", "those", "there", "here", "why", "how", "who", "one", "two",
    "can't", "cant", "won't", "wont", "it's", "its", "we're",
}


def looks_english(text: str, threshold: int = 5) -> bool:
    words = re.findall(r"[a-zA-Z']+", text.lower())
    return sum(1 for word in words if word in ENGLISH_COMMON_WORDS) > threshold


def embed_lyrics(mp3_path: Path, lrc_text: str, write_sylt: bool, dry_run: bool, english_threshold: int = 5):
    lang = "eng" if looks_english(lrc_text, english_threshold) else "und"
    if dry_run:
        return lang
    try:
        tags = ID3(mp3_path)
    except ID3NoHeaderError:
        tags = ID3()
    tags.delall("USLT")
    tags.add(USLT(encoding=Encoding.UTF8, lang=lang, desc="", text=lrc_text))
    tags.delall("SYLT")  # Never retain synchronized lyrics for an older USLT payload.
    if write_sylt:
        pairs = parse_lrc(lrc_text)
        if pairs:
            tags.add(SYLT(encoding=Encoding.UTF8, lang=lang, format=2, type=1, desc="",
                          text=[(text, milliseconds) for milliseconds, text in pairs]))
    tags.save(mp3_path)
    return lang


def backup_lrc(mp3_path: Path, lrc_text: str, library_dir: Path, backup_dir: Path, dry_run: bool) -> Path:
    target = backup_path_for(mp3_path, library_dir, backup_dir)
    if not dry_run:
        marker = backup_dir / BACKUP_FORMAT_MARKER
        if not marker.exists():
            atomic_write_text(marker, "2\n")
        atomic_write_text(target, lrc_text)
    return target


def remove_sidecar_after_success(sidecar: Path, backup_path: Path, dry_run: bool) -> None:
    if not dry_run and sidecar.exists() and sidecar.resolve() != backup_path.resolve():
        sidecar.unlink()


def run_library(library_dir: Path, backup_dir: Path, *, session_file: Path, report_path: Path,
                do_tidal: bool = True, do_dedupe: bool = True, write_sylt: bool = True,
                duration_tolerance: float = 3.0, english_threshold: int = 5,
                dry_run: bool = False, log: LogFunction = print):
    library_dir, backup_dir = validate_directories(library_dir, backup_dir)
    mp3_files = iter_mp3_files(library_dir, backup_dir)
    if not mp3_files:
        log(f"No MP3 files were found in {library_dir}.")
        return []

    log(f"Music library: {library_dir}")
    log(f"LRC backup:    {backup_dir}")
    if dry_run:
        log("DRY RUN: media files, backups, credentials, and reports will not be written.")
    log("")

    tidal = None
    tidal_unavailable: str | None = None
    rows: list[list[object]] = []
    resolved_count = 0
    for mp3_path in mp3_files:
        artist = title = isrc = None
        try:
            sidecar = mp3_path.with_suffix(".lrc")
            artist, title, isrc, duration, error = read_tags(mp3_path)
            if error:
                raise ValueError(error)
            existing_uslt = read_existing_uslt(mp3_path)
            duplicate_note = f"{len(existing_uslt)} USLT frames" if len(existing_uslt) > 1 else ""
            lrc_text, status, ambiguous = None, "", False

            if sidecar.exists():
                lrc_text, status = read_lrc_file(sidecar).strip(), "local LRC"
            elif do_tidal and tidal_unavailable is None:
                if tidal is None:
                    try:
                        tidal = TidalClient(session_file, persist_session=not dry_run, log=log)
                    except Exception as exc:
                        tidal_unavailable = str(exc)
                        log(f"[ERROR] TIDAL is unavailable for this run: {exc}")
                if tidal is not None:
                    track = tidal.find_by_isrc(isrc) if isrc else None
                    status = "TIDAL ISRC" if track else ""
                    if not track:
                        track = tidal.find_by_search(artist, title, duration, duration_tolerance)
                        status = "TIDAL search" if track else ""
                    if track:
                        lrc_text = tidal.fetch_lrc(track)
                        if not lrc_text:
                            status = "track found without lyrics"

            if not lrc_text and do_dedupe and existing_uslt:
                lrc_text, ambiguous = choose_best_existing_uslt(existing_uslt)
                status = "normalized existing USLT"
                if ambiguous:
                    status += " (different texts; review required)"
            if not lrc_text:
                status = status or ("TIDAL unavailable" if tidal_unavailable else "NOT FOUND")
                rows.append([mp3_path, artist, title, isrc, status, "manual review required"])
                log(f"[MISSING] {mp3_path.name}")
                continue

            backup_path = backup_lrc(mp3_path, lrc_text, library_dir, backup_dir, dry_run)
            lang_used = embed_lyrics(mp3_path, lrc_text, write_sylt, dry_run, english_threshold)
            remove_sidecar_after_success(sidecar, backup_path, dry_run)
            notes = [str(backup_path)]
            if duplicate_note:
                notes.append(f"{duplicate_note} normalized")
            if ambiguous:
                notes.append("source USLT frames differed")
            rows.append([mp3_path, artist, title, isrc, status, "; ".join(notes)])
            resolved_count += 1
            prefix = "[WOULD UPDATE]" if dry_run else "[OK]"
            log(f"{prefix} {mp3_path.name} <- {status} [lang={lang_used}]")
        except Exception as exc:
            rows.append([mp3_path, artist, title, isrc, "ERROR", str(exc)])
            log(f"[ERROR] {mp3_path}: {exc}")

    if dry_run:
        log(f"\nDry run complete. Report would be written to: {report_path}")
    else:
        report_path = report_path.expanduser().resolve()
        atomic_write_report(report_path, rows)
        log(f"\nReport: {report_path}")
    log(f"Resolved: {resolved_count}/{len(rows)}; unresolved or failed: {len(rows) - resolved_count}.")
    return rows


def main() -> None:
    runtime_dir = default_runtime_dir()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library_dir", type=Path, nargs="?", default=Path.cwd())
    parser.add_argument("backup_dir", type=Path, nargs="?", default=None)
    parser.add_argument("--session-file", type=Path, default=runtime_dir / "tidal_session.json")
    parser.add_argument("--report", type=Path, default=runtime_dir / "lyrics_report.csv")
    parser.add_argument("--no-sylt", action="store_true", help="Write USLT only and remove stale SYLT frames")
    parser.add_argument("--no-tidal", action="store_true", help="Never contact TIDAL")
    parser.add_argument("--no-dedupe", action="store_true", help="Do not normalize existing USLT frames")
    parser.add_argument("--duration-tolerance", type=float, default=3.0, metavar="SECONDS")
    parser.add_argument("--english-threshold", type=int, default=5, metavar="WORDS")
    parser.add_argument("--dry-run", action="store_true", help="Do not write media, backups, credentials, or reports")
    args = parser.parse_args()
    backup_dir = args.backup_dir or args.library_dir / "_lrc_backup"
    run_library(args.library_dir, backup_dir, session_file=args.session_file,
                report_path=args.report, do_tidal=not args.no_tidal,
                do_dedupe=not args.no_dedupe, write_sylt=not args.no_sylt,
                duration_tolerance=args.duration_tolerance,
                english_threshold=args.english_threshold, dry_run=args.dry_run)


if __name__ == "__main__":
    main()
