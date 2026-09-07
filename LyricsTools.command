#!/bin/bash
set -u
export PATH="/usr/local/bin:/opt/homebrew/bin:$PATH"

root_dir="$(cd "$(dirname "$0")" && pwd)"
if [[ -d "$root_dir/Tools" ]]; then
    tools_dir="$root_dir/Tools"
else
    tools_dir="$root_dir/tools"
fi

python_bin="$(command -v python3 || true)"
if [[ -z "$python_bin" ]]; then
    echo "Python 3 was not found. Install it from https://www.python.org/downloads/macos/"
    read -r -p "Press Return to close..."
    exit 1
fi

if ! "$python_bin" -c 'import tkinter' >/dev/null 2>&1; then
    echo "This Python installation does not include Tkinter. Install Python from python.org."
    read -r -p "Press Return to close..."
    exit 1
fi

if ! "$python_bin" -c 'import mutagen, tidalapi' >/dev/null 2>&1; then
    echo "LyricsTools needs the Mutagen and tidalapi packages."
    read -r -p "Install the Python packages for this user now? [y/N] " answer
    if [[ "$answer" == "y" || "$answer" == "Y" ]]; then
        "$python_bin" -m pip install --user -r "$tools_dir/requirements.txt" || exit 1
    else
        exit 1
    fi
fi

exec "$python_bin" "$tools_dir/lyrics_tools_gui.py" "$@"
