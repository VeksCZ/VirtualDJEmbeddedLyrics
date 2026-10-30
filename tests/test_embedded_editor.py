import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from mutagen.id3 import ID3, SYLT, TIT1, TXXX, USLT, Encoding

TOOL_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOL_DIR))

import lyrics_tag_converter as converter  # noqa: E402


def make_mp3(directory: Path, *frames) -> Path:
    path = directory / "song.mp3"
    tags = ID3()
    for frame in frames:
        tags.add(frame)
    tags.save(path)
    return path


class EditorBackendTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_timestamp_format(self):
        self.assertEqual(converter.format_lrc_timestamp(0), "[00:00.00]")
        self.assertEqual(converter.format_lrc_timestamp(61_230), "[01:01.23]")
        self.assertEqual(converter.format_lrc_timestamp(-5), "[00:00.00]")

    def test_read_texts_from_sylt_and_round_trip(self):
        mp3 = make_mp3(self.dir, SYLT(encoding=Encoding.UTF8, lang="eng", format=2, type=1,
                                      desc="", text=[("Hello", 1000), ("World", 62500)]))
        timed, untimed, synchronized = converter.read_editor_texts(mp3)
        self.assertEqual(timed, "[00:01.00] Hello\n[01:02.50] World")
        self.assertEqual(untimed, "")
        self.assertTrue(synchronized)
        self.assertEqual(converter.save_editor_text(mp3, timed, True), 0)
        self.assertEqual(converter.read_editor_texts(mp3)[0], timed)

    def test_read_texts_from_uslt_only(self):
        mp3 = make_mp3(self.dir, USLT(encoding=Encoding.UTF8, lang="eng", desc="", text="Line one\nLine two"))
        timed, untimed, synchronized = converter.read_editor_texts(mp3)
        self.assertEqual((timed, untimed, synchronized), ("", "Line one\nLine two", False))

    def test_timestamped_uslt_counts_as_timed(self):
        mp3 = make_mp3(self.dir, USLT(encoding=Encoding.UTF8, lang="eng", desc="", text="[00:01.00]Hi"))
        timed, untimed, synchronized = converter.read_editor_texts(mp3)
        self.assertEqual(timed, "[00:01.00]Hi")
        self.assertTrue(synchronized)

    def test_legacy_txxx_is_read(self):
        mp3 = make_mp3(
            self.dir,
            TXXX(encoding=3, desc="SYNCEDLYRICS", text=["[00:02.00]Legacy"]),
            TXXX(encoding=3, desc="UNSYNCEDLYRICS", text=["[00:02.00]Plain words"]))
        timed, untimed, _ = converter.read_editor_texts(mp3)
        self.assertEqual(timed, "[00:02.00]Legacy")
        self.assertEqual(untimed, "Plain words")

    def test_missing_tag_gives_empty_editor(self):
        path = self.dir / "bare.mp3"
        path.write_bytes(b"")
        self.assertEqual(converter.read_editor_texts(path), ("", "", False))

    def test_save_timed_replaces_untimed_and_marks_grouping(self):
        mp3 = make_mp3(self.dir, USLT(encoding=Encoding.UTF8, lang="eng", desc="", text="old"),
                       TIT1(encoding=3, text=["Mine"]))
        self.assertEqual(converter.save_editor_text(mp3, "[00:03.00] New", True), 0)
        tags = ID3(mp3)
        self.assertEqual(tags.getall("USLT"), [])
        self.assertEqual(len(tags.getall("SYLT")), 1)
        self.assertEqual(str(tags["TIT1"].text[0]), "Mine; Lyrics: Synced")

    def test_save_untimed_strips_timestamps_and_removes_sylt(self):
        mp3 = make_mp3(self.dir, SYLT(encoding=Encoding.UTF8, lang="eng", format=2, type=1,
                                      desc="", text=[("old", 1000)]))
        self.assertEqual(converter.save_editor_text(mp3, "[00:01.00]Only words\n[ar:X]", False), 0)
        tags = ID3(mp3)
        self.assertEqual(tags.getall("SYLT"), [])
        self.assertEqual(str(tags.getall("USLT")[0].text), "Only words")

    def test_invalid_input_is_rejected_without_touching_the_file(self):
        mp3 = make_mp3(self.dir, USLT(encoding=Encoding.UTF8, lang="eng", desc="", text="keep"))
        self.assertEqual(converter.save_editor_text(mp3, "no timestamps here", True), 3)
        self.assertEqual(converter.save_editor_text(mp3, "   \n", False), 4)
        self.assertEqual(str(ID3(mp3).getall("USLT")[0].text), "keep")
        self.assertIn(3, converter.EDITOR_ERRORS)

    def test_plugin_text_file_path_still_works_and_cleans_up(self):
        mp3 = make_mp3(self.dir)
        text = self.dir / "edit.txt"
        text.write_text("[00:01.00]From plugin", encoding="utf-8")
        self.assertEqual(converter.write_editor_text(mp3, text, True), 0)
        self.assertFalse(text.exists())
        text.write_text("nothing timed", encoding="utf-8")
        self.assertEqual(converter.write_editor_text(mp3, text, True), 3)
        self.assertTrue(text.exists())
        self.assertEqual(converter.write_editor_text(mp3, self.dir / "missing.txt", True), 2)


