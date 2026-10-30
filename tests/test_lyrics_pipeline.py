import sys
import tempfile
import threading
import time
import unittest
import urllib.error
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

TOOL_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOL_DIR))

import lrc_tool  # noqa: E402


class FakeLrclib:
    def __init__(self, records, delay=0.0):
        self.records = records
        self.delay = delay
        self.calls = []
        self.active = 0
        self.peak = 0
        self._lock = threading.Lock()
        self.last_error = ""

    def find_lyrics(self, artist, title, duration, tolerance):
        with self._lock:
            self.calls.append(title)
            self.active += 1
            self.peak = max(self.peak, self.active)
        time.sleep(self.delay)
        with self._lock:
            self.active -= 1
        text = self.records.get(title)
        return (text, "LRCLIB synced") if text else (None, "")


def fake_tags(path):
    stem = Path(path).stem
    return "Artist", stem.split("#")[0], None, 120.0, None


class ParallelRunTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.library = self.root / "music"
        self.library.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def run_library(self, lrclib, workers=4, names=()):
        for name in names:
            (self.library / f"{name}.mp3").touch()
        written = []
        with mock.patch.object(lrc_tool, "read_tags", fake_tags), \
                mock.patch.object(lrc_tool, "read_existing_uslt", lambda _p: []), \
                mock.patch.object(lrc_tool, "LrclibClient", lambda: lrclib), \
                mock.patch.object(lrc_tool, "TidalClient", side_effect=RuntimeError("offline")), \
                mock.patch.object(lrc_tool, "embed_lyrics",
                                  lambda p, text, *a, **k: written.append((p.name, text)) or "und"):
            logs = []
            rows = lrc_tool.run_library(
                self.library, self.root / "backup", session_file=self.root / "s.json",
                report_path=self.root / "report.csv", workers=workers, log=logs.append)
        return rows, written, logs

    def test_results_keep_library_order_when_lookups_finish_out_of_order(self):
        names = [f"song{i:02d}" for i in range(12)]
        lrclib = FakeLrclib({n: f"[00:01.00]{n}" for n in names}, delay=0.01)
        rows, written, _logs = self.run_library(lrclib, names=names)
        self.assertEqual([Path(r[0]).stem for r in rows], names)
        self.assertEqual([name for name, _ in written], [f"{n}.mp3" for n in names])

    def test_lookups_actually_run_in_parallel(self):
        names = [f"p{i}" for i in range(8)]
        lrclib = FakeLrclib({n: "[00:01.00]x" for n in names}, delay=0.05)
        self.run_library(lrclib, workers=4, names=names)
        self.assertGreater(lrclib.peak, 1)
        self.assertLessEqual(lrclib.peak, 4)

    def test_single_worker_is_sequential(self):
        names = [f"s{i}" for i in range(4)]
        lrclib = FakeLrclib({n: "[00:01.00]x" for n in names}, delay=0.02)
        self.run_library(lrclib, workers=1, names=names)
        self.assertEqual(lrclib.peak, 1)

    def test_duplicate_tracks_are_looked_up_once(self):
        names = ["Same#a", "Same#b", "Same#c"]
        lrclib = FakeLrclib({"Same": "[00:01.00]x"})
        rows, written, _logs = self.run_library(lrclib, workers=1, names=names)
        self.assertEqual(len(written), 3)
        self.assertEqual(lrclib.calls, ["Same"])
        self.assertIn("cached", rows[1][4])

    def test_missing_lyrics_do_not_stop_other_files(self):
        lrclib = FakeLrclib({"found": "[00:01.00]x"})
        rows, written, logs = self.run_library(lrclib, names=["lost", "found"])
        self.assertEqual([name for name, _ in written], ["found.mp3"])
        self.assertTrue(any(line.startswith("[MISSING] lost.mp3") for line in logs))
        self.assertEqual(len(rows), 2)

    def test_worker_exception_is_reported_per_file(self):
        class Exploding(FakeLrclib):
            def find_lyrics(self, artist, title, duration, tolerance):
                if title == "bad":
                    raise RuntimeError("boom")
                return super().find_lyrics(artist, title, duration, tolerance)

        rows, written, logs = self.run_library(
            Exploding({"good": "[00:01.00]x"}), names=["bad", "good"])
        self.assertEqual(rows[0][4], "ERROR")
        self.assertEqual([name for name, _ in written], ["good.mp3"])

    def test_transient_error_status_is_not_cached(self):
        cache = lrc_tool.LookupCache()

        class Flaky:
            last_error = "HTTP 503"

            def find_lyrics(self, *args):
                return None, ""

        path = self.library / "t.mp3"
        path.touch()
        with mock.patch.object(lrc_tool, "read_tags", fake_tags):
            result = lrc_tool.lookup_file(path, None, Flaky(), 3.0, cache)
        self.assertIn("unavailable", result.status)
        self.assertIsNone(cache.get(lrc_tool.LookupCache.key("Artist", "t", 120.0)))

    def test_workers_value_is_clamped(self):
        lrclib = FakeLrclib({"a": "[00:01.00]x"})
        rows, written, _ = self.run_library(lrclib, workers=-5, names=["a"])
        self.assertEqual(len(written), 1)


