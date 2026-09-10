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
import lyrics_issues
import lyrics_tag_converter
import restore_lrc
import vdj_playlist_sync
import vdj_setup
import gui_common


SCRIPT_DIR = Path(__file__).resolve().parent
APP_DATA_DIR = lrc_tool.default_runtime_dir()
SETTINGS_FILE = APP_DATA_DIR / "gui_settings.json"
SESSION_FILE = APP_DATA_DIR / "tidal_session.json"
REPORT_FILE = APP_DATA_DIR / "lyrics_report.csv"
TAB_NAMES = ("plugin", "playlist_sync", "import", "mark", "tidal", "issues", "restore")


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
        self.opt_advanced = tk.BooleanVar(value=bool(settings.get("opt_advanced", False)))
        self.last_operation = tk.StringVar(value="No operation has been run in this session.")
        self.last_backup: Path | None = None
        self.issue_results: list[lyrics_issues.LyricsIssue] = []
        default_list_root = f"Folder Sync - {initial_library.name or 'Music'}"
        self.playlist_root_name = tk.StringVar(
            value=str(
                default_list_root if ARGV_LIBRARY is not None
                else settings.get("playlist_root_name") or default_list_root
            ))
        self.opt_adopt_playlist_root = tk.BooleanVar(
            value=bool(settings.get("opt_adopt_playlist_root", False)))
        self.opt_add_search_db = tk.BooleanVar(
            value=bool(settings.get("opt_add_search_db", True)))
        self.opt_dryrun = tk.BooleanVar(value=bool(settings.get("opt_dryrun", True)))
        self.opt_import_overwrite = tk.BooleanVar(
            value=bool(settings.get("opt_import_overwrite", False)))
        self.opt_delete_sidecars = tk.BooleanVar(
            value=bool(settings.get("opt_delete_sidecars", False)))
        self.opt_delete_redundant_txt = tk.BooleanVar(
            value=bool(settings.get("opt_delete_redundant_txt", False)))
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
            self.package_layout = vdj_setup.locate_package_layout(
                gui_common.package_tools_dir(SCRIPT_DIR))
            self.package_error = ""
        except Exception as exc:
            self.package_layout = None
            self.package_error = str(exc)

        self.worker_running = False
        self.events = EventQueue()
        self.vdj_home_combos = []
        self.setup_buttons = []
        self._build_ui()
        requested_tab = REQUESTED_TAB or str(settings.get("active_tab", "plugin"))
        if requested_tab in self.tabs and (self.opt_advanced.get() or requested_tab not in {"tidal", "restore"}):
            self.notebook.select(self.tabs[requested_tab])
        self._tab_changed()
        self.after(100, self._poll_events)
        self.after(250, self._initial_vdj_detection)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _settings_payload(self) -> dict:
        return {
            "library_dir": self.library_dir.get(),
            "backup_dir": self.backup_dir.get(),
            "vdj_home": self.vdj_home.get(),
            "opt_advanced": self.opt_advanced.get(),
            "playlist_root_name": self.playlist_root_name.get(),
            "opt_adopt_playlist_root": self.opt_adopt_playlist_root.get(),
            "opt_add_search_db": self.opt_add_search_db.get(),
            "active_tab": self._active_tab_name(),
            "opt_dryrun": self.opt_dryrun.get(),
            "opt_import_overwrite": self.opt_import_overwrite.get(),
            "opt_delete_sidecars": self.opt_delete_sidecars.get(),
            "opt_delete_redundant_txt": self.opt_delete_redundant_txt.get(),
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
        header = tk.Frame(self, bg="#eaf2f8", padx=12, pady=8)
        header.grid(row=0, column=0, sticky="ew", padx=8, pady=(8, 0))
        header.columnconfigure(1, weight=1)
        tk.Label(header, text="VirtualDJ", bg="#eaf2f8", font=("TkDefaultFont", 10, "bold")).grid(row=0, column=0)
        tk.Label(header, textvariable=self.vdj_status, bg="#eaf2f8", anchor="w").grid(row=0, column=1, sticky="ew", padx=10)
        self.header_state = tk.Label(header, textvariable=self.vdj_installation_status, bg="#3b536b", fg="white", padx=9, pady=4)
        self.header_state.grid(row=0, column=2)
        ttk.Button(header, text="⚙", width=3, command=self._show_settings).grid(
            row=0, column=3, padx=(10, 0))

        self.notebook = ttk.Notebook(self)
        self.notebook.grid(row=1, column=0, sticky="ew", padx=8, pady=(8, 4))
        self.tabs = {name: ttk.Frame(self.notebook, padding=10) for name in TAB_NAMES}
        self.notebook.add(self.tabs["plugin"], text="Plugin")
        self.notebook.add(self.tabs["playlist_sync"], text="Playlists")
        self.notebook.add(self.tabs["import"], text="Local: Import")
        self.notebook.add(self.tabs["mark"], text="Local: Tags")
        self.notebook.add(self.tabs["tidal"], text="Online")
        self.notebook.add(self.tabs["issues"], text="Problems")
        self.notebook.add(self.tabs["restore"], text="Recovery")
        self.notebook.bind("<<NotebookTabChanged>>", self._tab_changed)

        plugin_tab = self.tabs["plugin"]
        self._description(
            plugin_tab,
            "Install or update LRC Master and LRC BlackOut in the selected VirtualDJ "
            "home folder. Existing plugin files are backed up before every change.",
        )
        self._vdj_path_row(plugin_tab)
        ttk.Label(
            plugin_tab, textvariable=self.vdj_status, wraplength=790, justify="left"
        ).pack(anchor="w", pady=(0, 8))
        package_text = (
            f"Included plugin version: {gui_common.packaged_version(SCRIPT_DIR)}"
            if self.package_layout else f"Plugin payload unavailable: {self.package_error}"
        )
        ttk.Label(plugin_tab, text=package_text, wraplength=790, justify="left").pack(
            anchor="w", pady=(0, 10))
        setup_actions = ttk.Frame(plugin_tab)
        setup_actions.pack(fill="x")
        for text, command in (
            ("Uninstall plugin", lambda: self._run_plugin_action("uninstall")),
            ("Restore newest backup", lambda: self._run_plugin_action("restore")),
            ("Reset video window layout", self._reset_video_window),
        ):
            button = ttk.Button(setup_actions, text=text, command=command)
            button.pack(side="left", padx=(0, 8))
            self.setup_buttons.append(button)
        ttk.Label(
            plugin_tab,
            text=(
                "VirtualDJ must be closed before plugin files or its saved video-window "
                "layout are changed. If it is running, LyricsTools can ask it to close "
                "normally and will never force-terminate it."
            ),
            wraplength=790,
            justify="left",
        ).pack(anchor="w", pady=(12, 0))

        import_tab = self.tabs["import"]
        self._folder_panel(import_tab)
        self._description(
            import_tab,
            "Import same-name .lrc and .txt files into MP3 ID3 tags. LRC has priority "
            "for synchronized lyrics. Writes are verified before sources are deleted. "
            "A real run also updates #sylt/#uslt in VirtualDJ User 1.",
        )
        ttk.Checkbutton(import_tab, text="Replace existing destination lyrics frames",
                        variable=self.opt_import_overwrite).pack(anchor="w", pady=3)
        ttk.Checkbutton(
            import_tab,
            text="Delete successfully imported LRC/TXT sidecars after verification",
            variable=self.opt_delete_sidecars,
        ).pack(anchor="w", pady=3)
        ttk.Checkbutton(
            import_tab,
            text="Also delete redundant timed TXT after a same-name LRC was imported and verified",
            variable=self.opt_delete_redundant_txt,
        ).pack(anchor="w", pady=3)
        language_row = ttk.Frame(import_tab)
        language_row.pack(anchor="w", pady=3)
        ttk.Label(language_row, text="Three-letter ID3 language code:").pack(side="left")
        ttk.Entry(language_row, width=6, textvariable=self.opt_language).pack(
            side="left", padx=6)
        self._dry_run_checkbox(
            import_tab, "Preview only (do not modify MP3 files or delete sidecars)")

        mark_tab = self.tabs["mark"]
        self._folder_panel(mark_tab)
        self._description(
            mark_tab,
            "Scan embedded lyrics and set the portable ID3 Grouping marker to "
            "Lyrics: Synced or Lyrics: Unsynced, then update #sylt/#uslt in VirtualDJ "
            "User 1. Unrelated values are preserved.",
        )
        self._dry_run_checkbox(mark_tab, "Preview only (do not modify Grouping tags)")

        issues_tab = self.tabs["issues"]
        self._folder_panel(issues_tab)
        self._description(
            issues_tab,
            "Scan locally for missing lyrics, invalid LRC files, unreadable tags and "
            "sidecars without a matching MP3. No filenames are sent online.",
        )
        issue_actions = ttk.Frame(issues_tab)
        issue_actions.pack(fill="x", pady=(0, 6))
        self.issue_summary = ttk.Label(issue_actions, text="No problem scan has been run.")
        self.issue_summary.pack(side="left")
        ttk.Button(issue_actions, text="Export CSV...", command=self._export_issues).pack(side="right")
        columns = ("problem", "file", "detail")
        issue_list = ttk.Frame(issues_tab)
        issue_list.pack(fill="both", expand=True)
        issue_list.columnconfigure(0, weight=1)
        issue_list.rowconfigure(0, weight=1)
        self.issue_tree = ttk.Treeview(issue_list, columns=columns, show="headings", height=10)
        self.issue_tree.heading("problem", text="Problem")
        self.issue_tree.heading("file", text="File")
        self.issue_tree.heading("detail", text="Detail")
        self.issue_tree.column("problem", width=145, stretch=False)
        self.issue_tree.column("file", width=250)
        self.issue_tree.column("detail", width=350)
        self.issue_tree.grid(row=0, column=0, sticky="nsew")
        issue_scrollbar = ttk.Scrollbar(
            issue_list, orient="vertical", command=self.issue_tree.yview)
        issue_scrollbar.grid(row=0, column=1, sticky="ns")
        self.issue_tree.configure(yscrollcommand=issue_scrollbar.set)

        tidal_tab = self.tabs["tidal"]
        self._folder_panel(tidal_tab, include_backup=True)
        self._description(
            tidal_tab,
            "Prefer local LRC sidecars, optionally retrieve missing lyrics from TIDAL, "
            "normalize USLT frames, back up lyrics before editing MP3 tags, and update "
            "VirtualDJ User 1 markers after a real run.",
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
        self._folder_panel(restore_tab, include_backup=True)
        self._description(
            restore_tab,
            "Restore LRC sidecars from the structured backup. Ambiguous backups from "
            "the older flat format are skipped for safety.",
        )
        ttk.Checkbutton(restore_tab, text="Replace sidecars that already exist next to MP3 files",
                        variable=self.opt_overwrite_restore).pack(anchor="w", pady=3)
        self._dry_run_checkbox(restore_tab, "Preview only (do not write sidecar files)")

        playlist_tab = self.tabs["playlist_sync"]
        self._folder_panel(playlist_tab)
        self._description(
            playlist_tab,
            "Mirror the selected music folder directly into one isolated VirtualDJ "
            "MyLists root. New tracks are added and tracks or folders no longer on disk "
            "are removed from that managed root only.",
        )
        self._vdj_path_row(playlist_tab)
        ttk.Label(
            playlist_tab, textvariable=self.vdj_status, wraplength=790, justify="left"
        ).pack(anchor="w", pady=(0, 4))
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
        ttk.Checkbutton(
            playlist_tab,
            text="Add synchronized tracks to Search DB (preserve all existing data)",
            variable=self.opt_add_search_db,
        ).pack(anchor="w", pady=3)
        self._dry_run_checkbox(
            playlist_tab, "Preview only (scan and compare, but do not change MyLists)")
        ttk.Label(
            playlist_tab,
            text=(
                "Only the named managed root is replaced. Other VirtualDJ lists remain "
                "untouched. A timestamped backup is created before every real change. "
                "Search DB is add-only: existing tracks and analyses are never removed. "
                "Folders containing both tracks and subfolders receive a separate "
                "'_ Tracks in this folder' list."
            ),
            wraplength=790,
            justify="left",
        ).pack(anchor="w", pady=(8, 0))

        log_frame = ttk.LabelFrame(self, text="Activity")
        log_frame.grid(row=2, column=0, sticky="nsew", padx=8, pady=4)
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)
        self.log_text = tk.Text(log_frame, wrap="word", state="disabled")
        self.log_text.tag_configure("success", foreground="#166534")
        self.log_text.tag_configure("warning", foreground="#1d4ed8")
        self.log_text.tag_configure("error", foreground="#b91c1c")
        self.log_text.tag_configure("neutral", foreground="#334155")
        self.log_text.grid(row=0, column=0, sticky="nsew")
        scrollbar = ttk.Scrollbar(log_frame, command=self.log_text.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.log_text.configure(yscrollcommand=scrollbar.set)

        actions = ttk.Frame(self)
        actions.grid(row=3, column=0, sticky="ew", padx=8, pady=(4, 8))
        ttk.Label(actions, textvariable=self.last_operation).pack(side="left")
        self.backup_button = ttk.Button(actions, text="Open backup", state="disabled", command=self._open_last_backup)
        self.backup_button.pack(side="left", padx=6)
        self.run_button = ttk.Button(
            actions, text="Run", command=self._run_current_tab)
        self.run_button.pack(side="right")
        self._toggle_advanced()
        self._update_run_button()

    def _folder_panel(self, parent, *, include_backup: bool = False) -> None:
        folders = ttk.LabelFrame(parent, text="Music folders")
        folders.pack(fill="x", pady=(0, 8))
        folders.columnconfigure(1, weight=1)
        self._path_row(folders, 0, "Music library:", self.library_dir, True)
        if include_backup:
            self._path_row(
                folders, 1, "Structured LRC backup:", self.backup_dir, False)

    def _vdj_path_row(self, parent) -> None:
        row = ttk.Frame(parent)
        row.pack(fill="x", pady=(2, 4))
        row.columnconfigure(1, weight=1)
        ttk.Label(row, text="VirtualDJ home:").grid(row=0, column=0, sticky="w")
        combo = ttk.Combobox(row, textvariable=self.vdj_home, state="normal")
        combo.grid(row=0, column=1, sticky="ew", padx=6)
        self.vdj_home_combos.append(combo)
        ttk.Button(
            row, text="Find automatically", command=self._detect_vdj_home
        ).grid(row=0, column=2, padx=(0, 4))
        ttk.Button(
            row, text="Browse...", command=self._browse_vdj_home
        ).grid(row=0, column=3)

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
        if self.vdj_home.get().strip() and self._validate_vdj_home(show_error=False):
            return
        self._detect_vdj_home(show_error=False)

    def _detect_vdj_home(self, show_error: bool = True) -> None:
        try:
            result = vdj_setup.query_virtualdj(self.package_layout)
        except Exception as exc:
            self.vdj_status.set(str(exc))
            self.vdj_installation_status.set("VirtualDJ detection failed")
            self.header_state.configure(bg="#b42318")
            if show_error:
                messagebox.showerror("VirtualDJ detection failed", str(exc))
            return

        candidates = result.get("Candidates") or []
        candidate_paths = [str(item.get("Path")) for item in candidates if item.get("Path")]
        for combo in self.vdj_home_combos:
            combo.configure(values=candidate_paths)
        selected = result.get("Selected")
        if selected:
            self.vdj_home.set(str(selected))
            self.vdj_status.set(str(selected))
            level, text = gui_common.plugin_state(
                Path(str(selected)), gui_common.packaged_version(SCRIPT_DIR), vdj_setup)
            self.vdj_installation_status.set(text)
            self.header_state.configure(bg={"ok": "#167a3f", "warning": "#b56500", "error": "#b42318"}[level])
        elif candidate_paths:
            self.vdj_installation_status.set("Select a VirtualDJ folder to check installation status.")
            self.header_state.configure(bg="#b56500")
            self.vdj_status.set(
                str(result.get("Message") or "Choose the active VirtualDJ home folder from the list."))
        else:
            self.vdj_installation_status.set("Select a VirtualDJ folder to check installation status.")
            self.header_state.configure(bg="#b42318")
            self.vdj_status.set(
                str(result.get("Message") or "Choose the active VirtualDJ home folder manually."))

    def _validate_vdj_home(self, show_error: bool) -> Path | None:
        value = self.vdj_home.get().strip()
        if not value:
            if show_error:
                messagebox.showerror("VirtualDJ folder required", "Select the active VirtualDJ home folder.")
            return None
        try:
            result = vdj_setup.query_virtualdj(self.package_layout, value)
        except Exception as exc:
            self.vdj_status.set(str(exc))
            self.vdj_installation_status.set("VirtualDJ folder is invalid")
            self.header_state.configure(bg="#b42318")
            if show_error:
                messagebox.showerror("Invalid VirtualDJ folder", str(exc))
            return None
        if not result.get("Valid"):
            error = str(result.get("Message") or "The selected folder is not a VirtualDJ home folder.")
            self.vdj_status.set(error)
            self.vdj_installation_status.set("VirtualDJ folder is invalid")
            self.header_state.configure(bg="#b42318")
            if show_error:
                messagebox.showerror("Invalid VirtualDJ folder", error)
            return None
        selected = Path(str(result.get("Selected") or value)).expanduser().resolve()
        self.vdj_home.set(str(selected))
        self.vdj_status.set(str(selected))
        level, text = gui_common.plugin_state(
            selected, gui_common.packaged_version(SCRIPT_DIR), vdj_setup)
        self.vdj_installation_status.set(text)
        self.header_state.configure(bg={"ok": "#167a3f", "warning": "#b56500", "error": "#b42318"}[level])
        return selected

    def _toggle_advanced(self) -> None:
        advanced_tabs = (
            ("tidal", "Online"),
            ("restore", "Recovery"),
        )
        visible = {self.notebook.tab(tab_id, "text") for tab_id in self.notebook.tabs()}
        if self.opt_advanced.get():
            for name, title in advanced_tabs:
                if title not in visible:
                    self.notebook.add(self.tabs[name], text=title)
        else:
            for name, _title in advanced_tabs:
                try:
                    self.notebook.forget(self.tabs[name])
                except tk.TclError:
                    pass
        self._update_run_button()

    def _show_settings(self) -> None:
        dialog = tk.Toplevel(self)
        dialog.title("LyricsTools settings")
        dialog.transient(self)
        dialog.resizable(False, False)
        body = ttk.Frame(dialog, padding=14)
        body.pack(fill="both", expand=True)
        ttk.Checkbutton(
            body, text="Advanced mode", variable=self.opt_advanced,
            command=self._toggle_advanced,
        ).pack(anchor="w", pady=(0, 12))
        ttk.Separator(body).pack(fill="x", pady=(0, 12))
        ttk.Button(body, text="Check for updates", command=self._check_updates).pack(
            fill="x", pady=3)
        ttk.Button(body, text="Create diagnostic ZIP…", command=self._create_diagnostics).pack(
            fill="x", pady=3)
        ttk.Button(body, text="Close", command=dialog.destroy).pack(
            anchor="e", pady=(14, 0))
        dialog.grab_set()

    def _active_tab_name(self) -> str:
        selected = self.notebook.select()
        return next(
            (name for name, frame in self.tabs.items() if str(frame) == selected),
            "import",
        )

    def _tab_changed(self, _event=None) -> None:
        self._update_run_button()

    def _update_run_button(self) -> None:
        if not hasattr(self, "run_button"):
            return
        active_tab = self._active_tab_name()
        labels = {
            "plugin": "Install / update plugin",
            "playlist_sync": "Preview / sync playlists",
            "import": "Import LRC / TXT",
            "mark": "Scan / mark lyrics tags",
            "tidal": "Download / normalize lyrics",
            "issues": "Scan for problems",
            "restore": "Restore LRC sidecars",
        }
        label = labels.get(active_tab, "Run")
        state = "disabled" if self.worker_running else "normal"
        self.run_button.configure(text=label, state=state)
        setup_state = "disabled" if self.worker_running or self.package_layout is None else "normal"
        for button in getattr(self, "setup_buttons", []):
            button.configure(state=setup_state)

    def _confirm_virtualdj_close(self, purpose: str) -> bool | None:
        if self.package_layout is None:
            messagebox.showerror("Plugin payload unavailable", self.package_error)
            return None
        try:
            running = vdj_setup.virtualdj_running(self.package_layout)
        except Exception as exc:
            messagebox.showerror("VirtualDJ status", str(exc))
            return None
        if not running:
            return False
        accepted = messagebox.askyesno(
            "VirtualDJ is running",
            "Save any current work first. VirtualDJ must be closed before "
            f"{purpose}. Ask VirtualDJ to close normally now?",
        )
        return True if accepted else None

    def _run_plugin_action(self, action: str) -> None:
        home = self._validate_vdj_home(show_error=True)
        if home is None or self.package_layout is None:
            return
        close_requested = self._confirm_virtualdj_close("plugin files are changed")
        if close_requested is None:
            return
        label = {"install": "Install / update", "uninstall": "Uninstall",
                 "restore": "Restore the newest backup"}[action]
        if not messagebox.askyesno(
            "Confirm plugin setup", f"{label} in:\n\n{home}?",
        ):
            return

        def job() -> Path | None:
            if close_requested and not vdj_setup.request_virtualdj_close(self.package_layout):
                raise RuntimeError(
                    "VirtualDJ did not close within 15 seconds. Close it manually and try again."
                )
            vdj_setup.run_action(self.package_layout, action, home, self.events.log)
            return gui_common.newest_backup(home, "LRC Lyrics Backups")

        self._start_worker(job, f"Plugin {action}")

    def _reset_video_window(self) -> None:
        home = self._validate_vdj_home(show_error=True)
        if home is None or self.package_layout is None:
            return
        close_requested = self._confirm_virtualdj_close(
            "the external video window layout is reset")
        if close_requested is None:
            return
        if not messagebox.askyesno(
            "Reset video window layout",
            "Forget only the saved size and position of VirtualDJ's external video "
            "window? A settings backup will be created first.",
        ):
            return

        def job() -> Path | None:
            if close_requested and not vdj_setup.request_virtualdj_close(self.package_layout):
                raise RuntimeError(
                    "VirtualDJ did not close within 15 seconds. Close it manually and try again."
                )
            return vdj_setup.reset_video_window_layout(home, self.package_layout, self.events.log)

        self._start_worker(job, "Video window layout reset")

    def _log(self, message) -> None:
        value = str(message)
        upper = value.upper()
        if "ERROR" in upper or "FAILED" in upper:
            style = "error"
        elif any(word in upper for word in ("WARNING", "WARN", "SKIP", "DRY-RUN")):
            style = "warning"
        elif any(word in upper for word in (
                "OK", "WRITE", "SUCCESS", "VERIFIED", "CREATED", "RESTORED", "SUMMARY")):
            style = "success"
        else:
            style = "neutral"
        self.log_text.configure(state="normal")
        self.log_text.insert("end", value + "\n", style)
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _open_last_backup(self) -> None:
        if self.last_backup is not None:
            gui_common.open_path(self.last_backup)

    def _check_updates(self) -> None:
        if self.worker_running:
            return
        current = gui_common.packaged_version(SCRIPT_DIR)
        self.worker_running = True
        self._update_run_button()
        self._log("Checking GitHub for a new LyricsTools release...")
        def worker() -> None:
            try:
                release = gui_common.latest_release()
                latest = str(release.get("tag_name") or "").removeprefix("v")
                available = gui_common.version_tuple(latest) > gui_common.version_tuple(current)
                self.events.queue.put(("update", (release, latest, available, current)))
            except Exception as exc:
                self.events.queue.put(("error", f"Update check failed: {exc}"))
            finally:
                self.events.done()
        threading.Thread(target=worker, daemon=False, name="lyrics-update-check").start()

    def _download_update(self, release: dict, latest: str) -> None:
        self.worker_running = True
        self._update_run_button()
        self._log(f"Downloading and verifying LyricsTools {latest}...")
        def worker() -> None:
            try:
                package = gui_common.download_update(release, "LyricsTools")
                target = gui_common.launch_updated_app(package, "LyricsTools")
                self.events.queue.put(("update_ready", str(target)))
            except Exception as exc:
                self.events.queue.put(("error", f"Update failed: {exc}"))
            finally:
                self.events.done()
        threading.Thread(target=worker, daemon=False, name="lyrics-update-download").start()

    def _export_issues(self) -> None:
        if not self.issue_results:
            messagebox.showinfo("Problem queue", "Run a problem scan first.")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".csv", initialfile="lyrics-problems.csv",
            filetypes=[("CSV report", "*.csv")],
        )
        if path:
            report = lyrics_issues.write_report(
                Path(path), self.issue_results, Path(self.library_dir.get()))
            self.last_operation.set(f"Problem report created: {report.name}")
            gui_common.open_path(report)

    def _create_diagnostics(self) -> None:
        current = gui_common.packaged_version(SCRIPT_DIR)
        path = filedialog.asksaveasfilename(
            defaultextension=".zip",
            initialfile=f"LyricsTools-diagnostics-{current}.zip",
            filetypes=[("ZIP archive", "*.zip")],
        )
        if not path:
            return
        result = gui_common.create_diagnostic_zip(
            Path(path), app_name="LyricsTools", version=current,
            status=self.vdj_installation_status.get(),
            recent_log=self.log_text.get("1.0", "end"),
        )
        messagebox.showinfo(
            "Diagnostics created",
            f"Created:\n{result}\n\nNo music, tags, credentials, or absolute user paths were included.",
        )

    def _poll_events(self) -> None:
        try:
            while True:
                kind, payload = self.events.queue.get_nowait()
                if kind == "log":
                    self._log(payload)
                elif kind == "operation":
                    name, backup = payload
                    self.last_operation.set(f"Last operation completed: {name}")
                    self.last_backup = backup
                    self.backup_button.configure(state="normal" if backup else "disabled")
                    self.after(0, lambda: self._validate_vdj_home(show_error=False))
                elif kind == "error":
                    self.last_operation.set(f"Last operation failed: {payload}")
                elif kind == "issues":
                    self.issue_results = payload
                    self.issue_tree.delete(*self.issue_tree.get_children())
                    root = Path(self.library_dir.get()).expanduser().resolve()
                    for issue in payload:
                        try:
                            display = issue.path.resolve().relative_to(root)
                        except ValueError:
                            display = issue.path.name
                        self.issue_tree.insert("", "end", values=(issue.kind, str(display), issue.detail))
                    self.issue_summary.configure(text=f"{len(payload)} item(s) need attention.")
                elif kind == "update":
                    release, latest, available, current = payload
                    if available:
                        if messagebox.askyesno(
                            "Update available",
                            f"LyricsTools {latest} is available.\n\nDownload, verify and launch it now?",
                        ):
                            self.after(0, lambda r=release, v=latest: self._download_update(r, v))
                    else:
                        messagebox.showinfo("No update", f"Version {current} is current.")
                elif kind == "update_ready":
                    self._log(f"Verified update launched: {payload}")
                    self.last_operation.set("The verified updated LyricsTools was launched.")
                elif kind == "done":
                    self.worker_running = False
                    self._update_run_button()
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

    def _start_worker(self, target, operation_name: str = "operation") -> None:
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
                backup = target()
                self.events.queue.put(("operation", (operation_name, backup)))
            except Exception as exc:
                self.events.log(f"[ERROR] {exc}")
                self.events.queue.put(("error", str(exc)))
            finally:
                self.events.done()

        threading.Thread(
            target=wrapper, daemon=False, name="lyrics-tools-worker"
        ).start()

    def _run_current_tab(self) -> None:
        tab = self._active_tab_name()
        dry_run = self.opt_dryrun.get()
        if tab == "plugin":
            self._run_plugin_action("install")
            return
        if tab == "issues":
            library = self._validated_library()
            if library is None:
                return
            def job() -> None:
                issues = lyrics_issues.scan_library(library, self.events.log)
                self.events.queue.put(("issues", issues))
            self._start_worker(job, "Problem scan")
            return
        if tab == "playlist_sync":
            library = self._validated_library()
            virtualdj_home = self._validate_vdj_home(show_error=True)
            if library is None or virtualdj_home is None:
                return
            try:
                target_name = vdj_playlist_sync.validate_target_name(
                    self.playlist_root_name.get())
            except ValueError as exc:
                messagebox.showerror("Invalid MyLists root", str(exc))
                return
            adopt_existing = self.opt_adopt_playlist_root.get()
            add_search_db = self.opt_add_search_db.get()
            if not dry_run:
                try:
                    preview = vdj_playlist_sync.sync_library(
                        library, virtualdj_home, target_name, dry_run=True,
                        adopt_existing=adopt_existing, add_search_db=add_search_db,
                        log=lambda _line: None)
                except Exception as exc:
                    messagebox.showerror("Preview failed", str(exc))
                    return
                unchanged = max(0, preview.tracks - preview.added_tracks)
                warning = (
                    f"Planned playlist changes:\n\n"
                    f"Add: {preview.added_tracks}\nRemove: {preview.removed_tracks}\n"
                    f"Unchanged: {unchanged}\n\nPlaylist files to update: "
                    f"{preview.created_or_updated}; obsolete files: {preview.removed}\n\n"
                    f"Mirror {library} into {target_name}? A backup is created first."
                )
                if adopt_existing:
                    warning += (
                        "\n\nAdoption is enabled: an existing root with this name may be replaced."
                    )
                if add_search_db:
                    warning += (
                        "\n\nCurrent tracks will also be added to Search DB. Existing "
                        "database entries and analyses will be preserved."
                    )
                if not messagebox.askyesno("Confirm VirtualDJ folder sync", warning):
                    return

            def job():
                if not dry_run:
                    vdj_setup.assert_virtualdj_closed(self.package_layout)
                result = vdj_playlist_sync.sync_library(
                    library,
                    virtualdj_home,
                    target_name,
                    dry_run=dry_run,
                    adopt_existing=adopt_existing,
                    add_search_db=add_search_db,
                    log=self.events.log,
                )
                return result.backup

            self._start_worker(job, "VirtualDJ folder sync")
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

        tag_vdj_home = None
        if tab in ("import", "mark", "tidal") and not dry_run:
            tag_vdj_home = self._validate_vdj_home(show_error=True)
            if tag_vdj_home is None:
                return

        def sync_user1_after_tag_write():
            if dry_run or tag_vdj_home is None:
                return None
            vdj_setup.assert_virtualdj_closed(self.package_layout)
            markers = lyrics_tag_converter.collect_virtualdj_lyrics_markers(library)
            return vdj_playlist_sync.sync_lyrics_user1_markers(
                tag_vdj_home, markers, log=self.events.log)

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
            delete_redundant_txt = self.opt_delete_redundant_txt.get()

            def job() -> None:
                if not dry_run:
                    vdj_setup.assert_virtualdj_closed(self.package_layout)
                lyrics_tag_converter.import_sidecars(
                    library,
                    write=not dry_run,
                    overwrite=overwrite,
                    delete_sidecars=delete_sidecars,
                    delete_redundant_txt=delete_redundant_txt,
                    language=language,
                    log=self.events.log,
                )
                return sync_user1_after_tag_write()
        elif tab == "mark":
            def job() -> None:
                if not dry_run:
                    vdj_setup.assert_virtualdj_closed(self.package_layout)
                found, changed, errors = lyrics_tag_converter.mark_existing_mp3(
                    library, write=not dry_run, log=self.events.log
                )
                self.events.log(
                    f"Summary: lyrics={found}, changed={changed}, errors={errors}")
                return sync_user1_after_tag_write()
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
                if not dry_run:
                    vdj_setup.assert_virtualdj_closed(self.package_layout)
                lrc_tool.run_library(
                    library, backup, session_file=SESSION_FILE, report_path=REPORT_FILE,
                    do_tidal=do_tidal, do_dedupe=do_dedupe, write_sylt=write_sylt,
                    english_threshold=english_threshold, dry_run=dry_run, log=self.events.log,
                )
                return sync_user1_after_tag_write()
        else:
            overwrite = self.opt_overwrite_restore.get()

            def job() -> None:
                restore_lrc.restore_library(
                    library, backup, overwrite=overwrite, dry_run=dry_run, log=self.events.log,
                )

        operation_name = self.notebook.tab(self.notebook.select(), "text")
        self._start_worker(job, operation_name)


if __name__ == "__main__":
    App().mainloop()
