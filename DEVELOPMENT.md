# Development

This document is for contributors and source builds. End users should download
the release ZIP and open `LyricsTools` as described in the main README.

## Requirements

- Windows 10 or 11, x64
- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.24 or newer
- Python 3 with dependencies from `requirements.txt` and `requirements-build.txt`

The public VirtualDJ 8 SDK headers are vendored under
`tools/sdk/VirtualDJ8_SDK_20211003`.

## Build and package

```powershell
py -m pip install -r requirements.txt
py -m pip install -r requirements-build.txt
./build-release.ps1
```

The script builds both supported DLLs and the standalone LyricsTools application,
runs C++ and Python tests (including native installer integration tests), then
leaves only the publishable artifacts:

- `dist/LyricsTools-Windows-v<VERSION>.zip[.sha256]`

The release version comes from the root `VERSION` file and is compiled into both
plugins. Normal packages contain only LRC Master and LRC BlackOut. Temporary
compiler output and the extracted staging package are removed after a successful
build.

The cross-platform Python tools can be tested and packaged on macOS with:

```bash
python3 -m pip install -r requirements.txt
python3 -m pip install -r requirements-build.txt
./build-macos-tools-release.sh
```

This creates a `LyricsTools-macOS-<architecture>` ZIP. CI publishes one package
for `AppleSilicon` and one for `Intel`.
The macOS CI job builds
universal Metal/CoreText plugin bundles, verifies both architectures, exported
entry points, and code signatures, and tests the platform-neutral lyrics core.

## Local source-tree installation

After a successful release build, run the generated `LyricsTools.exe`, open its
first Plugin tab, confirm the detected home folder, and choose Install / update.

For source debugging, launch the same application directly:

```powershell
python tools/lyrics_tools_gui.py
```

Python implementation files live in `tools/`. Plugin discovery, installation,
backup, rollback, uninstall, and restore are implemented natively in the shared
Python application on both Windows and macOS.

## Tests

Parser and renderer tests are registered with CTest. Python tests use unittest
and create isolated temporary VirtualDJ folders to verify installation, legacy
cleanup, backup, settings migration, uninstall, and recovery behavior.

## Release checklist

1. Update `VERSION` and `RELEASE-NOTES.md`.
2. Run `build-release.ps1`.
3. Test LRC Master, LRC BlackOut, synchronized lyrics, untimed lyrics, missing
   lyrics and User 1 auto-tagging in VirtualDJ.
4. Verify a clean install from the generated ZIP.
5. Commit, push, create tag `v<VERSION>` and publish the ZIP plus SHA-256 file.
