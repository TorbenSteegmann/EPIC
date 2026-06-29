"""Tkinter GUI for running experiment presets and viewing diagnostics.

Save/run model (important):
  * Loading a preset fills the form as a *template*; the original file is not
    touched again unless you explicitly "Save As Preset".
  * The form's live state is written to a single working file, ``current.json``,
    and the GUI starts on that file so you resume where you left off.
  * "Save" writes the form to ``current.json``. "Run" / "Run + Render" write the
    form to ``current.json`` and run *that* (so the run uses what is selected).
  * "Save As Preset" is the only action that writes/overwrites a named preset.

Particle export is implicit: a single "Run" button renders (exporting particles
then deleting the raw dumps) when the preset's render settings produce an MP4 or
snapshots, and otherwise runs diagnostics only.
"""

from __future__ import annotations

import json
import math
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path
from tkinter import BooleanVar, Listbox, Misc, StringVar, Tk, filedialog, messagebox, ttk
from tkinter.scrolledtext import ScrolledText

import paths
import timing
from config_schema import METHODS, SCENES, VALID_RENDER_COLORS, render_produces_output
from diagnostics_viewer import DiagnosticsViewer
from orchestrator import OrchestratorError, estimate_runtime, load_preset

_PROGRESS_RE = re.compile(r"\[(\d+)/(\d+)\]")
_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)  # no flashing console on Windows


