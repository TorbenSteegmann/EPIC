#!/usr/bin/env python3
"""Render headless particle frame exports to PNG frames and MP4.

Examples:
    python render_particle_video.py outputs/tgv/particle_frames.bin --frames-only
    python render_particle_video.py outputs/tgv/particle_frames.bin --out outputs/tgv/tgv.mp4
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path


MAGIC = b"FLDPART1"
HEADER = struct.Struct("<8sIIIIdddddiiif")
FRAME_HEADER = struct.Struct("<ddII")
_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


@dataclass
class Export_Header:
    version: int
    dimension: int
    frame_count: int
    record_size: int
    fps: float
    domain_width: float
    domain_height: float
    domain_depth: float
    dx: float
    scene: int
    solver: int
    polypic_modes: int
    flip_percent: float


@dataclass
class Frame_Index:
    frame_time: float
    simulation_time: float
    particle_count: int
    data_offset: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Render particle_frames.bin to PNG frames and an optional MP4.")
    parser.add_argument("input", type=Path, help="particle_frames.bin from Fluid_Simulation_Profile")
    parser.add_argument("--out", type=Path, help="MP4 output path. Defaults to <input-dir>/particle_video.mp4")
    parser.add_argument("--frames-dir", type=Path, help="PNG directory. Defaults to <input-dir>/particle_video_frames")
    parser.add_argument("--frames-only", action="store_true", help="Write PNG frames without invoking FFmpeg")
    parser.add_argument("--fps", type=float, help="Playback FPS override. Defaults to the export FPS")
    parser.add_argument("--duration", type=float, help="Exact playback duration in seconds; trims excess endpoint frames")
    parser.add_argument("--width", type=int, default=960, help="Output width in pixels")
    parser.add_argument("--height", type=int, default=960, help="Output height in pixels")
    parser.add_argument("--projection", choices=("xy", "xz", "yz"), default="xy", help="Axes used for 3D exports")
    parser.add_argument("--color", choices=("speed", "solid", "ringing"), default="speed", help="Particle coloring")
    parser.add_argument("--solid-color", default="#3380ff", help="Particle color for --color solid")
    parser.add_argument("--max-speed", type=float, help="Speed mapped to red. Defaults to the maximum over all frames")
    parser.add_argument("--ringing-window", type=int, default=5, help="Frame window for render-only ringing coloring")
    parser.add_argument("--ringing-min-reversals", type=int, default=3, help="Strong reversals required inside --ringing-window")
    parser.add_argument("--ringing-speed-floor", type=float, default=0.05, help="Absolute speed gate for render-only ringing coloring")
    parser.add_argument("--ringing-speed-max-fraction", type=float, default=0.01, help="Additional speed gate as a fraction of --max-speed")
    parser.add_argument("--ringing-delta-floor", type=float, default=0.05, help="Absolute velocity-change gate for render-only ringing coloring")
    parser.add_argument("--ringing-delta-max-fraction", type=float, default=0.01, help="Additional velocity-change gate as a fraction of --max-speed")
    parser.add_argument("--radius-scale", type=float, default=1.0, help="Rendered particle-radius multiplier")
    parser.add_argument("--show-time", action="store_true", help="Draw simulation time in the upper-left corner")
    parser.add_argument("--snapshot-dir", type=Path, help="Directory for periodic PNG snapshots")
    parser.add_argument("--snapshot-interval", type=float, help="Write one PNG snapshot every N simulated seconds")
    parser.add_argument("--ffmpeg", type=Path, help="FFmpeg executable path")
    parser.add_argument("--crf", type=int, default=18, help="H.264 CRF quality value")
    parser.add_argument("--stream", action="store_true", help="Stream RGB frames directly to FFmpeg instead of storing PNG files")
    return parser.parse_args()


def require_dependencies():
    try:
        import numpy as np
        from PIL import Image, ImageColor, ImageDraw
    except ImportError as exc:
        raise SystemExit("numpy and Pillow are required to render particle videos.") from exc
    return np, Image, ImageColor, ImageDraw


class Particle_Export:
    def __init__(self, path: Path, np) -> None:
        self.path = path
        self.np = np
        self.stream = path.open("rb")
        self.header = self._read_header()
        self.particle_dtype = np.dtype(
            [
                ("entity", "<u4"),
                ("radius", "<f4"),
                ("position", "<f4", (3,)),
                ("velocity", "<f4", (3,)),
            ]
        )
        if self.particle_dtype.itemsize != self.header.record_size:
            raise SystemExit(
                f"Unsupported particle record size {self.header.record_size}; expected {self.particle_dtype.itemsize}."
            )
        self.frames = self._index_frames()

    def _read_header(self) -> Export_Header:
        raw = self.stream.read(HEADER.size)
        if len(raw) != HEADER.size:
            raise SystemExit(f"{self.path} is too short to contain a particle export header.")
        values = HEADER.unpack(raw)
        if values[0] != MAGIC:
            raise SystemExit(f"{self.path} is not a supported particle frame export.")
        return Export_Header(*values[1:])

    def _index_frames(self) -> list[Frame_Index]:
        frames = []
        for frame_index in range(self.header.frame_count):
            raw = self.stream.read(FRAME_HEADER.size)
            if len(raw) != FRAME_HEADER.size:
                raise SystemExit(f"Particle export ended while reading frame {frame_index}.")
            frame_time, simulation_time, particle_count, _ = FRAME_HEADER.unpack(raw)
            data_offset = self.stream.tell()
            frames.append(Frame_Index(frame_time, simulation_time, particle_count, data_offset))
            self.stream.seek(particle_count * self.header.record_size, 1)
        return frames

    def read_frame(self, index: int):
        frame = self.frames[index]
        self.stream.seek(frame.data_offset)
        particles = self.np.fromfile(self.stream, dtype=self.particle_dtype, count=frame.particle_count)
        if len(particles) != frame.particle_count:
            raise SystemExit(f"Particle export ended while reading frame {index}.")
        return frame, particles

    def close(self) -> None:
        self.stream.close()


def parse_color(ImageColor, value: str) -> tuple[int, int, int]:
    try:
        return ImageColor.getrgb(value)
    except ValueError as exc:
        raise SystemExit(f"Invalid color '{value}'. Use a name or #RRGGBB.") from exc


def projection_axes(projection: str) -> tuple[int, int]:
    return {"xy": (0, 1), "xz": (0, 2), "yz": (1, 2)}[projection]


def projection_extent(header: Export_Header, projection: str) -> tuple[float, float]:
    extents = (header.domain_width, header.domain_height, header.domain_depth)
    axis_x, axis_y = projection_axes(projection)
    width = extents[axis_x]
    height = extents[axis_y]
    if width <= 0.0 or height <= 0.0:
        raise SystemExit(f"Projection {projection} is unavailable for this {header.dimension}D export.")
    return width, height


def find_max_speed(export: Particle_Export, np) -> float:
    max_speed = 0.0
    for index in range(len(export.frames)):
        _, particles = export.read_frame(index)
        if len(particles) == 0:
            continue
        speeds = np.linalg.norm(particles["velocity"], axis=1)
        max_speed = max(max_speed, float(speeds.max(initial=0.0)))
    return max(max_speed, 1.0e-12)


def speed_colors(np, velocities, max_speed: float):
    speeds = np.linalg.norm(velocities, axis=1)
    t = np.clip(speeds / max_speed, 0.0, 1.0)[:, None]
    blue = np.array([51.0, 128.0, 255.0])
    red = np.array([255.0, 51.0, 0.0])
    return np.rint(blue * (1.0 - t) + red * t).astype(np.uint8)


def ringing_colors(np, scores):
    t = np.clip(scores, 0.0, 1.0)[:, None]
    quiet = np.array([70.0, 90.0, 130.0])
    ringing = np.array([255.0, 185.0, 30.0])
    return np.rint(quiet * (1.0 - t) + ringing * t).astype(np.uint8)


def compute_ringing_scores(args: argparse.Namespace, export: Particle_Export, np, max_speed: float) -> list:
    if args.ringing_window <= 0:
        raise SystemExit("--ringing-window must be positive.")
    if args.ringing_min_reversals <= 0 or args.ringing_min_reversals > args.ringing_window:
        raise SystemExit("--ringing-min-reversals must be in the range [1, --ringing-window].")
    if (
        args.ringing_speed_floor < 0.0
        or args.ringing_delta_floor < 0.0
        or args.ringing_speed_max_fraction < 0.0
        or args.ringing_delta_max_fraction < 0.0
    ):
        raise SystemExit("ringing floors and max-speed fractions must be non-negative.")

    speed_floor = max(args.ringing_speed_floor, args.ringing_speed_max_fraction * max_speed)
    delta_floor = max(args.ringing_delta_floor, args.ringing_delta_max_fraction * max_speed)
    previous_velocities: dict[int, object] = {}
    previous_deltas: dict[int, object] = {}
    windows: dict[int, list[bool]] = {}
    scores_by_frame = []

    for index in range(len(export.frames)):
        _, particles = export.read_frame(index)
        scores = np.zeros(len(particles), dtype=np.uint8)
        seen: set[int] = set()
        for particle_index, entity_value in enumerate(particles["entity"]):
            entity = int(entity_value)
            seen.add(entity)
            velocity = particles["velocity"][particle_index].astype(np.float64)
            previous = previous_velocities.get(entity)
            alternating_acceleration = False
            if previous is not None:
                speed = float(np.linalg.norm(velocity))
                previous_speed = float(np.linalg.norm(previous))
                delta = velocity - previous
                delta_length = float(np.linalg.norm(delta))
                previous_delta = previous_deltas.get(entity)
                if previous_delta is not None:
                    previous_delta_length = float(np.linalg.norm(previous_delta))
                    active = (
                        speed >= speed_floor
                        and previous_speed >= speed_floor
                        and delta_length >= delta_floor
                        and previous_delta_length >= delta_floor
                    )
                    if active:
                        alternating_acceleration = float(np.dot(delta, previous_delta)) <= -0.35 * delta_length * previous_delta_length
                previous_deltas[entity] = delta

            window = windows.setdefault(entity, [])
            window.append(alternating_acceleration)
            if len(window) > args.ringing_window:
                del window[0]
            if len(window) >= args.ringing_window and sum(window) >= args.ringing_min_reversals:
                scores[particle_index] = 1.0
            previous_velocities[entity] = velocity

        for entity in list(previous_velocities.keys()):
            if entity not in seen:
                del previous_velocities[entity]
                previous_deltas.pop(entity, None)
                windows.pop(entity, None)
        scores_by_frame.append(scores)

    print(
        "Computed render ringing masks from alternating acceleration "
        f"(window={args.ringing_window}, min_reversals={args.ringing_min_reversals}, "
        f"speed_floor={speed_floor:.6g}, delta_floor={delta_floor:.6g})"
    )
    return scores_by_frame


def render_setup(args: argparse.Namespace, export: Particle_Export, ImageColor) -> tuple:
    if args.width <= 0 or args.height <= 0:
        raise SystemExit("--width and --height must be positive.")
    if args.radius_scale <= 0.0:
        raise SystemExit("--radius-scale must be positive.")

    domain_width, domain_height = projection_extent(export.header, args.projection)
    axis_x, axis_y = projection_axes(args.projection)
    margin = 28
    scale = min((args.width - 2 * margin) / domain_width, (args.height - 2 * margin) / domain_height)
    draw_width = domain_width * scale
    draw_height = domain_height * scale
    left = (args.width - draw_width) * 0.5
    top = (args.height - draw_height) * 0.5

    return domain_width, domain_height, axis_x, axis_y, margin, scale, draw_width, draw_height, left, top, parse_color(ImageColor, args.solid_color)


def render_image(
    args: argparse.Namespace,
    export: Particle_Export,
    frame_index: int,
    setup: tuple,
    max_speed: float,
    ringing_scores_by_frame,
    np,
    Image,
    ImageDraw,
):
    _, _, axis_x, axis_y, margin, scale, draw_width, draw_height, left, top, solid_color = setup
    frame, particles = export.read_frame(frame_index)
    image = Image.new("RGB", (args.width, args.height), (238, 238, 238))
    draw = ImageDraw.Draw(image)
    draw.rectangle((left, top, left + draw_width, top + draw_height), outline=(10, 10, 10), width=4)

    if len(particles) > 0:
        positions = particles["position"]
        screen_x = np.rint(left + positions[:, axis_x] * scale).astype(np.int32)
        screen_y = np.rint(top + draw_height - positions[:, axis_y] * scale).astype(np.int32)
        radius = max(1, int(round(float(np.median(particles["radius"])) * scale * args.radius_scale)))
        if args.color == "speed":
            colors = speed_colors(np, particles["velocity"], max_speed)
        elif args.color == "ringing":
            colors = ringing_colors(np, ringing_scores_by_frame[frame_index])
        else:
            colors = np.tile(np.array(solid_color, dtype=np.uint8), (len(particles), 1))
        pixels = np.array(image, copy=True)
        offsets = np.asarray(
            [(dx, dy) for dy in range(-radius, radius + 1) for dx in range(-radius, radius + 1) if dx * dx + dy * dy <= radius * radius],
            dtype=np.int32,
        )
        for dx, dy in offsets:
            x = screen_x + dx
            y = screen_y + dy
            valid = (x >= 0) & (x < args.width) & (y >= 0) & (y < args.height)
            pixels[y[valid], x[valid]] = colors[valid]
        image = Image.fromarray(pixels, "RGB")

    if args.show_time:
        draw = ImageDraw.Draw(image)
        draw.text((margin + 6, margin + 6), f"t = {frame.simulation_time:.3f}", fill=(15, 15, 15))
    return image


def snapshot_tag(seconds: float) -> str:
    if abs(seconds - round(seconds)) < 1.0e-6:
        return f"{int(round(seconds)):04d}s"
    return f"{seconds:07.3f}s".replace("-", "m").replace(".", "p")


def snapshot_targets(args: argparse.Namespace, export: Particle_Export) -> list[tuple[float, int]]:
    if args.snapshot_interval is None:
        return []
    if args.snapshot_interval <= 0.0:
        raise SystemExit("--snapshot-interval must be positive.")
    if not export.frames:
        return []

    end_time = args.duration if args.duration is not None else export.frames[-1].frame_time
    if end_time <= 0.0:
        return [(0.0, 0)]

    requested_times: list[float] = []
    time_value = 0.0
    epsilon = args.snapshot_interval * 1.0e-6
    while time_value <= end_time + epsilon:
        requested_times.append(min(time_value, end_time))
        time_value += args.snapshot_interval
    if abs(requested_times[-1] - end_time) > max(1.0e-6, args.snapshot_interval * 1.0e-3):
        requested_times.append(end_time)

    targets: list[tuple[float, int]] = []
    used_indices: set[int] = set()
    frame_times = [frame.frame_time for frame in export.frames]
    for requested_time in requested_times:
        frame_index = min(range(len(frame_times)), key=lambda index: abs(frame_times[index] - requested_time))
        if frame_index in used_indices:
            continue
        used_indices.add(frame_index)
        targets.append((requested_time, frame_index))
    return targets


def render_snapshots(
    args: argparse.Namespace, export: Particle_Export, setup: tuple, max_speed: float, ringing_scores_by_frame, np, Image, ImageDraw
) -> Path | None:
    targets = snapshot_targets(args, export)
    if not targets:
        return None

    snapshots_dir = (args.snapshot_dir or export.path.parent / "snapshots").resolve()
    snapshots_dir.mkdir(parents=True, exist_ok=True)
    for old_snapshot in snapshots_dir.glob("snapshot_*.png"):
        old_snapshot.unlink()

    for requested_time, frame_index in targets:
        image = render_image(args, export, frame_index, setup, max_speed, ringing_scores_by_frame, np, Image, ImageDraw)
        frame = export.frames[frame_index]
        image.save(snapshots_dir / f"snapshot_t{snapshot_tag(requested_time)}_frame_{frame_index:06d}_sim_{snapshot_tag(frame.simulation_time)}.png")

    print(f"Wrote {len(targets)} snapshots to {snapshots_dir}")
    return snapshots_dir


def render_frames(args: argparse.Namespace, export: Particle_Export, np, Image, ImageColor, ImageDraw) -> Path:
    frames_dir = (args.frames_dir or export.path.parent / "particle_video_frames").resolve()
    frames_dir.mkdir(parents=True, exist_ok=True)
    for old_frame in frames_dir.glob("frame_*.png"):
        old_frame.unlink()

    setup = render_setup(args, export, ImageColor)
    max_speed = args.max_speed if args.max_speed is not None else find_max_speed(export, np)
    if max_speed <= 0.0:
        raise SystemExit("--max-speed must be positive.")
    ringing_scores_by_frame = compute_ringing_scores(args, export, np, max_speed) if args.color == "ringing" else None
    frame_count = output_frame_count(args, export)
    print(f"Rendering {frame_count} frames with max_speed={max_speed:.6g}")

    for index in range(frame_count):
        image = render_image(args, export, index, setup, max_speed, ringing_scores_by_frame, np, Image, ImageDraw)
        image.save(frames_dir / f"frame_{index:06d}.png")

    render_snapshots(args, export, setup, max_speed, ringing_scores_by_frame, np, Image, ImageDraw)
    return frames_dir


def ffmpeg_path(args: argparse.Namespace) -> str:
    ffmpeg = str(args.ffmpeg.resolve()) if args.ffmpeg else shutil.which("ffmpeg")
    if not ffmpeg:
        try:
            import imageio_ffmpeg

            ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
        except ImportError:
            pass
    if not ffmpeg:
        raise SystemExit("FFmpeg was not found. Install it, install imageio-ffmpeg, or pass --ffmpeg PATH.")
    return ffmpeg


def output_frame_count(args: argparse.Namespace, export: Particle_Export) -> int:
    if args.duration is None:
        return len(export.frames)
    fps = args.fps if args.fps is not None else export.header.fps
    if args.duration <= 0.0:
        raise SystemExit("--duration must be positive.")
    return min(len(export.frames), max(1, int(round(args.duration * fps))))


def stream_mp4(args: argparse.Namespace, export: Particle_Export, np, Image, ImageColor, ImageDraw) -> Path:
    fps = args.fps if args.fps is not None else export.header.fps
    if fps <= 0.0:
        raise SystemExit("--fps must be positive.")
    output = (args.out or export.path.parent / "particle_video.mp4").resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    setup = render_setup(args, export, ImageColor)
    max_speed = args.max_speed if args.max_speed is not None else find_max_speed(export, np)
    if max_speed <= 0.0:
        raise SystemExit("--max-speed must be positive.")
    ringing_scores_by_frame = compute_ringing_scores(args, export, np, max_speed) if args.color == "ringing" else None

    command = [
        ffmpeg_path(args),
        "-y",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgb24",
        "-s:v",
        f"{args.width}x{args.height}",
        "-r",
        f"{fps:.12g}",
        "-i",
        "-",
        "-an",
        "-c:v",
        "libx264",
        "-crf",
        str(args.crf),
        "-pix_fmt",
        "yuv420p",
        str(output),
    ]
    frame_count = output_frame_count(args, export)
    print(f"Streaming {frame_count} frames with max_speed={max_speed:.6g}")
    process = subprocess.Popen(command, stdin=subprocess.PIPE, creationflags=_NO_WINDOW)
    try:
        assert process.stdin is not None
        for index in range(frame_count):
            image = render_image(args, export, index, setup, max_speed, ringing_scores_by_frame, np, Image, ImageDraw)
            process.stdin.write(image.tobytes())
        process.stdin.close()
        return_code = process.wait()
    except BaseException:
        process.kill()
        process.wait()
        raise
    if return_code != 0:
        raise SystemExit(f"FFmpeg failed with exit code {return_code}.")
    render_snapshots(args, export, setup, max_speed, ringing_scores_by_frame, np, Image, ImageDraw)
    return output


def encode_mp4(args: argparse.Namespace, export: Particle_Export, frames_dir: Path) -> Path:
    fps = args.fps if args.fps is not None else export.header.fps
    if fps <= 0.0:
        raise SystemExit("--fps must be positive.")

    ffmpeg = ffmpeg_path(args)
    output = (args.out or export.path.parent / "particle_video.mp4").resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    command = [
        ffmpeg,
        "-y",
        "-framerate",
        f"{fps:.12g}",
        "-i",
        str(frames_dir / "frame_%06d.png"),
        "-c:v",
        "libx264",
        "-crf",
        str(args.crf),
        "-pix_fmt",
        "yuv420p",
    ]
    if args.duration is not None:
        command.extend(("-frames:v", str(output_frame_count(args, export))))
    command.append(str(output))
    subprocess.run(command, check=True, creationflags=_NO_WINDOW)
    return output


def main() -> None:
    args = parse_args()
    np, Image, ImageColor, ImageDraw = require_dependencies()
    input_path = args.input.resolve()
    if not input_path.exists():
        raise SystemExit(f"Particle export not found: {input_path}")

    export = Particle_Export(input_path, np)
    try:
        if args.stream:
            if args.frames_only:
                raise SystemExit("--stream and --frames-only cannot be combined.")
            output = stream_mp4(args, export, np, Image, ImageColor, ImageDraw)
            print(f"Wrote video to {output}")
        else:
            frames_dir = render_frames(args, export, np, Image, ImageColor, ImageDraw)
            print(f"Wrote PNG frames to {frames_dir}")
        if not args.frames_only and not args.stream:
            output = encode_mp4(args, export, frames_dir)
            print(f"Wrote video to {output}")
    finally:
        export.close()


if __name__ == "__main__":
    main()
