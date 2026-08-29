import tempfile
import unittest
from pathlib import Path

from mutagen.id3 import ID3, SYLT, TIT1, TXXX, USLT

from tools.lyrics_tag_converter import mark_existing_mp3


class MarkLyricsGroupingTests(unittest.TestCase):
    def test_marks_synced_and_preserves_existing_grouping(self):
        with tempfile.TemporaryDirectory() as directory:
            mp3 = Path(directory) / "synced.mp3"
            tags = ID3()
            tags.add(TIT1(encoding=3, text=["Wedding"]))
            tags.add(SYLT(encoding=3, lang="und", format=2, type=1,
                          desc="test", text=[("Hello", 1000)]))
            tags.save(mp3)
            self.assertEqual(mark_existing_mp3(Path(directory), True), (1, 1, 0))
            self.assertEqual(ID3(mp3).getall("TIT1")[0].text,
                             ["Wedding; Lyrics: Synced"])

    def test_synced_wins_and_existing_marker_is_replaced(self):
        with tempfile.TemporaryDirectory() as directory:
            mp3 = Path(directory) / "both.mp3"
            tags = ID3()
            tags.add(TIT1(encoding=3, text=["Lyrics: Unsynced"]))
            tags.add(USLT(encoding=3, lang="und", desc="", text="Plain"))
            tags.add(TXXX(encoding=3, desc="SYNCEDLYRICS", text=["[00:01.00]Timed"]))
            tags.save(mp3)
            self.assertEqual(mark_existing_mp3(mp3, True), (1, 1, 0))
            self.assertEqual(ID3(mp3).getall("TIT1")[0].text,
                             ["Lyrics: Synced"])

    def test_ignores_mp3_without_lyrics(self):
        with tempfile.TemporaryDirectory() as directory:
            mp3 = Path(directory) / "plain.mp3"
            ID3().save(mp3)
            self.assertEqual(mark_existing_mp3(Path(directory), True), (0, 0, 0))


if __name__ == "__main__":
    unittest.main()
