import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

from mutagen.id3 import ID3


SCRIPT = Path(__file__).parents[1] / "tools" / "lyrics_tag_converter.py"
SPEC = importlib.util.spec_from_file_location("lyrics_timed_txt_import", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class TimedTxtImportTests(unittest.TestCase):
    def test_timestamped_txt_is_written_only_to_synced_tags(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            txt = root / "song.txt"
            ID3().save(mp3, v2_version=3)
            txt.write_text("[00:01.20]First\n[00:02.500]Second\n", encoding="utf-8")

            message, changed = MODULE.write_frames(mp3, None, txt, "und", False)

            self.assertTrue(changed)
            self.assertIn("from timed TXT", message)
            tags = ID3(mp3)
            self.assertEqual(len(tags.getall("SYLT")), 1)
            self.assertEqual(tags.getall("SYLT")[0].text,
                             [("First", 1200), ("Second", 2500)])
            self.assertEqual(len([frame for frame in tags.getall("TXXX")
                                  if frame.desc == "SYNCEDLYRICS"]), 1)
            self.assertEqual(tags.getall("USLT"), [])
            self.assertEqual([frame for frame in tags.getall("TXXX")
                              if frame.desc == "UNSYNCEDLYRICS"], [])

    def test_verified_timed_txt_can_be_deleted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            txt = root / "song.txt"
            ID3().save(mp3, v2_version=3)
            txt.write_text("[00:01.00]Timed", encoding="utf-8")

            message, changed = MODULE.write_frames(
                mp3, None, txt, "und", False, delete_sidecars=True)

            self.assertTrue(changed)
            self.assertIn("TXT deleted", message)
            self.assertFalse(txt.exists())

    def test_lrc_has_priority_over_timestamped_txt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            lrc = root / "song.lrc"
            txt = root / "song.txt"
            ID3().save(mp3, v2_version=3)
            lrc.write_text("[00:01.00]From LRC", encoding="utf-8")
            txt.write_text("[00:02.00]From TXT", encoding="utf-8")

            message, changed = MODULE.write_frames(mp3, lrc, txt, "und", False)

            self.assertTrue(changed)
            self.assertIn("LRC has priority", message)
            self.assertEqual(ID3(mp3).getall("SYLT")[0].text, [("From LRC", 1000)])


if __name__ == "__main__":
    unittest.main()
