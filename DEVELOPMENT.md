# Development

This document is for contributors and source builds. End users should download
the release ZIP and run `Install.cmd` as described in the main README.

## Requirements

- Windows 10 or 11, x64
- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.24 or newer
- Python 3 with dependencies from `requirements.txt`

The public VirtualDJ 8 SDK headers are vendored under
`third_party/VirtualDJ8_SDK_20211003`.

## Build and package

```powershell
py -m pip install -r requirements.txt
./build-release.ps1
```

The script builds both supported DLLs, runs C++ tests, Python tests and installer
integration tests, then creates:

- `dist/LRC-Lyrics-VirtualDJ-v<VERSION>/`
- `dist/LRC-Lyrics-VirtualDJ-v<VERSION>.zip`
- `dist/LRC-Lyrics-VirtualDJ-v<VERSION>.zip.sha256`

The release version comes from the root `VERSION` file and is compiled into both
plugins. Normal packages contain only LRC Master and LRC BlackOut.

## Local source-tree installation

After a successful release build, close VirtualDJ and run:

```powershell
./Install.cmd
```

For automated testing or a custom location:

```powershell
./install-plugin.ps1 -VirtualDJHome 'D:\VirtualDJ' -NonInteractive
```

## Tests

Parser and renderer tests are registered with CTest. Python tests use unittest.
`tests/InstallerTests.ps1` creates an isolated VirtualDJ home folder under the
Windows temporary directory and verifies installation, legacy cleanup, backup,
repeat installation, settings migration, file hashes, uninstall and rollback.

## Experimental LRC Deck

The source tree retains an unsupported experimental audio-only visualization.
VirtualDJ hosts the selected `videoAudioOnlyVisualisation` in a shared slot, so
the SDK behavior does not provide the independent per-deck preview and crossfade
source implied by the name.

It can be built for development only:

```powershell
./build-release.ps1 -BuildLrcDeck
```

The DLL is placed under `dist/experimental` and is never included in the public
release ZIP.

## Release checklist

1. Update `VERSION` and `RELEASE-NOTES.md`.
2. Run `build-release.ps1`.
3. Test LRC Master, LRC BlackOut, synchronized lyrics, untimed lyrics, missing
   lyrics and User 1 auto-tagging in VirtualDJ.
4. Verify a clean install from the generated ZIP.
5. Commit, push, create tag `v<VERSION>` and publish the ZIP plus SHA-256 file.