class LrclibClientTests(unittest.TestCase):
    def test_slots_are_spaced_across_threads(self):
        client = lrc_tool.LrclibClient()
        client.MIN_INTERVAL = 0.05
        stamps = []
        lock = threading.Lock()

        def worker():
            client._wait_for_slot()
            with lock:
                stamps.append(time.monotonic())

        threads = [threading.Thread(target=worker) for _ in range(5)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        stamps.sort()
        gaps = [b - a for a, b in zip(stamps, stamps[1:])]
        self.assertGreaterEqual(min(gaps), 0.03)

    def _http_error(self, code):
        return urllib.error.HTTPError("u", code, "x", {"Retry-After": "0"}, None)

    def test_retries_transient_http_errors_then_succeeds(self):
        client = lrc_tool.LrclibClient()
        client.MIN_INTERVAL = 0
        response = mock.MagicMock()
        response.__enter__.return_value = response
        response.read.return_value = b'{"ok": 1}'
        with mock.patch.object(lrc_tool.urllib.request, "urlopen",
                               side_effect=[self._http_error(503), response]) as opener, \
                mock.patch.object(lrc_tool.time, "sleep"):
            self.assertEqual(client._get_json("get", {"a": "b"}), {"ok": 1})
        self.assertEqual(opener.call_count, 2)

    def test_not_found_and_client_errors_are_not_retried(self):
        client = lrc_tool.LrclibClient()
        client.MIN_INTERVAL = 0
        with mock.patch.object(lrc_tool.urllib.request, "urlopen",
                               side_effect=self._http_error(404)) as opener:
            self.assertIsNone(client._get_json("get", {}))
            self.assertEqual(client.last_error, "")
        self.assertEqual(opener.call_count, 1)
        with mock.patch.object(lrc_tool.urllib.request, "urlopen",
                               side_effect=self._http_error(400)) as opener:
            self.assertIsNone(client._get_json("get", {}))
            self.assertEqual(client.last_error, "HTTP 400")
        self.assertEqual(opener.call_count, 1)

    def test_gives_up_after_retries_and_reports_error(self):
        client = lrc_tool.LrclibClient()
        client.MIN_INTERVAL = 0
        with mock.patch.object(lrc_tool.urllib.request, "urlopen",
                               side_effect=OSError("down")) as opener, \
                mock.patch.object(lrc_tool.time, "sleep"):
            self.assertIsNone(client._get_json("get", {}))
        self.assertEqual(opener.call_count, client.RETRIES + 1)
        self.assertEqual(client.last_error, "down")

    def test_last_error_is_per_thread(self):
        client = lrc_tool.LrclibClient()
        client.last_error = "main"
        seen = []
        thread = threading.Thread(target=lambda: seen.append(client.last_error))
        thread.start()
        thread.join()
        self.assertEqual(seen, [""])
        self.assertEqual(client.last_error, "main")

    def test_synced_lyrics_preferred_and_instrumental_skipped(self):
        self.assertEqual(
            lrc_tool.LrclibClient._lyrics({"syncedLyrics": "[00:01.00]a", "plainLyrics": "a"}),
            ("[00:01.00]a", "LRCLIB synced"))
        self.assertEqual(lrc_tool.LrclibClient._lyrics({"plainLyrics": "a"}), ("a", "LRCLIB plain"))
        self.assertEqual(lrc_tool.LrclibClient._lyrics({"instrumental": True, "plainLyrics": "a"}),
                         (None, ""))


class HelperTests(unittest.TestCase):
    def test_normalize_title_strips_noise(self):
        self.assertEqual(lrc_tool.normalize_title("Song (feat. X) (Radio Edit)"), "song")
        self.assertEqual(lrc_tool.normalize_title(None), "")

    def test_normalize_artist(self):
        self.assertEqual(lrc_tool.normalize_artist("A & B feat. C"), "a and b")

    def test_read_lrc_file_handles_legacy_encodings(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "a.lrc"
            path.write_bytes("[00:01.00]Příliš žluťoučký".encode("cp1250"))
            self.assertIn("žluťoučký", lrc_tool.read_lrc_file(path))
            path.write_bytes("[00:01.00]Ahoj".encode("utf-16"))
            self.assertIn("Ahoj", lrc_tool.read_lrc_file(path))

    def test_choose_best_existing_uslt_prefers_timed(self):
        text, ambiguous = lrc_tool.choose_best_existing_uslt(
            [("eng", "", "plain words"), ("eng", "a", "[00:01.00]timed\n[00:02.00]more")])
        self.assertTrue(ambiguous)
        self.assertIn("[00:01.00]", text)
        self.assertEqual(lrc_tool.choose_best_existing_uslt([]), (None, False))

    def test_looks_english_threshold(self):
        self.assertTrue(lrc_tool.looks_english("the love and the night you know never like"))
        self.assertFalse(lrc_tool.looks_english("Ahoj světe"))


if __name__ == "__main__":
    unittest.main()
