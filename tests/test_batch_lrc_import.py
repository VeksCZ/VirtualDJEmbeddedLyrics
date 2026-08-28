import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

from mutagen.id3 import ID3


SCRIPT = Path(__file__).parents[1] / "tools" / "lyrics_tag_converter.py"
SPEC = importlib.util.spec_from_file_location("lyrics_batch_import", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class BatchLrcImportTests(unittest.TestCase):
    def test_dual_tag_write_verifies_before_deleting_lrc(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            lrc = root / "song.lrc"
            ID3().save(mp3, v2_version=3)
            lrc.write_text("[00:01.20]First\n[00:02.50]Second\n", encoding="utf-8")

            message, changed = MODULE.write_frames(
                mp3, lrc, None, "und", overwrite=False, delete_lrc=True)

            self.assertTrue(changed)
            self.assertIn("verified", message)
            self.assertIn("LRC deleted", message)
            self.assertFalse(lrc.exists())
            tags = ID3(mp3)
            self.assertEqual(len(tags.getall("SYLT")), 1)
            synced = [frame for frame in tags.getall("TXXX")
                      if frame.desc == "SYNCEDLYRICS"]
            self.assertEqual(len(synced), 1)
            self.assertTrue(synced[0].text[0].startswith(
                "[re:VirtualDJ Embedded Lyrics - imported from LRC]"))

    def test_existing_synced_tags_are_skipped_and_lrc_is_kept(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            lrc = root / "song.lrc"
            ID3().save(mp3, v2_version=3)
            lrc.write_text("[00:01.20]First\n", encoding="utf-8")
            MODULE.write_frames(mp3, lrc, None, "und", False, False)
            lrc.write_text("[00:03.00]Replacement\n", encoding="utf-8")

            message, changed = MODULE.write_frames(
                mp3, lrc, None, "und", overwrite=False, delete_lrc=True)

            self.assertFalse(changed)
            self.assertIn("skipped", message)
            self.assertTrue(lrc.exists())


if __name__ == "__main__":
    unittest.main()
