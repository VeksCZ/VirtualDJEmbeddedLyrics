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
        self.database = self.home / "database.xml"
        self.unrelated_database_path = self.root / "Other" / "Analyzed.mp3"
        self.database.write_text(
            '<?xml version="1.0" encoding="UTF-8"?>\n'
            '<VirtualDJ_Database Version="8.5">\n'
            f' <Song FilePath="{self.unrelated_database_path}" FileSize="123">\n'
            '  <Tags Author="Existing" Title="Analyzed" />\n'
            '  <Scan Version="801" Bpm="0.5" Key="Am" />\n'
            ' </Song>\n'
            '</VirtualDJ_Database>\n',
            encoding="utf-8",
        )

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

    def _database_paths(self) -> list[str]:
        root = ET.fromstring(self.database.read_bytes())
        return [song.attrib["FilePath"] for song in root.findall("Song")]

    @unittest.skipUnless(sys.platform == "win32", "Windows drive layout")
    def test_search_database_is_selected_per_track_drive(self):
        home = Path(r"C:\Users\DJ\VirtualDJ")

        self.assertEqual(
            sync._database_path_for_track(Path(r"C:\Music\Local.mp3"), home),
            home / "database.xml",
        )
        external = sync._database_path_for_track(
            Path(r"D:\Music\External.mp3"), home)
        self.assertEqual(external, Path(r"D:\VirtualDJ\database.xml"))
        self.assertEqual(sync._database_backup_name(external, home),
                         "database-D.before.xml")

    def test_preview_does_not_write_and_real_sync_mirrors_only_managed_root(self):
        original_database = self.database.read_bytes()
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
        self.assertEqual(
            self._database_paths(),
            [str(self.unrelated_database_path)],
        )

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
        self.assertEqual(result.added_tracks, 3)
        self.assertEqual(result.database_added, 3)
        self.assertIsNotNone(result.backup)
        self.assertEqual(
            (result.backup / "database.before.xml").read_bytes(),
            original_database,
        )
        self.assertIn(str(self.unrelated_database_path), self._database_paths())
        self.assertIn(str(first.resolve()), self._database_paths())

        first.unlink()
        new_track = self._track(self.library / "Genres" / "House" / "C.mp3")
        messages = []
        updated = sync.sync_library(
            self.library, self.home, "Folder Sync - DJ", dry_run=False,
            log=messages.append,
        )

        self.assertGreaterEqual(updated.removed, 1)
        self.assertEqual(updated.removed_tracks, 1)
        self.assertEqual(updated.added_tracks, 1)
        self.assertEqual(updated.database_added, 1)
        self.assertFalse((target / "Dance.vdjfolder").exists())
        paths = [song.attrib["path"] for song in ET.fromstring(nested.read_bytes())]
        self.assertEqual(paths, [str(second.resolve()), str(new_track.resolve())])
        self.assertEqual(unrelated.read_text(encoding="utf-8"), "manual")
        self.assertTrue(any((self.home / "Folder Sync Backups").iterdir()))
        database_paths = self._database_paths()
        self.assertIn(str(first.resolve()), database_paths)
        self.assertIn(str(new_track.resolve()), database_paths)
        self.assertIn(str(self.unrelated_database_path), database_paths)
        self.assertTrue(any(
            str(first.resolve()) in message and "REMOVE" in message
            for message in messages
        ))
        self.assertIn(
            '<Scan Version="801" Bpm="0.5" Key="Am" />',
            self.database.read_text(encoding="utf-8"),
        )

    def test_search_database_reactivates_current_track_without_touching_analysis(self):
        track = self._track(self.library / "Current.mp3")
        absent = self.root / "Missing.mp3"
        self.database.write_text(
            '<?xml version="1.0" encoding="UTF-8"?>\r\n'
            '<VirtualDJ_Database Version="8.5">\r\n'
            f' <Song FilePath="{track}" Flag="17">\r\n'
            '  <Scan Version="801" Bpm="0.4" Key="F#m" />\r\n'
            ' </Song>\r\n'
            f' <Song FilePath="{absent}" Flag="16">\r\n'
            '  <Scan Version="801" Bpm="0.6" Key="C" />\r\n'
            ' </Song>\r\n'
            '</VirtualDJ_Database>\r\n',
            encoding="utf-8",
        )

        updated, added, reactivated = sync.plan_search_database(
            self.database, (track.resolve(),))
        text = updated.decode("utf-8")

        self.assertEqual(added, 0)
        self.assertEqual(reactivated, 1)
        current = next(
            song for song in ET.fromstring(updated).findall("Song")
            if song.attrib["FilePath"] == str(track.resolve())
        )
        self.assertNotIn("Flag", current.attrib)
        self.assertIn('<Scan Version="801" Bpm="0.4" Key="F#m" />', text)
        self.assertIn(f'<Song FilePath="{absent}" Flag="16">', text)

    def test_search_database_can_be_disabled(self):
        self._track(self.library / "Not registered.mp3")
        original_database = self.database.read_bytes()

        result = sync.sync_library(
            self.library,
            self.home,
            "Lists only",
            dry_run=False,
            add_search_db=False,
            log=lambda _message: None,
        )

        self.assertEqual(result.database_added, 0)
        self.assertEqual(result.database_reactivated, 0)
        self.assertEqual(self.database.read_bytes(), original_database)
        self.assertFalse((result.backup / "database.before.xml").exists())

    def test_search_database_preview_counts_without_reading_track_metadata(self):
        track = self._track(self.library / "Preview.mp3")
        original_database = self.database.read_bytes()

        with mock.patch.object(sync, "_database_song_xml") as song_xml:
            updated, added, reactivated = sync.plan_search_database(
                self.database,
                (track.resolve(),),
                materialize_additions=False,
            )

        song_xml.assert_not_called()
        self.assertEqual(updated, original_database)
        self.assertEqual(added, 1)
        self.assertEqual(reactivated, 0)

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
        previous_database = self.database.read_bytes()
        track.unlink()
        self._track(self.library / "Dance" / "B.mp3")

        original_atomic_write = sync._atomic_write
        failed = False

        def fail_state_write(path, data):
            nonlocal failed
            if path.parent.name == "Folder Sync State" and not failed:
                failed = True
                raise OSError("simulated state write failure")
            return original_atomic_write(path, data)

        with mock.patch.object(sync, "_atomic_write", side_effect=fail_state_write):
            with self.assertRaises(OSError):
                sync.sync_library(
                    self.library, self.home, "Managed", dry_run=False,
                    log=lambda _message: None,
                )

        self.assertEqual(self._snapshot(target), previous_tree)
        self.assertEqual((self.mylists / "order").read_bytes(), previous_order)
        self.assertEqual(self.database.read_bytes(), previous_database)
        self.assertFalse(any(self.mylists.glob(".Managed.*.subfolders")))

    def test_invalid_or_empty_targets_are_rejected(self):
        for value in ("", "..", "Bad/Name", "CON", "Trailing."):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    sync.validate_target_name(value)


if __name__ == "__main__":
    unittest.main()
