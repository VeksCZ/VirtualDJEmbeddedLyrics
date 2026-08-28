import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

from mutagen.id3 import ID3


SCRIPT = Path(__file__).parents[1] / "tools" / "lyrics_tag_converter.py"
SPEC = importlib.util.spec_from_file_location("lyrics_batch_txt_import", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class BatchTxtImportTests(unittest.TestCase):
    def test_txt_writes_and_verifies_both_unsynchronized_tags(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            txt = root / "song.txt"
            ID3().save(mp3, v2_version=3)
            txt.write_text("First\nSecond\n", encoding="utf-8")

            message, changed = MODULE.write_frames(
                mp3, None, txt, "und", overwrite=False)

            self.assertTrue(changed)
            self.assertIn("USLT + UNSYNCEDLYRICS", message)
            self.assertIn("verified", message)
            tags = ID3(mp3)
            self.assertEqual(tags.getall("USLT")[0].text.replace("\r\n", "\n"),
                             "First\nSecond")
            unsynced = [frame for frame in tags.getall("TXXX")
                        if frame.desc == "UNSYNCEDLYRICS"]
            self.assertEqual(unsynced[0].text[0].replace("\r\n", "\n"),
                             "First\nSecond")

    def test_existing_unsynchronized_tag_keeps_txt_without_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            txt = root / "song.txt"
            ID3().save(mp3, v2_version=3)
            txt.write_text("Original", encoding="utf-8")
            MODULE.write_frames(mp3, None, txt, "und", False)
            txt.write_text("Replacement", encoding="utf-8")

            message, changed = MODULE.write_frames(mp3, None, txt, "und", False)

            self.assertFalse(changed)
            self.assertIn("skipped", message)


if __name__ == "__main__":
    unittest.main()
