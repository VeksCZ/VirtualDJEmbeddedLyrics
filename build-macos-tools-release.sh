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

dist_dir="$project_root/dist"
package_name="LRC-Lyrics-Tools-macOS-v$version"
stage_dir="$(mktemp -d "${TMPDIR:-/tmp}/lrc-tools.XXXXXX")"
trap 'rm -rf "$stage_dir"' EXIT
package_dir="$stage_dir/$package_name"
mkdir -p "$package_dir/Tools"

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

mkdir -p "$dist_dir"
zip_path="$dist_dir/$package_name.zip"
rm -f "$zip_path" "$zip_path.sha256"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$package_dir" "$zip_path"
hash="$(/usr/bin/shasum -a 256 "$zip_path" | awk '{print toupper($1)}')"
printf '%s  %s\n' "$hash" "$(basename "$zip_path")" > "$zip_path.sha256"
printf 'Release ZIP: %s\nSHA-256: %s\n' "$zip_path" "$hash"
