#!/bin/bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")" && pwd)"
version="$(tr -d '[:space:]' < "$project_root/VERSION")"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Invalid VERSION value: $version" >&2
    exit 1
fi

python_bin="${PYTHON:-python3}"
"$python_bin" -m unittest discover -s "$project_root/tests" -p 'test_*.py'

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/lrc-macos-build.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
build_dir="$work_dir/build"
cmake -S "$project_root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="${MACOS_ARCHITECTURES:-arm64;x86_64}" -DBUILD_TESTING=ON
cmake --build "$build_dir" --config Release
ctest --test-dir "$build_dir" -C Release --output-on-failure

dist_dir="$project_root/dist"
package_name="LRC-Lyrics-VirtualDJ-macOS-v$version"
stage_dir="$work_dir/stage"
package_dir="$stage_dir/$package_name"
mkdir -p "$package_dir/Tools" "$package_dir/Plugins"

master_bundle="$(find "$build_dir" -type d -name 'LRCMaster.bundle' -print -quit)"
blackout_bundle="$(find "$build_dir" -type d -name 'LRCBlackOut.bundle' -print -quit)"
if [[ -z "$master_bundle" || -z "$blackout_bundle" ]]; then
    echo "The macOS plugin bundles were not produced." >&2
    exit 1
fi
/usr/bin/lipo "$master_bundle/Contents/MacOS/LRCMaster" -verify_arch arm64 x86_64
/usr/bin/lipo "$blackout_bundle/Contents/MacOS/LRCBlackOut" -verify_arch arm64 x86_64
/usr/bin/nm -gU "$master_bundle/Contents/MacOS/LRCMaster" | /usr/bin/grep -q ' _DllGetClassObject$'
/usr/bin/nm -gU "$blackout_bundle/Contents/MacOS/LRCBlackOut" | /usr/bin/grep -q ' _DllGetClassObject$'
cp -R "$master_bundle" "$package_dir/Plugins/LRCMaster.bundle"
cp -R "$blackout_bundle" "$package_dir/Plugins/LRCBlackOut.bundle"

cp -R "$project_root/macos/LyricsTools.app" "$package_dir/LyricsTools.app"
cp "$project_root/LyricsTools.command" "$package_dir/LyricsTools.command"
chmod +x "$package_dir/LyricsTools.command" "$package_dir/LyricsTools.app/Contents/MacOS/LyricsTools"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $version" \
    "$package_dir/LyricsTools.app/Contents/Info.plist"
for name in lyrics_tag_converter.py lrc_tool.py restore_lrc.py lyrics_tools_gui.py vdj_setup.py vdj_playlist_sync.py; do
    cp "$project_root/tools/$name" "$package_dir/Tools/$name"
done
cp "$project_root/tools/README.md" "$package_dir/Tools/README.md"
cp "$project_root/README.md" "$package_dir/README.md"
cp "$project_root/requirements.txt" "$package_dir/Tools/requirements.txt"
cp "$project_root/VERSION" "$package_dir/VERSION"

# Ad-hoc signing preserves bundle integrity. Public notarization can replace this
# when an Apple Developer ID certificate is configured by the release runner.
/usr/bin/codesign --force --deep --sign - "$package_dir/Plugins/LRCMaster.bundle"
/usr/bin/codesign --force --deep --sign - "$package_dir/Plugins/LRCBlackOut.bundle"
/usr/bin/codesign --force --deep --sign - "$package_dir/LyricsTools.app"
/usr/bin/codesign --verify --deep --strict "$package_dir/Plugins/LRCMaster.bundle"
/usr/bin/codesign --verify --deep --strict "$package_dir/Plugins/LRCBlackOut.bundle"
/usr/bin/codesign --verify --deep --strict "$package_dir/LyricsTools.app"

mkdir -p "$dist_dir"
zip_path="$dist_dir/$package_name.zip"
rm -f "$zip_path" "$zip_path.sha256"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$package_dir" "$zip_path"
hash="$(/usr/bin/shasum -a 256 "$zip_path" | awk '{print toupper($1)}')"
printf '%s  %s\n' "$hash" "$(basename "$zip_path")" > "$zip_path.sha256"
printf 'Release ZIP: %s\nSHA-256: %s\n' "$zip_path" "$hash"
