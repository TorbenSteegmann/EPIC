"""Discovery and read-only loading of exported experiment diagnostics."""

from __future__ import annotations

import csv
import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


class DiagnosticsLoadError(ValueError):
    """Raised when a result folder cannot provide plottable diagnostics."""


@dataclass(frozen=True)
class ModeDiagnostics:
    key: str
    label: str
    source: Path
    series: dict[str, tuple[float, ...]]
    row_count: int
    warnings: tuple[str, ...] = ()

    @property
    def columns(self) -> tuple[str, ...]:
        return tuple(self.series)


@dataclass(frozen=True)
class ResultDiagnostics:
    path: Path
    modes: dict[str, ModeDiagnostics]
    warnings: tuple[str, ...] = ()
    metadata: dict[str, Any] = field(default_factory=dict)

    @property
    def available_series(self) -> tuple[str, ...]:
        names = {name for mode in self.modes.values() for name in mode.columns}
        priority = {"step": 0, "time": 1}
        return tuple(sorted(names, key=lambda name: (priority.get(name, 2), name.casefold())))

    @property
    def common_series(self) -> tuple[str, ...]:
        columns = [set(mode.columns) for mode in self.modes.values()]
        if not columns:
            return ()
        common = set.intersection(*columns)
        return tuple(name for name in self.available_series if name in common)

    def values(self, series_name: str, mode_keys: Iterable[str] | None = None) -> list[float]:
        keys = mode_keys if mode_keys is not None else self.modes
        return [
            value
            for key in keys
            if key in self.modes
            for value in self.modes[key].series.get(series_name, ())
            if math.isfinite(value)
        ]


def _diagnostics_file(mode_dir: Path) -> Path | None:
    for name in ("diagnostics.csv", "diagnostics.json"):
        candidate = mode_dir / name
        if candidate.is_file():
            return candidate
    return None


def _looks_like_result(path: Path) -> bool:
    runs = path / "runs"
    if not runs.is_dir():
        return False
    try:
        return any(child.is_dir() and _diagnostics_file(child) for child in runs.iterdir())
    except OSError:
        return False


def discover_result_folders(base_dir: Path) -> list[Path]:
    """Return folders containing ``runs/<mode>/diagnostics.(csv|json)``.

    The newest folders are returned first. A selected result folder can itself
    be used as the search root.
    """

    base_dir = Path(base_dir).expanduser()
    if not base_dir.is_dir():
        return []

    found: set[Path] = set()
    if _looks_like_result(base_dir):
        found.add(base_dir.resolve())
    try:
        run_dirs = base_dir.rglob("runs")
        for runs in run_dirs:
            if runs.is_dir() and _looks_like_result(runs.parent):
                found.add(runs.parent.resolve())
    except OSError:
        # A partially unreadable tree should not hide otherwise discoverable runs.
        pass

    def modified(path: Path) -> float:
        try:
            return path.stat().st_mtime
        except OSError:
            return 0.0

    return sorted(found, key=lambda path: (modified(path), str(path).casefold()), reverse=True)


def _coerce_rows(rows: list[dict[str, Any]], source: Path) -> tuple[dict[str, tuple[float, ...]], list[str]]:
    if not rows:
        raise DiagnosticsLoadError(f"{source.name} contains no data rows")

    columns: list[str] = []
    for row in rows:
        if not isinstance(row, dict):
            raise DiagnosticsLoadError(f"{source.name} must contain objects/column-value rows")
        for name in row:
            if name is None:
                raise DiagnosticsLoadError(f"{source.name} has a row with more fields than its header")
            clean_name = str(name).strip()
            if clean_name and clean_name not in columns:
                columns.append(clean_name)

    numeric: dict[str, tuple[float, ...]] = {}
    warnings: list[str] = []
    for column in columns:
        values: list[float] = []
        invalid = 0
        finite = 0
        for row in rows:
            raw = row.get(column, "")
            try:
                if raw is None or (isinstance(raw, str) and not raw.strip()):
                    raise ValueError
                value = float(raw)
                if not math.isfinite(value):
                    raise ValueError
                finite += 1
            except (TypeError, ValueError, OverflowError):
                value = math.nan
                invalid += 1
            values.append(value)
        if finite:
            numeric[column] = tuple(values)
            if invalid:
                warnings.append(f"{column}: ignored {invalid} empty/non-numeric value(s)")

    if not numeric:
        raise DiagnosticsLoadError(f"{source.name} contains no numeric diagnostic series")
    return numeric, warnings


def _load_csv(path: Path) -> tuple[dict[str, tuple[float, ...]], int, list[str]]:
    try:
        with path.open("r", encoding="utf-8-sig", newline="") as stream:
            reader = csv.DictReader(stream, strict=True)
            if not reader.fieldnames:
                raise DiagnosticsLoadError(f"{path.name} has no header")
            cleaned = [name.strip() if name else "" for name in reader.fieldnames]
            if not all(cleaned) or len(cleaned) != len(set(cleaned)):
                raise DiagnosticsLoadError(f"{path.name} has blank or duplicate column names")
            reader.fieldnames = cleaned
            rows = list(reader)
    except (OSError, csv.Error) as exc:
        raise DiagnosticsLoadError(f"could not read {path}: {exc}") from exc
    series, warnings = _coerce_rows(rows, path)
    return series, len(rows), warnings


