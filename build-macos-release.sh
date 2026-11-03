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
package_name="LRC-Plugins-macOS-$variant-v$version"
package_dir="$work_dir/stage/$package_name"
mkdir -p "$package_dir"

# Windowless helper the plugin starts to save tags and search lyrics online.
"$python_bin" -m PyInstaller --noconfirm --clean --onefile --console \
    --name LrcHelper --paths "$project_root/helper" \
    --distpath "$work_dir/helper-dist" --workpath "$work_dir/helper-build" \
    --specpath "$work_dir/helper-spec" "$project_root/helper/lrc_helper.py"
"$work_dir/helper-dist/LrcHelper" --help >/dev/null 2>&1 || test $? -eq 2
plugins="$package_dir/Plugins"
mkdir -p "$plugins"

master_bundle="$(find "$build_dir" -type d -name 'LRCMaster.bundle' -print -quit)"
blackout_bundle="$(find "$build_dir" -type d -name 'LRCBlackOut.bundle' -print -quit)"
deck_bundle="$(find "$build_dir" -type d -name 'LRCDeck.bundle' -print -quit)"
test -n "$deck_bundle" || { echo 'LRCDeck.bundle was not built' >&2; exit 1; }
if [[ -z "$master_bundle" || -z "$blackout_bundle" ]]; then
    echo "The macOS plugin bundles were not produced." >&2
    exit 1
fi
/usr/bin/lipo "$master_bundle/Contents/MacOS/LRCMaster" -verify_arch arm64 x86_64
/usr/bin/lipo "$blackout_bundle/Contents/MacOS/LRCBlackOut" -verify_arch arm64 x86_64
/usr/bin/lipo "$deck_bundle/Contents/MacOS/LRCDeck" -verify_arch arm64 x86_64
/usr/bin/nm -gU "$master_bundle/Contents/MacOS/LRCMaster" | /usr/bin/grep -q ' _DllGetClassObject$'
/usr/bin/nm -gU "$blackout_bundle/Contents/MacOS/LRCBlackOut" | /usr/bin/grep -q ' _DllGetClassObject$'
/usr/bin/nm -gU "$deck_bundle/Contents/MacOS/LRCDeck" | /usr/bin/grep -q ' _DllGetClassObject$'
cp -R "$master_bundle" "$plugins/LRCMaster.bundle"
cp -R "$blackout_bundle" "$plugins/LRCBlackOut.bundle"
cp -R "$deck_bundle" "$plugins/LRCDeck.bundle"
cp "$work_dir/helper-dist/LrcHelper" "$plugins/LrcHelper"
chmod 755 "$plugins/LrcHelper"
cp "$project_root/VERSION" "$package_dir/VERSION"

# Ad-hoc signatures (locally signed; VirtualDJ loads the bundles from its plugin folder).
for item in "$plugins/LRCMaster.bundle" "$plugins/LRCBlackOut.bundle" "$plugins/LRCDeck.bundle" "$plugins/LrcHelper"; do
    /usr/bin/codesign --force --sign - "$item"
    /usr/bin/codesign --verify --strict "$item"
done
cp "$project_root/README.md" "$package_dir/README.txt"
cp "$project_root/README.md" "$package_dir/README.md"
cp "$project_root/INSTALL.txt" "$package_dir/INSTALL.txt"
for required in LRCMaster.bundle LRCBlackOut.bundle LRCDeck.bundle LrcHelper; do
    test -e "$plugins/$required" || { echo "$required is missing from the package" >&2; exit 1; }
done

mkdir -p "$dist_dir"
zip_path="$dist_dir/$package_name.zip"
rm -f "$zip_path" "$zip_path.sha256"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$package_dir" "$zip_path"
hash="$(/usr/bin/shasum -a 256 "$zip_path" | awk '{print toupper($1)}')"
printf '%s  %s\n' "$hash" "$(basename "$zip_path")" > "$zip_path.sha256"
printf 'Release ZIP: %s\nSHA-256: %s\n' "$zip_path" "$hash"
if [[ -t 1 || "${GITHUB_ACTIONS:-}" == "true" ]]; then
    printf '\033[32mDone - release package and checksum are ready.\033[0m\n'
else
    printf 'Done - release package and checksum are ready.\n'
fi
