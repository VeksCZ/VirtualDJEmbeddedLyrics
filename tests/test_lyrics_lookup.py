import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

TOOL_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOL_DIR))

import lrc_tool  # noqa: E402
import lyrics_lookup  # noqa: E402


class FakeTidal:
    def __init__(self, *, isrc_track=None, search_track=None, lyrics=None):
        self.isrc_track, self.search_track, self.lyrics = isrc_track, search_track, lyrics
        self.last_lyrics_error = ""

    def find_by_isrc(self, _isrc):
        return self.isrc_track

    def find_by_search(self, *_args):
        return self.search_track

    @staticmethod
    def candidate_artist(track):
        return track.artist

    def fetch_lrc(self, _track):
        return self.lyrics


class FakeLrclib:
    def __init__(self, text=None, status=""):
        self.text, self.status, self.last_error = text, status, ""

    def find_lyrics(self, *_args):
        return self.text, self.status


TRACK = SimpleNamespace(artist="Chinaski", name="1970", duration=240)
TAGS = ("Chinaski", "1970", "CZABC1234567", 240.4, None)


class MatchingTests(unittest.TestCase):
    def test_unbracketed_featuring_is_dropped_from_titles(self):
        self.assertEqual(lrc_tool.normalize_title("Guess featuring billie eilish"), "guess")
        self.assertEqual(lrc_tool.normalize_title("Guess ft. Billie"), "guess")

    def test_multi_artist_tag_matches_primary_artist_despite_misspelling(self):
        score = lrc_tool.artist_similarity("Charlie Xcx, Billie Eilish", "Charli xcx",
                                           "Charli xcx, Billie Eilish")
        self.assertGreater(score, 0.9)

    def test_tidal_queries_fall_back_to_first_artist_and_bare_title(self):
        queries = lrc_tool.TidalClient.search_queries("Charlie Xcx, Billie Eilish", "Guess (feat. Billie Eilish)")
        self.assertEqual(queries, ["Charlie Xcx, Billie Eilish Guess (feat. Billie Eilish)",
                                   "charlie xcx Guess", "Guess"])

    def test_unrelated_artist_stays_low(self):
        self.assertLess(lrc_tool.artist_similarity("Charlie Xcx, Billie Eilish", "Vox Freaks"), 0.5)


class LookupTests(unittest.TestCase):
    def run_lookup(self, tidal=None, lrclib=None, tags=TAGS):
        with mock.patch.object(lrc_tool, "read_tags", return_value=tags):
            return lyrics_lookup.lookup(
                Path("song.mp3"), session_file=Path("session.json"),
                tidal_factory=(lambda: tidal) if tidal else None,
                lrclib_factory=lambda: lrclib or FakeLrclib())

    def test_tidal_isrc_hit_reports_source_match_and_timed(self):
        tidal = FakeTidal(isrc_track=TRACK, lyrics="[00:01.00] Nevim\n[00:03.00] Mozna")
        result = self.run_lookup(tidal)
        self.assertEqual(result["status"], "found")
        self.assertEqual(result["source"], "TIDAL (ISRC)")
        self.assertEqual(result["synchronized"], "1")
        self.assertEqual(result["matched"], "Chinaski - 1970 (4:00)")

    def test_falls_back_to_lrclib_plain_text(self):
        tidal = FakeTidal(search_track=TRACK, lyrics=None)
        result = self.run_lookup(tidal, FakeLrclib("Nevim\nMozna", "LRCLIB plain"))
        self.assertEqual(result["status"], "found")
        self.assertEqual(result["source"], "LRCLIB plain")
        self.assertEqual(result["synchronized"], "0")
        self.assertIn("no lyrics", result["message"])

    def test_missing_tidal_login_still_tries_lrclib(self):
        def no_login():
            raise lrc_tool.TidalClient.LoginRequired("sign in")
        with mock.patch.object(lrc_tool, "read_tags", return_value=TAGS):
            result = lyrics_lookup.lookup(
                Path("song.mp3"), session_file=Path("s.json"), tidal_factory=no_login,
                lrclib_factory=lambda: FakeLrclib())
        self.assertEqual(result["status"], "login_required")
        self.assertIn("sign in", result["message"])

    def test_result_file_has_single_line_header_then_text(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "out.txt"
            lyrics_lookup.write_result(path, {"status": "found", "message": "a\nb",
                                              "text": "line 1\nline 2"})
            header, text = path.read_text(encoding="utf-8").split("\n\n", 1)
            self.assertIn("status=found", header.splitlines())
            self.assertIn("message=a b", header.splitlines())
            self.assertEqual(text, "line 1\nline 2")


if __name__ == "__main__":
    unittest.main()
