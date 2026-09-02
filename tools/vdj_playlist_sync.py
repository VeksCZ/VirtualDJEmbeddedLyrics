"""Mirror a music directory tree into an isolated VirtualDJ MyLists root."""

from __future__ import annotations

import hashlib
import html
import json
import os
import re
import shutil
import tempfile
import time
import uuid
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path, PurePath
from typing import Callable

from mutagen import File as MutagenFile


AUDIO_EXTENSIONS = frozenset({
    ".aac", ".aif", ".aiff", ".alac", ".flac", ".m4a", ".mp3", ".ogg",
    ".opus", ".wav", ".wma",
})
INVALID_NAME_CHARS = frozenset('<>:"/\\|?*')
RESERVED_NAMES = frozenset({
    "CON", "PRN", "AUX", "NUL",
    *(f"COM{number}" for number in range(1, 10)),
    *(f"LPT{number}" for number in range(1, 10)),
})
DIRECT_TRACKS_LABEL = "_ Tracks in this folder"


@dataclass(frozen=True)
class MusicNode:
    name: str
    path: Path
    tracks: tuple[Path, ...]
    children: tuple["MusicNode", ...]

    @property
    def has_content(self) -> bool:
        return bool(self.tracks or any(child.has_content for child in self.children))


@dataclass(frozen=True)
class SyncSummary:
    source: Path
    target: Path
    directories: int
    playlists: int
    tracks: int
    created_or_updated: int
    removed: int
    dry_run: bool
    backup: Path | None = None
    added_tracks: int = 0
    removed_tracks: int = 0
    database_added: int = 0
    database_reactivated: int = 0


def validate_target_name(value: str) -> str:
    name = value.strip()
    if not name:
        raise ValueError("Enter a name for the managed VirtualDJ list root.")
    if name in {".", ".."} or name.endswith((".", " ")):
        raise ValueError("The VirtualDJ list root name is not valid on Windows.")
    if any(character in INVALID_NAME_CHARS or ord(character) < 32 for character in name):
        raise ValueError("The VirtualDJ list root name contains an invalid character.")
    if name.split(".", 1)[0].upper() in RESERVED_NAMES:
        raise ValueError("The VirtualDJ list root name is reserved by Windows.")
    if len(name) > 120:
        raise ValueError("The VirtualDJ list root name is too long.")
    return name


def scan_music_tree(source: Path) -> MusicNode:
    source = source.expanduser().resolve()
    if not source.is_dir():
        raise FileNotFoundError(f"Music library folder does not exist: {source}")

    def scan(directory: Path) -> MusicNode:
        tracks: list[Path] = []
        children: list[MusicNode] = []
        try:
            entries = list(os.scandir(directory))
        except OSError as exc:
            raise OSError(f"Unable to scan music folder: {directory}: {exc}") from exc
        for entry in sorted(entries, key=lambda item: (item.name.casefold(), item.name)):
            try:
                if entry.is_symlink():
                    continue
                if entry.is_dir(follow_symlinks=False):
                    child = scan(Path(entry.path))
                    if child.has_content:
                        children.append(child)
                elif (entry.is_file(follow_symlinks=False)
                      and Path(entry.name).suffix.casefold() in AUDIO_EXTENSIONS):
                    tracks.append(Path(entry.path).resolve())
            except OSError as exc:
                raise OSError(f"Unable to inspect music entry: {entry.path}: {exc}") from exc
        return MusicNode(directory.name, directory, tuple(tracks), tuple(children))

    root = scan(source)
    if not root.has_content:
        raise ValueError(f"No supported audio files were found under: {source}")
    return root


def _xml_attribute(value: str) -> str:
    return html.escape(value, quote=True).replace("&#x27;", "&#039;")


def render_playlist(tracks: tuple[Path, ...]) -> bytes:
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', "<VirtualFolder>"]
    for index, track in enumerate(tracks):
        lines.append(
            f'\t<song path="{_xml_attribute(str(track))}" idx="{index}" />')
    lines.append("</VirtualFolder>")
    return ("\r\n".join(lines) + "\r\n").encode("utf-8")


def _direct_tracks_label(node: MusicNode) -> str:
    occupied = {child.name.casefold() for child in node.children}
    label = DIRECT_TRACKS_LABEL
    counter = 2
    while label.casefold() in occupied:
        label = f"{DIRECT_TRACKS_LABEL} {counter}"
        counter += 1
    return label


