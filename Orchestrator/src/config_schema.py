"""Preset schema, validation, and JSON-to-headless argument translation.

The JSON preset is the single source of truth. This module defines what a valid
preset looks like, the catalogue of known scenes and transfer methods, and the
one place that turns a preset into a ``Fluid_Simulation_Profile.exe`` command
line. Keeping the translation here means Python never invents experiment
behaviour: it only maps declared JSON fields onto the solver's documented flags.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

FLUID_SCENES: dict[str, int] = {
    "dam_break": 0,
    "solid_block": 1,
    "full_grid": 2,
    "constant_stream": 3,
    "taylor_green_vortex": 4,
    "confined_vortex": 5,
    "settling_pool": 6,
    "settling_pool_obstacle": 7,
}

MPM_SCENES: dict[str, int] = {
    "compressed_hyperelastic_square": 1,
}

SCENES: dict[str, int] = {**FLUID_SCENES, **MPM_SCENES}

SCENES_2D_ONLY = {
    "taylor_green_vortex",
    "confined_vortex",
    "settling_pool",
    "settling_pool_obstacle",
}

METHODS: dict[str, dict[str, Any]] = {
    "PIC": {"label": "PIC", "solver": "pic"},
    "FLIP_050": {"label": "FLIP 0.50", "solver": "flip", "flip_percent": 0.50},
    "FLIP_095": {"label": "FLIP 0.95", "solver": "flip", "flip_percent": 0.95},
    "FLIP_098": {"label": "FLIP 0.98", "solver": "flip", "flip_percent": 0.98},
    "FLIP_099": {"label": "FLIP 0.99", "solver": "flip", "flip_percent": 0.99},
    "FLIP_100": {"label": "FLIP 1.00", "solver": "flip", "flip_percent": 1.00},
    "APIC": {"label": "APIC", "solver": "apic"},
    "POLYPIC": {"label": "PolyPIC", "solver": "polypic"},
}

VALID_RENDER_COLORS = ("speed", "solid", "ringing")

PRESET_DEFAULTS: dict[str, Any] = {
    "dim": 2,
    "jitter_seed": 0,
    "adaptive": False,
    "diagnostics": True,
    "export_particles": False,
    "particle_fps": 30.0,
    "polypic_modes": 4,
}


class PresetError(ValueError):
    """Raised when a preset is structurally invalid. Message is user-facing."""


@dataclass
class ResolvedMethod:
    """A transfer method resolved against the catalogue, with overrides applied."""

    key: str
    label: str
    solver: str
    flip_percent: float | None = None
    polypic_modes: int | None = None

    def to_flags(self) -> list[str]:
        flags = ["--solver", self.solver]
        if self.flip_percent is not None:
            flags += ["--flip-percent", f"{self.flip_percent:.12g}"]
        if self.polypic_modes is not None:
            flags += ["--polypic-modes", str(self.polypic_modes)]
        return flags


@dataclass
class ResolvedPreset:
    """A validated preset ready to run."""

    name: str
    engine: str
    scene: str
    scene_id: int
    dim: int
    domain: int
    dx: float
    dt: float
    steps: int
    methods: list[ResolvedMethod]
    jitter_seed: int
    adaptive: bool
    diagnostics: bool
    export_particles: bool
    particle_fps: float
    export_fps: float
    render: dict[str, Any]
    engine_options: dict[str, Any] = field(default_factory=dict)
    raw: dict[str, Any] = field(default_factory=dict)


def _resolve_method(entry: Any, preset: dict[str, Any], dimension: int, engine: str) -> ResolvedMethod:
    """Resolve a preset ``methods`` entry (catalogue key or inline object)."""

    if isinstance(entry, str):
        if entry not in METHODS:
            raise PresetError(
                f"Unknown method '{entry}'. Known methods: {', '.join(METHODS)}."
            )
        spec = dict(METHODS[entry])
        key = entry
    elif isinstance(entry, dict):
        spec = dict(entry)
        key = spec.get("key") or spec.get("label") or spec.get("solver", "method")
    else:
        raise PresetError(f"Each method must be a catalogue key or an object, got {entry!r}.")

    solver = spec.get("solver")
    if solver not in ("pic", "flip", "apic", "polypic"):
        raise PresetError(f"Method '{key}' has invalid solver '{solver}'.")

    polypic_modes = spec.get("polypic_modes")
    if solver == "polypic" and polypic_modes is None:
        default_modes = 9 if engine == "mpm" else 8 if dimension == 3 else PRESET_DEFAULTS["polypic_modes"]
        polypic_modes = preset.get("polypic_modes", default_modes)
    if solver == "polypic":
        polypic_modes = int(polypic_modes)
        max_modes = 9 if engine == "mpm" else 8 if dimension == 3 else 4
        if not 1 <= polypic_modes <= max_modes:
            raise PresetError(
                f"Method '{key}' has polypic_modes={polypic_modes}; "
                f"dimension {dimension} supports 1..{max_modes}."
            )

    return ResolvedMethod(
        key=str(key),
        label=str(spec.get("label", key)),
        solver=solver,
        flip_percent=spec.get("flip_percent"),
        polypic_modes=polypic_modes if solver == "polypic" else None,
    )


def validate(preset: dict[str, Any]) -> ResolvedPreset:
    """Validate a raw preset dict and return a :class:`ResolvedPreset`.

    Raises :class:`PresetError` with an actionable message on any problem.
    """

    if not isinstance(preset, dict):
        raise PresetError("Preset must be a JSON object.")

    def need(key: str) -> Any:
        if key not in preset:
            raise PresetError(f"Preset is missing required field '{key}'.")
        return preset[key]

    name = str(need("name"))

    scene = str(need("scene"))
    if scene not in SCENES:
        raise PresetError(
            f"Unknown scene '{scene}'. Known scenes: {', '.join(SCENES)}."
        )
    inferred_engine = "mpm" if scene in MPM_SCENES else "fluid"
    engine = str(preset.get("engine", inferred_engine)).lower()
    if engine not in ("fluid", "mpm"):
        raise PresetError("Field 'engine' must be 'fluid' or 'mpm'.")
    if engine != inferred_engine:
        raise PresetError(f"Scene '{scene}' belongs to engine '{inferred_engine}', not '{engine}'.")
    scene_id = SCENES[scene]

    dim = int(preset.get("dim", PRESET_DEFAULTS["dim"]))
    if dim not in (2, 3):
        raise PresetError("Field 'dim' must be 2 or 3.")
    if scene in SCENES_2D_ONLY and dim != 2:
        raise PresetError(f"Scene '{scene}' is only available in 2D (set dim to 2).")
    if engine == "mpm" and dim != 2:
        raise PresetError("The compressed hyperelastic square MPM scene is 2D (set dim to 2).")

    domain = int(need("domain"))
    dx = float(need("dx"))
    dt = float(need("dt"))
    if domain <= 0 or dx <= 0 or dt <= 0:
        raise PresetError("Fields 'domain', 'dx', and 'dt' must be positive.")

    # Step count: explicit 'steps' wins, else derived from 'duration'.
    if "steps" in preset:
        steps = int(preset["steps"])
    elif "duration" in preset:
        steps = max(1, round(float(preset["duration"]) / dt))
    else:
        raise PresetError("Preset must define either 'steps' or 'duration'.")
    if steps <= 0:
        raise PresetError("Computed step count must be positive.")

    method_entries = need("methods")
    if not isinstance(method_entries, list) or not method_entries:
        raise PresetError("Field 'methods' must be a non-empty list.")
    methods = [_resolve_method(entry, preset, dim, engine) for entry in method_entries]

    adaptive = bool(preset.get("adaptive", PRESET_DEFAULTS["adaptive"]))
    if engine == "mpm" and adaptive:
        raise PresetError("The MPM experiment uses a fixed timestep; set adaptive to false.")

    engine_options: dict[str, Any] = {}
    if engine == "mpm":
        mpm = preset.get("mpm", {})
        if not isinstance(mpm, dict):
            raise PresetError("Field 'mpm' must be an object when provided.")
        engine_options = {
            "square_cells": int(mpm.get("square_cells", 12)),
            "particles_per_cell_axis": int(mpm.get("particles_per_cell_axis", 2)),
            "compression": float(mpm.get("compression", 0.9)),
            "gravity": bool(mpm.get("gravity", False)),
            "diagnostic_samples": int(mpm.get("diagnostic_samples", 600)),
            "polypic_regularization": float(mpm.get("polypic_regularization", 0.02)),
        }
        if engine_options["square_cells"] <= 0:
            raise PresetError("mpm.square_cells must be positive.")
        if engine_options["particles_per_cell_axis"] <= 0:
            raise PresetError("mpm.particles_per_cell_axis must be positive.")
        if not 0.0 < engine_options["compression"] <= 1.0:
            raise PresetError("mpm.compression must be in (0, 1].")
        if engine_options["diagnostic_samples"] <= 0:
            raise PresetError("mpm.diagnostic_samples must be positive.")
        if engine_options["polypic_regularization"] < 0.0:
            raise PresetError("mpm.polypic_regularization must be non-negative.")

    render = dict(preset.get("render", {}))
    color = render.get("color", "speed")
    if color not in VALID_RENDER_COLORS:
        raise PresetError(
            f"render.color '{color}' invalid. Options: {', '.join(VALID_RENDER_COLORS)}."
        )

    particle_fps = float(preset.get("particle_fps", PRESET_DEFAULTS["particle_fps"]))
    # Export only the frames the video needs: cap the requested fps at the step
    # rate (1/dt). render.fps wins when set, else the preset's particle_fps. If
    # the requested fps exceeds what the timestep can produce, it drops to 1/dt.
    requested_fps = float(render["fps"]) if render.get("fps") else particle_fps
    export_fps = min(requested_fps, 1.0 / dt)
    if export_fps <= 0:
        export_fps = 1.0 / dt

    return ResolvedPreset(
        name=name,
        engine=engine,
        scene=scene,
        scene_id=scene_id,
        dim=dim,
        domain=domain,
        dx=dx,
        dt=dt,
        steps=steps,
        methods=methods,
        jitter_seed=int(preset.get("jitter_seed", PRESET_DEFAULTS["jitter_seed"])),
        adaptive=adaptive,
        diagnostics=bool(preset.get("diagnostics", PRESET_DEFAULTS["diagnostics"])),
        export_particles=bool(preset.get("export_particles", PRESET_DEFAULTS["export_particles"])),
        particle_fps=particle_fps,
        export_fps=export_fps,
        render=render,
        engine_options=engine_options,
        raw=preset,
    )


def build_command(
    executable: Path,
    preset: ResolvedPreset,
    method: ResolvedMethod,
    out_dir: Path,
) -> list[str]:
    """Translate a validated preset + method into a headless command line.

    Mirrors the flag mapping used by the existing
    ``run_ringing_metric_experiments.py`` so runs stay byte-for-byte comparable.
    """

    if preset.engine == "mpm":
        options = preset.engine_options
        nr = method.polypic_modes if method.solver == "polypic" else 3 if method.solver == "apic" else 1
        flip = 0.0 if method.solver == "pic" else method.flip_percent if method.flip_percent is not None else 0.95
        command = [
            str(executable),
            "--out", str(out_dir),
            "--solver", method.solver,
            "--nr", str(nr),
            "--flip", f"{flip:.12g}",
            "--reg", f"{options['polypic_regularization']:.12g}",
            "--dt", f"{preset.dt:.17g}",
            "--steps", str(preset.steps),
            "--particle-fps", f"{preset.export_fps:.12g}",
            "--domain", str(preset.domain),
            "--dx", f"{preset.dx:.12g}",
            "--square-cells", str(options["square_cells"]),
            "--ppc", str(options["particles_per_cell_axis"]),
            "--compression", f"{options['compression']:.12g}",
            "--diag-samples", str(options["diagnostic_samples"]),
            "--export" if preset.diagnostics else "--no-diagnostics",
            "--export-particles" if preset.export_particles else "--no-particles",
        ]
        if options["gravity"]:
            command.append("--gravity")
        return command

    command = [
        str(executable),
        "--dim", str(preset.dim),
        "--scene", str(preset.scene_id),
        "--steps", str(preset.steps),
        "--domain", str(preset.domain),
        "--dx", f"{preset.dx:.12g}",
        "--dt", f"{preset.dt:.17g}",
        "--jitter-seed", str(preset.jitter_seed),
    ]
    if not preset.adaptive:
        command.append("--no-adaptive")
    command += method.to_flags()
    if preset.diagnostics:
        command.append("--export")
    else:
        command.append("--no-diagnostics")
    if preset.export_particles:
        command += ["--export-particles", "--particle-fps", f"{preset.export_fps:.12g}"]
    command += ["--out", str(out_dir)]
    return command


# Fields a preset may carry that the headless CLI does NOT consume, kept only as
# documentation/metadata. The orchestrator warns when these are present.
INFORMATIONAL_FIELDS = (
    "projection_max_iterations",
    "projection_tolerance",
    "projection_tolerance_type",
)


def render_produces_output(render: dict[str, Any]) -> bool:
    """True if the render config would write an MP4 or PNG snapshots."""
    return bool(render.get("video", True)) or bool(render.get("snapshot_interval"))
