"""Embedded-lyrics editor tab shared by the Windows and macOS LyricsTools app."""

from __future__ import annotations

import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk
from typing import Callable

import lyrics_tag_converter


class LyricsEditorTab:
    """Edit the timed (SYLT) and untimed (USLT) lyrics tag of one MP3."""

    TIMED, UNTIMED = "timed", "untimed"

    def __init__(self, parent: ttk.Frame, *, ensure_vdj_closed: Callable[[], None],
                 on_saved: Callable[[Path], None] | None = None):
        self.ensure_vdj_closed = ensure_vdj_closed
        self.on_saved = on_saved
        self.path_var = tk.StringVar()
        self.mode = tk.StringVar(value=self.TIMED)
        self.status = tk.StringVar(value="Choose an MP3 file and load its embedded lyrics.")
        self.texts = {self.TIMED: "", self.UNTIMED: ""}
        self.loaded: Path | None = None
        self._shown_mode = self.TIMED

        ttk.Label(
            parent, wraplength=790, justify="left",
            text="Edit the lyrics stored inside one MP3. Timed and Untimed keep separate "
                 "contents; timed rows use [mm:ss.xx] text. Saving replaces the tag and "
                 "creates exactly one SYLT or USLT frame.",
        ).pack(anchor="w", pady=(0, 8))
        row = ttk.Frame(parent)
        row.pack(fill="x", pady=(0, 6))
        row.columnconfigure(0, weight=1)
        entry = ttk.Entry(row, textvariable=self.path_var)
        entry.grid(row=0, column=0, sticky="ew")
        entry.bind("<Return>", lambda _e: self.load())
        ttk.Button(row, text="Browse…", command=self.browse).grid(row=0, column=1, padx=(6, 0))
        ttk.Button(row, text="Load", command=self.load).grid(row=0, column=2, padx=(6, 0))

        mode_row = ttk.Frame(parent)
        mode_row.pack(fill="x", pady=(0, 6))
        ttk.Label(mode_row, text="Lyrics type:").pack(side="left")
        for label, value in (("Timed (LRC timestamps)", self.TIMED), ("Untimed", self.UNTIMED)):
            ttk.Radiobutton(mode_row, text=label, value=value, variable=self.mode,
                            command=self._switch_mode).pack(side="left", padx=(10, 0))

        frame = ttk.Frame(parent)
        frame.pack(fill="both", expand=True)
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=1)
        self.text = tk.Text(frame, height=12, wrap="word", undo=True)
        self.text.grid(row=0, column=0, sticky="nsew")
        scrollbar = ttk.Scrollbar(frame, orient="vertical", command=self.text.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.text.configure(yscrollcommand=scrollbar.set)
        ttk.Label(parent, textvariable=self.status, wraplength=790, justify="left").pack(
            anchor="w", pady=(6, 0))

    # -- actions -------------------------------------------------------
    def browse(self) -> None:
        chosen = filedialog.askopenfilename(
            title="Select MP3", filetypes=(("MP3 files", "*.mp3"), ("All files", "*.*")))
        if chosen:
            self.path_var.set(chosen)
            self.load()

    def open_file(self, path: Path) -> None:
        self.path_var.set(str(path))
        self.load()

    def load(self) -> bool:
        path = Path(self.path_var.get().strip().strip('"')).expanduser()
        if path.suffix.casefold() != ".mp3" or not path.is_file():
            messagebox.showerror("Select MP3", "Choose an existing .mp3 file.")
            return False
        try:
            timed, untimed, synchronized = lyrics_tag_converter.read_editor_texts(path)
        except Exception as exc:
            messagebox.showerror("Cannot read tag", str(exc))
            return False
        self.loaded = path
        self.texts = {self.TIMED: timed, self.UNTIMED: untimed}
        self.mode.set(self.TIMED if synchronized or not untimed else self.UNTIMED)
        self._shown_mode = self.mode.get()
        self._show(self._shown_mode)
        self.status.set(f"Loaded {path.name}: "
                        f"{'timed' if timed else 'no timed'}, {'untimed' if untimed else 'no untimed'} lyrics.")
        return True

    def save(self) -> bool:
        if self.loaded is None and not self.load():
            return False
        self._store()
        mode = self.mode.get()
        try:
            self.ensure_vdj_closed()
            code = lyrics_tag_converter.save_editor_text(
                self.loaded, self.texts[mode], mode == self.TIMED)
        except Exception as exc:
            messagebox.showerror("Cannot save", str(exc))
            return False
        if code:
            messagebox.showerror("Cannot save", lyrics_tag_converter.EDITOR_ERRORS.get(
                code, f"Saving failed (code {code})."))
            return False
        self.status.set(f"Saved {mode} lyrics to {self.loaded.name}.")
        if self.on_saved:
            self.on_saved(self.loaded)
        return True

    # -- text bookkeeping ---------------------------------------------
    def _store(self) -> None:
        self.texts[self._shown_mode] = self.text.get("1.0", "end-1c")

    def _show(self, mode: str) -> None:
        self.text.delete("1.0", "end")
        self.text.insert("1.0", self.texts[mode])
        self.text.edit_reset()

    def _switch_mode(self) -> None:
        self._store()
        self._shown_mode = self.mode.get()
        self._show(self._shown_mode)
