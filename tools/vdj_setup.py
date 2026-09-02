"""Safe bridge from the Python GUI to the verified PowerShell installer."""

from __future__ import annotations

import json
import locale
import os
import shutil
import subprocess
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


PAYLOAD_FILES = (
    "LRCMaster.dll",
    "LRCBlackOut.dll",
    "EmbeddedLyricsTagWriter.py",
)
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
    return path.is_dir() and all((path / name).is_file() for name in PAYLOAD_FILES)


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
            root / "dist" / f"LRC-Lyrics-VirtualDJ-v{version}" / "Plugins",
            root / "dist" / "full",
        )
        payload = next((path for path in payload_candidates if _complete_payload(path)), None)
        if payload is None:
            release_zip = root / "dist" / f"LRC-Lyrics-VirtualDJ-v{version}.zip"
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


def query_virtualdj(layout: PackageLayout, explicit_path: str = "") -> dict:
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
    overlay = virtualdj_home / "Plugins64" / "VideoOverlay"
    present = [name for name in PAYLOAD_FILES if (overlay / name).is_file()]
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


def assert_virtualdj_closed(layout: PackageLayout) -> None:
    result = query_virtualdj(layout)
    if result.get("VirtualDJRunning"):
        raise RuntimeError(
            "VirtualDJ is running. Close VirtualDJ completely before changing MyLists."
        )


def build_action_command(layout: PackageLayout, action: str, virtualdj_home: Path) -> list[str]:
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
