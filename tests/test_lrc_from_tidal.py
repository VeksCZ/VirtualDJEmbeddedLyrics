import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from mutagen.id3 import ID3, SYLT, Encoding


TOOL_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOL_DIR))

import lrc_tool  # noqa: E402
import restore_lrc  # noqa: E402


class LrcParserTests(unittest.TestCase):
    def test_multiple_timestamps_on_one_line(self):
        self.assertEqual(
            lrc_tool.parse_lrc("[00:01.00][00:02.50]Repeat"),
            [(1000, "Repeat"), (2500, "Repeat")],
        )

    def test_metadata_and_invalid_seconds_are_ignored(self):
        self.assertEqual(lrc_tool.parse_lrc("[ar:Artist]\n[00:60.00]Bad"), [])


class BackupAndRestoreTests(unittest.TestCase):
    def test_backups_preserve_relative_directories(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / "music"
            backup = root / "backup"
            first = library / "Album A" / "song.mp3"
            second = library / "Album B" / "song.mp3"
            first.parent.mkdir(parents=True)
            second.parent.mkdir(parents=True)
            first.touch()
            second.touch()

            first_target = lrc_tool.backup_lrc(first, "first", library, backup, False)
            second_target = lrc_tool.backup_lrc(second, "second", library, backup, False)

            self.assertNotEqual(first_target, second_target)
            self.assertEqual(first_target.read_text(encoding="utf-8"), "first")
            self.assertEqual(second_target.read_text(encoding="utf-8"), "second")

    def test_restore_uses_structured_backup(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / "music"
            backup = root / "backup"
            mp3 = library / "Album" / "song.mp3"
            mp3.parent.mkdir(parents=True)
            mp3.touch()
            source = backup / "Album" / "song.lrc"
            source.parent.mkdir(parents=True)
            source.write_text("lyrics", encoding="utf-8")
            (backup / lrc_tool.BACKUP_FORMAT_MARKER).write_text("2\n", encoding="utf-8")

            result = restore_lrc.restore_library(library, backup, log=lambda _: None)

            self.assertEqual(result, (1, 0, 0, 0))
            self.assertEqual(mp3.with_suffix(".lrc").read_text(encoding="utf-8"), "lyrics")

    def test_ambiguous_legacy_backup_is_never_restored(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / "music"
            backup = root / "backup"
            for album in ("A", "B"):
                mp3 = library / album / "song.mp3"
                mp3.parent.mkdir(parents=True)
                mp3.touch()
            backup.mkdir()
            (backup / "song.lrc").write_text("ambiguous", encoding="utf-8")

            result = restore_lrc.restore_library(library, backup, log=lambda _: None)

            self.assertEqual(result, (0, 2, 0, 0))
            self.assertFalse((library / "A" / "song.lrc").exists())
            self.assertFalse((library / "B" / "song.lrc").exists())

    def test_unique_legacy_flat_backup_is_supported(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / "music"
            backup = root / "backup"
            mp3 = library / "Album" / "unique.mp3"
            mp3.parent.mkdir(parents=True)
            mp3.touch()
            backup.mkdir()
            (backup / "unique.lrc").write_text("legacy", encoding="utf-8")

            result = restore_lrc.restore_library(library, backup, log=lambda _: None)

            self.assertEqual(result, (1, 0, 0, 0))
            self.assertEqual(mp3.with_suffix(".lrc").read_text(encoding="utf-8"), "legacy")

    def test_dry_run_writes_nothing(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / "music"
            library.mkdir()
            mp3 = library / "song.mp3"
            sidecar = library / "song.lrc"
            mp3.touch()
            sidecar.write_text("[00:01.00]Line", encoding="utf-8")
            backup = root / "backup"
            report = root / "report.csv"

            original_read_tags = lrc_tool.read_tags
            original_read_existing = lrc_tool.read_existing_uslt
            try:
                lrc_tool.read_tags = lambda _: ("Artist", "Title", None, 120.0, None)
                lrc_tool.read_existing_uslt = lambda _: []
                rows = lrc_tool.run_library(
                    library, backup, session_file=root / "session.json", report_path=report,
                    do_tidal=False, dry_run=True, log=lambda _: None,
                )
            finally:
                lrc_tool.read_tags = original_read_tags
                lrc_tool.read_existing_uslt = original_read_existing

            self.assertEqual(len(rows), 1)
            self.assertFalse(backup.exists())
            self.assertFalse(report.exists())
            self.assertTrue(sidecar.exists())


class Id3Tests(unittest.TestCase):
    def test_old_sylt_is_removed_when_replacement_has_no_timestamps(self):
        with tempfile.TemporaryDirectory() as temporary:
            mp3 = Path(temporary) / "song.mp3"
            tags = ID3()
            tags.add(SYLT(encoding=Encoding.UTF8, lang="eng", format=2, type=1,
                          desc="", text=[("old", 1000)]))
            tags.save(mp3)

            lrc_tool.embed_lyrics(mp3, "Untimed replacement", True, False)

            updated = ID3(mp3)
            self.assertEqual(len(updated.getall("USLT")), 1)
            self.assertEqual(updated.getall("SYLT"), [])


class TidalMatchingTests(unittest.TestCase):
    def test_isrc_uses_tidalapi_method(self):
        expected = object()
        client = lrc_tool.TidalClient.__new__(lrc_tool.TidalClient)
        client.log = lambda _: None
        client.session = SimpleNamespace(get_tracks_by_isrc=lambda value: [expected] if value == "ISRC" else [])
        self.assertIs(client.find_by_isrc("ISRC"), expected)

    def test_search_rejects_wrong_artist_with_same_title_and_duration(self):
        wrong = SimpleNamespace(name="Home", duration=200, artist=SimpleNamespace(name="Wrong Artist"))
        right = SimpleNamespace(name="Home", duration=200, artist=SimpleNamespace(name="Target Artist"))
        client = lrc_tool.TidalClient.__new__(lrc_tool.TidalClient)
        client.log = lambda _: None
        client.tidalapi = SimpleNamespace(media=SimpleNamespace(Track=object))
        client.session = SimpleNamespace(search=lambda *args, **kwargs: SimpleNamespace(tracks=[wrong, right]))
        self.assertIs(client.find_by_search("Target Artist", "Home", 200, 3), right)


class ValidationTests(unittest.TestCase):
    def test_backup_must_not_equal_or_contain_library(self):
        with tempfile.TemporaryDirectory() as temporary:
            library = Path(temporary) / "music"
            library.mkdir()
            with self.assertRaises(ValueError):
                lrc_tool.validate_directories(library, library)
            with self.assertRaises(ValueError):
                lrc_tool.validate_directories(library, library.parent)


if __name__ == "__main__":
    unittest.main()
