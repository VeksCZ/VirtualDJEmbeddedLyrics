"""Shared, privacy-conscious GUI helpers for LRC Lyrics applications."""

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
import urllib.request
import zipfile
from datetime import datetime
from pathlib import Path


REPOSITORY_API = "https://api.github.com/repos/VeksCZ/VirtualDJEmbeddedLyrics/releases/latest"


def packaged_version(source_dir: Path) -> str:
    candidates = [package_tools_dir(source_dir).parent / "VERSION", source_dir.parent / "VERSION"]
    for candidate in candidates:
        try:
            value = candidate.read_text(encoding="utf-8-sig").strip()
        except OSError:
            continue
        if version_tuple(value):
            return value
    return "0.0.0"


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
    data = latest_release(timeout)
    latest = str(data.get("tag_name") or "").removeprefix("v")
    url = str(data.get("html_url") or "")
    if not version_tuple(latest):
        raise RuntimeError("GitHub returned an invalid release version.")
    return latest, url, version_tuple(latest) > version_tuple(current)


def latest_release(timeout: int = 10) -> dict:
    request = urllib.request.Request(
        REPOSITORY_API,
        headers={"Accept": "application/vnd.github+json", "User-Agent": "LRC-Lyrics-Tools"},
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        data = json.load(response)
    if not isinstance(data, dict):
        raise RuntimeError("GitHub returned an invalid release response.")
    return data


def release_variant() -> str:
    if sys.platform == "win32":
        return "Windows"
    if sys.platform == "darwin":
        return "macOS-AppleSilicon" if platform.machine().lower() in {"arm64", "aarch64"} else "macOS-Intel"
    raise RuntimeError("Automatic updates are supported on Windows and macOS.")


def select_release_assets(release: dict, product: str) -> tuple[dict, dict]:
    """Return the ZIP and matching checksum asset for this computer."""
    prefix = f"{product}-{release_variant()}-v"
    assets = release.get("assets") or []
    by_name = {str(item.get("name")): item for item in assets if isinstance(item, dict)}
    zip_names = sorted(name for name in by_name if name.startswith(prefix) and name.endswith(".zip"))
    if len(zip_names) != 1:
        raise RuntimeError(f"The release does not contain one {product} package for {release_variant()}.")
    zip_asset = by_name[zip_names[0]]
    checksum = by_name.get(zip_names[0] + ".sha256")
    if checksum is None:
        raise RuntimeError("The release checksum is missing.")
    return zip_asset, checksum


def _download(url: str, destination: Path) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": "LRC-Lyrics-Tools"})
    with urllib.request.urlopen(request, timeout=60) as response, destination.open("wb") as output:
        shutil.copyfileobj(response, output)


def update_storage_dir(product: str, version: str) -> Path:
    if sys.platform == "win32":
        local = os.environ.get("LOCALAPPDATA")
        base = Path(local) if local else Path.home() / "AppData" / "Local"
        return base / "VirtualDJEmbeddedLyrics" / "Updates" / product / version
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / "VirtualDJEmbeddedLyrics" / "Updates" / product / version
    return Path(tempfile.gettempdir()) / "VirtualDJEmbeddedLyrics" / "Updates" / product / version


def download_update(release: dict, product: str, *, destination_root: Path | None = None) -> Path:
    """Download, verify and safely extract a release package."""
    zip_asset, checksum_asset = select_release_assets(release, product)
    version = str(release.get("tag_name") or "unknown").removeprefix("v")
    if not version_tuple(version):
        raise RuntimeError("The release has an invalid version.")
    root = destination_root or update_storage_dir(product, version)
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True, exist_ok=True)
    zip_path = root / str(zip_asset["name"])
    checksum_path = root / str(checksum_asset["name"])
    _download(str(zip_asset["browser_download_url"]), zip_path)
    _download(str(checksum_asset["browser_download_url"]), checksum_path)
    expected = checksum_path.read_text(encoding="utf-8-sig").split()[0].lower()
    digest = hashlib.sha256()
    with zip_path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    actual = digest.hexdigest()
    if not re.fullmatch(r"[0-9a-f]{64}", expected) or actual != expected:
        raise RuntimeError("The downloaded update failed SHA-256 verification.")
    extracted = root / "extracted"
    extracted.mkdir()
    with zipfile.ZipFile(zip_path) as archive:
        base = extracted.resolve()
        for member in archive.infolist():
            target = (extracted / member.filename).resolve()
            try:
                target.relative_to(base)
            except ValueError as exc:
                raise RuntimeError("The update archive contains an unsafe path.") from exc
        if sys.platform != "darwin":
            archive.extractall(extracted)
    if sys.platform == "darwin":
        completed = subprocess.run(
            ["/usr/bin/ditto", "-x", "-k", str(zip_path), str(extracted)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False, timeout=120,
        )
        if completed.returncode:
            raise RuntimeError("macOS could not extract the verified update.")
    children = list(extracted.iterdir())
    return children[0] if len(children) == 1 and children[0].is_dir() else extracted


def launch_updated_app(package: Path, executable_name: str) -> Path:
    if sys.platform == "win32":
        target = package / f"{executable_name}.exe"
        if not target.is_file():
            raise FileNotFoundError(f"Updated application is missing: {target.name}")
        subprocess.Popen([str(target)], creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    elif sys.platform == "darwin":
        target = package / f"{executable_name}.app"
        if not target.is_dir():
            raise FileNotFoundError(f"Updated application is missing: {target.name}")
        subprocess.Popen(["open", str(target)])
    else:
        raise RuntimeError("Automatic updates are supported on Windows and macOS.")
    return target


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