def build_virtualdj_files(root: MusicNode) -> tuple[dict[PurePath, bytes], int, int, int]:
    files: dict[PurePath, bytes] = {}
    directory_count = 1
    playlist_count = 0
    track_count = 0

    def render_container(node: MusicNode, relative: PurePath) -> None:
        nonlocal directory_count, playlist_count, track_count
        order: list[str] = []
        if node.tracks:
            label = _direct_tracks_label(node)
            files[relative / f"{label}.vdjfolder"] = render_playlist(node.tracks)
            order.append(label)
            playlist_count += 1
            track_count += len(node.tracks)

        for child in node.children:
            order.append(child.name)
            if child.children:
                directory_count += 1
                render_container(child, relative / f"{child.name}.subfolders")
            else:
                files[relative / f"{child.name}.vdjfolder"] = render_playlist(child.tracks)
                playlist_count += 1
                track_count += len(child.tracks)

        files[relative / "order"] = ("\r\n".join(order) + "\r\n").encode("utf-8")

    render_container(root, PurePath())
    return files, directory_count, playlist_count, track_count


def _existing_files(root: Path) -> dict[PurePath, bytes]:
    if not root.exists():
        return {}
    if not root.is_dir():
        raise ValueError(f"The managed MyLists target is not a folder: {root}")
    result: dict[PurePath, bytes] = {}
    for path in root.rglob("*"):
        if path.is_file():
            result[PurePath(path.relative_to(root))] = path.read_bytes()
    return result


def _path_key(value: str | Path) -> str:
    return os.path.normpath(str(value)).replace("/", "\\").casefold()


def _playlist_track_paths(files: dict[PurePath, bytes]) -> dict[str, Path]:
    tracks: dict[str, Path] = {}
    for relative, content in files.items():
        if relative.suffix.casefold() != ".vdjfolder":
            continue
        try:
            root = ET.fromstring(content)
        except ET.ParseError:
            continue
        for song in root.findall(".//song"):
            value = song.attrib.get("path")
            if value:
                tracks[_path_key(value)] = Path(value)
    return tracks


def _first_tag(metadata, name: str) -> str:
    if metadata is None or metadata.tags is None:
        return ""
    try:
        value = metadata.tags.get(name, [""])[0]
    except (AttributeError, IndexError, KeyError, TypeError):
        return ""
    return str(value).strip()


def _database_song_xml(track: Path, first_seen: int) -> str:
    try:
        stat = track.stat()
    except OSError:
        stat = None
    try:
        metadata = MutagenFile(track, easy=True)
    except Exception:
        metadata = None

    tag_values = {
        "Author": _first_tag(metadata, "artist"),
        "Title": _first_tag(metadata, "title") or track.stem,
        "Album": _first_tag(metadata, "album"),
        "Genre": _first_tag(metadata, "genre"),
    }
    year = _first_tag(metadata, "date") or _first_tag(metadata, "year")
    match = re.search(r"\d{4}", year)
    if match:
        tag_values["Year"] = match.group(0)
    tag_attributes = " ".join(
        f'{name}="{_xml_attribute(value)}"'
        for name, value in tag_values.items()
        if value
    )
    song_attributes = [f'FilePath="{_xml_attribute(str(track))}"']
    if stat is not None:
        song_attributes.append(f'FileSize="{stat.st_size}"')
    lines = [f" <Song {' '.join(song_attributes)}>"]
    if tag_attributes:
        lines.append(f"  <Tags {tag_attributes} />")
    info_values = {"FirstSeen": str(first_seen)}
    if stat is not None:
        info_values["LastModified"] = str(int(stat.st_mtime))
    info = getattr(metadata, "info", None)
    length = getattr(info, "length", 0.0) or 0.0
    bitrate = getattr(info, "bitrate", 0) or 0
    if length > 0:
        info_values["SongLength"] = f"{length:.6f}".rstrip("0").rstrip(".")
    if bitrate > 0:
        info_values["Bitrate"] = str(int(round(bitrate / 1000)))
    info_attributes = " ".join(
        f'{name}="{_xml_attribute(value)}"'
        for name, value in info_values.items()
    )
    lines.append(f"  <Infos {info_attributes} />")
    lines.append(" </Song>")
    return "\r\n".join(lines)


_SONG_OPEN_RE = re.compile(r"<Song\b[^>]*>", re.IGNORECASE)
_ATTRIBUTE_RE = re.compile(
    r'\s+(?P<name>[A-Za-z_:][\w:.-]*)="(?P<value>[^"]*)"',
    re.IGNORECASE,
)
_DATABASE_CLOSE_RE = re.compile(r"</VirtualDJ_Database\s*>", re.IGNORECASE)


