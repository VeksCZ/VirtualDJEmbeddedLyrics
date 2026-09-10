import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


TOOL_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOL_DIR))

import lyrics_issues  # noqa: E402


class LyricsIssuesTests(unittest.TestCase):
    def test_scan_reports_missing_lyrics_and_unmatched_sidecar(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            mp3 = root / "song.mp3"
            mp3.write_bytes(b"test")
            orphan = root / "orphan.lrc"
            orphan.write_text("[00:01.00]line", encoding="utf-8")
            with (mock.patch.object(
                    lyrics_issues.lyrics_tag_converter, "discover_pairs",
                    return_value=([(mp3, None, None)], [orphan])),
                  mock.patch.object(
                    lyrics_issues.lrc_tool, "read_tags",
                    return_value=(None, None, None, None, None)),
                  mock.patch.object(
                    lyrics_issues, "has_embedded_lyrics", return_value=False)):
                issues = lyrics_issues.scan_library(root, lambda _line: None)
            self.assertEqual(
                {issue.kind for issue in issues},
                {"Missing lyrics", "Unmatched sidecar"},
            )

    def test_report_uses_paths_relative_to_library(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            report = root / "reports" / "issues.csv"
            issue = lyrics_issues.LyricsIssue(
                "Missing lyrics", root / "Artist" / "song.mp3", "Missing")
            lyrics_issues.write_report(report, [issue], root)
            content = report.read_text(encoding="utf-8-sig")
            self.assertIn(str(Path("Artist") / "song.mp3"), content)
            self.assertNotIn(str(root), content)


if __name__ == "__main__":
    unittest.main()
