"""Learned runtime heuristics.

Persists timing data to ``timing_heuristics.json`` and uses it to estimate how
long a run and its render will take, then updates itself after every run so the
estimates sharpen over time.

Normalisation rules (as specified):
  * Separate buckets for MPM vs fluid engines.
  * Separate buckets per transfer-function *class*, where PIC, FLIP and every
    alpha blend collapse into one class ("pic_flip"); APIC and PolyPIC are their
    own classes.
  * Step cost is stored per step *per particle* so it transfers across domain
    sizes; particle counts per (scene, dim, domain, dx) are stored so a future
    run of the same scene/resolution can be estimated before it starts.
  * Render cost is stored per exported frame per particle.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import paths

HEURISTICS_FILE = paths.ORCHESTRATOR_DIR / "timing_heuristics.json"
_EMA_ALPHA = 0.3  


def transfer_class(solver: str) -> str:
    """Collapse transfer methods into timing-equivalent classes."""
    if solver in ("pic", "flip"):
        return "pic_flip"
    return solver 


def engine_of(preset_raw: dict[str, Any]) -> str:
    """Fluid scenes today; presets may declare engine='mpm' later."""
    return str(preset_raw.get("engine", "fluid"))


def _step_key(engine: str, tclass: str, dim: int, exported: bool) -> str:
    return f"{engine}|{tclass}|dim{dim}|export{int(bool(exported))}"


def _particle_key(scene: str, dim: int, domain: int, dx: float) -> str:
    return f"{scene}|dim{dim}|d{domain}|dx{dx:g}"


def load() -> dict[str, Any]:
    if HEURISTICS_FILE.is_file():
        try:
            data = json.loads(HEURISTICS_FILE.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            data = {}
    else:
        data = {}
    data.setdefault("step", {})
    data.setdefault("particles", {})
    data.setdefault("render", {})
    return data


def save(store: dict[str, Any]) -> None:
    HEURISTICS_FILE.write_text(json.dumps(store, indent=2, sort_keys=True), encoding="utf-8")


def _ema(entry: dict[str, Any] | None, sample: float) -> dict[str, Any]:
    if entry is None or "rate" not in entry:
        return {"rate": sample, "samples": 1}
    rate = _EMA_ALPHA * sample + (1.0 - _EMA_ALPHA) * float(entry["rate"])
    return {"rate": rate, "samples": int(entry.get("samples", 0)) + 1}


def estimate_particles(store: dict, scene: str, dim: int, domain: int, dx: float) -> int | None:
    value = store["particles"].get(_particle_key(scene, dim, domain, dx))
    return int(value) if value else None


def estimate_step_seconds(
    store: dict, engine: str, tclass: str, dim: int, exported: bool, particles: int, steps: int
) -> float | None:
    entry = store["step"].get(_step_key(engine, tclass, dim, exported))
    if not entry or particles <= 0:
        return None
    return float(entry["rate"]) * particles * steps


def estimate_render_seconds(store: dict, engine: str, frames: int, particles: int) -> float | None:
    entry = store["render"].get(engine)
    if not entry or particles <= 0 or frames <= 0:
        return None
    return float(entry["rate"]) * frames * particles


def update_step(
    store: dict, engine: str, tclass: str, dim: int, exported: bool,
    particles: int, steps: int, elapsed_seconds: float,
) -> None:
    if particles <= 0 or steps <= 0 or elapsed_seconds <= 0:
        return
    sample = elapsed_seconds / (steps * particles)
    key = _step_key(engine, tclass, dim, exported)
    store["step"][key] = _ema(store["step"].get(key), sample)


def update_particles(store: dict, scene: str, dim: int, domain: int, dx: float, particles: int) -> None:
    if particles > 0:
        store["particles"][_particle_key(scene, dim, domain, dx)] = int(particles)


def update_render(store: dict, engine: str, frames: int, particles: int, elapsed_seconds: float) -> None:
    if frames <= 0 or particles <= 0 or elapsed_seconds <= 0:
        return
    sample = elapsed_seconds / (frames * particles)
    store["render"][engine] = _ema(store["render"].get(engine), sample)


def format_duration(seconds: float) -> str:
    seconds = int(round(seconds))
    if seconds < 60:
        return f"{seconds}s"
    if seconds < 3600:
        return f"{seconds // 60}m {seconds % 60}s"
    return f"{seconds // 3600}h {(seconds % 3600) // 60}m"
