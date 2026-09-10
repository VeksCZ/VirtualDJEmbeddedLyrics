"""Dedicated LRC Lyrics plugin installer GUI (stdlib-only source)."""

from __future__ import annotations

import json
import queue
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

import gui_common
import vdj_setup


APP_VERSION = "0.0.0"
SCRIPT_DIR = Path(__file__).resolve().parent


class App(tk.Tk):
    COLORS = {"ok": "#167a3f", "warning": "#b56500", "error": "#b42318", "neutral": "#3b536b"}

    def __init__(self):
        super().__init__()
        self.title("LRC Plugin Setup")
        self.geometry("780x590")
        self.minsize(680, 520)
        self.columnconfigure(0, weight=1)
        self.rowconfigure(2, weight=1)
        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.home = tk.StringVar()
        self.path_status = tk.StringVar(value="Looking for VirtualDJ…")
        self.plugin_status = tk.StringVar(value="Plugin status is unknown")
        self.last_operation = tk.StringVar(value="No operation has been run in this session.")
        self.last_backup: Path | None = None
        self.running = False
        try:
            self.layout = vdj_setup.locate_package_layout(gui_common.package_tools_dir(SCRIPT_DIR))
            global APP_VERSION
            APP_VERSION = self.layout.version
            self.package_error = ""
        except Exception as exc:
            self.layout = None
            self.package_error = str(exc)
        self._build()
        self.after(100, self._poll)
        self.after(200, self._detect)

    def _build(self) -> None:
        header = tk.Frame(self, bg="#eaf2f8", padx=14, pady=10)
        header.grid(row=0, column=0, sticky="ew")
        header.columnconfigure(1, weight=1)
        tk.Label(header, text="VirtualDJ", bg="#eaf2f8", font=("TkDefaultFont", 10, "bold")).grid(row=0, column=0, sticky="w")
        tk.Label(header, textvariable=self.path_status, bg="#eaf2f8", anchor="w").grid(row=0, column=1, sticky="ew", padx=12)
        self.state_label = tk.Label(header, textvariable=self.plugin_status, bg=self.COLORS["neutral"], fg="white", padx=10, pady=4)
        self.state_label.grid(row=0, column=2, sticky="e")

        body = ttk.Frame(self, padding=14)
        body.grid(row=1, column=0, sticky="ew")
        body.columnconfigure(1, weight=1)
        ttk.Label(body, text="VirtualDJ home:").grid(row=0, column=0, sticky="w")
        self.home_box = ttk.Combobox(body, textvariable=self.home)
        self.home_box.grid(row=0, column=1, sticky="ew", padx=8)
        ttk.Button(body, text="Find", command=self._detect).grid(row=0, column=2, padx=(0, 5))
        ttk.Button(body, text="Browse…", command=self._browse).grid(row=0, column=3)
        ttk.Label(body, text=(f"Package version: {APP_VERSION}" if self.layout else self.package_error), wraplength=720).grid(row=1, column=0, columnspan=4, sticky="w", pady=(10, 8))
        buttons = ttk.Frame(body)
        buttons.grid(row=2, column=0, columnspan=4, sticky="w")
        self.install_button = ttk.Button(buttons, text="Install / update", command=lambda: self._run("install"))
        self.install_button.pack(side="left")
        self.uninstall_button = ttk.Button(buttons, text="Uninstall", command=lambda: self._run("uninstall"))
        self.uninstall_button.pack(side="left", padx=8)
        self.restore_button = ttk.Button(buttons, text="Restore newest backup", command=lambda: self._run("restore"))
        self.restore_button.pack(side="left")
        self.window_button = ttk.Button(
            buttons, text="Reset video window layout", command=self._reset_video_window)
        self.window_button.pack(side="left", padx=(8, 0))
        ttk.Button(buttons, text="Check for updates", command=self._check_updates).pack(side="left", padx=8)

        activity = ttk.LabelFrame(self, text="Activity and last operation", padding=8)
        activity.grid(row=2, column=0, sticky="nsew", padx=14, pady=(0, 10))
        activity.columnconfigure(0, weight=1)
        activity.rowconfigure(1, weight=1)
        ttk.Label(activity, textvariable=self.last_operation, wraplength=720).grid(row=0, column=0, sticky="ew")
        self.log = tk.Text(activity, height=12, state="disabled", wrap="word")
        self.log.grid(row=1, column=0, sticky="nsew", pady=6)
        actions = ttk.Frame(activity)
        actions.grid(row=2, column=0, sticky="ew")
        self.backup_button = ttk.Button(actions, text="Open last backup", state="disabled", command=self._open_backup)
        self.backup_button.pack(side="left")
        ttk.Button(actions, text="Create diagnostic ZIP…", command=self._diagnostics).pack(side="right")

    def _set_state(self, level: str, text: str) -> None:
        self.plugin_status.set(text)
        self.state_label.configure(bg=self.COLORS[level])

    def _append(self, value: str) -> None:
        self.log.configure(state="normal")
        self.log.insert("end", value + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _detect(self) -> None:
        try:
            result = vdj_setup.query_virtualdj(self.layout, self.home.get()) if self.home.get().strip() else vdj_setup.query_virtualdj(self.layout)
            paths = [str(item["Path"]) for item in result.get("Candidates", []) if item.get("Path")]
            self.home_box.configure(values=paths)
            selected = result.get("Selected")
            if not selected:
                raise FileNotFoundError(result.get("Message") or "VirtualDJ folder was not found.")
            self.home.set(str(selected))
            self.path_status.set(str(selected))
            level, text = gui_common.plugin_state(Path(str(selected)), APP_VERSION, vdj_setup)
            self._set_state(level, text)
        except Exception as exc:
            self.path_status.set(str(exc))
            self._set_state("error", "VirtualDJ not found")

    def _browse(self) -> None:
        selected = filedialog.askdirectory(initialdir=self.home.get() or str(Path.home()))
        if selected:
            self.home.set(selected)
            self._detect()

    def _valid_home(self) -> Path | None:
        try:
            result = vdj_setup.query_virtualdj(self.layout, self.home.get())
            if not result.get("Valid"):
                raise ValueError(result.get("Message") or "Invalid VirtualDJ folder.")
            return Path(str(result["Selected"]))
        except Exception as exc:
            messagebox.showerror("VirtualDJ folder", str(exc))
            return None

    def _set_running(self, running: bool) -> None:
        self.running = running
        state = "disabled" if running or self.layout is None else "normal"
        for button in (self.install_button, self.uninstall_button, self.restore_button, self.window_button):
            button.configure(state=state)

    def _reset_video_window(self) -> None:
        home = self._valid_home()
        if home is None or self.layout is None:
            return
        try:
            running = vdj_setup.virtualdj_running(self.layout)
        except Exception as exc:
            messagebox.showerror("VirtualDJ status", str(exc))
            return
        close_requested = False
        if running:
            close_requested = messagebox.askyesno(
                "VirtualDJ is running",
                "Save any current work first. Ask VirtualDJ to close before resetting "
                "the external video window layout?",
            )
            if not close_requested:
                return
        if not messagebox.askyesno(
            "Reset video window layout",
            "Forget only the saved size and position of VirtualDJ's external video "
            "window? A settings backup will be created first.",
        ):
            return
        self._set_running(True)
        def worker() -> None:
            try:
                if close_requested and not vdj_setup.request_virtualdj_close(self.layout):
                    raise RuntimeError(
                        "VirtualDJ did not close within 15 seconds. Close it manually and try again."
                    )
                backup = vdj_setup.reset_video_window_layout(
                    home, self.layout, lambda line: self.events.put(("log", str(line))))
                self.events.put(("success", ("video window layout reset", backup)))
            except Exception as exc:
                self.events.put(("error", str(exc)))
            finally:
                self.events.put(("done", None))
        threading.Thread(target=worker, daemon=False, name="video-window-reset").start()

    def _run(self, action: str) -> None:
        home = self._valid_home()
        if home is None or self.layout is None:
            return
        try:
            running = vdj_setup.virtualdj_running(self.layout)
        except Exception as exc:
            messagebox.showerror("VirtualDJ status", str(exc))
            return
        close_requested = False
        if running:
            close_requested = messagebox.askyesno(
                "VirtualDJ is running",
                "VirtualDJ must be closed before plugin files can be changed.\n\n"
                "Save any current work first. Ask VirtualDJ to close now?",
            )
            if not close_requested:
                return
        if not messagebox.askyesno("Confirm setup", f"{action.title()} the plugin in:\n\n{home}"):
            return
        self._set_running(True)
        def worker() -> None:
            try:
                if close_requested and not vdj_setup.request_virtualdj_close(self.layout):
                    raise RuntimeError(
                        "VirtualDJ did not close within 15 seconds. Close it manually and try again."
                    )
                vdj_setup.run_action(self.layout, action, home, lambda line: self.events.put(("log", str(line))))
                backup = gui_common.newest_backup(home, "LRC Lyrics Backups")
                self.events.put(("success", (action, backup)))
            except Exception as exc:
                self.events.put(("error", str(exc)))
            finally:
                self.events.put(("done", None))
        threading.Thread(target=worker, daemon=False, name="plugin-setup-worker").start()

    def _poll(self) -> None:
        try:
            while True:
                kind, payload = self.events.get_nowait()
                if kind == "log": self._append(str(payload))
                elif kind == "error":
                    self._append(f"[ERROR] {payload}")
                    self.last_operation.set(f"Last operation failed: {payload}")
                    self._set_state("error", "Setup failed")
                elif kind == "update_error":
                    self._append(f"[ERROR] {payload}")
                    self.last_operation.set(str(payload))
                elif kind == "success":
                    action, self.last_backup = payload
                    self.last_operation.set(f"Last operation completed: {action}")
                    self.backup_button.configure(state="normal" if self.last_backup else "disabled")
                    self._detect()
                elif kind == "update":
                    release, latest, available = payload
                    if available:
                        if messagebox.askyesno(
                            "Update available",
                            f"LRC Plugin Setup {latest} is available.\n\n"
                            "Download, verify and launch it now?",
                        ):
                            self.after(0, lambda r=release, v=latest: self._download_update(r, v))
                    else:
                        messagebox.showinfo("No update", f"Version {APP_VERSION} is current.")
                elif kind == "update_ready":
                    self._append(f"Verified update launched: {payload}")
                    self.last_operation.set("The verified updated Plugin Setup was launched.")
                elif kind == "done": self._set_running(False)
        except queue.Empty:
            pass
        self.after(100, self._poll)

    def _open_backup(self) -> None:
        if self.last_backup: gui_common.open_path(self.last_backup)

    def _check_updates(self) -> None:
        if self.running:
            return
        self._set_running(True)
        self._append("Checking GitHub for a new Plugin Setup release...")
        def worker() -> None:
            try:
                release = gui_common.latest_release()
                latest = str(release.get("tag_name") or "").removeprefix("v")
                available = gui_common.version_tuple(latest) > gui_common.version_tuple(APP_VERSION)
                self.events.put(("update", (release, latest, available)))
            except Exception as exc:
                self.events.put(("update_error", f"Update check failed: {exc}"))
            finally:
                self.events.put(("done", None))
        threading.Thread(target=worker, daemon=False, name="plugin-update-check").start()

    def _download_update(self, release: dict, latest: str) -> None:
        self._set_running(True)
        self._append(f"Downloading and verifying LRC Plugin Setup {latest}...")
        def worker() -> None:
            try:
                package = gui_common.download_update(release, "LRC-Plugin-Setup")
                target = gui_common.launch_updated_app(package, "LRCPluginSetup")
                self.events.put(("update_ready", str(target)))
            except Exception as exc:
                self.events.put(("update_error", f"Update failed: {exc}"))
            finally:
                self.events.put(("done", None))
        threading.Thread(target=worker, daemon=False, name="plugin-update-download").start()

    def _diagnostics(self) -> None:
        path = filedialog.asksaveasfilename(defaultextension=".zip", initialfile=f"LRC-Plugin-Setup-diagnostics-{APP_VERSION}.zip", filetypes=[("ZIP archive", "*.zip")])
        if path:
            content = self.log.get("1.0", "end")
            result = gui_common.create_diagnostic_zip(Path(path), app_name="LRC Plugin Setup", version=APP_VERSION, status=self.plugin_status.get(), recent_log=content)
            messagebox.showinfo("Diagnostics created", f"Created:\n{result}\n\nNo music, tags, credentials, or absolute user paths were included.")


if __name__ == "__main__":
    App().mainloop()
