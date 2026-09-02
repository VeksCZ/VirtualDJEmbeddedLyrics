import sys
import tempfile
import unittest
from pathlib import Path

from mutagen.id3 import ID3, SYLT, TIT1, TXXX, USLT


PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT))

from tools import lyrics_tag_converter as converter  # noqa: E402


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


class BatchLrcImportTests(unittest.TestCase):
    def test_gui_api_preview_uses_callback_and_writes_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            lrc = root / "song.lrc"
            ID3().save(mp3, v2_version=3)
            lrc.write_text("[00:01.20]First\n", encoding="utf-8")
            messages = []

            summary = converter.import_sidecars(root, write=False, log=messages.append)

            self.assertEqual(summary, (1, 0, 0, 0))
            self.assertTrue(any("DRY-RUN" in message for message in messages))
            self.assertTrue(lrc.exists())
            self.assertFalse(ID3(mp3).getall("SYLT"))

    def test_dual_tag_write_verifies_before_deleting_lrc(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            lrc = root / "song.lrc"
            ID3().save(mp3, v2_version=3)
            lrc.write_text("[00:01.20]First\n[00:02.50]Second\n", encoding="utf-8")

            message, changed = converter.write_frames(
                mp3, lrc, None, "und", overwrite=False, delete_lrc=True)

            self.assertTrue(changed)
            self.assertIn("verified", message)
            self.assertIn("LRC deleted", message)
            self.assertFalse(lrc.exists())
            tags = ID3(mp3)
            self.assertEqual(len(tags.getall("SYLT")), 1)
            synced = [
                frame for frame in tags.getall("TXXX")
                if frame.desc == "SYNCEDLYRICS"
            ]
            self.assertEqual(len(synced), 1)
            self.assertEqual(tags.getall("TIT1")[0].text, ["Lyrics: Synced"])
            self.assertTrue(synced[0].text[0].startswith(
                "[re:VirtualDJ Embedded Lyrics - imported from LRC]"))

    def test_existing_synced_tags_are_skipped_and_lrc_is_kept(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            lrc = root / "song.lrc"
            ID3().save(mp3, v2_version=3)
            lrc.write_text("[00:01.20]First\n", encoding="utf-8")
            converter.write_frames(mp3, lrc, None, "und", False, False)
            lrc.write_text("[00:03.00]Replacement\n", encoding="utf-8")

            message, changed = converter.write_frames(
                mp3, lrc, None, "und", overwrite=False, delete_lrc=True)

            self.assertFalse(changed)
            self.assertIn("skipped", message)
            self.assertTrue(lrc.exists())


class BatchTxtImportTests(unittest.TestCase):
    def test_txt_writes_and_verifies_both_unsynchronized_tags(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            txt = root / "song.txt"
            ID3().save(mp3, v2_version=3)
            txt.write_text("First\nSecond\n", encoding="utf-8")

            message, changed = converter.write_frames(
                mp3, None, txt, "und", overwrite=False)

            self.assertTrue(changed)
            self.assertIn("USLT + UNSYNCEDLYRICS", message)
            self.assertIn("verified", message)
            tags = ID3(mp3)
            self.assertEqual(
                tags.getall("USLT")[0].text.replace("\r\n", "\n"),
                "First\nSecond",
            )
            unsynced = [
                frame for frame in tags.getall("TXXX")
                if frame.desc == "UNSYNCEDLYRICS"
            ]
            self.assertEqual(tags.getall("TIT1")[0].text, ["Lyrics: Unsynced"])
            self.assertEqual(
                unsynced[0].text[0].replace("\r\n", "\n"),
                "First\nSecond",
            )

    def test_existing_unsynchronized_tag_keeps_txt_without_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            txt = root / "song.txt"
            ID3().save(mp3, v2_version=3)
            txt.write_text("Original", encoding="utf-8")
            converter.write_frames(mp3, None, txt, "und", False)
            txt.write_text("Replacement", encoding="utf-8")

            message, changed = converter.write_frames(mp3, None, txt, "und", False)

            self.assertFalse(changed)
            self.assertIn("skipped", message)


class TimedTxtImportTests(unittest.TestCase):
    def test_timestamped_txt_is_written_only_to_synced_tags(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            txt = root / "song.txt"
            ID3().save(mp3, v2_version=3)
            txt.write_text("[00:01.20]First\n[00:02.500]Second\n", encoding="utf-8")

            message, changed = converter.write_frames(mp3, None, txt, "und", False)

            self.assertTrue(changed)
            self.assertIn("from timed TXT", message)
            tags = ID3(mp3)
            self.assertEqual(len(tags.getall("SYLT")), 1)
            self.assertEqual(
                tags.getall("SYLT")[0].text,
                [("First", 1200), ("Second", 2500)],
            )
            self.assertEqual(len([
                frame for frame in tags.getall("TXXX")
                if frame.desc == "SYNCEDLYRICS"
            ]), 1)
            self.assertEqual(tags.getall("USLT"), [])
            self.assertEqual([
                frame for frame in tags.getall("TXXX")
                if frame.desc == "UNSYNCEDLYRICS"
            ], [])

    def test_verified_timed_txt_can_be_deleted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            txt = root / "song.txt"
            ID3().save(mp3, v2_version=3)
            txt.write_text("[00:01.00]Timed", encoding="utf-8")

            message, changed = converter.write_frames(
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

            message, changed = converter.write_frames(
                mp3, lrc, txt, "und", False)

            self.assertTrue(changed)
            self.assertIn("LRC has priority", message)
            self.assertEqual(
                ID3(mp3).getall("SYLT")[0].text,
                [("From LRC", 1000)],
            )


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

            message, changed = converter.write_frames(
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

            message, changed = converter.write_frames(
                mp3, None, txt, "und", False, delete_sidecars=True)

            self.assertTrue(changed)
            self.assertIn("skipped", message)
            self.assertIn("Grouping: Lyrics: Unsynced", message)
            self.assertTrue(txt.exists())


class MarkLyricsGroupingTests(unittest.TestCase):
    def test_marks_synced_and_preserves_existing_grouping(self):
        with tempfile.TemporaryDirectory() as directory:
            mp3 = Path(directory) / "synced.mp3"
            tags = ID3()
            tags.add(TIT1(encoding=3, text=["Wedding"]))
            tags.add(SYLT(
                encoding=3,
                lang="und",
                format=2,
                type=1,
                desc="test",
                text=[("Hello", 1000)],
            ))
            tags.save(mp3)

            self.assertEqual(converter.mark_existing_mp3(Path(directory), True), (1, 1, 0))
            self.assertEqual(
                ID3(mp3).getall("TIT1")[0].text,
                ["Wedding; Lyrics: Synced"],
            )

    def test_synced_wins_and_existing_marker_is_replaced(self):
        with tempfile.TemporaryDirectory() as directory:
            mp3 = Path(directory) / "both.mp3"
            tags = ID3()
            tags.add(TIT1(encoding=3, text=["Lyrics: Unsynced"]))
            tags.add(USLT(encoding=3, lang="und", desc="", text="Plain"))
            tags.add(TXXX(
                encoding=3,
                desc="SYNCEDLYRICS",
                text=["[00:01.00]Timed"],
            ))
            tags.save(mp3)

            self.assertEqual(converter.mark_existing_mp3(mp3, True), (1, 1, 0))
            self.assertEqual(
                ID3(mp3).getall("TIT1")[0].text,
                ["Lyrics: Synced"],
            )

    def test_ignores_mp3_without_lyrics(self):
        with tempfile.TemporaryDirectory() as directory:
            mp3 = Path(directory) / "plain.mp3"
            ID3().save(mp3)
            self.assertEqual(
                converter.mark_existing_mp3(Path(directory), True),
                (0, 0, 0),
            )

    def test_gui_api_preview_uses_callback(self):
        with tempfile.TemporaryDirectory() as directory:
            mp3 = Path(directory) / "lyrics.mp3"
            tags = ID3()
            tags.add(USLT(encoding=3, lang="und", desc="", text="Plain"))
            tags.save(mp3)
            messages = []

            self.assertEqual(
                converter.mark_existing_mp3(
                    Path(directory), False, log=messages.append),
                (1, 0, 0),
            )
            self.assertTrue(any("DRY-RUN" in message for message in messages))
            self.assertFalse(ID3(mp3).getall("TIT1"))


class TimingWriterTests(unittest.TestCase):
    def test_writes_both_synced_frames_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mp3 = root / "song.mp3"
            timing = root / "song.timing"
            ID3().save(mp3, v2_version=3)
            timing.write_text(
                "1200\tFirst line\n2500\tSecond line\n",
                encoding="utf-8",
            )

            self.assertEqual(converter.write_recording(mp3, timing), 0)
            tags = ID3(mp3)
            self.assertEqual(
                tags.getall("SYLT")[0].desc,
                "Manually timed in VirtualDJ Embedded Lyrics",
            )
            synced = [
                frame for frame in tags.getall("TXXX")
                if frame.desc == "SYNCEDLYRICS"
            ]
            self.assertEqual(len(synced), 1)
            self.assertTrue(str(synced[0].text[0]).startswith(
                "[re:VirtualDJ Embedded Lyrics - manual timing]"))
            self.assertFalse(timing.exists())

            second = root / "second.timing"
            second.write_text("3000\tReplacement\n", encoding="utf-8")
            self.assertEqual(converter.write_recording(mp3, second), 3)
            self.assertTrue(second.exists())


class UntimedSanitizingTests(unittest.TestCase):
    def test_removes_lrc_line_and_word_timestamps_and_metadata(self):
        source = (
            "[re:Downloaded lyrics]\n"
            "[00:01.20]First line\n"
            "(00:02.500)Second line\n"
            "[00:03][00:04.00]<00:04.00>Third <00:04.50>line\n"
        )

        self.assertEqual(
            converter.sanitize_untimed_text(source),
            "First line\nSecond line\nThird line",
        )

    def test_preserves_ordinary_parentheses_and_blank_lines(self):
        source = "(Background vocal)\n\nNext line"
        self.assertEqual(converter.sanitize_untimed_text(source), source)


if __name__ == "__main__":
    unittest.main()
