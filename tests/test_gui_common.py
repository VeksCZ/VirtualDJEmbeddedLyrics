import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock


TOOL_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOL_DIR))

import gui_common  # noqa: E402


class FakeSetup:
    PAYLOAD_FILES = ("LRCMaster.dll", "LRCBlackOut.dll")

    @staticmethod
    def plugin_directory(home):
        return home / "Plugins"


class GuiCommonTests(unittest.TestCase):
    def test_version_tuple_accepts_release_tags_and_rejects_other_text(self):
        self.assertEqual(gui_common.version_tuple("v1.2.3"), (1, 2, 3))
        self.assertEqual(gui_common.version_tuple("1.2"), (1, 2))
        self.assertEqual(gui_common.version_tuple("latest"), ())

    def test_diagnostic_zip_redacts_user_and_media_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "diagnostics.zip"
            private_path = str(Path.home() / "Music" / "Private Artist" / "track.mp3")
            gui_common.create_diagnostic_zip(
                destination,
                app_name="LyricsTools",
                version="1.2.3",
                status="Plugin is installed",
                recent_log=f"Reading {private_path}\nNo error",
            )
            with zipfile.ZipFile(destination) as archive:
                details = json.loads(archive.read("system.json"))
                log = archive.read("recent.log").decode()
            self.assertEqual(details["version"], "1.2.3")
            self.assertNotIn(str(Path.home()), log)
            self.assertNotIn("Private Artist", log)
            self.assertNotIn("track.mp3", log)
            self.assertIn("<REDACTED_PATH>", log)

    def test_plugin_state_distinguishes_missing_update_and_current(self):
        with tempfile.TemporaryDirectory() as temporary:
            home = Path(temporary)
            level, _text = gui_common.plugin_state(home, "2.0.0", FakeSetup)
            self.assertEqual(level, "error")
            plugins = home / "Plugins"
            plugins.mkdir()
            for name in FakeSetup.PAYLOAD_FILES:
                (plugins / name).write_bytes(b"plugin")
            (home / "LRC Lyrics Installation.json").write_text(
                '{"Version":"1.0.0"}', encoding="utf-8")
            level, _text = gui_common.plugin_state(home, "2.0.0", FakeSetup)
            self.assertEqual(level, "warning")
            (home / "LRC Lyrics Installation.json").write_text(
                '{"Version":"2.0.0"}', encoding="utf-8")
            level, _text = gui_common.plugin_state(home, "2.0.0", FakeSetup)
            self.assertEqual(level, "ok")

    def test_release_assets_are_selected_per_product_and_platform(self):
        release = {"assets": [
            {"name": "LyricsTools-Windows-v1.2.3.zip"},
            {"name": "LyricsTools-Windows-v1.2.3.zip.sha256"},
        ]}
        with mock.patch.object(gui_common.sys, "platform", "win32"):
            archive, checksum = gui_common.select_release_assets(release, "LyricsTools")
        self.assertEqual(archive["name"], "LyricsTools-Windows-v1.2.3.zip")
        self.assertTrue(checksum["name"].endswith(".sha256"))

    def test_wrong_product_asset_is_rejected(self):
        release = {"assets": [
            {"name": "OtherTool-Windows-v1.2.3.zip"},
            {"name": "OtherTool-Windows-v1.2.3.zip.sha256"},
        ]}
        with (mock.patch.object(gui_common.sys, "platform", "win32"),
              self.assertRaises(RuntimeError)):
            gui_common.select_release_assets(release, "LyricsTools")


if __name__ == "__main__":
    unittest.main()
