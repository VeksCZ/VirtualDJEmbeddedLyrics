import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock


TOOL_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOL_DIR))

import vdj_setup  # noqa: E402


class PackageLayoutTests(unittest.TestCase):
    @staticmethod
    def _touch_files(parent: Path, names) -> None:
        parent.mkdir(parents=True, exist_ok=True)
        for name in names:
            (parent / name).write_bytes(b"test")

    def test_release_layout_is_detected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tools = root / "Tools"
            tools.mkdir()
            self._touch_files(root, tuple(vdj_setup.INSTALLER_FILES.values()))
            self._touch_files(root, ("detect-vdj-home.ps1",))
            self._touch_files(root / "Plugins", vdj_setup.PAYLOAD_FILES)
            (root / "VERSION").write_text("1.2.3\n", encoding="utf-8")

            layout = vdj_setup.locate_package_layout(tools)

            self.assertEqual(layout.root, root)
            self.assertEqual(layout.scripts, root)
            self.assertEqual(layout.payload, root / "Plugins")
            self.assertEqual(layout.version, "1.2.3")

    def test_incomplete_payload_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tools = root / "Tools"
            tools.mkdir()
            self._touch_files(root, tuple(vdj_setup.INSTALLER_FILES.values()))
            self._touch_files(root, ("detect-vdj-home.ps1",))
            self._touch_files(root / "Plugins", vdj_setup.PAYLOAD_FILES[:-1])

            with self.assertRaises(FileNotFoundError):
                vdj_setup.locate_package_layout(tools)

    def test_source_layout_extracts_only_plugin_payload_from_release_zip(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tools = root / "tools"
            tools.mkdir()
            scripts = root / "installer"
            self._touch_files(scripts, tuple(vdj_setup.INSTALLER_FILES.values()))
            self._touch_files(scripts, ("detect-vdj-home.ps1",))
            (root / "VERSION").write_text("1.2.3\n", encoding="utf-8")

            archive_path = root / "dist" / "LRC-Lyrics-VirtualDJ-v1.2.3.zip"
            archive_path.parent.mkdir()
            with zipfile.ZipFile(archive_path, "w") as archive:
                for name in vdj_setup.PAYLOAD_FILES:
                    archive.writestr(f"Plugins/{name}", name.encode("utf-8"))
                archive.writestr("README.md", b"must not be extracted")

            runtime = root / "runtime"
            layout = vdj_setup.locate_package_layout(tools, runtime_dir=runtime)

            self.assertEqual(layout.payload, runtime / "1.2.3")
            self.assertEqual(
                sorted(path.name for path in layout.payload.iterdir()),
                sorted(vdj_setup.PAYLOAD_FILES),
            )
            self.assertFalse((runtime / "README.md").exists())

    def test_installed_status_reports_complete_and_partial_installations(self):
        with tempfile.TemporaryDirectory() as temporary:
            home = Path(temporary)
            overlay = home / "Plugins64" / "VideoOverlay"
            self._touch_files(overlay, vdj_setup.PAYLOAD_FILES)
            (home / "LRC Lyrics Installation.json").write_text(
                '{"Version":"1.2.3"}', encoding="utf-8")
            self.assertIn("Installed version 1.2.3", vdj_setup.installed_status(home))

            (overlay / vdj_setup.PAYLOAD_FILES[-1]).unlink()
            self.assertIn("Partial installation", vdj_setup.installed_status(home))

    def test_installed_status_reports_legacy_filenames(self):
        with tempfile.TemporaryDirectory() as temporary:
            home = Path(temporary)
            overlay = home / "Plugins64" / "VideoOverlay"
            self._touch_files(overlay, ("LRC Master.dll", "LRC BlackOut.dll"))

            self.assertIn("legacy installation", vdj_setup.installed_status(home))

    @mock.patch.object(vdj_setup, "_powershell", return_value="powershell.exe")
    def test_install_command_passes_home_and_payload_without_shell(self, _mock_ps):
        root = Path("C:/Release")
        layout = vdj_setup.PackageLayout(
            root=root,
            scripts=root,
            payload=root / "Plugins",
            detector=root / "detect-vdj-home.ps1",
            version="1.2.3",
        )

        command = vdj_setup.build_action_command(
            layout, "install", Path("D:/Custom VirtualDJ Home"))

        self.assertIn("D:\\Custom VirtualDJ Home", str(Path("D:/Custom VirtualDJ Home")))
        self.assertEqual(command[-2:], ["-PayloadDirectory", str(root / "Plugins")])
        self.assertIn("-NonInteractive", command)
        self.assertNotIn("-SkipProcessCheck", command)

    @mock.patch.object(
        vdj_setup, "query_virtualdj", return_value={"VirtualDJRunning": True})
    def test_playlist_changes_are_rejected_while_virtualdj_runs(self, _mock_query):
        layout = vdj_setup.PackageLayout(
            root=Path("C:/Release"),
            scripts=Path("C:/Release"),
            payload=Path("C:/Release/Plugins"),
            detector=Path("C:/Release/detect-vdj-home.ps1"),
            version="1.2.3",
        )
        with self.assertRaises(RuntimeError):
            vdj_setup.assert_virtualdj_closed(layout)


if __name__ == "__main__":
    unittest.main()