class PresetEditor:
    def __init__(self, root: Misc) -> None:
        self.root = root
        self.root.winfo_toplevel().title("Experiment Orchestrator")
        self.template_path: Path | None = None
        self.raw: dict = {}
        self.proc: subprocess.Popen | None = None
        self.cancelled = False
        self.current_output_dir: Path | None = None
        self.queue: list[dict] = []
        self._temp_files: list[Path] = []

        self.vars: dict[str, StringVar] = {}
        self.flags: dict[str, BooleanVar] = {}
        self.method_vars: dict[str, BooleanVar] = {}
        self.status = StringVar(value="idle")
        self.eta = StringVar(value="")
        self.parallel_var = StringVar(value=str(max(1, (os.cpu_count() or 2) // 2)))
        self.template_label = StringVar(value="template: (none)  -  edits write to current.json")
        self._eta_active = False
        self._eta_after_id: str | None = None
        self._item_eta_deadline: float | None = None
        self._queue_eta_deadline: float | None = None

        self._build_form()
        self._build_queue()
        self._build_buttons()
        self._build_progress()
        self._build_log()
        self._start_on_current()


    def _build_form(self) -> None:
        frame = ttk.LabelFrame(self.root, text="Preset")
        frame.grid(row=0, column=0, padx=8, pady=8, sticky="nsew")

        def add_entry(row: int, label: str, key: str) -> None:
            ttk.Label(frame, text=label).grid(row=row, column=0, sticky="w", padx=4, pady=2)
            var = StringVar()
            ttk.Entry(frame, textvariable=var, width=28).grid(row=row, column=1, sticky="w", padx=4)
            self.vars[key] = var

        add_entry(0, "name", "name")
        ttk.Label(frame, text="scene").grid(row=1, column=0, sticky="w", padx=4, pady=2)
        self.vars["scene"] = StringVar()
        ttk.Combobox(frame, textvariable=self.vars["scene"], values=list(SCENES), width=26,
                     state="readonly").grid(row=1, column=1, sticky="w", padx=4)
        ttk.Label(frame, text="dim").grid(row=2, column=0, sticky="w", padx=4, pady=2)
        self.vars["dim"] = StringVar(value="2")
        ttk.Combobox(frame, textvariable=self.vars["dim"], values=["2", "3"], width=26,
                     state="readonly").grid(row=2, column=1, sticky="w", padx=4)
        add_entry(3, "domain (cells)", "domain")
        add_entry(4, "dx", "dx")
        add_entry(5, "dt", "dt")
        add_entry(6, "duration (s)", "duration")
        add_entry(7, "steps (overrides duration)", "steps")
        add_entry(8, "jitter_seed", "jitter_seed")
        add_entry(9, "particle_fps", "particle_fps")

        self.flags["adaptive"] = BooleanVar(value=False)
        self.flags["diagnostics"] = BooleanVar(value=True)
        ttk.Checkbutton(frame, text="adaptive timestep", variable=self.flags["adaptive"]).grid(
            row=10, column=0, columnspan=2, sticky="w", padx=4)
        ttk.Checkbutton(frame, text="diagnostics export", variable=self.flags["diagnostics"]).grid(
            row=11, column=0, columnspan=2, sticky="w", padx=4)

        methods_frame = ttk.LabelFrame(self.root, text="Transfer methods")
        methods_frame.grid(row=0, column=1, padx=8, pady=8, sticky="nsew")
        for i, key in enumerate(METHODS):
            var = BooleanVar(value=False)
            ttk.Checkbutton(methods_frame, text=METHODS[key]["label"], variable=var).grid(
                row=i, column=0, sticky="w", padx=4, pady=1)
            self.method_vars[key] = var

        ttk.Label(methods_frame, text="parallel simulation/render jobs (cores)").grid(
            row=len(METHODS), column=0, sticky="w", padx=4, pady=(8, 1))
        ttk.Spinbox(methods_frame, from_=1, to=max(1, os.cpu_count() or 2), width=6,
                    textvariable=self.parallel_var).grid(
            row=len(METHODS) + 1, column=0, sticky="w", padx=4, pady=(0, 2))

        render_frame = ttk.LabelFrame(self.root, text="Renderer (used by Run + Render)")
        render_frame.grid(row=1, column=1, padx=8, pady=8, sticky="nsew")
        ttk.Label(render_frame, text="color").grid(row=0, column=0, sticky="w", padx=4, pady=2)
        self.vars["render_color"] = StringVar(value="speed")
        ttk.Combobox(render_frame, textvariable=self.vars["render_color"],
                     values=list(VALID_RENDER_COLORS), width=14, state="readonly").grid(
            row=0, column=1, sticky="w", padx=4)
        ttk.Label(render_frame, text="width").grid(row=1, column=0, sticky="w", padx=4)
        self.vars["render_width"] = StringVar(value="960")
        ttk.Entry(render_frame, textvariable=self.vars["render_width"], width=14).grid(row=1, column=1, sticky="w", padx=4)
        ttk.Label(render_frame, text="height").grid(row=2, column=0, sticky="w", padx=4)
        self.vars["render_height"] = StringVar(value="960")
        ttk.Entry(render_frame, textvariable=self.vars["render_height"], width=14).grid(row=2, column=1, sticky="w", padx=4)
        ttk.Label(render_frame, text="snapshot every (s)").grid(row=3, column=0, sticky="w", padx=4)
        self.vars["render_snapshot"] = StringVar(value="")
        ttk.Entry(render_frame, textvariable=self.vars["render_snapshot"], width=14).grid(row=3, column=1, sticky="w", padx=4)
        self.flags["render_video"] = BooleanVar(value=True)
        ttk.Checkbutton(render_frame, text="write MP4 (needs ffmpeg)", variable=self.flags["render_video"]).grid(
            row=4, column=0, columnspan=2, sticky="w", padx=4)

    def _build_buttons(self) -> None:
        bar = ttk.Frame(self.root)
        bar.grid(row=2, column=0, columnspan=2, padx=8, pady=4, sticky="w")
        ttk.Button(bar, text="Load Preset", command=self.load_preset).grid(row=0, column=0, padx=3)
        ttk.Button(bar, text="Save (current)", command=self.save_current).grid(row=0, column=1, padx=3)
        ttk.Button(bar, text="Save As Preset...", command=self.save_as_preset).grid(row=0, column=2, padx=3)
        self.run_btn = ttk.Button(bar, text="Run", command=self.run)
        self.run_btn.grid(row=0, column=3, padx=3)
        self.cancel_btn = ttk.Button(bar, text="Cancel", command=self.cancel, state="disabled")
        self.cancel_btn.grid(row=0, column=4, padx=3)
        ttk.Label(self.root, textvariable=self.template_label).grid(
            row=3, column=0, columnspan=2, sticky="w", padx=10)

    def _set_running(self, running: bool) -> None:
        self.run_btn.config(state="disabled" if running else "normal")
        self.cancel_btn.config(state="normal" if running else "disabled")

    def _build_progress(self) -> None:
        frame = ttk.Frame(self.root)
        frame.grid(row=4, column=0, columnspan=2, padx=8, pady=2, sticky="we")
        self.progress = ttk.Progressbar(frame, mode="indeterminate", length=420)
        self.progress.grid(row=0, column=0, padx=4)
        ttk.Label(frame, textvariable=self.eta).grid(row=0, column=1, sticky="w", padx=(8, 4))
        ttk.Label(frame, textvariable=self.status).grid(row=0, column=2, sticky="w", padx=4)

    @staticmethod
    def _format_countdown(seconds: float) -> str:
        total = max(0, int(math.ceil(seconds)))
        hours, remainder = divmod(total, 3600)
        minutes, secs = divmod(remainder, 60)
        if hours:
            return f"{hours}:{minutes:02d}:{secs:02d}"
        return f"{minutes}:{secs:02d}"

    def _estimate_queue_items(self, specs: list[tuple[str, Path]], parallel: str) -> list[float | None]:
        """Estimate each sequential queue item; methods inside an item run in parallel."""

        max_parallel = int(parallel) if parallel.isdigit() and int(parallel) >= 1 else None
        store = timing.load()
        estimates: list[float | None] = []
        for _, path in specs:
            try:
                preset = load_preset(path)
                will_render = render_produces_output(preset.render)
                if will_render:
                    preset.export_particles = True
                estimate = estimate_runtime(preset, will_render, store, max_parallel)
                estimates.append(estimate["total_seconds"])
            except (OSError, ValueError, OrchestratorError):
                estimates.append(None)
        return estimates

    def _begin_item_eta(self, item_index: int, estimates: list[float | None]) -> None:
        if self._eta_after_id is not None:
            try:
                self.root.after_cancel(self._eta_after_id)
            except Exception:
                pass
            self._eta_after_id = None
        now = time.monotonic()
        item_seconds = estimates[item_index]
        remaining = estimates[item_index:]
        queue_seconds = sum(remaining) if all(value is not None for value in remaining) else None
        self._item_eta_deadline = now + item_seconds if item_seconds is not None else None
        self._queue_eta_deadline = now + queue_seconds if queue_seconds is not None else None
        self._tick_eta()

    def _deadline_text(self, deadline: float | None, now: float) -> str:
        if deadline is None:
            return "unknown"
        remaining = deadline - now
        if remaining >= 0.0:
            return self._format_countdown(remaining)
        return f"over {self._format_countdown(-remaining)}"

    def _tick_eta(self) -> None:
        if not self._eta_active:
            return
        now = time.monotonic()
        item_text = self._deadline_text(self._item_eta_deadline, now)
        queue_text = self._deadline_text(self._queue_eta_deadline, now)
        self.eta.set(f"ETA {item_text} (queue {queue_text})")
        self._eta_after_id = self.root.after(1000, self._tick_eta)

    def _finish_eta(self, completed: bool) -> None:
        self._eta_active = False
        if self._eta_after_id is not None:
            try:
                self.root.after_cancel(self._eta_after_id)
            except Exception:
                pass
            self._eta_after_id = None
        self._item_eta_deadline = None
        self._queue_eta_deadline = None
        self.eta.set("ETA 0:00 (queue 0:00)" if completed else "")

    def _build_log(self) -> None:
        self.log = ScrolledText(self.root, height=14, width=104)
        self.log.grid(row=5, column=0, columnspan=2, padx=8, pady=8, sticky="nsew")

    def _build_queue(self) -> None:
        frame = ttk.LabelFrame(self.root, text="Queue (runs top to bottom)")
        frame.grid(row=1, column=0, padx=8, pady=8, sticky="nsew")
        self.queue_list = Listbox(frame, height=7, width=38)
        self.queue_list.grid(row=0, column=0, columnspan=3, sticky="nsew", padx=4, pady=2)
        scrollbar = ttk.Scrollbar(frame, orient="vertical", command=self.queue_list.yview)
        scrollbar.grid(row=0, column=3, sticky="ns")
        self.queue_list.config(yscrollcommand=scrollbar.set)
        ttk.Button(frame, text="Add current", command=self.add_to_queue).grid(row=1, column=0, padx=2, pady=2)
        ttk.Button(frame, text="Remove", command=self.remove_from_queue).grid(row=1, column=1, padx=2)
        ttk.Button(frame, text="Clear", command=self.clear_queue).grid(row=1, column=2, padx=2)

    def add_to_queue(self) -> None:
        try:
            data = self._collect_from_widgets()
        except ValueError as exc:
            messagebox.showerror("Invalid field", f"Could not parse a numeric field: {exc}")
            return
        self.queue.append(data)
        self._refresh_queue()
        self._print(f"queued {data.get('name', 'item')} [{data.get('scene', '?')}]")

    def remove_from_queue(self) -> None:
        selection = self.queue_list.curselection()
        if selection:
            del self.queue[selection[0]]
            self._refresh_queue()

    def clear_queue(self) -> None:
        self.queue.clear()
        self._refresh_queue()

    def _refresh_queue(self) -> None:
        self.queue_list.delete(0, "end")
        for index, item in enumerate(self.queue, 1):
            self.queue_list.insert("end", f"{index}. {item.get('name', 'item')}  [{item.get('scene', '?')}]")

    def _start_on_current(self) -> None:
        """Resume on current.json if it exists, else sensible defaults named 'current'."""
        if paths.CURRENT_JSON.is_file():
            try:
                self._apply_to_widgets(json.loads(paths.CURRENT_JSON.read_text(encoding="utf-8")))
                self._print(f"resumed {paths.CURRENT_JSON}")
                return
            except (OSError, ValueError):
                pass
        self._load_defaults()

    def _load_defaults(self) -> None:
        defaults = {
            "name": "current", "scene": "settling_pool", "domain": "64",
            "dx": "1.0", "dt": "0.1", "duration": "120", "steps": "",
            "jitter_seed": "0", "particle_fps": "30",
            "render_color": "speed", "render_width": "960", "render_height": "960",
            "render_snapshot": "",
        }
        for key, value in defaults.items():
            self.vars[key].set(value)
        self.vars["dim"].set("2")
        for key in ("PIC", "FLIP_095", "APIC", "POLYPIC"):
            self.method_vars[key].set(True)

    def _apply_to_widgets(self, data: dict) -> None:
        self.raw = dict(data)
        self.vars["name"].set(str(data.get("name", "current")))
        self.vars["scene"].set(str(data.get("scene", "settling_pool")))
        self.vars["dim"].set(str(data.get("dim", 2)))
        self.vars["domain"].set(str(data.get("domain", 64)))
        self.vars["dx"].set(str(data.get("dx", 1.0)))
        self.vars["dt"].set(str(data.get("dt", 0.1)))
        self.vars["duration"].set(str(data.get("duration", "")))
        self.vars["steps"].set(str(data.get("steps", "")))
        self.vars["jitter_seed"].set(str(data.get("jitter_seed", 0)))
        self.vars["particle_fps"].set(str(data.get("particle_fps", 30)))
        self.flags["adaptive"].set(bool(data.get("adaptive", False)))
        self.flags["diagnostics"].set(bool(data.get("diagnostics", True)))
        selected = set(data.get("methods", []))
        for key, var in self.method_vars.items():
            var.set(key in selected)
        render = data.get("render", {})
        self.vars["render_color"].set(str(render.get("color", "speed")))
        self.vars["render_width"].set(str(render.get("width", 960)))
        self.vars["render_height"].set(str(render.get("height", 960)))
        self.vars["render_snapshot"].set(str(render.get("snapshot_interval", "")))
        self.flags["render_video"].set(bool(render.get("video", True)))

    def _collect_from_widgets(self) -> dict:
        data = dict(self.raw)  # keep unexposed fields (e.g. _description, analysis)
        data["name"] = self.vars["name"].get().strip() or "current"
        data["scene"] = self.vars["scene"].get().strip()
        data["dim"] = int(self.vars["dim"].get())
        data["domain"] = int(self.vars["domain"].get())
        data["dx"] = float(self.vars["dx"].get())
        data["dt"] = float(self.vars["dt"].get())
        data["jitter_seed"] = int(self.vars["jitter_seed"].get())
        data["particle_fps"] = float(self.vars["particle_fps"].get())
        data["adaptive"] = self.flags["adaptive"].get()
        data["diagnostics"] = self.flags["diagnostics"].get()
        # export_particles is implicit (Run + Render forces it); not a form field.
        data.pop("export_particles", None)

        steps = self.vars["steps"].get().strip()
        duration = self.vars["duration"].get().strip()
        data.pop("steps", None)
        data.pop("duration", None)
        if steps:
            data["steps"] = int(steps)
        elif duration:
            data["duration"] = float(duration)

        data["methods"] = [key for key, var in self.method_vars.items() if var.get()]

        render = dict(data.get("render", {}))
        render["color"] = self.vars["render_color"].get()
        render["width"] = int(self.vars["render_width"].get())
        render["height"] = int(self.vars["render_height"].get())
        render["video"] = self.flags["render_video"].get()
        snapshot = self.vars["render_snapshot"].get().strip()
        if snapshot:
            render["snapshot_interval"] = float(snapshot)
        else:
            render.pop("snapshot_interval", None)
        data["render"] = render
        return data


    def _print(self, message: str) -> None:
        self.log.insert("end", message + "\n")
        self.log.see("end")

    def load_preset(self) -> None:
        path = filedialog.askopenfilename(initialdir=str(paths.PRESETS_DIR), filetypes=[("JSON", "*.json")])
        if not path:
            return
        try:
            data = json.loads(Path(path).read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            messagebox.showerror("Load failed", str(exc))
            return
        self.template_path = Path(path)
        self._apply_to_widgets(data)
        self.template_label.set(f"template: {self.template_path}  -  edits write to current.json")
        self._print(f"loaded template {path}")

    def _write_current(self) -> bool:
        try:
            data = self._collect_from_widgets()
        except ValueError as exc:
            messagebox.showerror("Invalid field", f"Could not parse a numeric field: {exc}")
            return False
        paths.CURRENT_JSON.write_text(json.dumps(data, indent=2), encoding="utf-8")
        self.raw = data
        return True

    def save_current(self) -> None:
        if self._write_current():
            self._print(f"saved current -> {paths.CURRENT_JSON}")

    def save_as_preset(self) -> None:
        path = filedialog.asksaveasfilename(
            initialdir=str(paths.PRESETS_DIR), defaultextension=".json",
            filetypes=[("JSON", "*.json")], title="Save As Preset (explicit overwrite)")
        if not path:
            return
        try:
            data = self._collect_from_widgets()
        except ValueError as exc:
            messagebox.showerror("Invalid field", f"Could not parse a numeric field: {exc}")
            return
        Path(path).write_text(json.dumps(data, indent=2), encoding="utf-8")
        self.template_path = Path(path)
        self.template_label.set(f"template: {path}  -  edits write to current.json")
        self._print(f"saved preset -> {path}")

    def run(self) -> None:
        if self.proc is not None:
            return 

        self._temp_files = []
        if self.queue:
            specs = []
            for index, item in enumerate(self.queue, 1):
                path = paths.ORCHESTRATOR_DIR / f".queue_{index:02d}.json"
                path.write_text(json.dumps(item, indent=2), encoding="utf-8")
                self._temp_files.append(path)
                specs.append((item.get("name", f"item{index}"), path))
        else:
            if not self._write_current():
                return
            name = json.loads(paths.CURRENT_JSON.read_text(encoding="utf-8")).get("name", "current")
            specs = [(name, paths.CURRENT_JSON)]

        self.cancelled = False
        self.current_output_dir = None
        self.status.set("starting...")
        self.progress.start(15)
        self._set_running(True)
        parallel = self.parallel_var.get().strip()
        estimates = self._estimate_queue_items(specs, parallel)
        self._eta_active = True
        threading.Thread(target=self._run_queue, args=(specs, parallel, estimates), daemon=True).start()

    def cancel(self) -> None:
        proc = self.proc
        if proc is None or proc.poll() is not None:
            return
        self.cancelled = True
        self.status.set("cancelling...")
        self._print("cancelling run (terminating headless and renderer)...")
        try:
            if os.name == "nt":
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                               capture_output=True, creationflags=_NO_WINDOW)
            else:
                proc.terminate()
        except OSError as exc:
            self._print(f"cancel failed: {exc}")

    def _run_queue(
        self,
        specs: list[tuple[str, Path]],
        parallel: str = "",
        estimates: list[float | None] | None = None,
    ) -> None:
        returncode = 0
        estimates = estimates or [None] * len(specs)
        try:
            for index, (name, path) in enumerate(specs, 1):
                if self.cancelled:
                    break
                self.current_output_dir = None  
                self.root.after(0, self._begin_item_eta, index - 1, estimates)
                self.root.after(0, self._print, f"=== [{index}/{len(specs)}] {name} ===")
                command = [sys.executable, "-u", str(paths.RUN_EXPERIMENT), str(path), "--render"]
                if parallel.isdigit() and int(parallel) >= 1:
                    command += ["--parallel", parallel]
                try:
                    process = subprocess.Popen(
                        command, cwd=str(paths.ORCHESTRATOR_DIR),
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                        creationflags=_NO_WINDOW)
                except OSError as exc:
                    self.root.after(0, self._print, f"failed to start: {exc}")
                    returncode = -1
                    break
                self.proc = process
                assert process.stdout is not None
                for line in process.stdout:
                    self.root.after(0, self._on_line, line.rstrip())
                process.wait()
                self.proc = None
                returncode = process.returncode
                if self.cancelled:
                    break 
                if returncode != 0:
                    self.root.after(0, self._print, f"{name} failed (exit {returncode}); stopping queue.")
                    break
                self.current_output_dir = None  
        finally:
            for temp in self._temp_files:
                try:
                    temp.unlink()
                except OSError:
                    pass
            self._temp_files = []
        self.root.after(0, self._on_queue_done, returncode)

    def _on_line(self, line: str) -> None:
        self._print(line)
        if not line:
            return
        match = _PROGRESS_RE.search(line)
        if match:
            self.status.set(f"method {match.group(1)}/{match.group(2)}")
        elif line.startswith("render:"):
            self.status.set("rendering...")
        elif line.startswith("cleanup:"):
            self.status.set("cleaning up...")
        elif line.startswith("output: "):
            self.current_output_dir = Path(line[len("output: "):].strip())
        elif line.startswith("Outputs:"):
            self.status.set("finishing...")

    def _on_queue_done(self, returncode: int) -> None:
        self.progress.stop()
        self._set_running(False)
        completed = not self.cancelled and returncode == 0
        self._finish_eta(completed)
        if self.cancelled:
            self._print("[cancelled]")
            self._delete_cancelled_output() 
            self.queue.clear()
            self._refresh_queue()
            self.status.set("cancelled (queue cleared)")
        elif returncode != 0:
            self.status.set(f"stopped (exit {returncode})")
        else:
            self._print("[done]")
            if self.queue:
                self.queue.clear()
                self._refresh_queue()
            self.status.set("done")
        self.cancelled = False

    def _delete_cancelled_output(self) -> None:
        """Remove the aborted run's output directory (only if it lives under outputs/)."""
        out = self.current_output_dir
        if out is None:
            return
        try:
            out = out.resolve()
            if paths.OUTPUTS_DIR.resolve() in out.parents and out.exists():
                shutil.rmtree(out)
                self._print(f"deleted cancelled run output: {out}")
        except OSError as exc:
            self._print(f"could not delete output {out}: {exc}")


def main() -> int:
    root = Tk()
    root.title("Experiment Orchestrator")
    root.geometry("1280x820")
    root.minsize(1020, 700)

    notebook = ttk.Notebook(root)
    notebook.pack(fill="both", expand=True)
    runner_tab = ttk.Frame(notebook)
    runner_tab.columnconfigure(0, weight=1)
    runner_tab.columnconfigure(1, weight=1)
    runner_tab.rowconfigure(5, weight=1)
    viewer_tab = DiagnosticsViewer(notebook)
    notebook.add(runner_tab, text="Orchestrator / Runner")
    notebook.add(viewer_tab, text="Diagnostics Viewer")
    PresetEditor(runner_tab)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
