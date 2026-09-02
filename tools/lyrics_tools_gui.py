#!/usr/bin/env python3
"""Unified graphical interface for MP3 and lyrics maintenance tools."""

from __future__ import annotations

import argparse
import json
import queue
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

import lrc_tool
import lyrics_tag_converter
import restore_lrc
import vdj_playlist_sync
import vdj_setup


SCRIPT_DIR = Path(__file__).resolve().parent
APP_DATA_DIR = lrc_tool.default_runtime_dir()
SETTINGS_FILE = APP_DATA_DIR / "gui_settings.json"
SESSION_FILE = APP_DATA_DIR / "tidal_session.json"
REPORT_FILE = APP_DATA_DIR / "lyrics_report.csv"
TAB_NAMES = ("import", "mark", "tidal", "restore", "playlist_sync", "setup")


def parse_launch_arguments():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("library", nargs="?")
    parser.add_argument("--tab", choices=TAB_NAMES)
    arguments, _ = parser.parse_known_args()
    library = Path(arguments.library).expanduser().resolve() if arguments.library else None
    return library, arguments.tab


ARGV_LIBRARY, REQUESTED_TAB = parse_launch_arguments()


def load_settings() -> dict:
    try:
        data = json.loads(SETTINGS_FILE.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


class EventQueue:
    """Pass log and completion events from the worker to Tk's main thread."""

    def __init__(self):
        self.queue: queue.Queue[tuple[str, str | None]] = queue.Queue()

    def log(self, message) -> None:
        self.queue.put(("log", str(message)))

    def done(self) -> None:
        self.queue.put(("done", None))


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("MP3 & Lyrics Tools")
        self.geometry("860x760")
        self.minsize(760, 660)
        self.columnconfigure(0, weight=1)
        self.rowconfigure(2, weight=1)

        settings = load_settings()
        if ARGV_LIBRARY is not None:
            initial_library = ARGV_LIBRARY
            initial_backup = ARGV_LIBRARY / "_lrc_backup"
        else:
            initial_library = Path(settings.get("library_dir") or Path.cwd()).expanduser()
            initial_backup = Path(settings.get("backup_dir") or initial_library / "_lrc_backup").expanduser()

        self.library_dir = tk.StringVar(value=str(initial_library))
        self.backup_dir = tk.StringVar(value=str(initial_backup))
        self.vdj_home = tk.StringVar(value=str(settings.get("vdj_home") or ""))
        self.vdj_status = tk.StringVar(value="VirtualDJ folder has not been checked yet.")
        self.vdj_installation_status = tk.StringVar(value="Installation status is unknown.")
        self.setup_action = tk.StringVar(value=str(settings.get("setup_action") or "install"))
        default_list_root = f"Folder Sync - {initial_library.name or 'Music'}"
        self.playlist_root_name = tk.StringVar(
            value=str(
                default_list_root if ARGV_LIBRARY is not None
                else settings.get("playlist_root_name") or default_list_root
            ))
        self.opt_adopt_playlist_root = tk.BooleanVar(
            value=bool(settings.get("opt_adopt_playlist_root", False)))
        self.opt_dryrun = tk.BooleanVar(value=bool(settings.get("opt_dryrun", True)))
        self.opt_import_overwrite = tk.BooleanVar(
            value=bool(settings.get("opt_import_overwrite", False)))
        self.opt_delete_sidecars = tk.BooleanVar(
            value=bool(settings.get("opt_delete_sidecars", False)))
        self.opt_language = tk.StringVar(value=str(settings.get("opt_language", "und")))
        self.opt_tidal = tk.BooleanVar(value=bool(settings.get("opt_tidal", False)))
        self.opt_dedupe = tk.BooleanVar(value=bool(settings.get("opt_dedupe", True)))
        self.opt_sylt = tk.BooleanVar(value=bool(settings.get("opt_sylt", True)))
        self.opt_overwrite_restore = tk.BooleanVar(value=bool(settings.get("opt_overwrite_restore", False)))
        try:
            saved_threshold = int(settings.get("opt_english_threshold", 5))
        except (TypeError, ValueError):
            saved_threshold = 5
        self.opt_english_threshold = tk.IntVar(value=min(50, max(1, saved_threshold)))

        try:
            self.package_layout = vdj_setup.locate_package_layout(SCRIPT_DIR)
            self.package_error = ""
        except Exception as exc:
            self.package_layout = None
            self.package_error = str(exc)

        self.worker_running = False
        self.events = EventQueue()
        self._build_ui()
        requested_tab = REQUESTED_TAB or str(settings.get("active_tab", "import"))
        if requested_tab in self.tabs:
            self.notebook.select(self.tabs[requested_tab])
        self.after(100, self._poll_events)
        self.after(250, self._initial_vdj_detection)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _settings_payload(self) -> dict:
        return {
            "library_dir": self.library_dir.get(),
            "backup_dir": self.backup_dir.get(),
            "vdj_home": self.vdj_home.get(),
            "setup_action": self.setup_action.get(),
            "playlist_root_name": self.playlist_root_name.get(),
            "opt_adopt_playlist_root": self.opt_adopt_playlist_root.get(),
            "active_tab": self._active_tab_name(),
            "opt_dryrun": self.opt_dryrun.get(),
            "opt_import_overwrite": self.opt_import_overwrite.get(),
            "opt_delete_sidecars": self.opt_delete_sidecars.get(),
            "opt_language": self.opt_language.get(),
            "opt_tidal": self.opt_tidal.get(),
            "opt_dedupe": self.opt_dedupe.get(),
            "opt_sylt": self.opt_sylt.get(),
            "opt_overwrite_restore": self.opt_overwrite_restore.get(),
            "opt_english_threshold": self.opt_english_threshold.get(),
        }

    def _save_settings(self) -> None:
        try:
            lrc_tool.atomic_write_text(
                SETTINGS_FILE,
                json.dumps(self._settings_payload(), indent=2),
            )
        except Exception as exc:
            self._log(f"[WARNING] Settings could not be saved: {exc}")

    def _on_close(self) -> None:
        if self.worker_running:
            messagebox.showwarning(
                "Operation in progress",
                "Wait for the current operation to finish before closing the window. "
                "Closing Python during a file operation could leave that operation incomplete.",
            )
            return
        self._save_settings()
        self.destroy()

    @staticmethod
    def _description(parent, text: str) -> None:
        ttk.Label(parent, text=text, wraplength=750, justify="left").pack(
            anchor="w", pady=(0, 8))

    def _dry_run_checkbox(self, parent, text: str) -> None:
        ttk.Checkbutton(parent, text=text, variable=self.opt_dryrun).pack(
            anchor="w", pady=3)

    def _build_ui(self) -> None:
        self.folders_frame = ttk.LabelFrame(self, text="MP3 processing folders")
        self.folders_frame.grid(row=0, column=0, sticky="ew", padx=8, pady=(8, 4))
        self.folders_frame.columnconfigure(1, weight=1)
        self._path_row(self.folders_frame, 0, "Music library:", self.library_dir, True)
        self._path_row(self.folders_frame, 1, "Structured LRC backup:", self.backup_dir, False)

        self.notebook = ttk.Notebook(self)
        self.notebook.grid(row=1, column=0, sticky="ew", padx=8, pady=4)
        self.tabs = {name: ttk.Frame(self.notebook, padding=10) for name in TAB_NAMES}
        self.notebook.add(self.tabs["import"], text="Import LRC / TXT")
        self.notebook.add(self.tabs["mark"], text="Mark existing lyrics")
        self.notebook.add(self.tabs["tidal"], text="TIDAL / normalize")
        self.notebook.add(self.tabs["restore"], text="Restore sidecars")
        self.notebook.add(self.tabs["playlist_sync"], text="Folders to VDJ lists")
        self.notebook.add(self.tabs["setup"], text="VirtualDJ setup")
        self.notebook.bind("<<NotebookTabChanged>>", self._tab_changed)

        import_tab = self.tabs["import"]
        self._description(
            import_tab,
            "Import same-name .lrc and .txt files into MP3 ID3 tags. LRC has priority "
            "for synchronized lyrics. Writes are verified before sources are deleted.",
        )
        ttk.Checkbutton(import_tab, text="Replace existing destination lyrics frames",
                        variable=self.opt_import_overwrite).pack(anchor="w", pady=3)
        ttk.Checkbutton(
            import_tab,
            text="Delete successfully imported LRC/TXT sidecars after verification",
            variable=self.opt_delete_sidecars,
        ).pack(anchor="w", pady=3)
        language_row = ttk.Frame(import_tab)
        language_row.pack(anchor="w", pady=3)
        ttk.Label(language_row, text="Three-letter ID3 language code:").pack(side="left")
        ttk.Entry(language_row, width=6, textvariable=self.opt_language).pack(
            side="left", padx=6)
        self._dry_run_checkbox(
            import_tab, "Preview only (do not modify MP3 files or delete sidecars)")

        mark_tab = self.tabs["mark"]
        self._description(
            mark_tab,
            "Scan embedded lyrics and set the portable ID3 Grouping marker to "
            "Lyrics: Synced or Lyrics: Unsynced. Unrelated Grouping values are preserved.",
        )
        self._dry_run_checkbox(mark_tab, "Preview only (do not modify Grouping tags)")

        tidal_tab = self.tabs["tidal"]
        self._description(
            tidal_tab,
            "Prefer local LRC sidecars, optionally retrieve missing lyrics from TIDAL, "
            "normalize USLT frames, and back up lyrics before editing MP3 tags.",
        )
        ttk.Checkbutton(tidal_tab, text="Download missing lyrics from TIDAL (browser login required)",
                        variable=self.opt_tidal).pack(anchor="w", pady=3)
        ttk.Checkbutton(tidal_tab, text="Normalize existing USLT frames when no better source is found",
                        variable=self.opt_dedupe).pack(anchor="w", pady=3)
        ttk.Checkbutton(tidal_tab, text="Create synchronized SYLT frames when timestamps are available",
                        variable=self.opt_sylt).pack(anchor="w", pady=3)
        self._dry_run_checkbox(
            tidal_tab,
            "Preview only (do not modify media, backups, credentials, or reports)",
        )

        threshold_row = ttk.Frame(tidal_tab)
        threshold_row.pack(anchor="w", pady=3)
        ttk.Label(threshold_row, text="English-language detection threshold:").pack(side="left")
        ttk.Spinbox(threshold_row, from_=1, to=50, width=5,
                    textvariable=self.opt_english_threshold).pack(side="left", padx=6)

        restore_tab = self.tabs["restore"]
        self._description(
            restore_tab,
            "Restore LRC sidecars from the structured backup. Ambiguous backups from "
            "the older flat format are skipped for safety.",
        )
        ttk.Checkbutton(restore_tab, text="Replace sidecars that already exist next to MP3 files",
                        variable=self.opt_overwrite_restore).pack(anchor="w", pady=3)
        self._dry_run_checkbox(restore_tab, "Preview only (do not write sidecar files)")

        playlist_tab = self.tabs["playlist_sync"]
        self._description(
            playlist_tab,
            "Mirror the selected music folder directly into one isolated VirtualDJ "
            "MyLists root. New tracks are added and tracks or folders no longer on disk "
            "are removed from that managed root only.",
        )
        playlist_vdj_row = ttk.Frame(playlist_tab)
        playlist_vdj_row.pack(fill="x", pady=3)
        ttk.Label(playlist_vdj_row, text="VirtualDJ home:").pack(side="left")
        ttk.Label(playlist_vdj_row, textvariable=self.vdj_home).pack(
            side="left", padx=6, fill="x", expand=True)
        ttk.Button(
            playlist_vdj_row, text="Find automatically", command=self._detect_vdj_home
        ).pack(side="right")
        root_row = ttk.Frame(playlist_tab)
        root_row.pack(fill="x", pady=3)
        ttk.Label(root_row, text="Managed MyLists root:").pack(side="left")
        ttk.Entry(root_row, textvariable=self.playlist_root_name).pack(
            side="left", padx=6, fill="x", expand=True)
        ttk.Button(
            root_row, text="Use folder name", command=self._playlist_root_from_library
        ).pack(side="right")
        ttk.Checkbutton(
            playlist_tab,
            text="Adopt and replace an existing MyLists root with this exact name",
            variable=self.opt_adopt_playlist_root,
        ).pack(anchor="w", pady=3)
        self._dry_run_checkbox(
            playlist_tab, "Preview only (scan and compare, but do not change MyLists)")
        ttk.Label(
            playlist_tab,
            text=(
                "Only the named managed root is replaced. Other VirtualDJ lists remain "
                "untouched. A timestamped backup is created before every real change. "
                "Folders containing both tracks and subfolders receive a separate "
                "'_ Tracks in this folder' list."
            ),
            wraplength=790,
            justify="left",
        ).pack(anchor="w", pady=(8, 0))

        setup_tab = self.tabs["setup"]
        self._description(
            setup_tab,
            "Install or update the bundled LRC Master and LRC BlackOut DLLs. The same "
            "verified installer used by Install.cmd creates backups, removes obsolete "
            "plugin variants, and requires VirtualDJ to be closed.",
        )
        vdj_path_row = ttk.Frame(setup_tab)
        vdj_path_row.pack(fill="x", pady=(2, 4))
        vdj_path_row.columnconfigure(1, weight=1)
        ttk.Label(vdj_path_row, text="VirtualDJ home:").grid(row=0, column=0, sticky="w")
        self.vdj_home_combo = ttk.Combobox(
            vdj_path_row, textvariable=self.vdj_home, state="normal")
        self.vdj_home_combo.grid(row=0, column=1, sticky="ew", padx=6)
        ttk.Button(vdj_path_row, text="Find automatically", command=self._detect_vdj_home).grid(
            row=0, column=2, padx=(0, 4))
        ttk.Button(vdj_path_row, text="Browse...", command=self._browse_vdj_home).grid(
            row=0, column=3)
        ttk.Label(
            setup_tab, textvariable=self.vdj_status, wraplength=790, justify="left"
        ).pack(anchor="w", pady=(0, 8))
        ttk.Label(
            setup_tab, textvariable=self.vdj_installation_status,
            wraplength=790, justify="left",
        ).pack(anchor="w", pady=(0, 8))

        package_text = (
            f"Bundled plugin package: version {self.package_layout.version} "
            f"({self.package_layout.payload})"
            if self.package_layout is not None
            else f"Bundled plugin package unavailable: {self.package_error}"
        )
        ttk.Label(setup_tab, text=package_text, wraplength=790, justify="left").pack(
            anchor="w", pady=(0, 8))
        ttk.Radiobutton(
            setup_tab,
            text="Install or update the VirtualDJ plugin (recommended)",
            variable=self.setup_action,
            value="install",
            command=self._update_run_button,
        ).pack(anchor="w", pady=3)
        ttk.Radiobutton(
            setup_tab,
            text="Uninstall the plugin and create a backup first",
            variable=self.setup_action,
            value="uninstall",
            command=self._update_run_button,
        ).pack(anchor="w", pady=3)
        ttk.Radiobutton(
            setup_tab,
            text="Restore the newest installer backup",
            variable=self.setup_action,
            value="restore",
            command=self._update_run_button,
        ).pack(anchor="w", pady=3)

        log_frame = ttk.LabelFrame(self, text="Activity")
        log_frame.grid(row=2, column=0, sticky="nsew", padx=8, pady=4)
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)
        self.log_text = tk.Text(log_frame, wrap="word", state="disabled")
        self.log_text.grid(row=0, column=0, sticky="nsew")
        scrollbar = ttk.Scrollbar(log_frame, command=self.log_text.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.log_text.configure(yscrollcommand=scrollbar.set)

        actions = ttk.Frame(self)
        actions.grid(row=3, column=0, sticky="ew", padx=8, pady=(4, 8))
        ttk.Label(actions, text=f"Runtime data: {APP_DATA_DIR}").pack(side="left")
        self.run_button = ttk.Button(
            actions, text="Run selected tool", command=self._run_current_tab)
        self.run_button.pack(side="right")
        self._update_run_button()

    def _path_row(self, parent, row: int, label: str, variable, update_backup: bool) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=4, pady=4)
        ttk.Entry(parent, textvariable=variable).grid(row=row, column=1, sticky="ew", padx=4, pady=4)
        ttk.Button(parent, text="Browse...", command=lambda: self._browse(variable, update_backup)).grid(
            row=row, column=2, padx=4, pady=4)

    def _browse(self, variable, update_backup: bool) -> None:
        chosen = filedialog.askdirectory(initialdir=variable.get() or str(SCRIPT_DIR))
        if chosen:
            variable.set(chosen)
            if update_backup:
                self.backup_dir.set(str(Path(chosen) / "_lrc_backup"))

    def _browse_vdj_home(self) -> None:
        chosen = filedialog.askdirectory(
            initialdir=self.vdj_home.get() or str(Path.home()))
        if chosen:
            self.vdj_home.set(chosen)
            self._validate_vdj_home(show_error=True)

    def _playlist_root_from_library(self) -> None:
        library = Path(self.library_dir.get()).expanduser()
        name = library.name or "Music"
        self.playlist_root_name.set(f"Folder Sync - {name}")

    def _initial_vdj_detection(self) -> None:
        if self.package_layout is None:
            self.vdj_status.set(self.package_error)
            return
        if self.vdj_home.get().strip() and self._validate_vdj_home(show_error=False):
            return
        self._detect_vdj_home(show_error=False)

    def _detect_vdj_home(self, show_error: bool = True) -> None:
        if self.package_layout is None:
            if show_error:
                messagebox.showerror("Installer unavailable", self.package_error)
            return
        try:
            result = vdj_setup.query_virtualdj(self.package_layout)
        except Exception as exc:
            self.vdj_status.set(str(exc))
            if show_error:
                messagebox.showerror("VirtualDJ detection failed", str(exc))
            return

        candidates = result.get("Candidates") or []
        candidate_paths = [str(item.get("Path")) for item in candidates if item.get("Path")]
        self.vdj_home_combo.configure(values=candidate_paths)
        selected = result.get("Selected")
        if selected:
            self.vdj_home.set(str(selected))
            self.vdj_status.set("Active VirtualDJ home folder detected and validated.")
            self.vdj_installation_status.set(
                vdj_setup.installed_status(Path(str(selected))))
        elif candidate_paths:
            self.vdj_installation_status.set("Select a VirtualDJ folder to check installation status.")
            self.vdj_status.set(
                str(result.get("Message") or "Choose the active VirtualDJ home folder from the list."))
        else:
            self.vdj_installation_status.set("Select a VirtualDJ folder to check installation status.")
            self.vdj_status.set(
                str(result.get("Message") or "Choose the active VirtualDJ home folder manually."))

    def _validate_vdj_home(self, show_error: bool) -> Path | None:
        if self.package_layout is None:
            if show_error:
                messagebox.showerror("Installer unavailable", self.package_error)
            return None
        value = self.vdj_home.get().strip()
        if not value:
            if show_error:
                messagebox.showerror("VirtualDJ folder required", "Select the active VirtualDJ home folder.")
            return None
        try:
            result = vdj_setup.query_virtualdj(self.package_layout, value)
        except Exception as exc:
            self.vdj_status.set(str(exc))
            if show_error:
                messagebox.showerror("Invalid VirtualDJ folder", str(exc))
            return None
        if not result.get("Valid"):
            error = str(result.get("Message") or "The selected folder is not a VirtualDJ home folder.")
            self.vdj_status.set(error)
            if show_error:
                messagebox.showerror("Invalid VirtualDJ folder", error)
            return None
        selected = Path(str(result.get("Selected") or value)).expanduser().resolve()
        self.vdj_home.set(str(selected))
        self.vdj_status.set("VirtualDJ home folder is valid.")
        self.vdj_installation_status.set(vdj_setup.installed_status(selected))
        return selected

    def _active_tab_name(self) -> str:
        selected = self.notebook.select()
        return next(
            (name for name, frame in self.tabs.items() if str(frame) == selected),
            "import",
        )

    def _tab_changed(self, _event=None) -> None:
        if self._active_tab_name() == "setup":
            self.folders_frame.grid_remove()
        else:
            self.folders_frame.grid()
        self._update_run_button()

    def _update_run_button(self) -> None:
        if not hasattr(self, "run_button"):
            return
        active_tab = self._active_tab_name()
        if active_tab == "playlist_sync":
            label = "Preview / sync folders"
        elif active_tab != "setup":
            label = "Run selected tool"
        else:
            label = {
                "install": "Install / update plugin",
                "uninstall": "Uninstall plugin",
                "restore": "Restore newest backup",
            }.get(self.setup_action.get(), "Run setup action")
        self.run_button.configure(text=label)

    def _log(self, message) -> None:
        self.log_text.configure(state="normal")
        self.log_text.insert("end", str(message) + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _poll_events(self) -> None:
        try:
            while True:
                kind, payload = self.events.queue.get_nowait()
                if kind == "log":
                    self._log(payload)
                elif kind == "done":
                    self.worker_running = False
                    self.run_button.configure(state="normal")
        except queue.Empty:
            pass
        if self.winfo_exists():
            self.after(100, self._poll_events)

    def _validated_paths(self) -> tuple[Path, Path] | None:
        try:
            return lrc_tool.validate_directories(Path(self.library_dir.get()), Path(self.backup_dir.get()))
        except Exception as exc:
            messagebox.showerror("Invalid folders", str(exc))
            return None

    def _validated_library(self) -> Path | None:
        library = Path(self.library_dir.get()).expanduser().resolve()
        if not library.is_dir():
            messagebox.showerror(
                "Invalid music library", f"Folder does not exist: {library}")
            return None
        return library

    def _start_worker(self, target) -> None:
        if self.worker_running:
            messagebox.showinfo("Operation in progress", "Wait for the current operation to finish.")
            return
        self._save_settings()
        self.worker_running = True
        self.run_button.configure(state="disabled")
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

        def wrapper() -> None:
            try:
                target()
            except Exception as exc:
                self.events.log(f"[ERROR] {exc}")
            finally:
                self.events.done()

        threading.Thread(
            target=wrapper, daemon=False, name="lyrics-tools-worker"
        ).start()

    def _run_current_tab(self) -> None:
        tab = self._active_tab_name()
        dry_run = self.opt_dryrun.get()
        if tab == "playlist_sync":
            library = self._validated_library()
            virtualdj_home = self._validate_vdj_home(show_error=True)
            if library is None or virtualdj_home is None or self.package_layout is None:
                return
            try:
                target_name = vdj_playlist_sync.validate_target_name(
                    self.playlist_root_name.get())
            except ValueError as exc:
                messagebox.showerror("Invalid MyLists root", str(exc))
                return
            adopt_existing = self.opt_adopt_playlist_root.get()
            if not dry_run:
                warning = (
                    f"This will mirror:\n\n{library}\n\ninto the managed VirtualDJ "
                    f"root:\n\n{target_name}\n\nTracks and folders missing from the "
                    "source will be removed from that managed root. A backup is created first."
                )
                if adopt_existing:
                    warning += (
                        "\n\nAdoption is enabled: an existing root with this name may be replaced."
                    )
                if not messagebox.askyesno("Confirm VirtualDJ folder sync", warning):
                    return

            def job() -> None:
                if not dry_run:
                    vdj_setup.assert_virtualdj_closed(self.package_layout)
                vdj_playlist_sync.sync_library(
                    library,
                    virtualdj_home,
                    target_name,
                    dry_run=dry_run,
                    adopt_existing=adopt_existing,
                    log=self.events.log,
                )

            self._start_worker(job)
            return
        if tab == "setup":
            virtualdj_home = self._validate_vdj_home(show_error=True)
            if virtualdj_home is None or self.package_layout is None:
                return
            action = self.setup_action.get()
            descriptions = {
                "install": "install or update LRC Master and LRC BlackOut",
                "uninstall": "uninstall LRC Master and LRC BlackOut",
                "restore": "restore the newest LRC Lyrics installer backup",
            }
            if action not in descriptions:
                messagebox.showerror("Invalid setup action", "Choose a setup action.")
                return
            if not messagebox.askyesno(
                "Confirm VirtualDJ setup",
                f"Close VirtualDJ completely, then confirm that you want to "
                f"{descriptions[action]} in:\n\n{virtualdj_home}",
            ):
                return

            def job() -> None:
                vdj_setup.run_action(
                    self.package_layout, action, virtualdj_home, log=self.events.log)
                self.events.log(vdj_setup.installed_status(virtualdj_home))

            self._start_worker(job)
            return
        if tab in ("import", "mark"):
            library = self._validated_library()
            if library is None:
                return
        else:
            paths = self._validated_paths()
            if paths is None:
                return
            library, backup = paths

        if tab == "import":
            language = self.opt_language.get().strip().lower()
            if (len(language) != 3 or not language.isascii()
                    or not language.isalpha()):
                messagebox.showerror(
                    "Invalid language",
                    "Enter a three-letter ASCII code such as und or eng.",
                )
                return
            overwrite = self.opt_import_overwrite.get()
            delete_sidecars = self.opt_delete_sidecars.get()

            def job() -> None:
                lyrics_tag_converter.import_sidecars(
                    library,
                    write=not dry_run,
                    overwrite=overwrite,
                    delete_sidecars=delete_sidecars,
                    language=language,
                    log=self.events.log,
                )
        elif tab == "mark":
            def job() -> None:
                found, changed, errors = lyrics_tag_converter.mark_existing_mp3(
                    library, write=not dry_run, log=self.events.log
                )
                self.events.log(
                    f"Summary: lyrics={found}, changed={changed}, errors={errors}")
        elif tab == "tidal":
            do_tidal = self.opt_tidal.get()
            do_dedupe = self.opt_dedupe.get()
            write_sylt = self.opt_sylt.get()
            try:
                english_threshold = self.opt_english_threshold.get()
            except tk.TclError:
                messagebox.showerror("Invalid value", "The English-language threshold must be an integer.")
                return

            def job() -> None:
                lrc_tool.run_library(
                    library, backup, session_file=SESSION_FILE, report_path=REPORT_FILE,
                    do_tidal=do_tidal, do_dedupe=do_dedupe, write_sylt=write_sylt,
                    english_threshold=english_threshold, dry_run=dry_run, log=self.events.log,
                )
        else:
            overwrite = self.opt_overwrite_restore.get()

            def job() -> None:
                restore_lrc.restore_library(
                    library, backup, overwrite=overwrite, dry_run=dry_run, log=self.events.log,
                )

        self._start_worker(job)


if __name__ == "__main__":
    App().mainloop()
