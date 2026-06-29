"""Centralised filesystem locations for the experiment orchestrator.

All Python source lives in ``src/``; the project root (one level up) holds
``presets/``, ``outputs/``, ``logs/``, the working ``current.json``, and the
batch files. The compiled C++ executables live under ``<project>/build``; their
location is discovered automatically and can be overridden via an environment
variable or ``paths.local.json``.
"""

from __future__ import annotations

import json
import os
from pathlib import Path

# --- Project layout ---------------------------------------------------------

SRC_DIR = Path(__file__).resolve().parent
ORCHESTRATOR_DIR = SRC_DIR.parent

PRESETS_DIR = ORCHESTRATOR_DIR / "presets"
OUTPUTS_DIR = ORCHESTRATOR_DIR / "outputs"
LOGS_DIR = ORCHESTRATOR_DIR / "logs"
CURRENT_JSON = ORCHESTRATOR_DIR / "current.json"
RUN_EXPERIMENT = SRC_DIR / "run_experiment.py"

# --- Compiled project (the repo this orchestrator ships inside) --------------

PROJECT_ROOT = ORCHESTRATOR_DIR.parent
CODE_REPO = PROJECT_ROOT
CODE_BUILD = PROJECT_ROOT / "build"
RENDER_SCRIPT = ORCHESTRATOR_DIR / "render_particle_video.py"

_LOCAL_OVERRIDES = ORCHESTRATOR_DIR / "paths.local.json"
_EXE_SUFFIX = ".exe" if os.name == "nt" else ""
_HEADLESS_NAME = "Fluid_Simulation_Profile"
_PARTICLE_RENDERER_NAME = "Fluid_Particle_Renderer"
_MPM_EXPERIMENT_NAME = "MPM_PolyPIC_Experiment"


def _local_overrides() -> dict:
    if _LOCAL_OVERRIDES.is_file():
        try:
            return json.loads(_LOCAL_OVERRIDES.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return {}
    return {}


def _discover_executable(stem: str, env_var: str, override_key: str) -> Path:
    """Resolve a compiled target by name.

    Order: environment override, then ``paths.local.json``, then the newest
    matching binary anywhere under ``build`` (the recursive search handles the
    generator-specific subfolders), then a plain ``build/Release`` fallback. Both
    the bare and ``.exe`` names are matched so the same logic works on Windows,
    Linux and macOS.
    """

    override = os.environ.get(env_var) or _local_overrides().get(override_key)
    if override:
        return Path(override).expanduser().resolve()

    candidates: list[Path] = []
    if CODE_BUILD.is_dir():
        for pattern in (f"**/{stem}.exe", f"**/{stem}"):
            candidates.extend(p for p in CODE_BUILD.glob(pattern) if p.is_file())
        candidates = sorted(set(candidates), key=lambda p: p.stat().st_mtime, reverse=True)
    if candidates:
        return candidates[0].resolve()

    return (CODE_BUILD / "Release" / f"{stem}{_EXE_SUFFIX}").resolve()


def headless_executable() -> Path:
    return _discover_executable(_HEADLESS_NAME, "FLUID_PROFILE_EXE", "headless")


def particle_renderer_executable() -> Path:
    return _discover_executable(_PARTICLE_RENDERER_NAME, "FLUID_PARTICLE_RENDERER_EXE", "particle_renderer")


def mpm_experiment_executable() -> Path:
    return _discover_executable(_MPM_EXPERIMENT_NAME, "MPM_EXPERIMENT_EXE", "mpm_experiment")


def ensure_runtime_dirs() -> None:
    OUTPUTS_DIR.mkdir(parents=True, exist_ok=True)
    LOGS_DIR.mkdir(parents=True, exist_ok=True)
