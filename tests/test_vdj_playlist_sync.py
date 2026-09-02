import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from unittest import mock


TOOL_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOL_DIR))

import vdj_playlist_sync as sync  # noqa: E402


class VirtualDJPlaylistSyncTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.library = self.root / "Music"
        self.home = self.root / "VirtualDJ"
        self.mylists = self.home / "MyLists"
        self.library.mkdir()
        self.mylists.mkdir(parents=True)
        (self.mylists / "order").write_text("Personal\n", encoding="utf-8")

    def tearDown(self):
        self.temporary.cleanup()

    @staticmethod
    def _track(path: Path) -> Path:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"audio")
        return path

    @staticmethod
    def _snapshot(path: Path) -> dict[str, bytes]:
        return {
            str(item.relative_to(path)): item.read_bytes()
            for item in path.rglob("*") if item.is_file()
        }

    def test_preview_does_not_write_and_real_sync_mirrors_only_managed_root(self):
        root_track = self._track(self.library / "Root & One.mp3")
        first = self._track(self.library / "Dance" / "A.mp3")
        second = self._track(self.library / "Genres" / "House" / "B & DJ's Mix.flac")
        (self.library / "Dance" / "cover.jpg").write_bytes(b"image")
        unrelated = self.mylists / "Personal.subfolders" / "Manual.vdjfolder"
        unrelated.parent.mkdir()
        unrelated.write_text("manual", encoding="utf-8")

        preview = sync.sync_library(
            self.library, self.home, "Folder Sync - DJ", dry_run=True,
            log=lambda _message: None,
        )

        target = self.mylists / "Folder Sync - DJ.subfolders"
        self.assertTrue(preview.dry_run)
        self.assertEqual(preview.tracks, 3)
        self.assertFalse(target.exists())
        self.assertFalse((self.home / "Folder Sync Backups").exists())

        result = sync.sync_library(
            self.library, self.home, "Folder Sync - DJ", dry_run=False,
            log=lambda _message: None,
        )

        self.assertFalse(result.dry_run)
        self.assertTrue((target / "_ Tracks in this folder.vdjfolder").is_file())
        self.assertTrue((target / "Dance.vdjfolder").is_file())
        nested = target / "Genres.subfolders" / "House.vdjfolder"
        self.assertTrue(nested.is_file())
        self.assertEqual(unrelated.read_text(encoding="utf-8"), "manual")
        xml_root = ET.fromstring(nested.read_bytes())
        self.assertEqual(xml_root.find("song").attrib["path"], str(second.resolve()))
        root_xml = ET.fromstring(
            (target / "_ Tracks in this folder.vdjfolder").read_bytes())
        self.assertEqual(root_xml.find("song").attrib["path"], str(root_track.resolve()))
        self.assertEqual(
            (self.mylists / "order").read_text(encoding="utf-8").splitlines(),
            ["Personal", "Folder Sync - DJ"],
        )

        first.unlink()
        new_track = self._track(self.library / "Genres" / "House" / "C.mp3")
        updated = sync.sync_library(
            self.library, self.home, "Folder Sync - DJ", dry_run=False,
            log=lambda _message: None,
        )

        self.assertGreaterEqual(updated.removed, 1)
        self.assertFalse((target / "Dance.vdjfolder").exists())
        paths = [song.attrib["path"] for song in ET.fromstring(nested.read_bytes())]
        self.assertEqual(paths, [str(second.resolve()), str(new_track.resolve())])
        self.assertEqual(unrelated.read_text(encoding="utf-8"), "manual")
        self.assertTrue(any((self.home / "Folder Sync Backups").iterdir()))

    def test_existing_unowned_root_requires_explicit_adoption(self):
        self._track(self.library / "Dance" / "A.mp3")
        existing = self.mylists / "Existing.subfolders"
        existing.mkdir()
        (existing / "Manual.vdjfolder").write_text("manual", encoding="utf-8")

        with self.assertRaises(PermissionError):
            sync.sync_library(
                self.library, self.home, "Existing", dry_run=True,
                log=lambda _message: None,
            )

        preview = sync.sync_library(
            self.library, self.home, "Existing", dry_run=True,
            adopt_existing=True, log=lambda _message: None,
        )
        self.assertTrue(preview.dry_run)
        self.assertEqual((existing / "Manual.vdjfolder").read_text(), "manual")

    def test_failure_restores_previous_tree_and_order(self):
        track = self._track(self.library / "Dance" / "A.mp3")
        sync.sync_library(
            self.library, self.home, "Managed", dry_run=False,
            log=lambda _message: None,
        )
        target = self.mylists / "Managed.subfolders"
        previous_tree = self._snapshot(target)
        previous_order = (self.mylists / "order").read_bytes()
        track.unlink()
        self._track(self.library / "Dance" / "B.mp3")

        original_atomic_write = sync._atomic_write
        failed = False

        def fail_first_order_write(path, data):
            nonlocal failed
            if path == self.mylists / "order" and not failed:
                failed = True
                raise OSError("simulated order write failure")
            return original_atomic_write(path, data)

        with mock.patch.object(sync, "_atomic_write", side_effect=fail_first_order_write):
            with self.assertRaises(OSError):
                sync.sync_library(
                    self.library, self.home, "Managed", dry_run=False,
                    log=lambda _message: None,
                )

        self.assertEqual(self._snapshot(target), previous_tree)
        self.assertEqual((self.mylists / "order").read_bytes(), previous_order)
        self.assertFalse(any(self.mylists.glob(".Managed.*.subfolders")))

    def test_invalid_or_empty_targets_are_rejected(self):
        for value in ("", "..", "Bad/Name", "CON", "Trailing."):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    sync.validate_target_name(value)


if __name__ == "__main__":
    unittest.main()
