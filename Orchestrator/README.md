# Experiment Orchestrator

One unified workflow for running the thesis fluid and MPM experiments. The C++
headless solvers remain the simulation authority; JSON
presets are the single source of truth for configuration. Python only
orchestrates: it validates presets, builds the headless command line, launches
runs, organises outputs, and calls the existing renderer.

## Layout

```
Orchestrator/
├─ src/                    all Python source
│  ├─ orchestrator.py      core: load preset, run methods, organise outputs, optional render
│  ├─ run_experiment.py    command-line entry point
│  ├─ render_results.py    frame-replay and final video orchestration
│  ├─ json_editor.py       tkinter GUI preset editor
│  ├─ diagnostics_viewer.py result discovery + plotting (with diagnostics_*.py)
│  ├─ config_schema.py     scene/method catalogue, validation, JSON->flags translation
│  └─ paths.py             all filesystem locations (discovered, no hardcoded abs paths)
├─ presets/               experiment presets (the source of truth)
│  ├─ final_experiment.json, settling_pool.json, ringing_instability_metric.json
│  ├─ taylor_green.json, confined_vortex.json, dam_break.json
│  └─ compressed_hyperelastic_square.json, 3d_dam_break.json, smoke_test.json
├─ render_particle_video.py  2D particle-frame replay -> video / snapshots
├─ outputs/               run outputs
├─ logs/                  per-run logs (git-ignored)
├─ current.json           GUI working file (git-ignored)
├─ orchestrator.bat       launch the GUI editor (Windows)
└─ orchestrator.sh        launch the GUI editor (Linux/macOS)
```

## Running

Open the editor (Windows; on Linux/macOS run `./orchestrator.sh`):

```
orchestrator.bat
```

The window has two top-level tabs. **Orchestrator / Runner** is the existing
preset editor and run queue. **Diagnostics Viewer** discovers completed results
under `outputs/` and plots them without rerunning or changing the experiment.

### Diagnostics Viewer

1. Open **Diagnostics Viewer** and select a discovered result, or use
   **Browse...** to select a result folder containing `runs/<METHOD>/`.
2. Choose the X and Y diagnostic series and tick the transfer modes to compare.
3. Edit axis labels, choose linear/log/symlog scales, and optionally enter axis
   limits. **Reset / Autoscale** restores data-derived limits.
4. Use **Generate / Update Plot**, choose an export format (SVG by default), and
   click **Save Plot...**. PNG exports use 300 dpi.

Transfer modes and numeric columns are discovered from each result. Bad or
missing files are skipped with an explanatory status message; supported
diagnostic data files are `diagnostics.csv` and row-oriented
`diagnostics.json`.

The series menus also include entries prefixed with **Calculated**. These are
computed in memory from available exported columns (for example normalised
energy, angular-momentum retention, transfer-energy changes, fluid-measure
drift, and scene-normalised residual energy). They are available only when
their source columns exist and are never written back to the diagnostics.

Command line (runs the preset file directly):

```
python src/run_experiment.py presets/settling_pool.json
python src/run_experiment.py presets/dam_break.json --render
python src/run_experiment.py presets/settling_pool.json --render --keep-particles
python src/run_experiment.py presets/taylor_green.json --output outputs/tgv_manual --force
python src/run_experiment.py presets/compressed_hyperelastic_square.json --render
python src/run_experiment.py --list
python src/render_results.py outputs/settling_pool/<timestamp> --color ringing --snapshot-interval 5
```

### Editor save/run model

- **Load Preset** loads a file into the form as a *template*; the original file
  is never modified again unless you explicitly **Save As Preset**.
- The form's live state is written to a single working file, **`current.json`**.
  The GUI starts on `current.json` (default name `current`) so you resume where
  you left off.
- **Save (current)** writes the form to `current.json`.
- **Run** writes the form to `current.json` and runs *that*, so a run always uses
  what is selected, not a stale file on disk. It renders when the preset's render
  settings produce an MP4 or snapshots, and otherwise runs headless (one button
  covers both cases).
- **Save As Preset** is the only action that writes/overwrites a named preset.
- A run shows an animated progress bar + status line (`method i/N`,
  `rendering...`, `cleaning up...`) and streams the log live. Its ETA displays
  the current item's remaining time followed by the remaining queue total in
  parentheses, for example `ETA 1:12:30 (queue 4:48:10)`.
- The **parallel simulation/render jobs** value bounds both concurrent solver
  processes and concurrent per-method render/encode jobs.
- **Cancel** stops an in-flight run, terminating the whole process tree (the
  launched Python, the headless solver, and the renderer).
- Runs launched from the GUI spawn **no console windows** (children use
  `CREATE_NO_WINDOW`); the GUI itself opens windowless via `pythonw`.

