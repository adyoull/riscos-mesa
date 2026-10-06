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
| `render-tex.c` | texturing as games use it: generic compressed formats (stored uncompressed), each wrap mode, filter and fog setting drawn on swrast's fast textured triangles and on the general path (with the largest difference between them), GL_FASTEST (perspective in segments, one mipmap level per triangle) against exact |
| `render-image.c` | textures using memory they don't own (GL_OES_EGL_image): a pixel array in either byte order registered through `OSMesaSetImageLookup`, drawn flat, in perspective and blended, nearest and linear, each compared with the same pixels uploaded by glTexImage2D; a change to the pixels shows at the next draw; re-specifying the texture leaves them alone |
| `render-fog.c` | fog on smooth-shaded, untextured triangles (a floor into the distance, walls at fixed depths): linear, exp and exp2, pixel fog (GL_NICEST) and per-vertex fog, two fog colours; each pixel checked against fog worked out from its depth (within 3, mean within 1), alpha unchanged |
| `render-paths.c` | fast paths kept out of states they don't handle: OSMesa's depth-tested triangles and lines with a framebuffer object bound (16/24-bit depth, smaller and larger than the window) and with GL_DEPTH_CLAMP; the fast textured triangles under GL_FASTEST with GL_CLAMP and a linear filter, against the general path |
| `render-rows.c` | blending, colour masking and logic ops, each into an aligned buffer (the destination row is read in place) and a misaligned one (it is unpacked): both hashes must match |
| `glsl-basic.c` | GLSL vertex and fragment shaders: lighting, textures, uniforms, discard, blending |
| `glsl-control.c` | indexed uniform arrays, loops with break/continue, nested if/else, discard, gl_FrontFacing, uniforms changed between draws, program switches, ARB programs (one replaced in place) |
| `glsl-edge.c` | early return, derivatives, per-pixel loop counts, discard in loops, gl_FragDepth, projective and LOD texturing, a runaway loop |
| `glsl-special.c` | denormals, NaNs and infinities in shaders (the NEON code hands these back to the C code); the NaN cases are "undefined" here, and `arm/run-arm.sh` checks they match with NEON and with `MESA_NO_NEON` |
| `glsl-es2compat.c` | GL_ARB_ES2_compatibility in a desktop GL context: `#version 100` shaders draw what `#version 120` ones do, GL_FIXED attributes, glClearDepthf/glDepthRangef, the ES 2.0 queries |
| `render-etc1.c` | ETC1 textures in ES 1.1 and 2.0: every texel against an ETC1 decoder written from the specification, sub-images, mipmap levels, the errors, and the same picture as the texels loaded as GL_RGB |

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
scenes (`perf.c`, which includes `tests/glbench.c`, run under valgrind),
and for starting a program up to its first context being current
(`startup`, a total), and fails if any of them needs more than 2% more
than `expected/perf.txt`.
Instruction counts are the same on every run, unlike timings, so small
slowdowns are caught reliably. They depend on the host compiler: the
baseline records the gcc version, and with a different compiler compare
two builds with `REF=<dir>` instead. `UPDATE=1` rewrites the baseline
after an intended change.

    M=<as above> ./perf.sh

This measures the code on the host; the Pi's own figures come from
`glbench` in the tests zip.
