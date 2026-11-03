# Development

This document is for contributors and source builds. End users install the plugin package as
described in the main README (or with DJ Music Manager).

## Requirements

- Windows 10 or 11, x64
- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.24 or newer
- Python 3 with dependencies from `requirements.txt` and `requirements-build.txt`

The public VirtualDJ 8 SDK headers are vendored under
`third_party/sdk/VirtualDJ8_SDK_20211003`.

## Build and package

```powershell
py -m pip install -r requirements.txt
py -m pip install -r requirements-build.txt
./build-release.ps1
```

The script builds the three plugin DLLs and the `LrcHelper.exe` helper, runs C++ and Python
tests, then leaves only the publishable artifacts:

- `dist/LRC-Plugins-Windows-v<VERSION>.zip[.sha256]`

The release version comes from the root `VERSION` file and is compiled into all
plugins. Packages contain LRC Master, LRC BlackOut and LRC Deck. Temporary
compiler output and the extracted staging package are removed after a successful
build.

The macOS plugin bundles and the helper can be built, tested and packaged on a Mac with:

```bash
python3 -m pip install -r requirements.txt
python3 -m pip install -r requirements-build.txt
./build-macos-release.sh
```

This creates a `LRC-Plugins-macOS-<architecture>` ZIP. CI publishes one package
for `AppleSilicon` and one for `Intel`.
The macOS CI job builds
universal Metal/CoreText plugin bundles, verifies both architectures, exported
entry points, and code signatures, and tests the platform-neutral lyrics core.

## Local source-tree installation

After a successful release build, install the generated package with DJ Music Manager
(VirtualDJ tab, **LRC plugin** installs the latest *published* release) or by hand as described in
`INSTALL.txt`. The lyrics tools, the plugin installer and their tests live in the DJ Music Manager
repository (`lyricstools/`); `helper/` here keeps only the four Python files that are built into
`LrcHelper` / `EmbeddedLyricsTagWriter.py`, as a snapshot (see `helper/README.md`).

## Tests

Parser and renderer tests are registered with CTest. Python tests use unittest and cover the
helper modules (lyrics lookup, LRC pipeline, ID3 tag writing).

## Release checklist

1. Update `VERSION` and `RELEASE-NOTES.md`.
2. Run `build-release.ps1`.
3. Test LRC Master, LRC BlackOut, synchronized lyrics, untimed lyrics, missing
   lyrics and User 1 auto-tagging in VirtualDJ.
4. Verify a clean install from the generated ZIP (`INSTALL.txt`) and with DJ Music Manager.
   If the Python helper files changed, run `./sync-from-manager.ps1` first (it copies them from `lyricstools/` in DJ Music Manager).
5. Commit, push, create tag `v<VERSION>` and publish the ZIP plus SHA-256 file.
