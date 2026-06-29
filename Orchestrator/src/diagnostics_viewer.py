"""Tkinter tab for browsing, plotting, and exporting experiment diagnostics."""

from __future__ import annotations

import math
import re
from pathlib import Path
from tkinter import BooleanVar, StringVar, filedialog, messagebox, ttk

from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
from matplotlib.figure import Figure

import paths
from diagnostics_derived import dropdown_label, is_derived_series
from diagnostics_loader import DiagnosticsLoadError, ResultDiagnostics, discover_result_folders, load_result_folder
from diagnostics_plotter import PlotBuildError, draw_diagnostics, infer_scale, readable_label, save_figure


class DiagnosticsViewer(ttk.Frame):
    """Self-contained diagnostics viewer suitable for a ``ttk.Notebook`` tab."""

    EXPORT_FORMATS = ("svg", "png", "pdf")
    AXIS_SCALES = ("linear", "log", "symlog")

    def __init__(self, parent) -> None:
        super().__init__(parent, padding=8)
        self.result: ResultDiagnostics | None = None
        self.result_choices: dict[str, Path] = {}
        self.mode_vars: dict[str, BooleanVar] = {}
        self.series_display_to_key: dict[str, str] = {}
        self.series_key_to_display: dict[str, str] = {}
        self.limits_are_auto = True
        self.has_plot = False

        self.result_var = StringVar()
        self.path_var = StringVar(value="No result loaded")
        self.x_series_var = StringVar()
        self.y_series_var = StringVar()
        self.x_label_var = StringVar()
        self.y_label_var = StringVar()
        self.x_scale_var = StringVar(value="linear")
        self.y_scale_var = StringVar(value="linear")
        self.x_min_var = StringVar()
        self.x_max_var = StringVar()
        self.y_min_var = StringVar()
        self.y_max_var = StringVar()
        self.title_var = StringVar()
        self.export_format_var = StringVar(value="svg")
        self.status_var = StringVar(value="Discovering experiment results...")

        self._build_layout()
        self.after_idle(self.refresh_results)

    def _build_layout(self) -> None:
        self.columnconfigure(1, weight=1)
        self.rowconfigure(1, weight=1)

        source = ttk.LabelFrame(self, text="Experiment result")
        source.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 8))
        source.columnconfigure(1, weight=1)
        ttk.Label(source, text="Result").grid(row=0, column=0, padx=5, pady=5, sticky="w")
        self.result_combo = ttk.Combobox(source, textvariable=self.result_var, state="readonly", width=55)
        self.result_combo.grid(row=0, column=1, padx=5, pady=5, sticky="ew")
        self.result_combo.bind("<<ComboboxSelected>>", self._on_result_selected)
        ttk.Button(source, text="Refresh", command=self.refresh_results).grid(row=0, column=2, padx=3, pady=5)
        ttk.Button(source, text="Browse...", command=self.browse_result).grid(row=0, column=3, padx=5, pady=5)
        ttk.Label(source, textvariable=self.path_var).grid(
            row=1, column=0, columnspan=4, padx=5, pady=(0, 5), sticky="w"
        )

        controls = ttk.Frame(self)
        controls.grid(row=1, column=0, sticky="nsw", padx=(0, 8))

        series = ttk.LabelFrame(controls, text="Diagnostic series")
        series.grid(row=0, column=0, sticky="ew", pady=(0, 6))
        ttk.Label(series, text="X series").grid(row=0, column=0, padx=4, pady=3, sticky="w")
        self.x_series_combo = ttk.Combobox(series, textvariable=self.x_series_var, state="readonly", width=39)
        self.x_series_combo.grid(row=1, column=0, padx=4, pady=(0, 4), sticky="ew")
        ttk.Label(series, text="Y series").grid(row=2, column=0, padx=4, pady=3, sticky="w")
        self.y_series_combo = ttk.Combobox(series, textvariable=self.y_series_var, state="readonly", width=39)
        self.y_series_combo.grid(row=3, column=0, padx=4, pady=(0, 5), sticky="ew")
        ttk.Label(
            series,
            text="Calculated entries are generated in memory; diagnostics files stay unchanged.",
            wraplength=285,
        ).grid(row=4, column=0, padx=4, pady=(0, 5), sticky="w")
        self.x_series_combo.bind("<<ComboboxSelected>>", self._on_series_changed)
        self.y_series_combo.bind("<<ComboboxSelected>>", self._on_series_changed)

        axes = ttk.LabelFrame(controls, text="Axes")
        axes.grid(row=1, column=0, sticky="ew", pady=(0, 6))
        ttk.Label(axes, text="X label").grid(row=0, column=0, padx=4, pady=2, sticky="w")
        ttk.Entry(axes, textvariable=self.x_label_var, width=27).grid(row=0, column=1, columnspan=3, padx=4, pady=2, sticky="ew")
        ttk.Label(axes, text="Y label").grid(row=1, column=0, padx=4, pady=2, sticky="w")
        ttk.Entry(axes, textvariable=self.y_label_var, width=27).grid(row=1, column=1, columnspan=3, padx=4, pady=2, sticky="ew")
        ttk.Label(axes, text="X scale").grid(row=2, column=0, padx=4, pady=2, sticky="w")
        x_scale = ttk.Combobox(axes, textvariable=self.x_scale_var, values=self.AXIS_SCALES, state="readonly", width=9)
        x_scale.grid(row=2, column=1, padx=4, pady=2, sticky="w")
        ttk.Label(axes, text="Y scale").grid(row=2, column=2, padx=4, pady=2, sticky="w")
        y_scale = ttk.Combobox(axes, textvariable=self.y_scale_var, values=self.AXIS_SCALES, state="readonly", width=9)
        y_scale.grid(row=2, column=3, padx=4, pady=2, sticky="w")
        x_scale.bind("<<ComboboxSelected>>", self._on_scale_changed)
        y_scale.bind("<<ComboboxSelected>>", self._on_scale_changed)

        ttk.Label(axes, text="Limits").grid(row=3, column=0, padx=4, pady=(6, 2), sticky="w")
        ttk.Label(axes, text="minimum").grid(row=3, column=1, padx=4, pady=(6, 2))
        ttk.Label(axes, text="maximum").grid(row=3, column=2, padx=4, pady=(6, 2))
        ttk.Label(axes, text="X").grid(row=4, column=0, padx=4, pady=2, sticky="w")
        x_min = ttk.Entry(axes, textvariable=self.x_min_var, width=12)
        x_max = ttk.Entry(axes, textvariable=self.x_max_var, width=12)
        x_min.grid(row=4, column=1, padx=4, pady=2)
        x_max.grid(row=4, column=2, padx=4, pady=2)
        ttk.Label(axes, text="Y").grid(row=5, column=0, padx=4, pady=2, sticky="w")
        y_min = ttk.Entry(axes, textvariable=self.y_min_var, width=12)
        y_max = ttk.Entry(axes, textvariable=self.y_max_var, width=12)
        y_min.grid(row=5, column=1, padx=4, pady=2)
        y_max.grid(row=5, column=2, padx=4, pady=2)
        for entry in (x_min, x_max, y_min, y_max):
            entry.bind("<KeyRelease>", self._on_limit_edited)
        ttk.Button(axes, text="Reset / Autoscale", command=self.reset_autoscale).grid(
            row=6, column=0, columnspan=4, padx=4, pady=5, sticky="ew"
        )

        modes = ttk.LabelFrame(controls, text="Transfer modes")
        modes.grid(row=2, column=0, sticky="ew", pady=(0, 6))
        self.modes_frame = ttk.Frame(modes)
        self.modes_frame.grid(row=0, column=0, padx=4, pady=3, sticky="ew")
        mode_buttons = ttk.Frame(modes)
        mode_buttons.grid(row=1, column=0, padx=2, pady=(0, 3), sticky="w")
        ttk.Button(mode_buttons, text="All", command=lambda: self._set_all_modes(True)).grid(row=0, column=0, padx=2)
        ttk.Button(mode_buttons, text="None", command=lambda: self._set_all_modes(False)).grid(row=0, column=1, padx=2)

        output = ttk.LabelFrame(controls, text="Plot and export")
        output.grid(row=3, column=0, sticky="ew")
        ttk.Label(output, text="Optional title").grid(row=0, column=0, columnspan=2, padx=4, pady=2, sticky="w")
        ttk.Entry(output, textvariable=self.title_var, width=31).grid(
            row=1, column=0, columnspan=2, padx=4, pady=(0, 4), sticky="ew"
        )
        ttk.Button(output, text="Generate / Update Plot", command=self.generate_plot).grid(
            row=2, column=0, columnspan=2, padx=4, pady=4, sticky="ew"
        )
        ttk.Label(output, text="Format").grid(row=3, column=0, padx=4, pady=4, sticky="w")
        ttk.Combobox(
            output, textvariable=self.export_format_var, values=self.EXPORT_FORMATS, state="readonly", width=8
        ).grid(row=3, column=1, padx=4, pady=4, sticky="e")
        ttk.Button(output, text="Save Plot...", command=self.save_plot).grid(
            row=4, column=0, columnspan=2, padx=4, pady=(2, 5), sticky="ew"
        )

        plot_frame = ttk.Frame(self)
        plot_frame.grid(row=1, column=1, sticky="nsew")
        self.figure = Figure(figsize=(8.5, 6.2), dpi=100, constrained_layout=True)
        axis = self.figure.add_subplot(111)
        axis.text(0.5, 0.5, "Select an experiment result to begin", ha="center", va="center", transform=axis.transAxes)
        axis.set_axis_off()
        self.canvas = FigureCanvasTkAgg(self.figure, master=plot_frame)
        self.canvas.draw()
        self.canvas.get_tk_widget().pack(fill="both", expand=True)
        toolbar = NavigationToolbar2Tk(self.canvas, plot_frame, pack_toolbar=False)
        toolbar.update()
        toolbar.pack(fill="x")

        ttk.Label(self, textvariable=self.status_var, wraplength=1050).grid(
            row=2, column=0, columnspan=2, sticky="ew", pady=(7, 0)
        )

    def refresh_results(self) -> None:
        current = self.result.path if self.result else None
        folders = discover_result_folders(paths.OUTPUTS_DIR)
        self.result_choices.clear()
        for folder in folders:
            try:
                display = str(folder.relative_to(paths.OUTPUTS_DIR))
            except ValueError:
                display = str(folder)
            self.result_choices[display] = folder
        self.result_combo.configure(values=tuple(self.result_choices))
        if not folders:
            self.status_var.set(f"No experiment results with diagnostics found under {paths.OUTPUTS_DIR}")
            return
        selected = next((name for name, folder in self.result_choices.items() if folder == current), None)
        selected = selected or next(iter(self.result_choices))
        self.result_var.set(selected)
        self.load_result(self.result_choices[selected])

    def browse_result(self) -> None:
        selected = filedialog.askdirectory(
            title="Select experiment result folder", initialdir=str(paths.OUTPUTS_DIR)
        )
        if selected:
            self.load_result(Path(selected))

    def _on_result_selected(self, _event=None) -> None:
        selected = self.result_choices.get(self.result_var.get())
        if selected:
            self.load_result(selected)

    def load_result(self, result_path: Path) -> None:
        try:
            result = load_result_folder(result_path)
        except DiagnosticsLoadError as exc:
            self.status_var.set(str(exc))
            messagebox.showerror("Could not load diagnostics", str(exc), parent=self.winfo_toplevel())
            return

        self.result = result
        self.path_var.set(str(result.path))
        self._populate_modes()
        series = result.available_series
        display_order = (
            *(name for name in ("time", "step") if name in series),
            *(name for name in series if is_derived_series(name)),
            *(name for name in series if name not in {"time", "step"} and not is_derived_series(name)),
        )
        self.series_display_to_key = {dropdown_label(name): name for name in display_order}
        self.series_key_to_display = {name: display for display, name in self.series_display_to_key.items()}
        display_series = tuple(self.series_display_to_key)
        self.x_series_combo.configure(values=display_series)
        self.y_series_combo.configure(values=display_series)
        x_default = "time" if "time" in series else ("step" if "step" in series else series[0])
        y_default = next(
            (name for name in ("total_energy", "kinetic_energy", "max_speed", "ringing_index") if name in series),
            next((name for name in series if name != x_default), x_default),
        )
        self.x_series_var.set(self.series_key_to_display[x_default])
        self.y_series_var.set(self.series_key_to_display[y_default])
        self.x_label_var.set(readable_label(x_default))
        self.y_label_var.set(readable_label(y_default))
        self._infer_scales()
        self.limits_are_auto = True
        warning = self._warning_summary(result.warnings)
        calculated = sum(is_derived_series(name) for name in series)
        self.status_var.set(
            f"Loaded {len(result.modes)} mode(s), {len(series) - calculated} exported and {calculated} calculated series from {result.path.name}.{warning}"
        )
        self.generate_plot(show_dialog=False)

    def _populate_modes(self) -> None:
        for child in self.modes_frame.winfo_children():
            child.destroy()
        self.mode_vars.clear()
        if not self.result:
            return
        for row, (key, mode) in enumerate(self.result.modes.items()):
            variable = BooleanVar(value=True)
            label = mode.label if mode.label == key else f"{mode.label} ({key})"
            ttk.Checkbutton(self.modes_frame, text=label, variable=variable).grid(
                row=row, column=0, sticky="w", padx=2, pady=1
            )
            self.mode_vars[key] = variable

    def _set_all_modes(self, selected: bool) -> None:
        for variable in self.mode_vars.values():
            variable.set(selected)

    def _selected_modes(self) -> list[str]:
        return [key for key, variable in self.mode_vars.items() if variable.get()]

    def _selected_series(self, variable: StringVar) -> str:
        return self.series_display_to_key.get(variable.get(), variable.get())

    def _on_series_changed(self, _event=None) -> None:
        if not self.result:
            return
        if _event is None or _event.widget is self.x_series_combo:
            self.x_label_var.set(readable_label(self._selected_series(self.x_series_var)))
        if _event is None or _event.widget is self.y_series_combo:
            self.y_label_var.set(readable_label(self._selected_series(self.y_series_var)))
        self._infer_scales()
        self.reset_autoscale()

    def _infer_scales(self) -> None:
        if not self.result:
            return
        selected = self._selected_modes() or list(self.result.modes)
        self.x_scale_var.set(infer_scale(self.result.values(self._selected_series(self.x_series_var), selected)))
        self.y_scale_var.set(infer_scale(self.result.values(self._selected_series(self.y_series_var), selected)))

    def _on_scale_changed(self, _event=None) -> None:
        self.reset_autoscale()

    def _on_limit_edited(self, _event=None) -> None:
        self.limits_are_auto = False

    def reset_autoscale(self) -> None:
        self.limits_are_auto = True
        self.generate_plot(show_dialog=False)

    @staticmethod
    def _parse_limit(value: str, label: str) -> float | None:
        if not value.strip():
            return None
        try:
            parsed = float(value)
        except ValueError as exc:
            raise PlotBuildError(f"{label} must be a number or left blank") from exc
        if not math.isfinite(parsed):
            raise PlotBuildError(f"{label} must be finite")
        return parsed

    def _manual_limits(self):
        if self.limits_are_auto:
            return None, None
        return (
            (
                self._parse_limit(self.x_min_var.get(), "X minimum"),
                self._parse_limit(self.x_max_var.get(), "X maximum"),
            ),
            (
                self._parse_limit(self.y_min_var.get(), "Y minimum"),
                self._parse_limit(self.y_max_var.get(), "Y maximum"),
            ),
        )

    def _set_limit_entries(self, x_limits: tuple[float, float], y_limits: tuple[float, float]) -> None:
        for variable, value in zip(
            (self.x_min_var, self.x_max_var, self.y_min_var, self.y_max_var), (*x_limits, *y_limits)
        ):
            variable.set(f"{value:.7g}")

    def generate_plot(self, show_dialog: bool = True) -> None:
        if not self.result:
            if show_dialog:
                messagebox.showinfo("No diagnostics", "Select a result folder first.", parent=self.winfo_toplevel())
            return
        selected = self._selected_modes()
        if not selected:
            message = "Select at least one transfer mode."
            self.status_var.set(message)
            if show_dialog:
                messagebox.showwarning("No transfer modes", message, parent=self.winfo_toplevel())
            return
        try:
            x_limits, y_limits = self._manual_limits()
            outcome = draw_diagnostics(
                self.figure,
                self.result.modes,
                selected,
                self._selected_series(self.x_series_var),
                self._selected_series(self.y_series_var),
                x_label=self.x_label_var.get().strip(),
                y_label=self.y_label_var.get().strip(),
                x_scale=self.x_scale_var.get(),
                y_scale=self.y_scale_var.get(),
                x_limits=x_limits,
                y_limits=y_limits,
                title=self.title_var.get(),
            )
        except PlotBuildError as exc:
            self.status_var.set(str(exc))
            if show_dialog:
                messagebox.showerror("Could not create plot", str(exc), parent=self.winfo_toplevel())
            return
        self._set_limit_entries(outcome.x_limits, outcome.y_limits)
        self.canvas.draw_idle()
        self.has_plot = True
        warnings = (*self.result.warnings, *outcome.warnings)
        self.status_var.set(f"Plotted {len(outcome.plotted_modes)} transfer mode(s).{self._warning_summary(warnings)}")

    @staticmethod
    def _warning_summary(warnings: tuple[str, ...]) -> str:
        if not warnings:
            return ""
        shown = "; ".join(warnings[:3])
        remaining = len(warnings) - min(3, len(warnings))
        suffix = f" (+{remaining} more)" if remaining else ""
        return f" Warnings: {shown}{suffix}"

    def save_plot(self) -> None:
        if not self.has_plot or not self.result:
            messagebox.showinfo("No plot", "Generate a plot before saving it.", parent=self.winfo_toplevel())
            return
        image_format = self.export_format_var.get()
        stem = re.sub(r"[^A-Za-z0-9_.-]+", "_", f"{self.result.path.parent.name}_{self._selected_series(self.y_series_var)}_vs_{self._selected_series(self.x_series_var)}")
        destination = filedialog.asksaveasfilename(
            title="Save diagnostics plot",
            initialdir=str(self.result.path),
            initialfile=f"{stem}.{image_format}",
            defaultextension=f".{image_format}",
            filetypes=[(image_format.upper(), f"*.{image_format}"), ("All files", "*.*")],
        )
        if not destination:
            return
        path = Path(destination)
        if path.suffix.casefold() != f".{image_format}":
            path = path.with_suffix(f".{image_format}")
        try:
            save_figure(self.figure, path, image_format)
        except (OSError, PlotBuildError) as exc:
            messagebox.showerror("Could not save plot", str(exc), parent=self.winfo_toplevel())
            self.status_var.set(f"Could not save plot: {exc}")
            return
        self.status_var.set(f"Saved {image_format.upper()} plot to {path}")
