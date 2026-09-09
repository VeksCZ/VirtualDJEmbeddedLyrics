"""Shared, privacy-conscious GUI helpers for LRC Lyrics applications."""

from __future__ import annotations

import json
import os
import platform
import re
import subprocess
import sys
import urllib.request
import zipfile
from datetime import datetime
from pathlib import Path


REPOSITORY_API = "https://api.github.com/repos/VeksCZ/VirtualDJEmbeddedLyrics/releases/latest"


def package_tools_dir(source_dir: Path) -> Path:
    """Return the packaged Tools directory from source or a frozen app."""
    if not getattr(sys, "frozen", False):
        return source_dir.resolve()
    executable = Path(sys.executable).resolve()
    package_root = executable.parents[3] if sys.platform == "darwin" else executable.parent
    return package_root / "Tools"


def version_tuple(value: str) -> tuple[int, ...]:
    match = re.fullmatch(r"v?(\d+(?:\.\d+)*)", value.strip())
    return tuple(map(int, match.group(1).split("."))) if match else ()


def check_latest_version(current: str, timeout: int = 10) -> tuple[str, str, bool]:
    request = urllib.request.Request(
        REPOSITORY_API,
        headers={"Accept": "application/vnd.github+json", "User-Agent": "LRC-Lyrics-Tools"},
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        data = json.load(response)
    latest = str(data.get("tag_name") or "").removeprefix("v")
    url = str(data.get("html_url") or "")
    if not version_tuple(latest):
        raise RuntimeError("GitHub returned an invalid release version.")
    return latest, url, version_tuple(latest) > version_tuple(current)


def plugin_state(home: Path | None, package_version: str, setup_module) -> tuple[str, str]:
    if home is None:
        return "error", "VirtualDJ folder not found"
    overlay = setup_module.plugin_directory(home)
    present = [name for name in setup_module.PAYLOAD_FILES if (overlay / name).exists()]
    if len(present) != len(setup_module.PAYLOAD_FILES):
        return ("warning", "Plugin installation is incomplete") if present else ("error", "Plugin is not installed")
    manifest = home / "LRC Lyrics Installation.json"
    try:
        installed = str(json.loads(manifest.read_text(encoding="utf-8-sig")).get("Version") or "")
    except (OSError, ValueError, AttributeError):
        installed = ""
    if installed and version_tuple(installed) < version_tuple(package_version):
        return "warning", f"Update available: {installed} → {package_version}"
    return "ok", f"Plugin {installed or package_version} is installed"


def open_path(path: Path) -> None:
    resolved = path.expanduser().resolve()
    if sys.platform == "win32":
        os.startfile(resolved)  # type: ignore[attr-defined]
    elif sys.platform == "darwin":
        subprocess.Popen(["open", str(resolved)])
    else:
        subprocess.Popen(["xdg-open", str(resolved)])


def newest_backup(home: Path | None, folder_name: str) -> Path | None:
    root = home / folder_name if home else None
    if root is None or not root.is_dir():
        return None
    candidates = [path for path in root.iterdir() if path.is_dir()]
    return max(candidates, key=lambda path: path.name) if candidates else None


def create_diagnostic_zip(destination: Path, *, app_name: str, version: str,
                          status: str, recent_log: str = "") -> Path:
    """Create diagnostics without media, tags, credentials, or absolute paths."""
    destination = destination.expanduser().resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    home = str(Path.home())
    sanitized = re.sub(
        r"(?i)(?:[A-Z]:\\|/Volumes/|/Users/)[^\r\n]+",
        "<REDACTED_PATH>",
        recent_log,
    ).replace(home, "<USER_HOME>")
    details = {
        "app": app_name,
        "version": version,
        "created": datetime.now().isoformat(timespec="seconds"),
        "platform": platform.platform(),
        "architecture": platform.machine(),
        "python": platform.python_version(),
        "status": status,
        "privacy": "No music, ID3 tags, credentials, or absolute user paths are included.",
    }
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("system.json", json.dumps(details, indent=2, ensure_ascii=False) + "\n")
        archive.writestr("recent.log", sanitized[-20000:])
    return destination
