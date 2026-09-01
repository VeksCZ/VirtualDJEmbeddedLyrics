#!/usr/bin/env python3
"""Restore sidecar LRC files from a structure-preserving backup."""

from __future__ import annotations

import argparse
import os
import shutil
import tempfile
from collections import Counter
from collections.abc import Callable
from pathlib import Path

from lrc_tool import BACKUP_FORMAT_MARKER, backup_path_for, iter_mp3_files, validate_directories


LogFunction = Callable[[object], None]


def restore_library(library_dir: Path, backup_dir: Path, *, overwrite: bool = False,
                    dry_run: bool = False, log: LogFunction = print):
    """Restore matching sidecars while refusing ambiguous legacy flat backups."""
    library_dir, backup_dir = validate_directories(library_dir, backup_dir)
    mp3_files = iter_mp3_files(library_dir, backup_dir)
    basename_counts = Counter(path.stem.casefold() for path in mp3_files)
    marker = backup_dir / BACKUP_FORMAT_MARKER
    try:
        structured_format = marker.exists() and marker.read_text(
            encoding="utf-8", errors="replace"
        ).strip() == "2"
    except OSError as exc:
        structured_format = False
        log(f"[WARNING] Backup format marker could not be read: {exc}")

    log(f"Music library: {library_dir}")
    log(f"LRC backup:    {backup_dir}")
    if dry_run:
        log("DRY RUN: no sidecar files will be written.")
    log("")

    restored = skipped = missing = errors = 0
    for mp3_path in mp3_files:
        target = mp3_path.with_suffix(".lrc")
        source = backup_path_for(mp3_path, library_dir, backup_dir) if structured_format else None
        legacy_source = backup_dir / f"{mp3_path.stem}.lrc"

        if (source is None or not source.exists()) and legacy_source.exists():
            if basename_counts[mp3_path.stem.casefold()] == 1:
                source = legacy_source
                log(f"[LEGACY BACKUP] {mp3_path.name}")
            else:
                log(f"[AMBIGUOUS LEGACY BACKUP] {mp3_path}; skipped for safety")
                skipped += 1
                continue

        if source is None or not source.exists():
            log(f"[MISSING BACKUP] {mp3_path.name}")
            missing += 1
            continue
        if target.exists() and not overwrite:
            log(f"[SKIPPED: SIDECAR EXISTS] {mp3_path.name}")
            skipped += 1
            continue

        prefix = "[WOULD RESTORE]" if dry_run else "[RESTORED]"
        try:
            if not dry_run:
                target.parent.mkdir(parents=True, exist_ok=True)
                descriptor, temporary_name = tempfile.mkstemp(
                    prefix=f".{target.name}.", suffix=".restore.tmp", dir=target.parent
                )
                os.close(descriptor)
                temporary = Path(temporary_name)
                try:
                    shutil.copy2(source, temporary)
                    os.replace(temporary, target)
                finally:
                    temporary.unlink(missing_ok=True)
            restored += 1
            log(f"{prefix} {mp3_path.name}")
        except Exception as exc:
            errors += 1
            log(f"[ERROR] {mp3_path}: {exc}")

    log(f"\nComplete. Restored: {restored}; skipped: {skipped}; "
        f"missing: {missing}; errors: {errors}; total MP3 files: {len(mp3_files)}.")
    return restored, skipped, missing, errors


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library_dir", type=Path, nargs="?", default=Path.cwd())
    parser.add_argument("backup_dir", type=Path, nargs="?", default=None)
    parser.add_argument("--overwrite", action="store_true", help="Replace existing sidecars")
    parser.add_argument("--dry-run", action="store_true", help="Do not write sidecar files")
    args = parser.parse_args()
    backup_dir = args.backup_dir or args.library_dir / "_lrc_backup"
    restore_library(args.library_dir, backup_dir, overwrite=args.overwrite, dry_run=args.dry_run)


if __name__ == "__main__":
    raise SystemExit(main())
