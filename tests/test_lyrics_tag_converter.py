import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "tools" / "lyrics_tag_converter.py"
SPEC = importlib.util.spec_from_file_location("lyrics_tag_converter", SCRIPT)
converter = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = converter
SPEC.loader.exec_module(converter)


class DiscoveryTests(unittest.TestCase):
    def test_directory_pairs_sidecars_case_insensitively(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Song.MP3").touch()
            (root / "song.lrc").write_text("[00:01.00]Line", encoding="utf-8")
            (root / "SONG.TXT").write_text("Plain", encoding="utf-8")
            pairs, unmatched = converter.discover_pairs(root)
            self.assertEqual(unmatched, [])
            self.assertEqual(len(pairs), 1)
            self.assertEqual(pairs[0][0].name, "Song.MP3")
            self.assertEqual(pairs[0][1].name, "song.lrc")
            self.assertEqual(pairs[0][2].name, "SONG.TXT")

    def test_reports_sidecar_without_mp3(self):
        with tempfile.TemporaryDirectory() as directory:
            sidecar = Path(directory) / "Missing.txt"
            sidecar.write_text("Plain", encoding="utf-8")
            pairs, unmatched = converter.discover_pairs(sidecar)
            self.assertEqual(pairs, [])
            self.assertEqual(unmatched, [sidecar])


if __name__ == "__main__":
    unittest.main()
