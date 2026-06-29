"""Core experiment orchestration.

Responsibilities (and nothing else):
  * load and validate a JSON preset,
  * create a deterministic output directory (outputs/<name>/<timestamp>/),
  * estimate the run/render time from learned heuristics and print it,
  * launch the C++ headless solver once per transfer method,
  * capture stdout/stderr into logs,
  * verify the expected diagnostics outputs appeared,
  * when rendering: force particle export, render, then delete the raw particle
    dumps, leaving only the rendered artefacts,
  * record measured timings back into the heuristics so estimates improve.

Simulation behaviour lives entirely in the C++ solver. Experiment configuration
lives entirely in JSON. This module only wires the two together.
"""

from __future__ import annotations

import csv
import json
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import asdict
from datetime import datetime
from pathlib import Path
from typing import Any

import paths
import timing
from config_schema import (
    INFORMATIONAL_FIELDS,
    PresetError,
    ResolvedPreset,
    build_command,
    render_produces_output,
    validate,
)


_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


class OrchestratorError(RuntimeError):
    """User-facing orchestration failure."""


def _timestamp() -> str:
    return datetime.now().strftime("%Y%m%d-%H%M%S")


def _default_parallelism(job_count: int) -> int:
    """Default concurrency: about one process per physical core, capped by job count.

    ``os.cpu_count()`` reports logical processors; halving it approximates the
    physical-core count on hyperthreaded machines, which is the right budget for
    the single-threaded solver processes.
    """
    cores = os.cpu_count() or 2
    return max(1, min(job_count, cores // 2 or 1))


def load_preset(preset_path: Path) -> ResolvedPreset:
    """Read and validate a preset file, with clear errors for the common faults."""

    preset_path = Path(preset_path)
    if not preset_path.is_file():
        raise OrchestratorError(
            f"Preset file not found: {preset_path}\n"
            f"Available presets live under {paths.PRESETS_DIR}."
        )
    try:
        raw = json.loads(preset_path.read_text(encoding="utf-8"))
    except ValueError as exc:
        raise OrchestratorError(f"Preset is not valid JSON ({preset_path}): {exc}") from exc

    raw.setdefault("name", preset_path.stem)
    try:
        return validate(raw)
    except PresetError as exc:
        raise OrchestratorError(f"Invalid preset ({preset_path}): {exc}") from exc


def make_output_dir(preset: ResolvedPreset, timestamp: str, override: Path | None = None) -> Path:
    """Deterministic output directory: outputs/<name>/<timestamp>/ (or an override)."""

    if override is not None:
        out = Path(override).resolve()
    else:
        out = (paths.OUTPUTS_DIR / preset.name / timestamp).resolve()
    out.mkdir(parents=True, exist_ok=True)
    return out


def _resolve_executable(preset: ResolvedPreset) -> Path:
    executable = paths.mpm_experiment_executable() if preset.engine == "mpm" else paths.headless_executable()
    if not executable.is_file():
        label = "MPM experiment" if preset.engine == "mpm" else "fluid headless"
        raise OrchestratorError(
            f"{label} executable not found: {executable}\n"
            "Build the corresponding Release target or configure its executable "
            f"in {paths.ORCHESTRATOR_DIR / 'paths.local.json'}."
        )
    return executable


def _git_commit(repo: Path) -> str:
    safe_directory = next((candidate for candidate in (repo, *repo.parents) if (candidate / ".git").exists()), repo)
    try:
        result = subprocess.run(
            ["git", "-c", f"safe.directory={safe_directory}", "rev-parse", "HEAD"],
            cwd=repo, capture_output=True, text=True, creationflags=_NO_WINDOW,
        )
        return result.stdout.strip() if result.returncode == 0 else "unavailable"
    except OSError:
        return "unavailable"


def _read_particle_count(diagnostics: Path) -> int:
    """Particle count from the first diagnostics row (stable for a given scene/res)."""
    try:
        with diagnostics.open(newline="", encoding="utf-8") as stream:
            row = next(csv.DictReader(stream), None)
        return int(float(row["particle_count"])) if row and "particle_count" in row else 0
    except (OSError, ValueError, KeyError, StopIteration):
        return 0


def _expected_frames(preset: ResolvedPreset) -> int:
    return max(1, round(preset.steps * preset.dt * preset.export_fps))


def _parallel_wall_seconds(durations: list[float], max_parallel: int | None) -> float:
    """Estimate ThreadPoolExecutor wall time using its queued-job behaviour."""

    if not durations:
        return 0.0
    workers = (
        max(1, min(len(durations), max_parallel))
        if max_parallel
        else _default_parallelism(len(durations))
    )
    lanes = [0.0] * workers
    for duration in durations:
        lane = min(range(workers), key=lanes.__getitem__)
        lanes[lane] += duration
    return max(lanes)


def estimate_runtime(
    preset: ResolvedPreset,
    render: bool,
    store: dict,
    max_parallel: int | None = None,
) -> dict[str, Any]:
    """Return phase and combined wall-clock estimates for one preset.

    Simulation and rendering are separate parallel phases. Their predicted wall
    times are therefore the longest occupied worker lane in each phase, while
    the combined ETA is the sum of those two phase times.
    """

    engine = timing.engine_of(preset.raw)
    particles = timing.estimate_particles(store, preset.scene, preset.dim, preset.domain, preset.dx)
    if particles is None:
        return {
            "particles": None,
            "simulation_seconds": None,
            "render_seconds": None,
            "total_seconds": None,
            "complete": False,
        }

    simulation_jobs: list[float] = []
    simulation_complete = True
    for method in preset.methods:
        secs = timing.estimate_step_seconds(
            store, engine, timing.transfer_class(method.solver),
            preset.dim, preset.export_particles, particles, preset.steps,
        )
        if secs is None:
            simulation_complete = False
        else:
            simulation_jobs.append(secs)

    simulation_seconds = _parallel_wall_seconds(simulation_jobs, max_parallel) if simulation_jobs else None
    render_seconds: float | None = None
    render_complete = not render
    if render:
        per_method = timing.estimate_render_seconds(store, engine, _expected_frames(preset), particles)
        if per_method is not None:
            render_seconds = _parallel_wall_seconds([per_method] * len(preset.methods), max_parallel)
            render_complete = True

    complete = simulation_complete and render_complete and simulation_seconds is not None
    total_seconds = None
    if complete:
        total_seconds = simulation_seconds + (render_seconds or 0.0)

    return {
        "particles": particles,
        "simulation_seconds": simulation_seconds,
        "render_seconds": render_seconds,
        "total_seconds": total_seconds,
        "complete": complete,
    }


def _print_estimate(
    preset: ResolvedPreset,
    render: bool,
    store: dict,
    log,
    max_parallel: int | None = None,
) -> None:
    estimate = estimate_runtime(preset, render, store, max_parallel)
    particles = estimate["particles"]
    if particles is None:
        log("[estimate] no particle-count history for this scene/resolution yet; "
            "estimates will appear after the first run.")
        return

    parts = []
    if estimate["simulation_seconds"] is not None:
        parts.append(f"sim wall ~{timing.format_duration(estimate['simulation_seconds'])}")
    if render and estimate["render_seconds"] is not None:
        parts.append(f"render wall ~{timing.format_duration(estimate['render_seconds'])}")
    if estimate["total_seconds"] is not None:
        parts.append(f"combined ~{timing.format_duration(estimate['total_seconds'])}")
    if parts:
        suffix = "" if estimate["complete"] else " (partial: some timings unseen)"
        log(f"[estimate] ~{particles} particles, {preset.steps} steps x {len(preset.methods)} method(s): "
            + ", ".join(parts) + suffix)
    else:
        log("[estimate] timing history is incomplete for these methods; will learn from this run.")


def run_preset(
    preset: ResolvedPreset, output_dir: Path, store: dict, *, log_handle=None, force: bool = False,
    max_parallel: int | None = None,
) -> dict[str, Any]:
    """Run every method in the preset. Returns a manifest dict (also written to disk)."""

    executable = _resolve_executable(preset)
    engine = timing.engine_of(preset.raw)

    def log(message: str) -> None:
        print(message, flush=True)
        if log_handle is not None:
            log_handle.write(message + "\n")
            log_handle.flush()

    for field_name in INFORMATIONAL_FIELDS:
        if field_name in preset.raw:
            log(
                f"[warn] preset field '{field_name}' is informational only; "
                "the headless CLI does not accept it and it will not affect the run."
            )

    runs_dir = output_dir / "runs"
    runs_dir.mkdir(parents=True, exist_ok=True)

    manifest: dict[str, Any] = {
        "preset_name": preset.name,
        "engine": preset.engine,
        "scene": preset.scene,
        "scene_id": preset.scene_id,
        "dim": preset.dim,
        "steps": preset.steps,
        "dt": preset.dt,
        "dx": preset.dx,
        "domain": preset.domain,
        "jitter_seed": preset.jitter_seed,
        "engine_options": preset.engine_options,
        "export_particles": preset.export_particles,
        "export_fps": preset.export_fps,
        "executable": str(executable),
        "code_commit": _git_commit(paths.CODE_REPO),
        "created_at": datetime.now().isoformat(timespec="seconds"),
        "methods": [],
    }

    total = len(preset.methods)
    results_by_key: dict[str, dict[str, Any]] = {}

    to_run: list[tuple[int, Any]] = []
    for index, method in enumerate(preset.methods, start=1):
        diagnostics = runs_dir / method.key / "diagnostics.csv"
        if not force and diagnostics.is_file():
            log(f"[{index}/{total}] {method.label}: reusing {diagnostics}")
            results_by_key[method.key] = {"key": method.key, "status": "reused"}
        else:
            to_run.append((index, method))

    if to_run:
        workers = max(1, min(len(to_run), max_parallel)) if max_parallel else _default_parallelism(len(to_run))
        log(f"running {len(to_run)} method(s), up to {workers} in parallel")

        commands: dict[str, list[str]] = {}
        for index, method in to_run:
            method_dir = runs_dir / method.key
            method_dir.mkdir(parents=True, exist_ok=True)
            commands[method.key] = build_command(executable, preset, method, method_dir)
            log(f"[{index}/{total}] {method.label}: {' '.join(commands[method.key])}")

        def run_method(method) -> tuple[int, float]:
            started = time.monotonic()
            result = subprocess.run(commands[method.key], cwd=executable.parent, capture_output=True,
                                    text=True, creationflags=_NO_WINDOW)
            elapsed = time.monotonic() - started
            (runs_dir / method.key / "run.log").write_text(result.stdout + result.stderr, encoding="utf-8")
            return result.returncode, elapsed

        failures: list[str] = []
        with ThreadPoolExecutor(max_workers=workers) as pool:
            futures = {pool.submit(run_method, method): (index, method) for index, method in to_run}
            for future in as_completed(futures):
                index, method = futures[future]
                method_dir = runs_dir / method.key
                diagnostics = method_dir / "diagnostics.csv"
                try:
                    returncode, elapsed = future.result()
                except Exception as exc:  # pragma: no cover - launch failure
                    failures.append(f"{method.label}: {exc}")
                    results_by_key[method.key] = {"key": method.key, "status": "error"}
                    continue
                if returncode != 0:
                    failures.append(f"{method.label}: exit code {returncode} (see {method_dir / 'run.log'})")
                    results_by_key[method.key] = {"key": method.key, "status": "failed"}
                    continue
                if preset.diagnostics and not diagnostics.is_file():
                    failures.append(f"{method.label}: produced no diagnostics.csv (see {method_dir / 'run.log'})")
                    results_by_key[method.key] = {"key": method.key, "status": "failed"}
                    continue

                particles = _read_particle_count(diagnostics)
                timing.update_step(store, engine, timing.transfer_class(method.solver),
                                   preset.dim, preset.export_particles, particles, preset.steps, elapsed)
                timing.update_particles(store, preset.scene, preset.dim, preset.domain, preset.dx, particles)
                timing.save(store)

                solver_summary = None
                solver_run = method_dir / "run.json"
                if solver_run.is_file():
                    try:
                        solver_summary = json.loads(solver_run.read_text(encoding="utf-8"))
                    except (OSError, ValueError):
                        pass
                run_record = {
                    "method": asdict(method),
                    "command": commands[method.key],
                    "steps": preset.steps,
                    "particles": particles,
                    "elapsed_seconds": round(elapsed, 3),
                    "created_at": datetime.now().isoformat(timespec="seconds"),
                }
                if solver_summary is not None:
                    run_record["solver_summary"] = solver_summary
                solver_run.write_text(
                    json.dumps(run_record, indent=2),
                    encoding="utf-8",
                )
                results_by_key[method.key] = {
                    "key": method.key, "status": "ran",
                    "particles": particles, "elapsed_seconds": round(elapsed, 3),
                }
                log(f"[{index}/{total}] {method.label}: done in {timing.format_duration(elapsed)} -> {method_dir}")

        if failures:
            raise OrchestratorError("Method run(s) failed:\n  " + "\n  ".join(failures))

    manifest["methods"] = [results_by_key[method.key] for method in preset.methods if method.key in results_by_key]

    (output_dir / "preset.json").write_text(json.dumps(preset.raw, indent=2), encoding="utf-8")
    (output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return manifest


def _delete_particle_dumps(output_dir: Path, log) -> None:
    for particle_bin in sorted((output_dir / "runs").glob("*/particle_frames.bin")):
        try:
            particle_bin.unlink()
            log(f"cleanup: deleted {particle_bin}")
        except OSError as exc:
            log(f"cleanup: could not delete {particle_bin}: {exc}")


def orchestrate(
    preset_path: Path,
    *,
    output_dir: Path | None = None,
    render: bool = False,
    force: bool = False,
    keep_particles: bool = False,
    max_parallel: int | None = None,
) -> Path:
    """End-to-end run for one preset. Returns the output directory."""

    paths.ensure_runtime_dirs()
    preset = load_preset(preset_path)
    will_render = render and render_produces_output(preset.render)
    if will_render:
        preset.export_particles = True  # rendering needs particle data

    store = timing.load()
    engine = timing.engine_of(preset.raw)
    timestamp = _timestamp()
    out_dir = make_output_dir(preset, timestamp, output_dir)
    log_path = paths.LOGS_DIR / f"{preset.name}_{timestamp}.log"

    with log_path.open("w", encoding="utf-8") as log_handle:
        def log(message: str) -> None:
            print(message, flush=True)
            log_handle.write(message + "\n")
            log_handle.flush()

        log(f"orchestrator run: {preset.name}")
        log(f"preset: {preset_path}")
        log(f"output: {out_dir}")
        _print_estimate(preset, will_render, store, log, max_parallel)
        log("")

        run_preset(preset, out_dir, store, log_handle=log_handle, force=force, max_parallel=max_parallel)

        if will_render:
            import render_results

            log_handle.write("\n-- rendering --\n")
            r_started = time.monotonic()
            rendered = render_results.render_output_dir(
                out_dir,
                preset,
                log_handle=log_handle,
                max_parallel=max_parallel,
            )
            r_elapsed = time.monotonic() - r_started
            if rendered:
                particles = _read_particle_count(rendered[0] / "diagnostics.csv")
                frames = _expected_frames(preset) * len(rendered)
                render_workers = (
                    max(1, min(len(rendered), max_parallel))
                    if max_parallel
                    else _default_parallelism(len(rendered))
                )
                timing.update_render(store, engine, frames, particles, r_elapsed * render_workers)
                timing.save(store)
                log(f"render: {len(rendered)} method(s) in {timing.format_duration(r_elapsed)}")
            if not keep_particles:
                _delete_particle_dumps(out_dir, log)
        elif render:
            log("[render] neither MP4 nor snapshots is enabled; nothing to render (ran headless only).")

    print(f"\nOutputs: {out_dir}", flush=True)
    print(f"Log:     {log_path}", flush=True)
    return out_dir


if __name__ == "__main__":  
    if len(sys.argv) < 2:
        print("usage: python orchestrator.py <preset.json> [--render] [--force] [--keep-particles] [--parallel N]")
        raise SystemExit(2)
    rest = sys.argv[2:]
    parallel = None
    if "--parallel" in rest:
        position = rest.index("--parallel")
        if position + 1 < len(rest):
            parallel = int(rest[position + 1])
    orchestrate(
        Path(sys.argv[1]),
        render="--render" in rest,
        force="--force" in rest,
        keep_particles="--keep-particles" in rest,
        max_parallel=parallel,
    )
