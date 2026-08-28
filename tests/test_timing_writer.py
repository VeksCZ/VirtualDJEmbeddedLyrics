import importlib.util
import tempfile
import unittest
from pathlib import Path

from mutagen.id3 import ID3


SCRIPT = Path(__file__).parents[1] / "tools" / "lyrics_tag_converter.py"
SPEC = importlib.util.spec_from_file_location("lyrics_tag_converter_writer", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
import sys
sys.modules[SPEC.name] = MODULE
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class TimingWriterTests(unittest.TestCase):
    def test_writes_both_synced_frames_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            timing = root / "song.timing"
            ID3().save(mp3, v2_version=3)
            timing.write_text("1200\tFirst line\n2500\tSecond line\n", encoding="utf-8")

            self.assertEqual(MODULE.write_recording(mp3, timing), 0)
            tags = ID3(mp3)
            self.assertEqual(tags.getall("SYLT")[0].desc,
                             "Manually timed in VirtualDJ Embedded Lyrics")
            synced = [frame for frame in tags.getall("TXXX")
                      if frame.desc == "SYNCEDLYRICS"]
            self.assertEqual(len(synced), 1)
            self.assertTrue(str(synced[0].text[0]).startswith(
                "[re:VirtualDJ Embedded Lyrics - manual timing]"))
            self.assertFalse(timing.exists())

            second = root / "second.timing"
            second.write_text("3000\tReplacement\n", encoding="utf-8")
            self.assertEqual(MODULE.write_recording(mp3, second), 3)
            self.assertTrue(second.exists())


if __name__ == "__main__":
    unittest.main()