### Queue

The **Queue** panel (under the Preset form) holds a list of presets to run
consecutively:

- **Add current** snapshots the form into the queue; build up several scenes this
  way. **Remove** / **Clear** manage the list.
- **Run** executes the queue top to bottom. With an empty queue, Run just runs
  the current form.
- **Cancel** stops after the running item, **deletes only that unfinished item's
  output** (completed items are kept), and clears the queue.
- The queue is cleared once it finishes successfully (the work is consumed).

The scene selects the engine automatically. `compressed_hyperelastic_square`
dispatches `MPM_PolyPIC_Experiment.exe` through the same queue, parallel-method,
render, cleanup, manifest, and Diagnostics Viewer workflow as the fluid scenes.

### Particles and rendering

Particle export is implicit, so there is no checkbox for it:

- **Run** renders when the preset produces an MP4 or snapshots, forcing particle
  export, rendering, then **deleting** the raw `particle_frames.bin` files and
  leaving only the rendered artefacts. If the preset produces neither, Run is a
  plain headless run with no particle dump. Pass `--keep-particles` on the CLI to
  retain the dumps.
- Renderer output depends on two settings (`color`, `width`/`height` apply throughout):
  - **write MP4 on** → an MP4 only (streamed to ffmpeg, no leftover per-frame PNGs);
  - **MP4 off + `snapshot_interval` set** → periodic PNG snapshots only;
  - **both off** → nothing is rendered, so Run + Render collapses to a plain
    headless run (and skips particle export entirely).
  - MP4 needs ffmpeg (or `imageio-ffmpeg`); snapshots-only does not.

For three-dimensional fluid exports, rendering is a replay step rather than
part of the simulation. The headless solver first writes the usual diagnostics
and `particle_frames.bin`. `Fluid_Particle_Renderer.exe` then replays those
frames through the locked OpenGL camera into transient PNGs, and the
orchestrator assembles the final MP4 with FFmpeg. The transient PNG directory is
always removed; the raw particle export follows the existing
`--keep-particles` cleanup policy. Three-dimensional particles use a fixed blue
so method comparisons do not change colour scale between runs.

### Run-time estimates (learned)

Before a run the orchestrator prints an estimate (e.g.
`[estimate] ~12000 particles, 1200 steps x 5 method(s): sim ~8m 20s, render ~2m`),
derived from `timing_heuristics.json` and refined after every run. Buckets:

- **engine**: fluid vs MPM tracked separately;
- **transfer class**: PIC, FLIP and every α blend share one bucket; APIC and
  PolyPIC are separate;
- step cost is stored **per step per particle** (so it transfers across domain
  sizes), particle counts are stored per `(scene, dim, domain, dx)`, and render
  cost is stored **per frame per particle**.

The first run of a new scene/resolution has no estimate; it learns from that run.
`timing_heuristics.json` is a local cache (git-ignored).

### FPS decimation

The export only writes the frames the video needs. The export rate is
`min(render.fps or particle_fps, 1/dt)`: e.g. with `dt = 1/60` and a 27 fps video,
~27 frames/second are exported while all 60 steps/second are still simulated. If
the requested fps exceeds `1/dt`, it drops to `1/dt`.

## Outputs

Each run creates:

```
outputs/<name>/<YYYYMMDD-HHMMSS>/
├─ preset.json           exact preset used
├─ manifest.json         run metadata (executable, code commit, steps, methods)
└─ runs/<METHOD>/
   ├─ diagnostics.csv     from the headless --export
   ├─ run.log             headless stdout+stderr
   ├─ run.json            method + exact command line
   └─ (particle_video.mp4 / snapshots/ when Run + Render)
```

A per-run log is written to `logs/<name>_<timestamp>.log`.

## Headless interface (as discovered, not assumed)

Flag-driven, not `--config`-driven. The orchestrator translates each
preset+method into:

```
Fluid_Simulation_Profile.exe --dim D --scene N --steps S --domain G --dx X --dt T \
  [--no-adaptive] --solver pic|flip|apic|polypic [--flip-percent F] [--polypic-modes M] \
  --jitter-seed J (--export | --no-diagnostics) [--export-particles --particle-fps P] --out DIR
```

Scenes (`src/world.hh`): `dam_break`=0, `solid_block`=1,
`full_grid`=2, `constant_stream`=3, `taylor_green_vortex`=4, `confined_vortex`=5,
`settling_pool`=6, `settling_pool_obstacle`=7. PIC = `flip` with `--flip-percent 0`.
TGV, confined-vortex, and settling-pool scenes are 2D only.

The executable is located automatically (newest build under `<project>/build`);
override with `FLUID_PROFILE_EXE` or `paths.local.json`.
