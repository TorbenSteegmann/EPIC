"""Renderer / post-processing module.

Separate from the orchestrator: it only consumes outputs a headless run already
produced. Two-dimensional exports use the existing Python projection renderer;
three-dimensional exports use ``Fluid_Particle_Renderer`` for the locked OpenGL
camera and then FFmpeg for MP4 assembly. Callable standalone or from the
orchestrator.

Render options (read from a preset's ``render`` block, all optional):
    color             speed | solid | ringing      (default speed)
    width, height     output pixels                 (default 960x960)
    fps               playback override             (default: export fps)
    video             write an MP4 (needs ffmpeg)   (default true)
    snapshot_interval seconds between PNG snapshots (default: off)

Standalone:
    python render_results.py outputs/run_dir
    python render_results.py outputs/run_dir --color ringing --snapshot-interval 5
    python render_results.py outputs/run_dir --no-video --snapshot-interval 2
"""

from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Any

import paths
from config_schema import VALID_RENDER_COLORS, ResolvedPreset, validate


_PARTICLE_HEADER = struct.Struct("<8sIIIIdddddiiif")
_FRAME_HEADER = struct.Struct("<ddII")
_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def _default_parallelism(job_count: int) -> int:
    """Match the simulation pool: about one worker per physical core."""

    cores = os.cpu_count() or 2
    return max(1, min(job_count, cores // 2 or 1))


def _particle_export_index(path: Path) -> tuple[int, float, list[tuple[float, float, int]]]:
    """Return dimension, export FPS and frame timing without loading particles."""

    with path.open("rb") as stream:
        raw = stream.read(_PARTICLE_HEADER.size)
        if len(raw) != _PARTICLE_HEADER.size:
            raise RuntimeError(f"Particle export header is incomplete: {path}")
        header = _PARTICLE_HEADER.unpack(raw)
        if header[0] != b"FLDPART1" or header[1] != 1:
            raise RuntimeError(f"Unsupported particle export: {path}")
        dimension, frame_count, record_size, fps = int(header[2]), int(header[3]), int(header[4]), float(header[5])
        frames: list[tuple[float, float, int]] = []
        for frame_index in range(frame_count):
            raw_frame = stream.read(_FRAME_HEADER.size)
            if len(raw_frame) != _FRAME_HEADER.size:
                raise RuntimeError(f"Particle export ended at frame {frame_index}: {path}")
            frame_time, simulation_time, particle_count, _ = _FRAME_HEADER.unpack(raw_frame)
            frames.append((frame_time, simulation_time, frame_index))
            stream.seek(int(particle_count) * record_size, 1)
    return dimension, fps, frames


def _ffmpeg_path() -> str:
    executable = shutil.which("ffmpeg")
    if not executable:
        try:
            import imageio_ffmpeg

            executable = imageio_ffmpeg.get_ffmpeg_exe()
        except ImportError:
            pass
    if not executable:
        raise RuntimeError("FFmpeg was not found. Install ffmpeg or imageio-ffmpeg to create MP4 output.")
    return executable


def _snapshot_tag(seconds: float) -> str:
    if abs(seconds - round(seconds)) < 1.0e-6:
        return f"{int(round(seconds)):04d}s"
    return f"{seconds:07.3f}s".replace("-", "m").replace(".", "p")


def _write_snapshots(
    frames_dir: Path,
    frames: list[tuple[float, float, int]],
    destination: Path,
    interval: float,
) -> int:
    if interval <= 0.0:
        raise ValueError("snapshot_interval must be positive")
    if not frames:
        return 0
    destination.mkdir(parents=True, exist_ok=True)
    for old_snapshot in destination.glob("snapshot_*.png"):
        old_snapshot.unlink()

    end_time = frames[-1][0]
    requested: list[float] = []
    value = 0.0
    epsilon = interval * 1.0e-6
    while value <= end_time + epsilon:
        requested.append(min(value, end_time))
        value += interval
    if abs(requested[-1] - end_time) > max(1.0e-6, interval * 1.0e-3):
        requested.append(end_time)

    copied = 0
    used: set[int] = set()
    for requested_time in requested:
        frame_time, simulation_time, frame_index = min(frames, key=lambda frame: abs(frame[0] - requested_time))
        if frame_index in used:
            continue
        used.add(frame_index)
        source = frames_dir / f"frame_{frame_index:06d}.png"
        target = destination / (
            f"snapshot_t{_snapshot_tag(requested_time)}_frame_{frame_index:06d}_sim_{_snapshot_tag(simulation_time)}.png"
        )
        shutil.copy2(source, target)
        copied += 1
    return copied


def _encode_mp4(frames_dir: Path, output: Path, fps: float) -> subprocess.CompletedProcess[str]:
    if not math.isfinite(fps) or fps <= 0.0:
        raise ValueError("render FPS must be positive")
    command = [
        _ffmpeg_path(), "-y", "-framerate", f"{fps:.12g}",
        "-i", str(frames_dir / "frame_%06d.png"),
        "-c:v", "libx264", "-crf", "18", "-pix_fmt", "yuv420p", str(output),
    ]
    return subprocess.run(command, capture_output=True, text=True, creationflags=_NO_WINDOW)


def _render_one_python(particle_bin: Path, options: dict[str, Any], log) -> None:
    if not paths.RENDER_SCRIPT.is_file():
        raise FileNotFoundError(
            f"Renderer script not found: {paths.RENDER_SCRIPT}. "
            "2D rendering uses Orchestrator/render_particle_video.py."
        )

    command = [
        sys.executable,
        str(paths.RENDER_SCRIPT),
        str(particle_bin),
        "--color", options["color"],
        "--width", str(options["width"]),
        "--height", str(options["height"]),
        "--projection", options["projection"],
    ]
    if options.get("fps") is not None:
        command += ["--fps", f"{float(options['fps']):.12g}"]

    video = bool(options.get("video", True))
    interval = options.get("snapshot_interval")
    if video:
        command += ["--out", str(particle_bin.with_name("particle_video.mp4")), "--stream"]
    elif interval:
        command += ["--frames-only"]
    else:
        return  

    if interval:
        command += [
            "--snapshot-dir", str(particle_bin.with_name("snapshots")),
            "--snapshot-interval", f"{float(interval):.12g}",
        ]

    log(f"render: {' '.join(command)}")
    result = subprocess.run(command, capture_output=True, text=True,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    particle_bin.with_name("render.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode != 0:
        raise RuntimeError(
            f"Renderer failed for {particle_bin} (exit {result.returncode}); "
            f"see {particle_bin.with_name('render.log')}."
        )

    if not video and interval:
        frames_dir = particle_bin.with_name("particle_video_frames")
        if frames_dir.is_dir():
            shutil.rmtree(frames_dir, ignore_errors=True)
            log(f"render: kept snapshots only (removed {frames_dir.name})")
    log(f"render: done -> {particle_bin.parent}")


def _render_one_cpp(particle_bin: Path, options: dict[str, Any], log) -> None:
    renderer = paths.particle_renderer_executable()
    if not renderer.is_file():
        raise FileNotFoundError(
            f"C++ particle renderer not found: {renderer}. Build the Fluid_Particle_Renderer target first."
        )

    dimension, export_fps, frames = _particle_export_index(particle_bin)
    if dimension != 3:
        raise ValueError(f"C++ replay renderer requires a 3D export, got dimension {dimension}")

    frames_dir = particle_bin.with_name("particle_video_frames")
    command = [
        str(renderer), str(particle_bin),
        "--frames-dir", str(frames_dir),
        "--width", str(options["width"]),
        "--height", str(options["height"]),
    ]
    log(f"render: {' '.join(command)}")
    render_result = subprocess.run(
        command, cwd=renderer.parent, capture_output=True, text=True, creationflags=_NO_WINDOW
    )
    combined_log = render_result.stdout + render_result.stderr
    render_log = particle_bin.with_name("render.log")

    try:
        if render_result.returncode != 0:
            raise RuntimeError(
                f"C++ renderer failed for {particle_bin} (exit {render_result.returncode}); see {render_log}."
            )

        interval = options.get("snapshot_interval")
        if interval:
            copied = _write_snapshots(frames_dir, frames, particle_bin.with_name("snapshots"), float(interval))
            combined_log += f"Wrote {copied} snapshot(s).\n"

        if bool(options.get("video", True)):
            video_path = particle_bin.with_name("particle_video.mp4")
            playback_fps = float(options["fps"]) if options.get("fps") is not None else export_fps
            encode_result = _encode_mp4(frames_dir, video_path, playback_fps)
            combined_log += encode_result.stdout + encode_result.stderr
            if encode_result.returncode != 0:
                raise RuntimeError(f"FFmpeg failed for {particle_bin}; see {render_log}.")
            combined_log += f"Wrote video to {video_path}.\n"
    finally:
        render_log.write_text(combined_log, encoding="utf-8")
        if frames_dir.is_dir():
            shutil.rmtree(frames_dir, ignore_errors=True)
            log(f"render: removed transient {frames_dir.name}")

    log(f"render: done -> {particle_bin.parent}")


def render_output_dir(
    output_dir: Path,
    preset: ResolvedPreset | None = None,
    *,
    overrides: dict[str, Any] | None = None,
    log_handle=None,
    max_parallel: int | None = None,
) -> list[Path]:
    """Render particle exports concurrently, with one independent job per method."""

    output_dir = Path(output_dir)
    log_lock = threading.Lock()

    def log(message: str) -> None:
        with log_lock:
            print(message, flush=True)
            if log_handle is not None:
                log_handle.write(message + "\n")
                log_handle.flush()

    cfg: dict[str, Any] = dict(preset.render) if preset is not None else {}
    cfg.update({k: v for k, v in (overrides or {}).items() if v is not None})

    options = {
        "color": cfg.get("color", "speed"),
        "width": int(cfg.get("width", 960)),
        "height": int(cfg.get("height", 960)),
        "projection": cfg.get("projection", "xy"),
        "fps": cfg.get("fps"),
        "video": bool(cfg.get("video", True)),
        "snapshot_interval": cfg.get("snapshot_interval"),
    }
    if options["color"] not in VALID_RENDER_COLORS:
        raise ValueError(f"color '{options['color']}' invalid; options: {', '.join(VALID_RENDER_COLORS)}")

    if not options["video"] and not options["snapshot_interval"]:
        log("render: neither MP4 nor snapshots enabled; nothing to render.")
        return []

    bins = sorted((output_dir / "runs").glob("*/particle_frames.bin"))
    if not bins:
        log(
            "render: no particle_frames.bin found. The preset likely ran with "
            "export_particles=false; enable it to produce renderable particle data."
        )
        return []

    workers = (
        max(1, min(len(bins), max_parallel))
        if max_parallel
        else _default_parallelism(len(bins))
    )
    log(f"render: rendering {len(bins)} method(s), up to {workers} in parallel")

    def render_one(particle_bin: Path) -> Path:
        dimension, _, _ = _particle_export_index(particle_bin)
        if dimension == 3:
            _render_one_cpp(particle_bin, options, log)
        else:
            _render_one_python(particle_bin, options, log)
        return particle_bin.parent

    rendered_by_bin: dict[Path, Path] = {}
    failures: list[str] = []
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(render_one, particle_bin): particle_bin for particle_bin in bins}
        for future in as_completed(futures):
            particle_bin = futures[future]
            try:
                rendered_by_bin[particle_bin] = future.result()
            except Exception as exc:
                failures.append(f"{particle_bin.parent.name}: {exc}")

    if failures:
        raise RuntimeError("Render job(s) failed:\n  " + "\n  ".join(failures))

    return [rendered_by_bin[particle_bin] for particle_bin in bins]


def _load_preset_from_output(output_dir: Path) -> ResolvedPreset | None:
    preset_file = output_dir / "preset.json"
    if preset_file.is_file():
        try:
            return validate(json.loads(preset_file.read_text(encoding="utf-8")))
        except (OSError, ValueError):
            return None
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Render/post-process an orchestrator output directory.")
    parser.add_argument("output_dir", type=Path, help="An orchestrator output directory (contains runs/).")
    parser.add_argument("--color", choices=VALID_RENDER_COLORS, help="Particle colouring (overrides preset).")
    parser.add_argument("--fps", type=float, help="Playback FPS override.")
    parser.add_argument("--width", type=int, help="Output width in pixels.")
    parser.add_argument("--height", type=int, help="Output height in pixels.")
    parser.add_argument("--snapshot-interval", type=float, help="Seconds between PNG snapshots (omit to disable).")
    parser.add_argument("--no-video", action="store_true", help="Skip the MP4 (PNG frames/snapshots only, no ffmpeg).")
    parser.add_argument("--parallel", type=int, default=None,
                        help="Maximum render jobs to run concurrently (default: about one per physical core).")
    args = parser.parse_args()

    if not args.output_dir.is_dir():
        parser.exit(1, f"error: output directory not found: {args.output_dir}\n")

    overrides = {
        "color": args.color,
        "fps": args.fps,
        "width": args.width,
        "height": args.height,
        "snapshot_interval": args.snapshot_interval,
        "video": False if args.no_video else None,
    }
    try:
        render_output_dir(
            args.output_dir,
            _load_preset_from_output(args.output_dir),
            overrides=overrides,
            max_parallel=args.parallel,
        )
    except (FileNotFoundError, RuntimeError, ValueError) as exc:
        parser.exit(1, f"error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