try:
    import tkinter as tk
    from tkinter import ttk
    import lyrics_editor_tab
except ImportError:  # pragma: no cover - Tk is optional on CI images
    tk = None


@unittest.skipIf(tk is None, "tkinter is unavailable")
class EditorTabTests(unittest.TestCase):
    def setUp(self):
        try:
            self.root = tk.Tk()
        except tk.TclError:
            self.skipTest("no display available")
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)
        self.closed_calls = 0

        def ensure_closed():
            self.closed_calls += 1

        self.saved = []
        self.tab = lyrics_editor_tab.LyricsEditorTab(
            ttk.Frame(self.root), ensure_vdj_closed=ensure_closed, on_saved=self.saved.append)

    def tearDown(self):
        self.root.destroy()
        self.temp.cleanup()

    def test_modes_keep_separate_texts(self):
        mp3 = make_mp3(self.dir, USLT(encoding=Encoding.UTF8, lang="eng", desc="", text="plain"))
        self.tab.open_file(mp3)
        self.assertEqual(self.tab.mode.get(), "untimed")
        self.assertEqual(self.tab.text.get("1.0", "end-1c"), "plain")
        self.tab.mode.set("timed")
        self.tab._switch_mode()
        self.assertEqual(self.tab.text.get("1.0", "end-1c"), "")
        self.tab.text.insert("1.0", "[00:01.00] typed")
        self.tab.mode.set("untimed")
        self.tab._switch_mode()
        self.assertEqual(self.tab.text.get("1.0", "end-1c"), "plain")

    def test_save_writes_selected_type_and_checks_virtualdj(self):
        mp3 = make_mp3(self.dir)
        self.tab.path_var.set(str(mp3))
        self.assertTrue(self.tab.load())
        self.tab.mode.set("timed")
        self.tab._switch_mode()
        self.tab.text.insert("1.0", "[00:05.00] Saved")
        self.assertTrue(self.tab.save())
        self.assertEqual(self.closed_calls, 1)
        self.assertEqual(converter.read_editor_texts(mp3)[0], "[00:05.00] Saved")
        self.assertEqual(self.saved, [mp3])

    def test_failed_save_reports_and_does_not_notify(self):
        mp3 = make_mp3(self.dir)
        self.tab.open_file(mp3)
        self.tab.text.insert("1.0", "no timestamps")
        with mock.patch.object(lyrics_editor_tab.messagebox, "showerror") as error:
            self.assertFalse(self.tab.save())
        error.assert_called_once()
        self.assertEqual(self.saved, [])

    def test_virtualdj_running_blocks_save(self):
        mp3 = make_mp3(self.dir)
        self.tab.open_file(mp3)
        self.tab.text.insert("1.0", "[00:01.00] x")
        self.tab.ensure_vdj_closed = mock.Mock(side_effect=RuntimeError("VirtualDJ is running"))
        with mock.patch.object(lyrics_editor_tab.messagebox, "showerror") as error:
            self.assertFalse(self.tab.save())
        self.assertIn("running", error.call_args[0][1])
        self.assertEqual(converter.read_editor_texts(mp3), ("", "", False))

    def test_load_rejects_non_mp3(self):
        self.tab.path_var.set(str(self.dir / "x.txt"))
        with mock.patch.object(lyrics_editor_tab.messagebox, "showerror") as error:
            self.assertFalse(self.tab.load())
        error.assert_called_once()


if __name__ == "__main__":
    unittest.main()
