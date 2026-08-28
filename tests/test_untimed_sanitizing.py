import importlib.util
import sys
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "tools" / "lyrics_tag_converter.py"
SPEC = importlib.util.spec_from_file_location("lyrics_untimed_sanitizing", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class UntimedSanitizingTests(unittest.TestCase):
    def test_removes_lrc_line_and_word_timestamps_and_metadata(self):
        source = (
            "[re:Downloaded lyrics]\n"
            "[00:01.20]First line\n"
            "(00:02.500)Second line\n"
            "[00:03][00:04.00]<00:04.00>Third <00:04.50>line\n"
        )

        self.assertEqual(
            MODULE.sanitize_untimed_text(source),
            "First line\nSecond line\nThird line",
        )

    def test_preserves_ordinary_parentheses_and_blank_lines(self):
        source = "(Background vocal)\n\nNext line"
        self.assertEqual(MODULE.sanitize_untimed_text(source), source)


if __name__ == "__main__":
    unittest.main()
