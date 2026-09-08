"""Safe bridge from the Python GUI to the verified PowerShell installer."""

from __future__ import annotations

import json
import locale
import os
import platform
import shutil
import subprocess
import sys
import tempfile
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
INSTALLER_FILES = {
    "install": "install-plugin.ps1",
    "uninstall": "uninstall-plugin.ps1",
    "restore": "restore-backup.ps1",
}


@dataclass(frozen=True)
class PackageLayout:
    root: Path
    scripts: Path
    payload: Path
    detector: Path
    version: str

    def action_script(self, action: str) -> Path:
        try:
            name = INSTALLER_FILES[action]
        except KeyError as exc:
            raise ValueError(f"Unsupported setup action: {action}") from exc
        return self.scripts / name


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
        for name in PAYLOAD_FILES:
            data = archive.read(f"Plugins/{name}")
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
    """Locate installer scripts and the matching plugin payload.

    Release packages keep installers at their root. The source tree keeps the
    canonical scripts in ``installer`` and extracts the matching built release
    payload into the local runtime directory when necessary.
    """

    script_dir = script_dir.expanduser().resolve()
    root = script_dir.parent

    release_payload = root / "Plugins"
    if sys.platform == "darwin" and _complete_payload(release_payload):
        return PackageLayout(
            root=root,
            scripts=root,
            payload=release_payload,
            detector=root / "detect-vdj-home.ps1",
            version=_read_version(root),
        )
    if (all((root / name).is_file() for name in INSTALLER_FILES.values())
            and (root / "detect-vdj-home.ps1").is_file()
            and _complete_payload(release_payload)):
        return PackageLayout(
            root=root,
            scripts=root,
            payload=release_payload,
            detector=root / "detect-vdj-home.ps1",
            version=_read_version(root),
        )

    source_scripts = root / "installer"
    if (all((source_scripts / name).is_file() for name in INSTALLER_FILES.values())
            and (source_scripts / "detect-vdj-home.ps1").is_file()):
        version = _read_version(root)
        payload_candidates = (
            root / "dist" / f"LRC-Lyrics-VirtualDJ-Windows-v{version}" / "Plugins",
            root / "dist" / f"LRC-Lyrics-VirtualDJ-v{version}" / "Plugins",
            root / "dist" / "full",
        )
        payload = next((path for path in payload_candidates if _complete_payload(path)), None)
        if payload is None and sys.platform != "darwin":
            release_zip = root / "dist" / f"LRC-Lyrics-VirtualDJ-Windows-v{version}.zip"
            if release_zip.is_file():
                try:
                    payload = _extract_payload_from_zip(release_zip, version, runtime_dir)
                except (OSError, KeyError, zipfile.BadZipFile):
                    payload = None
        if payload is not None:
            return PackageLayout(
                root=root,
                scripts=source_scripts,
                payload=payload,
                detector=source_scripts / "detect-vdj-home.ps1",
                version=version,
            )

    if sys.platform == "darwin":
        raise FileNotFoundError(
            "The macOS LRC Master and LRC BlackOut bundles were not found. "
            "Use the complete macOS release package or build it on macOS."
        )
    raise FileNotFoundError(
        "The complete installer payload was not found. Extract the complete "
        "release ZIP, or run build-release.ps1 before using setup from source."
    )


def _powershell() -> str:
    executable = shutil.which("powershell.exe") or shutil.which("powershell")
    if executable is None:
        raise FileNotFoundError("Windows PowerShell was not found.")
    return executable


def _base_command(script: Path) -> list[str]:
    return [
        _powershell(),
        "-NoLogo",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(script),
    ]


def _decode(data: bytes) -> str:
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16")
    if data and data.count(b"\x00") > len(data) // 4:
        return data.decode("utf-16-le", errors="replace")
    for encoding in ("utf-8-sig", locale.getpreferredencoding(False), "cp1252"):
        try:
            return data.decode(encoding)
        except (UnicodeDecodeError, LookupError):
            continue
    return data.decode("utf-8", errors="replace")


def query_virtualdj(layout: PackageLayout | None = None, explicit_path: str = "") -> dict:
    if sys.platform == "darwin":
        return _query_virtualdj_mac(explicit_path)
    if layout is None:
        raise FileNotFoundError("The Windows VirtualDJ detector is unavailable.")
    command = _base_command(layout.detector)
    if explicit_path.strip():
        command.extend(("-ExplicitPath", explicit_path.strip()))
    completed = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        timeout=15,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    output = _decode(completed.stdout).strip().lstrip("\ufeff")
    if completed.returncode != 0:
        raise RuntimeError(output or "VirtualDJ folder detection failed.")
    for line in reversed(output.splitlines()):
        try:
            result = json.loads(line.strip().lstrip("\ufeff"))
        except (json.JSONDecodeError, TypeError):
            continue
        if isinstance(result, dict):
            return result
    raise RuntimeError("VirtualDJ folder detection returned an invalid response.")


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


def build_action_command(layout: PackageLayout, action: str, virtualdj_home: Path) -> list[str]:
    if sys.platform == "darwin":
        raise RuntimeError("macOS setup actions run directly and do not use PowerShell.")
    command = _base_command(layout.action_script(action))
    command.extend(("-VirtualDJHome", str(virtualdj_home), "-NonInteractive"))
    if action == "install":
        command.extend(("-PayloadDirectory", str(layout.payload)))
    return command


def run_action(
    layout: PackageLayout,
    action: str,
    virtualdj_home: Path,
    log: Callable[[str], None] = print,
) -> None:
    if sys.platform == "darwin":
        _run_action_mac(layout, action, virtualdj_home, log)
        return
    command = build_action_command(layout, action, virtualdj_home)
    log(f"VirtualDJ home: {virtualdj_home}")
    log(f"Package version: {layout.version}")
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    assert process.stdout is not None
    for raw_line in iter(process.stdout.readline, b""):
        line = _decode(raw_line).rstrip("\r\n")
        if line:
            log(line)
    return_code = process.wait()
    if return_code:
        raise RuntimeError(f"VirtualDJ setup failed with exit code {return_code}.")


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