def _load_json(path: Path) -> tuple[dict[str, tuple[float, ...]], int, list[str]]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as exc:
        raise DiagnosticsLoadError(f"could not read {path}: {exc}") from exc
    if isinstance(payload, dict):
        payload = payload.get("rows", payload.get("diagnostics"))
    if not isinstance(payload, list):
        raise DiagnosticsLoadError(f"{path.name} must be a row list or contain a 'rows' list")
    series, warnings = _coerce_rows(payload, path)
    return series, len(payload), warnings


def _mode_label(mode_dir: Path) -> tuple[str, list[str]]:
    run_json = mode_dir / "run.json"
    if not run_json.is_file():
        return mode_dir.name, []
    try:
        metadata = json.loads(run_json.read_text(encoding="utf-8-sig"))
        method = metadata.get("method", {}) if isinstance(metadata, dict) else {}
        label = method.get("label") if isinstance(method, dict) else None
        return (str(label).strip() or mode_dir.name), []
    except (OSError, json.JSONDecodeError, AttributeError) as exc:
        return mode_dir.name, [f"{mode_dir.name}/run.json could not be read: {exc}"]


def _ordered_mode_dirs(result_dir: Path, runs_dir: Path) -> tuple[list[Path], list[str]]:
    mode_dirs = sorted((path for path in runs_dir.iterdir() if path.is_dir()), key=lambda path: path.name.casefold())
    manifest = result_dir / "manifest.json"
    if not manifest.is_file():
        return mode_dirs, []
    try:
        payload = json.loads(manifest.read_text(encoding="utf-8-sig"))
        methods = payload.get("methods", []) if isinstance(payload, dict) else []
        ordered_keys = [
            str(method["key"])
            for method in methods
            if isinstance(method, dict) and method.get("key") is not None
        ]
    except (OSError, json.JSONDecodeError, TypeError) as exc:
        return mode_dirs, [f"manifest.json could not be read: {exc}"]

    by_name = {path.name: path for path in mode_dirs}
    ordered = [by_name.pop(key) for key in ordered_keys if key in by_name]
    ordered.extend(sorted(by_name.values(), key=lambda path: path.name.casefold()))
    return ordered, []


def load_result_folder(result_dir: Path) -> ResultDiagnostics:
    """Load all usable transfer modes from a result folder without modifying it."""

    result_dir = Path(result_dir).expanduser().resolve()
    if result_dir.name.casefold() == "runs":
        result_dir = result_dir.parent
    runs_dir = result_dir / "runs"
    if not runs_dir.is_dir():
        raise DiagnosticsLoadError(f"No 'runs' folder found in {result_dir}")

    modes: dict[str, ModeDiagnostics] = {}
    metadata: dict[str, Any] = {}
    warnings: list[str] = []
    try:
        mode_dirs, manifest_warnings = _ordered_mode_dirs(result_dir, runs_dir)
        warnings.extend(manifest_warnings)
    except OSError as exc:
        raise DiagnosticsLoadError(f"Could not inspect {runs_dir}: {exc}") from exc

    if not mode_dirs:
        raise DiagnosticsLoadError(f"No transfer-mode folders found in {runs_dir}")

    for mode_dir in mode_dirs:
        source = _diagnostics_file(mode_dir)
        if source is None:
            warnings.append(f"{mode_dir.name}: no diagnostics.csv or diagnostics.json")
            continue
        label, label_warnings = _mode_label(mode_dir)
        warnings.extend(label_warnings)
        try:
            if source.suffix.casefold() == ".csv":
                series, row_count, mode_warnings = _load_csv(source)
            else:
                series, row_count, mode_warnings = _load_json(source)
        except DiagnosticsLoadError as exc:
            warnings.append(f"{mode_dir.name}: {exc}")
            continue
        qualified_warnings = tuple(f"{mode_dir.name}: {message}" for message in mode_warnings)
        modes[mode_dir.name] = ModeDiagnostics(
            key=mode_dir.name,
            label=label,
            source=source,
            series=series,
            row_count=row_count,
            warnings=qualified_warnings,
        )
        warnings.extend(qualified_warnings)

    manifest = result_dir / "manifest.json"
    if manifest.is_file():
        try:
            payload = json.loads(manifest.read_text(encoding="utf-8-sig"))
            if isinstance(payload, dict):
                metadata = payload
        except (OSError, json.JSONDecodeError) as exc:
            warnings.append(f"manifest.json metadata could not be read: {exc}")

    if not modes:
        detail = "; ".join(warnings) if warnings else "no readable diagnostic files"
        raise DiagnosticsLoadError(f"No plottable diagnostics in {result_dir}: {detail}")
    # Import locally to keep the raw file loader independent and avoid a module cycle.
    from diagnostics_derived import add_derived_series

    modes = add_derived_series(modes, metadata)
    return ResultDiagnostics(path=result_dir, modes=modes, warnings=tuple(warnings), metadata=metadata)
