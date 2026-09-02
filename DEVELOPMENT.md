# Development

This document is for contributors and source builds. End users should download
the release ZIP and run `Install.cmd` as described in the main README.

## Requirements

- Windows 10 or 11, x64
- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.24 or newer
- Python 3 with dependencies from `requirements.txt`

The public VirtualDJ 8 SDK headers are vendored under
`tools/sdk/VirtualDJ8_SDK_20211003`.

## Build and package

```powershell
py -m pip install -r requirements.txt
./build-release.ps1
```

The script builds both supported DLLs, runs C++ tests, Python tests and installer
integration tests, then leaves only the publishable artifacts:

- `dist/LRC-Lyrics-VirtualDJ-v<VERSION>.zip`
- `dist/LRC-Lyrics-VirtualDJ-v<VERSION>.zip.sha256`

The release version comes from the root `VERSION` file and is compiled into both
plugins. Normal packages contain only LRC Master and LRC BlackOut. Temporary
compiler output and the extracted staging package are removed after a successful
build.

## Local source-tree installation

After a successful release build, close VirtualDJ and run the root
`LyricsTools.cmd`, open **VirtualDJ setup**, confirm the detected home
folder and choose **Install / update plugin**. The source GUI reads the exact
plugin payload from the built release ZIP.

For command-line testing, extract the release ZIP and run its `Install.cmd`.
The underlying script also accepts an explicit custom location:

```powershell
./install-plugin.ps1 -VirtualDJHome 'D:\VirtualDJ' -PayloadDirectory './Plugins' -NonInteractive
```

The only source-tree end-user launcher is `LyricsTools.cmd`; all Python
implementation files live in `tools/`, and canonical installation scripts live
in `installer/`. `build-release.ps1` places the GUI launcher and the no-Python
installer entry points in the release root.

## Tests

Parser and renderer tests are registered with CTest. Python tests use unittest.
`tests/InstallerTests.ps1` creates an isolated VirtualDJ home folder under the
Windows temporary directory and verifies installation, legacy cleanup, backup,
repeat installation, settings migration, file hashes, uninstall and rollback.

## Release checklist

1. Update `VERSION` and `RELEASE-NOTES.md`.
2. Run `build-release.ps1`.
3. Test LRC Master, LRC BlackOut, synchronized lyrics, untimed lyrics, missing
   lyrics and User 1 auto-tagging in VirtualDJ.
4. Verify a clean install from the generated ZIP.
5. Commit, push, create tag `v<VERSION>` and publish the ZIP plus SHA-256 file.