def plan_search_database(
    database_path: Path,
    tracks: tuple[Path, ...],
    *,
    materialize_additions: bool = True,
    log: Callable[[str], None] | None = None,
) -> tuple[bytes, int, int]:
    """Return an add-only VirtualDJ database update.

    Existing Song elements and all analysis children are preserved byte-for-byte.
    Current library tracks are appended when absent. For matching existing tracks,
    only the hidden/missing bits are cleared from the Song Flag attribute.
    """

    track_by_key = {_path_key(track): track for track in tracks}
    if database_path.is_file():
        original = database_path.read_bytes()
        had_bom = original.startswith(b"\xef\xbb\xbf")
        text = original.decode("utf-8-sig")
        try:
            ET.fromstring(text)
        except ET.ParseError as exc:
            raise ValueError(f"VirtualDJ database.xml is not valid XML: {exc}") from exc
    else:
        original = b""
        had_bom = False
        text = (
            '<?xml version="1.0" encoding="UTF-8"?>\r\n'
            '<VirtualDJ_Database Version="8.5">\r\n'
            '</VirtualDJ_Database>\r\n'
        )

    existing: set[str] = set()
    reactivated: set[str] = set()

    def update_song_open(match: re.Match[str]) -> str:
        opening = match.group(0)
        attributes = {
            item.group("name").casefold(): html.unescape(item.group("value"))
            for item in _ATTRIBUTE_RE.finditer(opening)
        }
        path_value = attributes.get("filepath")
        if not path_value:
            return opening
        key = _path_key(path_value)
        if key not in track_by_key:
            return opening
        existing.add(key)

        flag_match = re.search(r'\s+Flag="(?P<value>\d+)"', opening, re.IGNORECASE)
        if flag_match is None:
            return opening
        flag = int(flag_match.group("value"))
        updated_flag = flag & ~17
        if updated_flag == flag:
            return opening
        reactivated.add(key)
        if updated_flag:
            return (
                opening[:flag_match.start("value")]
                + str(updated_flag)
                + opening[flag_match.end("value"):]
            )
        return opening[:flag_match.start()] + opening[flag_match.end():]

    updated_text = _SONG_OPEN_RE.sub(update_song_open, text)
    missing_keys = sorted(set(track_by_key) - existing)
    if missing_keys and materialize_additions:
        closing_matches = list(_DATABASE_CLOSE_RE.finditer(updated_text))
        if len(closing_matches) != 1:
            raise ValueError("VirtualDJ database.xml has an unexpected root structure.")
        closing = closing_matches[0]
        first_seen = int(time.time())
        entries_list = []
        total = len(missing_keys)
        for index, key in enumerate(missing_keys, start=1):
            entries_list.append(_database_song_xml(track_by_key[key], first_seen))
            if log is not None and (index == 1 or index % 500 == 0 or index == total):
                log(f"[SEARCH DB] Reading track metadata: {index}/{total}")
        entries = "\r\n".join(entries_list)
        prefix = updated_text[:closing.start()].rstrip("\r\n")
        suffix = updated_text[closing.start():]
        updated_text = f"{prefix}\r\n{entries}\r\n{suffix}"

    try:
        ET.fromstring(updated_text)
    except ET.ParseError as exc:
        raise ValueError(f"Generated VirtualDJ database update is invalid: {exc}") from exc

    encoded = updated_text.encode("utf-8")
    if had_bom:
        encoded = b"\xef\xbb\xbf" + encoded
    if (not missing_keys or not materialize_additions) and not reactivated:
        encoded = original
    return encoded, len(missing_keys), len(reactivated)


def _all_tracks(root: MusicNode) -> tuple[Path, ...]:
    tracks = list(root.tracks)
    for child in root.children:
        tracks.extend(_all_tracks(child))
    return tuple(tracks)


def _database_path_for_track(track: Path, virtualdj_home: Path) -> Path:
    track_drive = track.drive.casefold()
    home_drive = virtualdj_home.drive.casefold()
    if not track_drive or track_drive == home_drive:
        return virtualdj_home / "database.xml"
    if not track.anchor:
        raise ValueError(f"Cannot determine the VirtualDJ database drive for: {track}")
    return Path(track.anchor) / "VirtualDJ" / "database.xml"


