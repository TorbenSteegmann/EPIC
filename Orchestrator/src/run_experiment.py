"""Command-line entry point for running an experiment preset.

The CLI runs the preset file you pass it directly (unlike the GUI, which runs the
edited form via current.json).

Examples
--------
    python run_experiment.py presets/final_experiment.json
    python run_experiment.py presets/settling_pool.json --render
    python run_experiment.py presets/settling_pool.json --render --keep-particles
    python run_experiment.py presets/taylor_green.json --output outputs/tgv_manual --force
    python run_experiment.py --list
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path

import paths
from orchestrator import OrchestratorError, orchestrate


def _list_presets() -> None:
    if not paths.PRESETS_DIR.is_dir():
        print(f"No presets directory at {paths.PRESETS_DIR}")
        return
    print(f"Presets under {paths.PRESETS_DIR}:")
    for preset in sorted(paths.PRESETS_DIR.rglob("*.json")):
        print(f"  {preset.relative_to(paths.PRESETS_DIR)}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run a C++ headless experiment from a JSON preset.")
    parser.add_argument("preset", nargs="?", type=Path, help="Path to a preset JSON file.")
    parser.add_argument("--render", action="store_true",
                        help="Export particles, render, then delete the raw particle dumps.")
    parser.add_argument("--keep-particles", action="store_true",
                        help="With --render, keep the raw particle_frames.bin files.")
    parser.add_argument("--output", type=Path, help="Override the output directory.")
    parser.add_argument("--force", action="store_true", help="Rerun methods even if diagnostics already exist.")
    parser.add_argument("--parallel", type=int, default=None,
                        help="Maximum simulation and render jobs to run concurrently "
                             "(default: about one per physical core).")
    parser.add_argument("--headless", type=Path, help="Override the headless executable path for this run.")
    parser.add_argument("--list", action="store_true", help="List available presets and exit.")
    args = parser.parse_args()

    if args.list or args.preset is None:
        _list_presets()
        return 0 if args.list else 2

    if args.headless:
        os.environ["FLUID_PROFILE_EXE"] = str(args.headless.resolve())

    try:
        orchestrate(
            args.preset,
            output_dir=args.output,
            render=args.render,
            force=args.force,
            keep_particles=args.keep_particles,
            max_parallel=args.parallel,
        )
    except OrchestratorError as exc:
        parser.exit(status=1, message=f"error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
