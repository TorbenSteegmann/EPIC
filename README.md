# EPIC — Evaluation Platform for Particle-in-Cell Methods

An interactive and headless 2D/3D simulator for comparing particle-in-cell
transfer schemes (PIC, FLIP, APIC, PolyPIC) for fluids and MPM, together with a
Python orchestrator that drives the thesis experiments from JSON presets.

The project has three parts:

- **Interactive UI** (`Fluid_Simulation_Display`) — real-time visualisation and
  experimentation.
- **Headless solvers** — `Fluid_Simulation_Profile` (fluid) and
  `MPM_PolyPIC_Experiment` (MPM), plus `Fluid_Particle_Renderer` for replaying
  exported particle frames. These take command-line flags and write diagnostics.
- **Orchestrator** (`Orchestrator/`) — Python tooling that turns a JSON preset
  into headless command lines, runs the methods, organises outputs, and
  optionally renders videos.

## Dependencies

Build (C++):

- CMake ≥ 3.5 and a C++23-capable compiler (MSVC from Visual Studio 2022+,
  GCC 13+, or Clang 16+)
- [Intel TBB](https://github.com/oneapi-src/oneTBB) (`find_package(TBB)`)
- [Eigen3](https://eigen.tuxfamily.org/) (`find_package(Eigen3 CONFIG)`)
- GLFW — a prebuilt library is bundled in `libs/` for Windows (`glfw3.lib`) and
  macOS (`glfw3.dylib`); on Linux the system `glfw` is used (e.g.
  `libglfw3-dev`)
- OpenGL (system)

GLM, GLAD, Dear ImGui, KHR and stb headers are vendored under `include/` and
`src/`, so no extra setup is needed for them.

Orchestrator (Python 3.10+):

- Standard run: only the Python standard library.
- The GUI editor / diagnostics viewer use `tkinter` and `matplotlib`.
- `--render` additionally needs `numpy`, `Pillow`, `imageio-ffmpeg`, and
  `ffmpeg` available on `PATH`.

## Build

Configure and build into a directory named `build/` at the project root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

If TBB or Eigen3 are not found automatically, point CMake at them, e.g.:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DEigen3_DIR=/path/to/eigen3/cmake \
  -DTBB_DIR=/path/to/tbb/lib/cmake/tbb
```

On Windows the required runtime DLLs (e.g. TBB) are copied next to each
executable automatically after the build.

> Use `build/` as the build-directory name. The orchestrator discovers the
> compiled binaries by searching under `<project>/build` (any sub-folder name is
> fine), so a different build-root name would need a manual override — see
> "Orchestrator" below.

## Run the UI

```sh
# from the directory that contains the built binary, e.g. build/Release
cd build/Release        # Visual Studio / multi-config; Qt Creator uses build/<kit>-Release
./Fluid_Simulation_Display
```

Note on the working directory: the UI loads shaders and sprites with paths
relative to the current directory (`../../res/...`), which expects the binary to
sit two directories below the project root (the usual generator layout, e.g.
`build/Release/` or `build/<kit>-Release/`). Launch it from that directory so
the `res/` folder is found. The headless executables and the orchestrator have
no such constraint.

## Run the headless solvers directly

```sh
# fluid (PIC / FLIP / APIC / PolyPIC), writes diagnostics.csv into --out
build/Release/Fluid_Simulation_Profile --dim 2 --scene 0 --steps 200 \
  --domain 64 --dx 1 --dt 0.1 --solver flip --flip-percent 0.95 --export --out out_fluid

# MPM compressed hyperelastic square
build/Release/MPM_PolyPIC_Experiment --solver polypic --steps 200 \
  --domain 64 --square-cells 32 --export --out out_mpm
```

Run with no recognised arguments to see the full flag list.

## Run the orchestrator (experiment workflow)

```sh
# list available presets
python Orchestrator/src/run_experiment.py --list

# run a preset (all configured methods), outputs under Orchestrator/outputs/
python Orchestrator/src/run_experiment.py Orchestrator/presets/dam_break.json

# run and render a video (needs ffmpeg + numpy/Pillow/imageio-ffmpeg)
python Orchestrator/src/run_experiment.py Orchestrator/presets/dam_break.json --render
```

The orchestrator finds the compiled binaries automatically under
`<project>/build`. If you built elsewhere, override per binary with an
environment variable (`FLUID_PROFILE_EXE`, `MPM_EXPERIMENT_EXE`,
`FLUID_PARTICLE_RENDERER_EXE`) or copy `Orchestrator/paths.local.json.example`
to `Orchestrator/paths.local.json` and edit the paths. See
`Orchestrator/README.md` for the GUI editor and diagnostics viewer.

## Output locations

- Experiment runs: `Orchestrator/outputs/<experiment>/` (per-method
  `runs/<METHOD>/diagnostics.csv`, a `manifest.json`, and rendered media when
  `--render` is used).
- Per-run logs: `Orchestrator/logs/`.
- Direct headless runs: wherever you pass `--out`.

The raw diagnostic data from the thesis experiments **is** included under
`Orchestrator/outputs/` (per-method `diagnostics.csv`, `run.json` / `manifest.json`,
and run logs) so the recorded numbers can be inspected directly. Rendered media
(videos, PNG frames) and the generated plots are **not** included — they can be
reproduced from this diagnostic data and the commands above.

## Attribution and provenance

To be transparent about which parts are original work and which follow
established implementations:

- **Rendering / windowing layer.** The OpenGL and GLFW setup (window and context
  creation, the shader-compilation/`Shader_Program` and `Texture_2D` helpers,
  resource loading, and the basic render loop) follows common tutorial
  implementations, in particular [learnopengl.com](https://learnopengl.com/).
  These parts are standard boilerplate rather than novel contributions.
- **Engine scaffolding.** Some structures and names may look unusual for a
  simulation project — e.g. `Game`, `World`, the small ECS (`ecs/`),
  `player_controller`, and `camera`. These are historical: this
  project was forked from a 2D game framework the author wrote a few years
  earlier (itself built by following the OpenGL tutorials above), and the
  general-purpose scaffolding was carried over and adapted.
- **Original contribution.** The numerical methods themselves — the MAC grid and
  pressure projection, the PIC/FLIP/APIC/PolyPIC transfer schemes, and MPM — are
  established techniques from the literature and are *not* invented here; they are
  attributed to their source papers in the thesis. What this project contributes
  is the *implementation* of those methods in a single, directly comparable
  codebase and the evaluation platform (EPIC) built around them: the shared
  solver/runner structure, the diagnostics instrumentation, and the experiment
  orchestration used to compare the transfer schemes for this thesis.
- **Third-party libraries.** GLM, GLAD, Dear ImGui, and stb are vendored under
  `include/`/`src/` and remain under their respective upstream licenses; GLFW,
  Intel TBB, and Eigen3 are external dependencies under their own licenses.

## Platform support

Developed and **tested on Windows** (Visual Studio 2026 / MSVC, Ninja). The
build is plain CMake and the code paths are written to be portable to Linux
(system GLFW) and macOS (bundled `glfw3.dylib`), but those have not been
re-verified for this submission.

If anything is missing or does not build on your platform, please don't hesitate
to get in touch — **Torben Steegmann**.
