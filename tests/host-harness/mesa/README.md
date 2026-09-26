# Mesa checks: rendering and performance

Checks for riscos-mesa's changes to Mesa (`patches/mesa`), run on a Linux
host. Run them after any change to the Mesa patches; `tests/run-all.sh`
runs them with the other host tests.

## Rendering: `run.sh`

Renders a few thousand cases and compares a hash of every image with
`expected/*.txt`, so any change in what Mesa draws shows up:

| Program | Covers |
|---|---|
| `render-fixed.c` | every depth function, lighting, smooth and flat shading, depth writes, lines, four depth/stencil sizes; 14 texture formats x filters x wrap modes x texture environment modes |
| `glsl-basic.c` | GLSL vertex and fragment shaders: lighting, textures, uniforms, discard, blending |
| `glsl-control.c` | indexed uniform arrays, loops with break/continue, nested if/else, discard, gl_FrontFacing, uniforms changed between draws, program switches, ARB programs (one replaced in place) |
| `glsl-edge.c` | early return, derivatives, per-pixel loop counts, discard in loops, gl_FragDepth, projective and LOD texturing, a runaway loop |

    M=<mesa-20.3.5 with patches/mesa applied, built in $M/build> ./run.sh

`REF=<dir>` compares with another build's `libOSMesa.so.8` instead (A/B).
`SKIP='^tex '` leaves out the texture lines, which differ by design from
builds before 20.3.5-7. `UPDATE=1` rewrites `expected/` after an intended
change. Building the host Mesa is described in `../egl/README.md`.

## The same on the RISC OS build: `arm/run-arm.sh`

Links the RISC OS `libOSMesa.a` from the stage directory into an ARM Linux
program (`arm/shim.c` supplies the few UnixLib symbols it needs) and runs
the same checks under `qemu-arm`, comparing with the same `expected/`. So
the code GCCSDK compiled for the Pi is checked too. Needs `qemu-user` and
`gcc-arm-linux-gnueabihf`/`g++-arm-linux-gnueabihf`; takes a few minutes.

    STAGE=<stage dir> arm/run-arm.sh

## Performance: `perf.sh`

Counts the instructions Mesa executes per frame for each of glbench's
scenes (`perf.c`, which includes `tests/glbench.c`, run under valgrind)
and fails if any scene needs more than 2% more than `expected/perf.txt`.
Instruction counts are the same on every run, unlike timings, so small
slowdowns are caught reliably. They depend on the host compiler: the
baseline records the gcc version, and with a different compiler compare
two builds with `REF=<dir>` instead. `UPDATE=1` rewrites the baseline
after an intended change.

    M=<as above> ./perf.sh

This measures the code on the host; the Pi's own figures come from
`glbench` in the tests zip.
