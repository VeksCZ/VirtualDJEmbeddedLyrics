"""Mirror a music directory tree into an isolated VirtualDJ MyLists root."""

from __future__ import annotations

import hashlib
import html
import json
import os
import shutil
import tempfile
import uuid
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path, PurePath
from typing import Callable


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
    order_path = mylists / "order"
    old_order = order_path.read_bytes() if order_path.is_file() else None
    new_order = _root_order(old_order, target_name)
    order_changed = old_order != new_order

    mode = "PREVIEW" if dry_run else "SYNC"
    log(f"[{mode}] Music folder: {source}")
    log(f"[{mode}] Managed VirtualDJ root: {target_name}")
    log(f"[{mode}] {directories} folders, {playlists} playlists, {tracks} track references")
    log(
        f"[{mode}] {created_or_updated} playlist/order files to create or update; "
        f"{removed} obsolete files to remove")

    if dry_run:
        return SyncSummary(
            source, target, directories, playlists, tracks,
            created_or_updated, removed, True,
        )
    if not created_or_updated and not removed and not order_changed and owned:
        log("[SYNC] VirtualDJ lists are already up to date.")
        return SyncSummary(source, target, directories, playlists, tracks, 0, 0, False)

    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")[:-3]
    backup = virtualdj_home / "Folder Sync Backups" / f"{timestamp}-{target_name}"
    staging = mylists / f".{target_name}.sync-{uuid.uuid4().hex}.subfolders"
    retired = mylists / f".{target_name}.old-{uuid.uuid4().hex}.subfolders"
    backup.mkdir(parents=True)
    if old_order is not None:
        (backup / "order.before").write_bytes(old_order)
    if target.exists():
        shutil.copytree(target, backup / target.name)
    _write_staging(staging, expected)

    target_was_moved = False
    replacement_installed = False
    try:
        if target.exists():
            os.replace(target, retired)
            target_was_moved = True
        os.replace(staging, target)
        replacement_installed = True
        _atomic_write(order_path, new_order)
        state = {
            "Format": 1,
            "Source": str(source),
            "TargetName": target_name,
            "LastSync": datetime.now().isoformat(),
        }
        _atomic_write(
            state_path,
            (json.dumps(state, indent=2, ensure_ascii=False) + "\n").encode("utf-8"),
        )
    except Exception:
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
    )
