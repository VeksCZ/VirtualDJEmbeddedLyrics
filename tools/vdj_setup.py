"""Native VirtualDJ discovery, plugin installation, and recovery helpers."""

from __future__ import annotations

import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
import zipfile
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Callable


WINDOWS_PAYLOAD_FILES = (
    "LRCMaster.dll",
    "LRCBlackOut.dll",
    "EmbeddedLyricsTagWriter.py",
)
MAC_PAYLOAD_FILES = (
    "LRCMaster.bundle",
    "LRCBlackOut.bundle",
)
PAYLOAD_FILES = MAC_PAYLOAD_FILES if sys.platform == "darwin" else WINDOWS_PAYLOAD_FILES


@dataclass(frozen=True)
class PackageLayout:
    root: Path
    payload: Path
    version: str


def _complete_payload(path: Path) -> bool:
    return path.is_dir() and all((path / name).exists() for name in PAYLOAD_FILES)


def plugin_directory(virtualdj_home: Path) -> Path:
    if sys.platform == "darwin":
        architecture = platform.machine().lower()
        root = "PluginsArm" if architecture in {"arm64", "aarch64"} else "Plugins64"
        return virtualdj_home / root / "VideoOverlay"
    return virtualdj_home / "Plugins64" / "VideoOverlay"


def is_virtualdj_home(path: Path) -> bool:
    try:
        candidate = path.expanduser().resolve()
    except OSError:
        return False
    markers = ("settings.xml", "database.xml", "MyLists", "Folders", "Plugins64", "PluginsArm")
    return candidate.is_dir() and any((candidate / marker).exists() for marker in markers)


def _mac_candidates() -> list[dict[str, object]]:
    locations = (
        (Path.home() / "Library" / "Application Support" / "VirtualDJ",
         "current macOS default", True),
        (Path.home() / "Documents" / "VirtualDJ", "legacy macOS default", False),
    )
    result = []
    seen = set()
    for path, source, preferred in locations:
        try:
            resolved = path.expanduser().resolve()
        except OSError:
            continue
        key = str(resolved)
        if key in seen or not is_virtualdj_home(resolved):
            continue
        seen.add(key)
        result.append({"Path": key, "Source": source, "Preferred": preferred})
    return result


def _windows_candidates() -> list[dict[str, object]]:
    locations: list[tuple[Path, str, bool]] = []
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\VirtualDJ") as key:
            value, _kind = winreg.QueryValueEx(key, "HomeFolder")
        if value:
            locations.append((Path(str(value)), "VirtualDJ registry HomeFolder", True))
    except (ImportError, OSError):
        pass
    local = os.environ.get("LOCALAPPDATA")
    if local:
        locations.append((Path(local) / "VirtualDJ", "current Windows default", False))
    documents = Path.home() / "Documents"
    locations.append((documents / "VirtualDJ", "legacy Documents location", False))
    result = []
    seen = set()
    for path, source, preferred in locations:
        try:
            resolved = path.expanduser().resolve()
        except OSError:
            continue
        key = str(resolved).casefold()
        if key in seen or not is_virtualdj_home(resolved):
            continue
        seen.add(key)
        result.append({"Path": str(resolved), "Source": source, "Preferred": preferred})
    return result


