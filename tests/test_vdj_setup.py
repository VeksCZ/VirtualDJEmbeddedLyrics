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

            self.assertEqual(layout.root, root.resolve())
            self.assertEqual(layout.scripts, root.resolve())
            self.assertEqual(layout.payload, (root / "Plugins").resolve())
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

    @unittest.skipUnless(sys.platform == "win32", "Windows DLL ZIP extraction")
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

            self.assertEqual(layout.payload, (runtime / "1.2.3").resolve())
            self.assertEqual(
                sorted(path.name for path in layout.payload.iterdir()),
                sorted(vdj_setup.PAYLOAD_FILES),
            )
            self.assertFalse((runtime / "README.md").exists())

    def test_installed_status_reports_complete_and_partial_installations(self):
        with tempfile.TemporaryDirectory() as temporary:
            home = Path(temporary)
            overlay = vdj_setup.plugin_directory(home)
            self._touch_files(overlay, vdj_setup.PAYLOAD_FILES)
            (home / "LRC Lyrics Installation.json").write_text(
                '{"Version":"1.2.3"}', encoding="utf-8")
            self.assertIn("Installed version 1.2.3", vdj_setup.installed_status(home))

            (overlay / vdj_setup.PAYLOAD_FILES[-1]).unlink()
            self.assertIn("Partial installation", vdj_setup.installed_status(home))

    @unittest.skipUnless(sys.platform == "win32", "Windows legacy filenames")
    def test_installed_status_reports_legacy_filenames(self):
        with tempfile.TemporaryDirectory() as temporary:
            home = Path(temporary)
            overlay = vdj_setup.plugin_directory(home)
            self._touch_files(overlay, ("LRC Master.dll", "LRC BlackOut.dll"))

            self.assertIn("legacy installation", vdj_setup.installed_status(home))

    @mock.patch.object(vdj_setup, "_powershell", return_value="powershell.exe")
    @unittest.skipUnless(sys.platform == "win32", "Windows PowerShell installer")
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

    def test_macos_home_validation_does_not_require_powershell(self):
        with tempfile.TemporaryDirectory() as temporary:
            home = Path(temporary)
            (home / "MyLists").mkdir()
            with (mock.patch.object(vdj_setup.sys, "platform", "darwin"),
                  mock.patch.object(vdj_setup, "_mac_virtualdj_running", return_value=False)):
                result = vdj_setup.query_virtualdj(None, str(home))

            self.assertTrue(result["Valid"])
            self.assertEqual(Path(result["Selected"]), home.resolve())
            self.assertFalse(result["VirtualDJRunning"])

    def test_macos_plugin_directory_supports_apple_silicon_and_intel(self):
        home = Path("/Users/dj/Library/Application Support/VirtualDJ")
        with (mock.patch.object(vdj_setup.sys, "platform", "darwin"),
              mock.patch.object(vdj_setup.platform, "machine", return_value="arm64")):
            self.assertEqual(
                vdj_setup.plugin_directory(home),
                home / "PluginsArm" / "VideoOverlay",
            )
        with (mock.patch.object(vdj_setup.sys, "platform", "darwin"),
              mock.patch.object(vdj_setup.platform, "machine", return_value="x86_64")):
            self.assertEqual(
                vdj_setup.plugin_directory(home),
                home / "Plugins64" / "VideoOverlay",
            )

    def test_macos_install_uninstall_and_restore_preserve_bundle_contents(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            home = root / "VirtualDJ"
            payload = root / "Plugins"
            (home / "MyLists").mkdir(parents=True)
            for name in vdj_setup.MAC_PAYLOAD_FILES:
                executable = payload / name / "Contents" / "MacOS" / name.removesuffix(".bundle")
                executable.parent.mkdir(parents=True)
                executable.write_bytes(f"new-{name}".encode())
            layout = vdj_setup.PackageLayout(
                root=root, scripts=root, payload=payload,
                detector=root / "unused.ps1", version="1.2.3")

            with (mock.patch.object(vdj_setup.sys, "platform", "darwin"),
                  mock.patch.object(vdj_setup.platform, "machine", return_value="arm64"),
                  mock.patch.object(vdj_setup, "_mac_virtualdj_running", return_value=False)):
                vdj_setup.run_action(layout, "install", home, lambda _message: None)
                overlay = home / "PluginsArm" / "VideoOverlay"
                for name in vdj_setup.MAC_PAYLOAD_FILES:
                    self.assertTrue((overlay / name / "Contents" / "MacOS").is_dir())
                self.assertIn(
                    '"Platform": "macOS"',
                    (home / "LRC Lyrics Installation.json").read_text(encoding="utf-8"),
                )

                vdj_setup.run_action(layout, "uninstall", home, lambda _message: None)
                self.assertFalse(any((overlay / name).exists() for name in vdj_setup.MAC_PAYLOAD_FILES))
                vdj_setup.run_action(layout, "restore", home, lambda _message: None)
                self.assertTrue(all((overlay / name).is_dir() for name in vdj_setup.MAC_PAYLOAD_FILES))


if __name__ == "__main__":
    unittest.main()
