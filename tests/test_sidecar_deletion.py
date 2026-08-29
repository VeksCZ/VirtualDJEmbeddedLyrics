import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

from mutagen.id3 import ID3, TXXX


SCRIPT = Path(__file__).parents[1] / "tools" / "lyrics_tag_converter.py"
SPEC = importlib.util.spec_from_file_location("lyrics_sidecar_deletion", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class SidecarDeletionTests(unittest.TestCase):
    def test_deletes_lrc_and_txt_only_after_both_are_written_and_verified(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            lrc = root / "song.lrc"
            txt = root / "song.txt"
            ID3().save(mp3, v2_version=3)
            lrc.write_text("[00:01.00]Timed", encoding="utf-8")
            txt.write_text("Untimed", encoding="utf-8")

            message, changed = MODULE.write_frames(
                mp3, lrc, txt, "und", False, delete_sidecars=True)

            self.assertTrue(changed)
            self.assertIn("LRC deleted", message)
            self.assertIn("TXT deleted", message)
            self.assertFalse(lrc.exists())
            self.assertFalse(txt.exists())

    def test_does_not_delete_txt_when_existing_tag_causes_skip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            txt = root / "song.txt"
            tags = ID3()
            tags.add(TXXX(desc="UNSYNCEDLYRICS", text=["Existing"]))
            tags.save(mp3, v2_version=3)
            txt.write_text("Replacement", encoding="utf-8")

            message, changed = MODULE.write_frames(
                mp3, None, txt, "und", False, delete_sidecars=True)

            self.assertTrue(changed)
            self.assertIn("skipped", message)
            self.assertIn("Grouping: Lyrics: Unsynced", message)
            self.assertTrue(txt.exists())


if __name__ == "__main__":
    unittest.main()