def _windows_virtualdj_running() -> bool:
    try:
        completed = subprocess.run(
            ["tasklist", "/FI", "IMAGENAME eq virtualdj.exe", "/NH"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=False, timeout=5,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        return b"virtualdj.exe" in completed.stdout.lower()
    except (OSError, subprocess.SubprocessError):
        return False


def _query_virtualdj_windows(explicit_path: str = "") -> dict:
    candidates = _windows_candidates()
    if explicit_path.strip():
        selected = Path(explicit_path.strip()).expanduser().resolve()
        valid = is_virtualdj_home(selected)
        return {
            "Valid": valid, "Selected": str(selected) if valid else None,
            "Candidates": candidates,
            "Message": ("The selected VirtualDJ home folder is valid." if valid else
                        "The selected folder is not a VirtualDJ home folder."),
            "VirtualDJRunning": _windows_virtualdj_running(),
        }
    preferred = next((item for item in candidates if item["Preferred"]), None)
    selected_item = preferred or (candidates[0] if len(candidates) == 1 else None)
    return {
        "Valid": selected_item is not None,
        "Selected": selected_item["Path"] if selected_item else None,
        "Candidates": candidates,
        "Message": ("VirtualDJ home folder detected." if selected_item else
                    "Choose the active VirtualDJ home folder manually."),
        "VirtualDJRunning": _windows_virtualdj_running(),
    }


def _mac_virtualdj_running() -> bool:
    try:
        completed = subprocess.run(
            ["pgrep", "-x", "VirtualDJ"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=5,
        )
        return completed.returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def _query_virtualdj_mac(explicit_path: str = "") -> dict:
    candidates = _mac_candidates()
    if explicit_path.strip():
        selected = Path(explicit_path.strip()).expanduser().resolve()
        valid = is_virtualdj_home(selected)
        return {
            "Valid": valid,
            "Selected": str(selected) if valid else None,
            "Candidates": candidates,
            "Message": ("The selected VirtualDJ home folder is valid." if valid else
                        "The selected folder is not a VirtualDJ home folder."),
            "VirtualDJRunning": _mac_virtualdj_running(),
        }
    preferred = next((item for item in candidates if item["Preferred"]), None)
    selected_item = preferred or (candidates[0] if len(candidates) == 1 else None)
    return {
        "Valid": selected_item is not None,
        "Selected": selected_item["Path"] if selected_item else None,
        "Candidates": candidates,
        "Message": ("VirtualDJ home folder detected." if selected_item else
                    "Choose the active VirtualDJ home folder manually."),
        "VirtualDJRunning": _mac_virtualdj_running(),
    }


def _read_version(root: Path) -> str:
    try:
        value = (root / "VERSION").read_text(encoding="utf-8-sig").strip()
    except OSError:
        return "unknown"
    return value or "unknown"


def _extract_payload_from_zip(
    zip_path: Path,
    version: str,
    runtime_dir: Path | None,
) -> Path:
    if runtime_dir is None:
        local_app_data = os.environ.get("LOCALAPPDATA")
        base = Path(local_app_data) if local_app_data else Path.home() / "AppData" / "Local"
        runtime_dir = base / "VirtualDJEmbeddedLyrics" / "InstallerPayload"

    destination = runtime_dir.expanduser().resolve() / version
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path) as archive:
        members = [name.replace("\\", "/") for name in archive.namelist()]
        for name in PAYLOAD_FILES:
            suffixes = (f"/_internal/Plugins/{name}", f"/Plugins/{name}")
            candidates = [member for member in members
                          if any(("/" + member).endswith(suffix) for suffix in suffixes)]
            if len(candidates) != 1:
                raise KeyError(f"Release ZIP does not contain one plugin payload file: {name}")
            data = archive.read(candidates[0])
            target = destination / name
            temporary = destination / f".{name}.{os.getpid()}.tmp"
            try:
                temporary.write_bytes(data)
                os.replace(temporary, target)
            finally:
                if temporary.exists():
                    temporary.unlink()

    if not _complete_payload(destination):
        raise FileNotFoundError("The release ZIP contains an incomplete plugin payload.")
    return destination


def locate_package_layout(
    script_dir: Path,
    *,
    runtime_dir: Path | None = None,
) -> PackageLayout:
    """Locate the plugin payload shipped beside or built for LyricsTools.

    Release packages keep payload files in their private resources. A source
    checkout extracts only those files from its matching built release ZIP when
    necessary.
    """

    script_dir = script_dir.expanduser().resolve()
    root = script_dir.parent

    release_payload = root / "Plugins"
    if sys.platform == "darwin" and _complete_payload(release_payload):
        return PackageLayout(
            root=root,
            payload=release_payload,
            version=_read_version(root),
        )
    if _complete_payload(release_payload):
        return PackageLayout(
            root=root,
            payload=release_payload,
            version=_read_version(root),
        )

    if (root / "VERSION").is_file():
        version = _read_version(root)
        payload_candidates = (
            root / "dist" / f"LRC-Lyrics-VirtualDJ-Windows-v{version}" / "Plugins",
            root / "dist" / f"LRC-Lyrics-VirtualDJ-v{version}" / "Plugins",
            root / "dist" / "full",
        )
        payload = next((path for path in payload_candidates if _complete_payload(path)), None)
        if payload is None and sys.platform != "darwin":
            release_zips = (
                root / "dist" / f"LyricsTools-Windows-v{version}.zip",
                root / "dist" / f"LRC-Lyrics-VirtualDJ-Windows-v{version}.zip",
            )
            release_zip = next((path for path in release_zips if path.is_file()), None)
            if release_zip is not None:
                try:
                    payload = _extract_payload_from_zip(release_zip, version, runtime_dir)
                except (OSError, KeyError, zipfile.BadZipFile):
                    payload = None
        if payload is not None:
            return PackageLayout(
                root=root,
                payload=payload,
                version=version,
            )

    if sys.platform == "darwin":
        raise FileNotFoundError(
            "The macOS LRC Master and LRC BlackOut bundles were not found. "
            "Use the complete macOS release package or build it on macOS."
        )
    raise FileNotFoundError(
        "The complete plugin payload was not found. Extract the complete "
        "LyricsTools ZIP, or run build-release.ps1 before using setup from source."
    )


def query_virtualdj(layout: PackageLayout | None = None, explicit_path: str = "") -> dict:
    if sys.platform == "darwin":
        return _query_virtualdj_mac(explicit_path)
    return _query_virtualdj_windows(explicit_path)


def installed_status(virtualdj_home: Path) -> str:
    overlay = plugin_directory(virtualdj_home)
    present = [name for name in PAYLOAD_FILES if (overlay / name).exists()]
    legacy_present = [
        name for name in ("LRC Master.dll", "LRC BlackOut.dll")
        if (overlay / name).is_file()
    ]
    manifest_path = virtualdj_home / "LRC Lyrics Installation.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
        version = str(manifest.get("Version") or "unknown")
    except (OSError, ValueError, AttributeError):
        version = ""
    if len(present) == len(PAYLOAD_FILES):
        detail = f"Installed version {version}." if version else "Plugin files are installed."
        return f"{detail} All {len(PAYLOAD_FILES)} managed files are present."
    if present:
        return (
            f"Partial installation: {len(present)} of {len(PAYLOAD_FILES)} managed files "
            "are present. Run Install / update plugin."
        )
    if legacy_present:
        return "A legacy installation was detected. Run Install / update plugin."
    return "The LRC Lyrics plugin is not installed in this VirtualDJ folder."


def assert_virtualdj_closed(layout: PackageLayout | None = None) -> None:
    result = query_virtualdj(layout)
    if result.get("VirtualDJRunning"):
        raise RuntimeError(
            "VirtualDJ is running. Close VirtualDJ completely before changing MyLists."
        )


def virtualdj_running(layout: PackageLayout | None = None) -> bool:
    return bool(query_virtualdj(layout).get("VirtualDJRunning"))


def _windows_virtualdj_pids() -> set[int]:
    try:
        completed = subprocess.run(
            ["tasklist", "/FI", "IMAGENAME eq virtualdj.exe", "/FO", "CSV", "/NH"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, errors="replace",
            check=False, timeout=5,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
    except (OSError, subprocess.SubprocessError):
        return set()
    result: set[int] = set()
    for line in completed.stdout.splitlines():
        fields = [field.strip().strip('"') for field in line.split(",")]
        if len(fields) >= 2 and fields[0].casefold() == "virtualdj.exe":
            try:
                result.add(int(fields[1]))
            except ValueError:
                pass
    return result


def _request_windows_virtualdj_close() -> None:
    pids = _windows_virtualdj_pids()
    if not pids:
        return
    try:
        import ctypes
        from ctypes import wintypes

        user32 = ctypes.WinDLL("user32", use_last_error=True)
        callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

        @callback_type
        def close_window(hwnd, _lparam):
            process_id = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(process_id))
            if process_id.value in pids and user32.IsWindowVisible(hwnd):
                user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
            return True

        user32.EnumWindows(close_window, 0)
    except (AttributeError, OSError):
        return


def request_virtualdj_close(layout: PackageLayout | None = None, timeout: float = 15.0) -> bool:
    """Ask VirtualDJ to close normally and wait; never force-terminate it."""
    if not virtualdj_running(layout):
        return True
    if sys.platform == "darwin":
        subprocess.run(
            ["osascript", "-e", 'tell application "VirtualDJ" to quit'],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False, timeout=10,
        )
    else:
        _request_windows_virtualdj_close()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not virtualdj_running(layout):
            return True
        time.sleep(0.25)
    return False


def reset_video_window_layout(
    virtualdj_home: Path,
    layout: PackageLayout | None = None,
    log: Callable[[str], None] = print,
) -> Path | None:
    """Remove only VirtualDJ's remembered external-video window geometry."""
    virtualdj_home = virtualdj_home.expanduser().resolve()
    if not is_virtualdj_home(virtualdj_home):
        raise ValueError(f"The selected folder is not a VirtualDJ home folder: {virtualdj_home}")
    assert_virtualdj_closed(layout)
    settings = virtualdj_home / "settings.xml"
    data = settings.read_bytes()
    pattern = re.compile(
        rb"[ \t]*<videoWindowPosition\b[^>]*>.*?</videoWindowPosition>[ \t]*(?:\r?\n)?",
        re.DOTALL,
    )
    updated, count = pattern.subn(b"", data, count=1)
    if not count:
        log("No saved external-video window layout was found.")
        return None
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")[:-3]
    backup = virtualdj_home / "LRC Lyrics Backups" / f"{timestamp}-before-video-window-reset"
    backup.mkdir(parents=True)
    shutil.copy2(settings, backup / "settings.xml")
    descriptor, temporary_name = tempfile.mkstemp(prefix=".settings.", dir=settings.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(updated)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, settings)
    finally:
        temporary.unlink(missing_ok=True)
    log("Reset the saved external-video window size and position.")
    log(f"Settings backup: {backup}")
    return backup


def run_action(
    layout: PackageLayout,
    action: str,
    virtualdj_home: Path,
    log: Callable[[str], None] = print,
) -> None:
    if sys.platform == "darwin":
        _run_action_mac(layout, action, virtualdj_home, log)
        return
    _run_action_windows(layout, action, virtualdj_home, log)


def _copy_item(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        shutil.copytree(source, destination)
    else:
        shutil.copy2(source, destination)


def _backup_item(path: Path, virtualdj_home: Path, backup_root: Path) -> None:
    if not path.exists():
        return
    home = virtualdj_home.resolve()
    resolved = path.resolve()
    try:
        relative = resolved.relative_to(home)
    except ValueError as exc:
        raise RuntimeError(f"Refusing to back up a path outside VirtualDJ home: {path}") from exc
    _copy_item(path, backup_root / relative)


def _atomic_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(value, handle, indent=2)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _remove_item(path: Path) -> None:
    if path.is_dir():
        shutil.rmtree(path)
    else:
        path.unlink(missing_ok=True)


def _restore_tree(backup: Path, virtualdj_home: Path) -> int:
    backup_root = backup.resolve()
    home = virtualdj_home.resolve()
    try:
        backup_root.relative_to((home / "LRC Lyrics Backups").resolve())
    except ValueError as exc:
        raise RuntimeError("Refusing to restore a backup outside the backup folder.") from exc
    restored = 0
    for source in backup.rglob("*"):
        if not source.is_file():
            continue
        destination = home / source.relative_to(backup)
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.parent / f".{destination.name}.{uuid.uuid4().hex}.restoring"
        shutil.copy2(source, temporary)
        if _sha256(source) != _sha256(temporary):
            temporary.unlink(missing_ok=True)
            raise OSError(f"Backup verification failed: {source}")
        os.replace(temporary, destination)
        restored += 1
    return restored


def _new_backup(virtualdj_home: Path, action: str) -> Path:
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")[:-3]
    backup = virtualdj_home / "LRC Lyrics Backups" / f"{timestamp}-before-{action}"
    backup.mkdir(parents=True)
    return backup


def _copy_verified_file(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise FileNotFoundError(f"Plugin payload file is missing: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.parent / f".{destination.name}.{uuid.uuid4().hex}.installing"
    try:
        shutil.copy2(source, temporary)
        if _sha256(source) != _sha256(temporary):
            raise OSError(f"Plugin file verification failed: {source.name}")
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def _windows_legacy_paths(virtualdj_home: Path) -> list[Path]:
    plugins = virtualdj_home / "Plugins64"
    result: list[Path] = []
    common = (
        "EmbeddedLyricsDeck.dll", "EmbeddedLyricsMaster.dll", "Blackout.dll",
        "LRC Deck Basic.dll", "LRC Master Basic.dll", "EmbeddedLyricsTagWriter.py",
    )
    for folder in ("VideoEffect", "VideoOverlay", "Visualisations", "VideoSource"):
        result.extend(plugins / folder / name for name in common)
    result.extend((plugins / "VideoOverlay" / name) for name in (
        "LRC Master.dll", "LRC BlackOut.dll",
    ))
    result.extend((plugins / "VideoEffect" / name) for name in (
        "LRC Deck.dll", "LRC Master.dll", "LRC BlackOut.dll", "LRC Deck FX.dll",
        "EmbeddedLyricsDeck.ini", "EmbeddedLyricsMaster.ini", "LRC Deck.ini",
        "LRC Deck_2.ini", "LRC Deck FX.ini", "LRC Master.ini",
    ))
    result.extend((plugins / "VideoSource" / name) for name in ("LRC Deck.dll",))
    result.extend((plugins / "Visualisations" / name) for name in (
        "LRC Deck.dll", "LRC Deck.ini", "LRC Deck_2.ini",
    ))
    return list(dict.fromkeys(result))


def _run_action_windows(
    layout: PackageLayout,
    action: str,
    virtualdj_home: Path,
    log: Callable[[str], None],
) -> None:
    if action not in {"install", "uninstall", "restore"}:
        raise ValueError(f"Unsupported setup action: {action}")
    virtualdj_home = virtualdj_home.expanduser().resolve()
    if not is_virtualdj_home(virtualdj_home):
        raise ValueError(f"The selected folder is not a VirtualDJ home folder: {virtualdj_home}")
    assert_virtualdj_closed(layout)
    backups_root = virtualdj_home / "LRC Lyrics Backups"

    if action == "restore":
        candidates = sorted(
            (path for path in backups_root.iterdir() if path.is_dir()),
            key=lambda path: path.name, reverse=True,
        ) if backups_root.is_dir() else []
        if not candidates:
            raise FileNotFoundError("No LRC Lyrics backup is available.")
        restored = _restore_tree(candidates[0], virtualdj_home)
        if not restored:
            raise FileNotFoundError("The newest LRC Lyrics backup is empty.")
        log(f"Restored the newest backup: {candidates[0]}")
        return

    overlay = plugin_directory(virtualdj_home)
    managed = [overlay / name for name in WINDOWS_PAYLOAD_FILES]
    manifest = virtualdj_home / "LRC Lyrics Installation.json"
    legacy = _windows_legacy_paths(virtualdj_home)
    uninstall_paths = list(dict.fromkeys(managed + [
        overlay / "LRC Master.dll", overlay / "LRC BlackOut.dll", manifest,
    ]))
    affected = uninstall_paths if action == "uninstall" else list(
        dict.fromkeys(managed + legacy + [manifest]))
    if action == "uninstall" and not any(path.exists() for path in uninstall_paths):
        log("LRC Lyrics is already uninstalled.")
        return

    backup = _new_backup(virtualdj_home, action)
    for path in affected:
        _backup_item(path, virtualdj_home, backup)

    settings = virtualdj_home / "settings.xml"
    original_settings: bytes | None = None
    if action == "install" and settings.is_file():
        original_settings = settings.read_bytes()
        updated = re.sub(
            rb"(<videoAudioOnlyVisualisation>)LRC Deck(</videoAudioOnlyVisualisation>)",
            rb"\1None\2", original_settings,
        )
        if updated != original_settings:
            _backup_item(settings, virtualdj_home, backup)
            temporary = settings.parent / f".{settings.name}.{uuid.uuid4().hex}.updating"
            try:
                temporary.write_bytes(updated)
                os.replace(temporary, settings)
            finally:
                temporary.unlink(missing_ok=True)

    if action == "uninstall":
        for path in uninstall_paths:
            _remove_item(path)
        log(f"LRC Lyrics was uninstalled. Backup: {backup}")
        return

    installed: list[Path] = []
    try:
        for path in legacy:
            _remove_item(path)
        for name, destination in zip(WINDOWS_PAYLOAD_FILES, managed):
            _copy_verified_file(layout.payload / name, destination)
            installed.append(destination)
        _atomic_json(manifest, {
            "Version": layout.version,
            "InstalledAt": datetime.now().astimezone().isoformat(),
            "VirtualDJHome": str(virtualdj_home),
            "Files": [str(path.relative_to(virtualdj_home)) for path in managed],
        })
    except Exception:
        for path in installed:
            _remove_item(path)
        manifest.unlink(missing_ok=True)
        _restore_tree(backup, virtualdj_home)
        raise
    log(f"LRC Lyrics {layout.version} was installed into: {overlay}")
    log(f"Backup: {backup}")


def _run_action_mac(
    layout: PackageLayout,
    action: str,
    virtualdj_home: Path,
    log: Callable[[str], None],
) -> None:
    if action not in {"install", "uninstall", "restore"}:
        raise ValueError(f"Unsupported setup action: {action}")
    virtualdj_home = virtualdj_home.expanduser().resolve()
    if not is_virtualdj_home(virtualdj_home):
        raise ValueError(f"The selected folder is not a VirtualDJ home folder: {virtualdj_home}")
    assert_virtualdj_closed(layout)
    backups_root = virtualdj_home / "LRC Lyrics Backups"

    if action == "restore":
        candidates = sorted(
            (path for path in backups_root.iterdir() if path.is_dir()),
            key=lambda path: path.name,
            reverse=True,
        ) if backups_root.is_dir() else []
        if not candidates:
            raise FileNotFoundError("No LRC Lyrics backup is available.")
        selected = candidates[0]
        for source in selected.rglob("*"):
            if source.is_file():
                destination = virtualdj_home / source.relative_to(selected)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
        log(f"Restored the newest backup: {selected}")
        return

    overlay = plugin_directory(virtualdj_home)
    managed = [overlay / name for name in MAC_PAYLOAD_FILES]
    changed = action == "install" or any(path.exists() for path in managed)
    if not changed:
        log("LRC Lyrics is already uninstalled.")
        return

    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")[:-3]
    backup = backups_root / f"{timestamp}-before-{action}"
    backup.mkdir(parents=True)
    for path in managed:
        _backup_item(path, virtualdj_home, backup)
    manifest_path = virtualdj_home / "LRC Lyrics Installation.json"
    _backup_item(manifest_path, virtualdj_home, backup)

    if action == "uninstall":
        for path in managed:
            if path.is_dir():
                shutil.rmtree(path)
            else:
                path.unlink(missing_ok=True)
        manifest_path.unlink(missing_ok=True)
        log(f"LRC Lyrics was uninstalled. Backup: {backup}")
        return

    overlay.mkdir(parents=True, exist_ok=True)
    installed = []
    staging_paths = []
    try:
        for name, destination in zip(MAC_PAYLOAD_FILES, managed):
            source = layout.payload / name
            if not source.is_dir():
                raise FileNotFoundError(f"macOS plugin bundle is missing: {source}")
            staging = overlay / f".{name}.{uuid.uuid4().hex}.installing"
            staging_paths.append(staging)
            shutil.copytree(source, staging)
            if destination.is_dir():
                shutil.rmtree(destination)
            elif destination.exists():
                destination.unlink()
            os.replace(staging, destination)
            staging_paths.remove(staging)
            installed.append(destination)
        _atomic_json(manifest_path, {
            "Version": layout.version,
            "Platform": "macOS",
            "Files": [str(path.relative_to(virtualdj_home)) for path in managed],
        })
    except Exception:
        for staging in staging_paths:
            if staging.is_dir():
                shutil.rmtree(staging)
        for destination in installed:
            if destination.is_dir():
                shutil.rmtree(destination)
        for source in backup.rglob("*"):
            if source.is_file():
                destination = virtualdj_home / source.relative_to(backup)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
        raise
    log(f"LRC Lyrics {layout.version} was installed into: {overlay}")
    log(f"Backup: {backup}")
