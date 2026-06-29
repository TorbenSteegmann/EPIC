"""Reusable Matplotlib construction for experiment diagnostic curves."""

from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping

from matplotlib.figure import Figure

from diagnostics_derived import series_label
from diagnostics_loader import ModeDiagnostics


class PlotBuildError(ValueError):
    """Raised when selected diagnostics cannot produce a valid plot."""


@dataclass(frozen=True)
class PlotResult:
    plotted_modes: tuple[str, ...]
    warnings: tuple[str, ...]
    x_limits: tuple[float, float]
    y_limits: tuple[float, float]


def readable_label(series_name: str) -> str:
    if series_name == "time":
        return "Simulation time"
    return series_label(series_name)


def infer_scale(values: Iterable[float]) -> str:
    """Choose log only for finite, strictly positive data spanning 3+ decades."""

    finite = [value for value in values if math.isfinite(value)]
    if not finite or min(finite) <= 0:
        return "linear"
    low, high = min(finite), max(finite)
    return "log" if high > low and high / low >= 1000 else "linear"


def data_limits(values: Iterable[float], scale: str) -> tuple[float, float]:
    finite = [value for value in values if math.isfinite(value) and (scale != "log" or value > 0)]
    if not finite:
        raise PlotBuildError(f"No finite{' positive' if scale == 'log' else ''} data for {scale} scale")
    low, high = min(finite), max(finite)
    if scale == "log":
        if low == high:
            return low / 1.25, high * 1.25
        factor = (high / low) ** 0.04
        return low / factor, high * factor
    if low == high:
        margin = abs(low) * 0.05 or 1.0
    else:
        margin = (high - low) * 0.04
    lower, upper = low - margin, high + margin
    if low >= 0 and (high == 0 or low <= high * 0.05):
        lower = 0.0
    if high <= 0 and (low == 0 or abs(high) <= abs(low) * 0.05):
        upper = 0.0
    return lower, upper


def _merge_limits(
    derived: tuple[float, float], manual: tuple[float | None, float | None] | None, scale: str
) -> tuple[float, float]:
    if manual is None:
        return derived
    low = derived[0] if manual[0] is None else manual[0]
    high = derived[1] if manual[1] is None else manual[1]
    if low >= high:
        raise PlotBuildError("Axis minimum must be smaller than its maximum")
    if scale == "log" and low <= 0:
        raise PlotBuildError("Logarithmic axis limits must be greater than zero")
    return low, high


def draw_diagnostics(
    figure: Figure,
    modes: Mapping[str, ModeDiagnostics],
    selected_modes: Iterable[str],
    x_series: str,
    y_series: str,
    *,
    x_label: str,
    y_label: str,
    x_scale: str = "linear",
    y_scale: str = "linear",
    x_limits: tuple[float | None, float | None] | None = None,
    y_limits: tuple[float | None, float | None] | None = None,
    title: str = "",
) -> PlotResult:
    """Clear ``figure`` and draw one diagnostic curve per selected mode."""

    if x_scale not in {"linear", "log", "symlog"} or y_scale not in {"linear", "log", "symlog"}:
        raise PlotBuildError("Unsupported axis scale")

    figure.clear()
    axis = figure.add_subplot(111)
    warnings: list[str] = []
    plotted: list[str] = []
    all_x: list[float] = []
    all_y: list[float] = []

    for key in selected_modes:
        mode = modes.get(key)
        if mode is None:
            warnings.append(f"{key}: transfer mode is unavailable")
            continue
        if x_series not in mode.series or y_series not in mode.series:
            missing = [name for name in (x_series, y_series) if name not in mode.series]
            warnings.append(f"{mode.label}: missing {', '.join(missing)}")
            continue
        pairs = [
            (x, y)
            for x, y in zip(mode.series[x_series], mode.series[y_series])
            if math.isfinite(x)
            and math.isfinite(y)
            and (x_scale != "log" or x > 0)
            and (y_scale != "log" or y > 0)
        ]
        if not pairs:
            warnings.append(f"{mode.label}: no values valid for the selected scales")
            continue
        x_values, y_values = zip(*pairs)
        axis.plot(x_values, y_values, linewidth=1.8, label=mode.label)
        all_x.extend(x_values)
        all_y.extend(y_values)
        plotted.append(key)

    if not plotted:
        figure.clear()
        raise PlotBuildError("None of the selected transfer modes has plottable x/y data")

    axis.set_xscale(x_scale)
    axis.set_yscale(y_scale)
    derived_x = data_limits(all_x, x_scale)
    derived_y = data_limits(all_y, y_scale)
    final_x = _merge_limits(derived_x, x_limits, x_scale)
    final_y = _merge_limits(derived_y, y_limits, y_scale)
    axis.set_xlim(*final_x)
    axis.set_ylim(*final_y)
    axis.set_xlabel(x_label)
    axis.set_ylabel(y_label)
    if title.strip():
        axis.set_title(title.strip())
    axis.grid(True, which="both", color="#b0b0b0", alpha=0.35, linewidth=0.8)
    axis.legend(frameon=False)
    axis.tick_params(labelsize=9)
    figure.set_constrained_layout(True)
    return PlotResult(tuple(plotted), tuple(warnings), final_x, final_y)


def save_figure(figure: Figure, path: Path, image_format: str) -> None:
    image_format = image_format.casefold()
    if image_format not in {"svg", "png", "pdf"}:
        raise PlotBuildError(f"Unsupported export format: {image_format}")
    options = {"format": image_format, "bbox_inches": "tight"}
    if image_format == "png":
        options["dpi"] = 300
    figure.savefig(Path(path), **options)
