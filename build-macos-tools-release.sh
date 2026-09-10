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
"$python_bin" -c 'import PyInstaller' >/dev/null 2>&1 || {
    echo "PyInstaller is required. Install requirements-build.txt." >&2
    exit 1
}

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/lrc-macos-build.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
build_dir="$work_dir/build"
cmake -S "$project_root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="${MACOS_ARCHITECTURES:-arm64;x86_64}" -DBUILD_TESTING=ON
cmake --build "$build_dir" --config Release
ctest --test-dir "$build_dir" -C Release --output-on-failure

dist_dir="$project_root/dist"
variant="${MACOS_VARIANT:-$(uname -m)}"
setup_package_name="LRC-Plugin-Setup-macOS-$variant-v$version"
lyrics_package_name="LyricsTools-macOS-$variant-v$version"
stage_dir="$work_dir/stage"
setup_package_dir="$stage_dir/$setup_package_name"
lyrics_package_dir="$stage_dir/$lyrics_package_name"
mkdir -p "$setup_package_dir/Tools" "$setup_package_dir/Plugins" "$lyrics_package_dir"

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
cp -R "$master_bundle" "$setup_package_dir/Plugins/LRCMaster.bundle"
cp -R "$blackout_bundle" "$setup_package_dir/Plugins/LRCBlackOut.bundle"

"$python_bin" -m PyInstaller --noconfirm --clean --onedir --windowed \
    --name LRCPluginSetup --paths "$project_root/tools" \
    --distpath "$work_dir/setup-dist" --workpath "$work_dir/setup-build" \
    --specpath "$work_dir/setup-spec" "$project_root/tools/plugin_setup_gui.py"
cp -R "$work_dir/setup-dist/LRCPluginSetup.app" "$setup_package_dir/LRCPluginSetup.app"
"$python_bin" -m PyInstaller --noconfirm --clean --onedir --windowed \
    --name LyricsTools --paths "$project_root/tools" \
    --distpath "$work_dir/tools-dist" --workpath "$work_dir/tools-build" \
    --specpath "$work_dir/tools-spec" "$project_root/tools/lyrics_tools_gui.py"
cp -R "$work_dir/tools-dist/LyricsTools.app" "$lyrics_package_dir/LyricsTools.app"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $version" "$lyrics_package_dir/LyricsTools.app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $version" "$setup_package_dir/LRCPluginSetup.app/Contents/Info.plist"
for name in plugin_setup_gui.py gui_common.py vdj_setup.py; do
    cp "$project_root/tools/$name" "$setup_package_dir/Tools/$name"
done
cp "$project_root/tools/README.md" "$setup_package_dir/Tools/README.md"
cp "$project_root/README.md" "$setup_package_dir/README.md"
cp "$project_root/VERSION" "$setup_package_dir/VERSION"
cp "$project_root/VERSION" "$lyrics_package_dir/VERSION"
printf '%s\n' "Open LyricsTools.app. Plugin installation is a separate download." > "$lyrics_package_dir/README.txt"

# Ad-hoc signing preserves bundle integrity. Public notarization can replace this
# when an Apple Developer ID certificate is configured by the release runner.
/usr/bin/codesign --force --deep --sign - "$setup_package_dir/Plugins/LRCMaster.bundle"
/usr/bin/codesign --force --deep --sign - "$setup_package_dir/Plugins/LRCBlackOut.bundle"
/usr/bin/codesign --force --deep --sign - "$lyrics_package_dir/LyricsTools.app"
/usr/bin/codesign --force --deep --sign - "$setup_package_dir/LRCPluginSetup.app"
/usr/bin/codesign --verify --deep --strict "$setup_package_dir/Plugins/LRCMaster.bundle"
/usr/bin/codesign --verify --deep --strict "$setup_package_dir/Plugins/LRCBlackOut.bundle"
/usr/bin/codesign --verify --deep --strict "$lyrics_package_dir/LyricsTools.app"
/usr/bin/codesign --verify --deep --strict "$setup_package_dir/LRCPluginSetup.app"

mkdir -p "$dist_dir"
for package_dir in "$setup_package_dir" "$lyrics_package_dir"; do
    zip_path="$dist_dir/$(basename "$package_dir").zip"
    rm -f "$zip_path" "$zip_path.sha256"
    /usr/bin/ditto -c -k --sequesterRsrc --keepParent "$package_dir" "$zip_path"
    hash="$(/usr/bin/shasum -a 256 "$zip_path" | awk '{print toupper($1)}')"
    printf '%s  %s\n' "$hash" "$(basename "$zip_path")" > "$zip_path.sha256"
    printf 'Release ZIP: %s\nSHA-256: %s\n' "$zip_path" "$hash"
done