def _tracks_by_database(
    tracks: tuple[Path, ...],
    virtualdj_home: Path,
) -> dict[Path, tuple[Path, ...]]:
    grouped: dict[Path, list[Path]] = {}
    for track in tracks:
        database_path = _database_path_for_track(track, virtualdj_home)
        grouped.setdefault(database_path, []).append(track)
    return {
        path: tuple(grouped[path])
        for path in sorted(grouped, key=lambda item: str(item).casefold())
    }


def _database_backup_name(database_path: Path, virtualdj_home: Path) -> str:
    if _path_key(database_path) == _path_key(virtualdj_home / "database.xml"):
        return "database.before.xml"
    drive = database_path.drive.rstrip(":\\/")
    if len(drive) == 1 and drive.isalpha():
        return f"database-{drive.upper()}.before.xml"
    digest = hashlib.sha256(_path_key(database_path).encode("utf-8")).hexdigest()[:8]
    return f"database-{digest}.before.xml"


def _ownership_file(virtualdj_home: Path, target_name: str) -> Path:
    digest = hashlib.sha256(target_name.casefold().encode("utf-8")).hexdigest()[:16]
    return virtualdj_home / "Folder Sync State" / f"{digest}.json"


def _load_ownership(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def _atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def _root_order(existing: bytes | None, target_name: str) -> bytes:
    text = existing.decode("utf-8-sig", errors="replace") if existing else ""
    lines = [line.rstrip("\r") for line in text.splitlines() if line.strip()]
    lines = [line for line in lines if line.split("||", 1)[0].casefold() != target_name.casefold()]
    lines.append(target_name)
    return ("\r\n".join(lines) + "\r\n").encode("utf-8")


def _write_staging(staging: Path, files: dict[PurePath, bytes]) -> None:
    staging.mkdir(parents=False)
    for relative, content in files.items():
        destination = staging.joinpath(*relative.parts)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(content)


def sync_library(
    source: Path,
    virtualdj_home: Path,
    target_name: str,
    *,
    dry_run: bool = True,
    adopt_existing: bool = False,
    add_search_db: bool = True,
    log: Callable[[str], None] = print,
) -> SyncSummary:
    source = source.expanduser().resolve()
    virtualdj_home = virtualdj_home.expanduser().resolve()
    target_name = validate_target_name(target_name)
    mylists = virtualdj_home / "MyLists"
    if not mylists.is_dir():
        raise FileNotFoundError(f"VirtualDJ MyLists folder does not exist: {mylists}")

    tree = scan_music_tree(source)
    expected, directories, playlists, tracks = build_virtualdj_files(tree)
    target = mylists / f"{target_name}.subfolders"
    state_path = _ownership_file(virtualdj_home, target_name)
    ownership = _load_ownership(state_path)
    owned = (
        str(ownership.get("TargetName", "")).casefold() == target_name.casefold()
        and Path(str(ownership.get("Source", ""))).resolve() == source
    ) if ownership else False
    if target.exists() and not owned and not adopt_existing:
        raise PermissionError(
            f"The MyLists root already exists and is not owned by this tool: {target}. "
            "Choose another name or explicitly allow adopting it."
        )

    current = _existing_files(target)
    created_or_updated = sum(current.get(path) != content for path, content in expected.items())
    removed = len(set(current) - set(expected))
    current_track_paths = _playlist_track_paths(current)
    expected_track_paths = _playlist_track_paths(expected)
    added_track_keys = sorted(set(expected_track_paths) - set(current_track_paths))
    removed_track_keys = sorted(set(current_track_paths) - set(expected_track_paths))
    order_path = mylists / "order"
    old_order = order_path.read_bytes() if order_path.is_file() else None
    new_order = _root_order(old_order, target_name)
    order_changed = old_order != new_order
    database_plans: dict[Path, tuple[bytes | None, bytes, int, int, bool]] = {}
    database_added = 0
    database_reactivated = 0
    database_changed = False
    if add_search_db:
        for database_path, database_tracks in _tracks_by_database(
                _all_tracks(tree), virtualdj_home).items():
            original = database_path.read_bytes() if database_path.is_file() else None
            updated, added, reactivated = plan_search_database(
                database_path,
                database_tracks,
                materialize_additions=not dry_run,
                log=log if not dry_run else None,
            )
            changed = original != updated or (dry_run and added > 0)
            database_plans[database_path] = (
                original, updated, added, reactivated, changed)
            database_added += added
            database_reactivated += reactivated
            database_changed = database_changed or changed

    mode = "PREVIEW" if dry_run else "SYNC"
    log(f"[{mode}] Music folder: {source}")
    log(f"[{mode}] Managed VirtualDJ root: {target_name}")
    log(f"[{mode}] {directories} folders, {playlists} playlists, {tracks} track references")
    log(
        f"[{mode}] {created_or_updated} playlist/order files to create or update; "
        f"{removed} obsolete files to remove")
    log(
        f"[{mode}] Playlist track changes: +{len(added_track_keys)} added, "
        f"-{len(removed_track_keys)} removed")
    for key in removed_track_keys:
        log(f"[{mode}] REMOVE missing playlist track: {current_track_paths[key]}")
    if len(added_track_keys) <= 50:
        for key in added_track_keys:
            log(f"[{mode}] ADD playlist track: {expected_track_paths[key]}")
    elif added_track_keys:
        log(f"[{mode}] Added-track details omitted for {len(added_track_keys)} entries.")
    if add_search_db:
        log(
            f"[{mode}] Search DB additions: {database_added}; "
            f"existing tracks reactivated: {database_reactivated}")
        log(f"[{mode}] Search DB removals: 0 (existing entries and analyses are preserved)")
        for database_path, plan in database_plans.items():
            log(
                f"[{mode}] Search DB {database_path}: "
                f"+{plan[2]} added, {plan[3]} reactivated")
    else:
        log(f"[{mode}] Search DB update is disabled")

    if dry_run:
        return SyncSummary(
            source, target, directories, playlists, tracks,
            created_or_updated, removed, True,
            added_tracks=len(added_track_keys),
            removed_tracks=len(removed_track_keys),
            database_added=database_added,
            database_reactivated=database_reactivated,
        )
    if (not created_or_updated and not removed and not order_changed
            and not database_changed and owned):
        log("[SYNC] VirtualDJ lists are already up to date.")
        return SyncSummary(
            source, target, directories, playlists, tracks, 0, 0, False,
            added_tracks=0,
            removed_tracks=0,
            database_added=0,
            database_reactivated=0,
        )

    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")[:-3]
    backup = virtualdj_home / "Folder Sync Backups" / f"{timestamp}-{target_name}"
    staging = mylists / f".{target_name}.sync-{uuid.uuid4().hex}.subfolders"
    retired = mylists / f".{target_name}.old-{uuid.uuid4().hex}.subfolders"
    backup.mkdir(parents=True)
    if old_order is not None:
        (backup / "order.before").write_bytes(old_order)
    if target.exists():
        shutil.copytree(target, backup / target.name)
    for database_path, plan in database_plans.items():
        original, _updated, _added, _reactivated, changed = plan
        if changed and original is not None:
            (backup / _database_backup_name(database_path, virtualdj_home)).write_bytes(
                original)
    _write_staging(staging, expected)

    target_was_moved = False
    replacement_installed = False
    databases_written: list[tuple[Path, bytes | None]] = []
    try:
        if target.exists():
            os.replace(target, retired)
            target_was_moved = True
        os.replace(staging, target)
        replacement_installed = True
        _atomic_write(order_path, new_order)
        for database_path, plan in database_plans.items():
            original, updated, _added, _reactivated, changed = plan
            if changed:
                _atomic_write(database_path, updated)
                databases_written.append((database_path, original))
        state = {
            "Format": 1,
            "Source": str(source),
            "TargetName": target_name,
            "LastSync": datetime.now().isoformat(),
            "AddSearchDB": add_search_db,
            "SearchDatabases": [str(path) for path in database_plans],
        }
        _atomic_write(
            state_path,
            (json.dumps(state, indent=2, ensure_ascii=False) + "\n").encode("utf-8"),
        )
    except Exception:
        for database_path, original in reversed(databases_written):
            if original is None:
                database_path.unlink(missing_ok=True)
            else:
                _atomic_write(database_path, original)
        if replacement_installed and target.exists():
            shutil.rmtree(target)
        if target_was_moved and retired.exists():
            os.replace(retired, target)
        if old_order is None:
            if order_path.exists():
                order_path.unlink()
        else:
            _atomic_write(order_path, old_order)
        raise
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    if retired.exists():
        shutil.rmtree(retired)

    log(f"[SYNC] Completed. Backup: {backup}")
    log("[SYNC] Start VirtualDJ to load the updated MyLists tree.")
    return SyncSummary(
        source, target, directories, playlists, tracks,
        created_or_updated, removed, False, backup,
        added_tracks=len(added_track_keys),
        removed_tracks=len(removed_track_keys),
        database_added=database_added,
        database_reactivated=database_reactivated,
    )
