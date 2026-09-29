# Changelog

Releases are numbered after the Mesa version they contain; `-N` is the Nth
riscos-mesa build of it. Each release's full notes are on the GitHub
[Releases](../../releases) page.

## 20.3.5-10 (in development)

- **SDL GL windows can draw at a smaller size and use the hardware
  overlay.** Asked for by the Warzone 2100 port. Set the hint (or system
  variable) `SDL_RISCOS_GL_RENDER_SIZE` to `"WxH"` and the program draws
  at that size, stretched to fill its window or the screen; it sees a
  WxH window throughout, mouse positions included. Set
  `SDL_RISCOS_GL_OVERLAY` to `"1"` and, on a Pi with VideoOverlay, the
  display shows the frames and does the stretching. Both go through
  riscos-mesa's EGL. Without the hints, GL windows work exactly as before.
  See "Drawing faster" in `docs/porting/sdl2.md`.
- **Link change:** because libSDL2 now contains this EGL route, programs
  using the devkit's SDL2 link `-lEGL` too:
  `-lSDL2 -lGLU -lEGL -lOSMesa -lstdc++ -lz -lm`. `sdl2-config`,
  `sdl2.pc` and `openal.pc` say so already. SDL builds without GL, such
  as riscos-openttd's, are unchanged.
- **First Pi 4 figures** (2026-09-29), sdlgltest's lit cube in a
  1024x768 window: 78 fps drawing at full size (the old route), 134 fps
  drawing at 640x480 stretched by the plot, 214 fps drawing at 640x480
  through an overlay. Mouse positions were right in every corner. This
  was also the first Pi run of 20.3.5-9's overlay and render size. Not
  tried yet: menus over the overlay, full screen, mode changes, vsync.
- **Example 2 (`!GLWindow`) has a menu** with "Hardware overlay" and
  "Draw at 240x180", each one `eglSurfaceAttrib` call, and its title bar
  shows how frames reach the screen. Its `!Run` loads VideoOverlay if
  it's there.
- **Docs:** the EGL guide's "Hardware overlays" section now starts with
  a quick start, the Pi figures and a short list of things to know, with
  the exact rules after it. The SDL guide has a "Drawing faster" section
  with the two hints as code. The README, the devkit's beginner's guide
  and `egl/README.md` cover both options. New: a draft Khronos-style spec,
  `docs/khronos/EGL_RISCOS_overlay.txt`.
- **Smaller changes:**
  - EGL: a surface whose render size equals the size it's shown at uses
    the ordinary plot, not a scaled one.
  - sdlgltest: `-S WxH`, `-V`/`-N` and key D (desktop-size full screen),
    with Obey files `sdl-scaled`, `sdl-scaled-plot` and `sdl-plain`.
  - Host tests: the SDL GL harness runs both routes (the EGL one against
    a fake VideoOverlay), and the examples harness drives example 2's
    menu.
  - SDL's core has one small hook (`src.video.SDL_video.c.p`) so full
    screen keeps the render size.

## 20.3.5-9: hardware overlays and a scaled render size for EGL

EGL can now show a window through the display hardware's video overlay
(the VideoOverlay module on a Raspberry Pi), and a program can render at
a smaller size and have it stretched to fill its window or the screen:
by the overlay at no cost, or by the sprite plot. Both are opt-in. They
come from riscos-ffmpeg's work on its Reel video player, which measured
the Pi's overlays with the new `ovltest` and found the vsync waits that
this release removes. The overlay code has been checked against a fake
VideoOverlay on the host test harness and follows the Pi 4 measurements,
but **EGL's use of overlays, the render size and the screen bank change
have not yet been run on a Pi**; the tests zip has Obey files for each.

- **EGL window surfaces can use hardware overlays** (`EGL_RISCOS_overlay`),
  opt-in. A program asks with `EGL_OVERLAY_RISCOS` = `EGL_TRUE` (when
  creating the surface, or with `eglSurfaceAttrib` at any time: a
  "Hardware acceleration" menu item); `*Set EGL$Overlay on` gives one to
  every program that hasn't refused, `off` to none. Then, when the
  VideoOverlay module is loaded, a window surface covering the visible
  area is shown through a display overlay: `eglSwapBuffers`
  copies the frame into an overlay buffer (three, else two) and the
  hardware shows it at the next vsync, instead of plotting the sprite.
  Fallbacks to plotting for every problem (no module, no
  overlay of that size, GPU memory short, any error). While a window or
  menu overlaps the surface the overlay is hidden and the frame plotted
  (the Pi's overlays sit on top of everything). A program that stops
  swapping (a paused video) calls `eglCheckOverlaysRISCOS` on null
  events: a quarter of a second after the last swap it goes back to the
  plotted frame, keeping the overlay for the next swap. Opt-in rather
  than automatic because an overlay covers menus over a program that
  stops swapping without telling EGL, can make
  swaps wait for a vsync, isn't in screen grabs and takes GPU memory;
  the default may change once it has been tried on Pis.
  `eglQuerySurface(EGL_OVERLAY_RISCOS)` says whether it's in use. Only
  once a program is animating (three swaps in a row, each within a
  quarter of a second), so a window redrawn now and then is plotted as
  before. freeglut and the ports' Wimp helper call
  `eglCheckOverlaysRISCOS` while idle when overlays may be in use. Tested
  on the host harness (about 60 new checks against a fake VideoOverlay);
  **not yet tried on a Pi**: `egl-overlay` and `egl-no-overlay` in the
  tests zip (egltest `-V` / `-n`; keys H and P).
- **Render size** (`EGL_RENDER_WIDTH_RISCOS`, `EGL_RENDER_HEIGHT_RISCOS`):
  a window or full screen surface can render at a fixed size, stretched
  to fill the window or screen when shown. Through a hardware overlay the
  display does the scaling at no cost, so a game can render at 640x360
  and fill a 1920x1080 window for a ninth of the rendering; otherwise the
  sprite plot scales it. egltest `-S WxH` (key S), Obey files
  `egl-scaled`, `egl-scaled-plot`, `egl-scaled-full`. Asked for by
  riscos-ffmpeg; not yet tried on a Pi.
- **Screen banks don't block for nothing.** A full screen surface with
  screen banks now waits only until its swap interval of vsyncs has
  passed since the last bank switch, instead of always waiting for the
  next vsync (on average half a frame blocked per swap at 60 fps).
  Found by riscos-ffmpeg while making Reel play 60 fps video.
- **`eglSwapWouldWaitRISCOS`**: says whether a swap right now would block
  for a vsync (overlay or screen banks), so a program with other work,
  such as decoding the next video frame, can do it first and swap on its
  next pass. The EGL guide also gains Reel's advice for overlays: draw
  anything to be seen over the picture into the frame, and open other
  windows beside it, not over it.
- **Hardware overlay tests (`ovltest`).** Before EGL could use the
  display hardware's overlays, a Pi had to answer some questions: which
  formats and sizes it offers, how fast overlay memory is, whether cached
  writes show, whether buffers can stay mapped across Wimp_Poll, whether
  switches tear, how scaling looks, how an overlay copes with menus,
  other windows and mode changes, and whether YV12 colours come out
  right. `ovltest` measures each (tests zip, Obey files `ovl-*`; results
  go to `ovlresults`). Asked for by riscos-ffmpeg, whose Reel player will
  use overlays for video. Run on a Pi 4 (VideoOverlay 0.02): Vet doesn't
  work there (probe with Create), overlay memory is uncached (fast to
  write, very slow to read), switches take effect at vsync (three
  buffers and a vsync wait don't tear), YV12 colours are right, and the
  overlay stays on top of every window and menu. `tests/host-harness/ovl`
  runs it on the host against a fake VideoOverlay (in `run-all.sh`).
- **Docs:** where to get SharedSoundBuffer and StreamManager (the
  ssb.zip download on the RDPClient page, and the Internet Archive copy
  of the original); SharedSound is part of RISC OS.

## 20.3.5-8: EGL follows the Khronos rules, Cortex-A8/A9, a devkit guide

The EGL library was run against Khronos's own EGL tests (dEQP-EGL, from
the conformance suite) on the host test harness, and the places where it
differed from the EGL 1.4 specification were fixed: 858 of the tests pass
(842 before), and the 12 that fail are one test's use of an empty pbuffer
(see `tests/host-harness/deqp/README.md`). On a Pi 4 the devkit's six
example programs run with it; threads haven't been tried there yet. The
release also runs on Cortex-A8/A9 machines (VFPv3), comes with a
beginner's guide and six example programs in the devkit, and links every
program with UnixLib 5.0.1 and its PThreadTicker module.

- **Depth and stencil belong to surfaces**, as EGL says: two contexts
  drawing into one surface share its depth buffer. A new OSMesa patch
  (`patches/mesa/mesa-20.3.5-riscos-osmesa-buffers.patch`) gives each
  surface its own buffers. **A context and its surfaces must now have the
  same depth and stencil sizes** (before, any depth worked with any
  surface): `eglMakeCurrent` gives `EGL_BAD_MATCH` otherwise. Programs
  that use one config for both, as nearly all do, aren't affected.
- **Different draw and read surfaces** in `eglMakeCurrent` work.
- **One current context per API:** a GL and an ES context can be current
  in a thread at once; `eglBindAPI` picks which one GL calls go to.
- **Threads:** each thread has its own current context, error and bound
  API; making a context or surface current in two threads gives
  `EGL_BAD_ACCESS`; every EGL call takes one lock; a thread that ends
  with a context current releases it. Releasing a context
  (`eglMakeCurrent` with `EGL_NO_CONTEXT`) now really releases it.
- **Stricter checks:** `eglChooseConfig` refuses invalid values for
  boolean and enum attributes (`EGL_BAD_ATTRIBUTE`);
  `eglSetDamageRegionKHR` refuses surfaces set to `EGL_BUFFER_PRESERVED`,
  as `EGL_KHR_partial_update` requires; a surface's `EGL_SWAP_BEHAVIOR`
  starts as `EGL_BUFFER_DESTROYED` (the contents are still kept in
  practice, except with screen banks).
- **`EGL_CONFORMANT` is 0** for every config: only implementations
  certified by Khronos may claim conformance. A program that asks for
  `EGL_CONFORMANT` in `eglChooseConfig` now gets no configs.
- **Khronos registration:** `docs/khronos/` has the specifications of
  `EGL_RISCOS_wimp_window` and `EGL_RISCOS_platform_wimp` and a pull
  request for the Khronos EGL registry, ready to submit. Until it's
  accepted the enum values stay as they are (provisional).
- **Tests:** `tests/host-harness/deqp` builds and runs dEQP-EGL on the
  host; the EGL harness has 39 new checks (draw/read, shared depth, the
  depth check, GL and ES current together, two threads rendering at once,
  a thread ending with a context current);
  the fake RISC OS serialises SWIs so threaded tests are safe.
- **Linked with UnixLib 5.0.1, and PThreadTicker included.** Every program
  riscos-mesa ships (tests, ports, the devkit's examples) is now linked
  with UnixLib 5.0.1 from riscos-unixlib, whose pthread ticker fix stops a
  threaded program (SDL sound, OpenAL) crashing other desktop tasks. The
  threaded ones (`altest` via `al-tone`, `!LoopWave`, example 6 `!Tune`)
  load UnixLib's **PThreadTicker** module from their own folder before
  they start; the devkit carries it in `riscos/` with its ReadMe and
  licence, and the guide explains what it's for and how to ship it. The
  libraries themselves don't contain UnixLib, so they were only rebuilt
  from clean for tidiness. `build/TOOLCHAIN.md` says how to put 5.0.1 in
  a GCCSDK. Not yet run on a Pi with this build.
- **Runs on Cortex-A8/A9 machines too:** everything is now built for VFPv3
  instead of VFPv4 (`build/env.sh`; `RO_FPU=vfpv4` gives the old build).
  The only VFPv4 instruction the compiler used was fused multiply-add, in
  2 of Mesa's 18,557 functions (the GLSL compiler's constant folding); an
  interleaved glbench A/B on the Pi 4 measured every scene within 0.5%.
  The VFPv4 build died on an emulated Cortex-A8 (in the GLSL compiler);
  the VFPv3 build passes every ARM rendering check there, which
  `tests/run-all.sh` ARM=1 now runs on an emulated Cortex-A8. Not yet
  tried on a real A8/A9 board. Build directories now start afresh when the
  FPU setting changes.
- **The devkit is easier to start with.** It now includes a beginner's
  guide (`README.md`, also as `ReadMe` for RISC OS): what each part is
  for, a first program in ten minutes, which API to choose, every build
  flag and why it's there, how a RISC OS application is put together, and
  the usual pitfalls. Six small, heavily commented example programs come
  with it (EGL full screen, EGL in a desktop window, OpenGL ES 2.0
  shaders, SDL2, GLUT, OpenAL), with a Makefile that builds each into a
  ready-to-run application and a zip that keeps RISC OS filetypes, plus
  `sdl2-config` and pkg-config files that work wherever the devkit is
  unpacked. `tests/run-all.sh` runs the examples on the host harness and
  checks what they draw and play. All six run on a Raspberry Pi 4 (the
  first Pi run of this release's EGL, VFPv3 build and OSMesa buffers). The devkit also carries the EGL
  programming guide and the porting guides in `docs/`. The EGL guide is
  brought up to date (threads and per-API contexts, swap behaviour,
  180 dpi desktops, VFPv3) and points newcomers to the devkit guide;
  `egl/README.md` is now a short note on how the library is built and
  tested, so usage is described in one place.
- **GLUT: programs with a menu drew nothing after their first frame**
  when they set the viewport only in their reshape callback. A GLUT menu
  is a window to freeglut; on RISC OS it's a Wimp menu with no GL
  context, and freeglut's default reshape ran for it with size 0x0 on
  the program's context (`glViewport(0, 0, 0, 0)`). Fixed in
  `glut/riscos/fg_main_riscos.c`; found by the new teapot example.
- **Docs:** the EGL guide, `egl/README.md` and the porting guide describe
  the new rules; the swap interval in desktop windows (accepted, doesn't
  wait, so other tasks keep running) is now documented as a deliberate
  choice.

## 20.3.5-7: faster rendering, OpenAL, video textures

Rendering is up to 2.9 times as fast as 20.3.5-6, with the same picture,
programs start faster and use less memory, textures as games use them
take the fast path, SDL programs quit properly from the desktop and no
longer lose short mouse clicks, the devkit has OpenAL, and EGL can use a
sprite as a texture with no copy (for video). The speed figures were
measured on a Raspberry Pi 4.

glbench on the Pi 4, 640x480, 24-bit depth + stencil, milliseconds per
frame (frames per second in brackets). The 20.3.5-6 figures are from a run
in the same session as a 20.3.5-7 pre-release; the 20.3.5-7 figures are
from pre-releases 7pre7/7pre8. The texture changes made after them (below)
are aimed at games; for glbench's texture scene they take 3.9% fewer
instructions on a Linux host, and nothing else changes:

| Scene | 20.3.5-6 | 20.3.5-7 | |
| --- | --- | --- | --- |
| lit cube | 5.38 (186) | 3.11 (322) | 1.7x |
| full-screen bilinear texture | 44.04 (22.7) | 23.47 (42.6) | 1.9x |
| 12288 lit triangles | 25.79 (38.8) | 11.06 (90.4) | 2.3x |
| GLSL per-pixel shaded cube | 108.91 (9.2) | 38.05 (26.3) | 2.9x |
| 4 blended full-screen quads | 28.29 (35.3) | 20.45 (48.9) | 1.4x |
| clear | 1.53 (654) | 1.51 (664) | |

### GLSL: a rebuilt shader interpreter

Mesa's software renderer runs GLSL (and ARB) programs in an interpreter.
It has been reworked in two stages. With the other speed-ups below, the
first took the GLSL scene on the Pi from 108.9 to 59.9 ms a frame; the
second took it to 38.8 ms:

- **Programs are decoded once**
  (`patches/mesa/mesa-20.3.5-riscos-glsl-decode.patch`). The interpreter
  used to work out every operand of every instruction again for every
  pixel and every vertex: which register bank it's in, range checks, the
  swizzle and negation. Each program is now decoded once into a compact
  form with those answers ready, and the operand fetch and store are
  inlined (in the speed patch). This speeds up vertex shaders as well
  as fragment shaders. The decoded copy is checked against the program
  at every span and vertex batch, so a shader that is changed, relinked
  or replaced is always picked up.
- **Fragment shaders run 32 pixels at a time**
  (`patches/mesa/mesa-20.3.5-riscos-glsl-batch.patch`). Each instruction
  is dispatched once and applied to up to 32 pixels, with branch-free
  loops for the common case (no negation, indexing or saturation).
  Branching is followed separately for every pixel with lane masks:
  `if`/`else`, loops with `break` and `continue`, `discard`, and `return`
  from `main`. Each pixel has its own address register for indexed
  arrays. Shaders with subroutine calls, and any group that hits Mesa's
  runaway-loop limit, run a pixel at a time as before, so that limit
  still applies to each pixel exactly as it did. The per-pixel arithmetic
  is generated from the interpreter's own code, so results are identical.

### Other speed-ups

In `patches/mesa/mesa-20.3.5-riscos-speed.patch`:

- **Depth testing:** 24-bit depth buffers (what EGL, SDL and GLUT ask
  for) are now as fast as 16-bit ones. Every depth-tested span used to be
  copied to a malloc'd buffer, unpacked, tested, packed and copied back;
  it's now tested in place. OSMesa's fast shaded and flat triangles,
  which were only used with 16-bit depth and `GL_LESS`, now also work
  with 24-bit depth and `GL_LEQUAL`. This is most of the gain for the lit
  cube (1.8x) and the triangle scene (2.3x).
- **Texturing:** `GL_RGBA`/`GL_UNSIGNED_BYTE` textures (the usual kind)
  now use Mesa's integer textured-triangle path when they qualify (one
  texture, no mipmaps, `GL_REPEAT`, power-of-two sizes); it used to skip
  them because their bytes are in a different order from the formats it
  knew. They stay perspective-correct. Everywhere else, texels of the
  common 8-bit formats (RGBA, BGRA, RGB, L, A, LA, I, R, RG) are read
  directly instead of through a call per texel into Mesa's general
  unpacking code. Together: 1.7x for the bilinear texture scene.

Starting up and memory (`mesa-20.3.5-riscos-startup.patch`,
`mesa-20.3.5-riscos-size-limit.patch`):

- **Programs start faster:** creating the first GL context looked up
  about 2500 function names by comparing each with every one of Mesa's
  2339 built-in functions: about 87 million instructions, roughly 0.1 s
  on a Pi 4, before anything was drawn. A sorted index cuts creating a
  context from 93.5 to 10.3 million instructions (measured on a Linux
  host). `eglGetProcAddress` and `SDL_GL_GetProcAddress` use the same
  search.
- **13 MB less memory per context:** the largest surface, viewport,
  renderbuffer and texture is now 4096 x 4096 (was 16384), which covers
  every Pi screen mode up to 4K and matches EGL's pbuffer limit. Mesa's
  span buffers are sized by that width, and RISC OS commits all of it in
  the application slot: a context now allocates 9.6 MB instead of
  22.5 MB. GL reports the new limits (`GL_MAX_TEXTURE_SIZE`,
  `GL_MAX_VIEWPORT_DIMS`, `GL_MAX_RENDERBUFFER_SIZE` = 4096); before, it
  offered 16384. In a full screen mode wider or taller than 4096 pixels,
  `eglMakeCurrent` fails with `EGL_BAD_ALLOC`.

Hot paths (`mesa-20.3.5-riscos-span-speed.patch`, same picture):

- **Textured 2D and screen-aligned drawing:** the perspective texture
  code divided by q (in double precision) for every pixel. When q is the
  same across the triangle, as it is for HUDs, sprites, text and anything
  drawn flat-on, it's now divided once per span. The Pi's divide takes
  20-30 cycles, so this helps more there than the host's instruction
  count (tex scene: 6.4% fewer) suggests.
- **One-colour primitives** drawn with smooth shading (UI, particles,
  glbench's blend quads) are filled with their colour instead of
  stepping the colour per pixel: 12.6% fewer instructions for the blend
  scene.
- **No malloc per span** when converting shader output to 8-bit colour.
- **Blending, colour masks and logic ops read the screen in place**
  (`mesa-20.3.5-riscos-direct-rows.patch`) when it's in the usual RISC OS
  layout, instead of unpacking each row into a copy first; finished rows
  are copied back whole rather than packed pixel by pixel. Blend scene:
  9.3% fewer instructions.

Textures as games use them (`mesa-20.3.5-riscos-uncompressed.patch`,
`-fast-tex`, `-fast-fog`, `-fastest`), from profiling Warzone 2100, whose
textured triangles mostly missed Mesa's fast textured-triangle code:

- **Compressed textures are stored uncompressed** when a program asks for
  a generic compressed format (`GL_COMPRESSED_RGBA` etc.), as the GL spec
  allows. Decoding a compressed block for every texel took 44% of
  Warzone's frame.
- **The fast textured triangles now also take** `GL_CLAMP_TO_EDGE`
  textures, `GL_RGB` textures, and fog. Before, only `GL_REPEAT` RGBA
  textures without fog did. These differ from the slow path by up to
  3/255.
- **Opt-in faster texturing:** a program that sets
  `glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST)` gets perspective
  corrected every 16 pixels instead of every pixel, and mipmapped
  textures on the fast path with one mipmap level per triangle. This isn't
  exact; without the hint, rendering is.
- **Fixed:** linear-filtered RGB textures in `GL_REPLACE`/`GL_DECAL` mode
  on the fast path set alpha to 255 instead of keeping the fragment's.

And in the build:

- **libOSMesa is no longer position-independent** (meson's default for
  static libraries). GCCSDK's `-fPIC` code reaches every global through
  the shared library tables at &8038; the library is only ever linked
  into programs. GLSL is about 5% faster for it, the rest the same.

### Checking

- **Same picture:** every change was checked image by image against
  20.3.5-6 on a Linux host and on the RISC OS build itself, run under ARM
  emulation: thousands of cases covering every depth function, lighting,
  flat and smooth shading, texture formats, filters, wrap and texture
  environment modes, and GLSL and ARB programs with branches, nested
  loops, per-pixel loop counts, `discard`, early `return`, derivatives,
  `gl_FragDepth`, indexed arrays, and programs changed between draws. The
  only differences: the integer texture path rounds by up to 2/255
  differently, and a shader that reads a variable it never wrote
  (undefined in GLSL) can see a different leftover value.
- Blending is 1.4x as fast from the one-colour span and in-place row
  changes. Before those, its time moved by a couple of milliseconds
  between builds of the benchmark program itself (an A/B test on the Pi
  gave the same time with 20.3.5-6's library and an early pre-release).
- The Mesa changes are thirteen new patches in `patches/mesa/`, described in
  `patches/mesa/README`. `build/build-mesa.sh` applies them in order and
  records them in `.riscos-patches-applied`, so an existing Mesa tree
  gets only the ones it lacks.

### Other changes

- **Video frames as textures, with no copy:** EGL now makes images of
  32bpp sprites (`EGL_KHR_image_pixmap`), and GL and GLES contexts have
  `GL_OES_EGL_image`: `glEGLImageTargetTexture2DOES` makes a texture use
  the sprite's pixels in place, so a program can decode each video frame
  straight into the sprite and draw it, without copying it with
  `glTexSubImage2D` every frame (about 3-4 ms a frame at 1080p on a Pi 4,
  and a second copy of the picture). The sprite can be any size and
  either 32bpp colour order; its top row is t = 0. The Mesa side is a new
  patch, `riscos-eglimage`. `egltest`'s
  checks (`egl-check`) texture with a sprite and change it between two
  draws. Requested by riscos-ffmpeg for its `ffegl` library. Not yet run
  on a Pi.
- **Programs started in a TaskWindow say what's wrong:** a TaskWindow is
  already the program's Wimp task, so opening a desktop window from one
  fails with the Wimp's "Window Manager is currently in use". SDL now says
  "Can't open a desktop window from a TaskWindow: start the program with
  *WimpTask", and freeglut, libbcm_host (in window mode), the porting
  helpers and the test programs give the same advice. The docs say how to
  start window programs. Reported by riscos-ffmpeg (ffplay).
- **OpenAL in the devkit:** `libopenal.a` and the `AL/` headers are OpenAL
  Soft 1.19.1 (the last release written in C), so ports that use OpenAL
  share one tested copy. It mixes in software and plays through SDL2's
  sound driver (SharedSoundBuffer, mixing with other programs); link with
  `-lopenal -lSDL2 -lOSMesa -lstdc++ -lz -lm`. Two small build fixes are
  in `patches/openal`. It is under the GNU LGPL (see `LICENCES.txt`). The
  tests zip has `altest` (Obey file `al-tone`), which plays tones in the
  middle, left and right and checks they play in real time. Requested by
  the Warzone 2100 port, which built its own copy until now. The same
  OpenAL, built for Linux, is checked by `tests/host-harness/openal`.
  Not yet run on a Pi.
- **SDL2 programs quit properly from the desktop:** a desktop shutdown
  or the Task Manager's Quit now asks the program to quit (SDL_QUIT), so it
  can confirm or save first, and the shutdown carries on once it has quit;
  before, the program only got SDL_QUIT and the desktop shut down around
  it. The close icon sends SDL's window-close event
  (SDL_WINDOWEVENT_CLOSE), as on other platforms; programs that don't
  handle it still quit. SDL_ShowWindow/SDL_HideWindow work on desktop
  windows. Requested by riscos-openttd, which takes its SDL from here.
- **SDL2 icon bar icons for 12-character names:** a program whose
  sprite name is the full 12 characters (e.g. `!Warzone2100`) got a blank
  icon bar icon, because the name was cut to 11 characters.
- **SDL2 window scale hint:** the desktop window scale can be set with
  `SDL_SetHint("SDL_RISCOS_WINDOW_SCALE", "1")` (the name is a string:
  it isn't in SDL's public headers), or the system variable
  `SDL_RISCOS_WINDOW_SCALE`; `SDL$WindowScale` still works.
- **SDL2 mouse clicks in a window are no longer lost:** the window's
  buttons were read once per `SDL_PumpEvents`, so a click pressed and
  released between two frames (easy at the 5-10 frames a second of a busy
  OpenGL game) never reached the program. The Wimp's `Mouse_Click` event
  is now remembered and reported as a press, at the place it was clicked,
  with the release on the next poll. Found with Warzone 2100, whose menus
  ignored normal clicks.
- **Host builds with GCC 13:** `-O3` vectorisation miscompiles one loop in
  Mesa's span code (some smooth-shaded pixels come out wrong), so it's
  turned off for that function. The RISC OS build (GCC 10, no NEON) was
  never affected; the host test harness was.
- **Tests for maintainers:** `tests/run-all.sh` runs every host test in
  one go. New in `tests/host-harness/mesa`: the rendering checks used for
  this release (a hash of every image, compared with `expected/`), the
  same checks on the RISC OS build under qemu-arm, and a performance
  regression check that counts the instructions Mesa executes per frame
  for glbench's scenes. `tools/gen-glsl-batch.py` regenerates the batch
  shader code from the interpreter (and checks it is up to date).
- **Diagnostic EGL attribute removed:** `EGL_FLIP_FIRST_RISCOS` (0x3FFF),
  added to find out whether switching screen banks before or after the
  vsync wait stops tearing on the Pi 4, is gone: banks tear either way, so
  the swap switches after the wait as before. Also gone: egltest's `-s fv`
  option and `tests/egl-banks-fv`. `EGL_SCREEN_BANKS_RISCOS` is unchanged
  (still experimental).
- **For maintainers:**
  - `build/build-all.sh [VERSION]` builds everything in order, logging
    each step to `stage/logs/`.
  - `tools/mesa-branch.sh` turns the Mesa patches into a git branch (one
    commit per patch) and writes them back, so they're edited as source,
    not as patch files. The patch files are now plain git diffs; the
    patched tree is unchanged, and existing Mesa trees are still
    recognised.
  - `egl/egl_riscos.c` is split into `egl/parts/` (screen, buffers,
    validation, configs, API, extensions), still built as one file; the
    compiled library is unchanged.
  - The SDL overlay in `patches/sdl2` is now the master copy:
    riscos-openttd takes its copy from here with
    `tools/sdl-overlay-export.sh`, and `tools/sdl-overlay-check.sh`
    reports a copy that has drifted.
  - The performance check (`tests/host-harness/mesa/perf.sh`) also
    counts the instructions to start a program and create its first
    context, so start-up slowdowns are caught as well.
- **Docs:**
  - **EGL guide** (`docs/EGL-GUIDE.md`), brought up to date for this
    release:
    - images, with a "Video frames as textures" walkthrough;
    - quitting properly from the desktop (Message_Quit and PreQuit);
    - starting window programs from the Filer or with `*WimpTask`;
    - the 20.3.5-7 speed table and a new section, "Getting speed out of
      the renderer" (the texture fast path, `GL_FASTEST`, compressed
      formats, the 4096 limit);
    - more troubleshooting entries.
  - **`egl/README.md`** has a section on images.
  - **Porting guides** (`docs/porting/`):
    - EGL images for video, and how Pi video code (OpenMAX) differs;
    - threads and the UnixLib ticker fix;
    - quitting and starting programs;
    - the OpenAL link line;
    - SDL's new quit, close and click behaviour;
    - texture speed advice for GLUT and SDL programs;
    - a warning against `popen()` and `system()`.
  - **DispmanX README** updated to match.
  - **`build/TOOLCHAIN.md`** says to apply riscos-openttd's UnixLib patch:
    without it, C++ programs that set up `std::locale` abort with
    "wctype not implemented".

## 20.3.5-6: freeglut, SDL sound

All the GLUT demos run on a Raspberry Pi. The SDL sound driver is tested
on a Linux host; Pi reports on sound are welcome.

- **freeglut 3.8.0 with a native RISC OS back end** (`glut/riscos`,
  `build/build-freeglut.sh`, `patches/freeglut`): `libglut.a` (OpenGL) and
  `libfreeglut-gles.a` (OpenGL ES 1.1/2.0), with `GL/glut.h` and
  `GL/freeglut*.h`. GLUT programs usually build unchanged:
  - GLUT windows are Wimp windows and multitask; subwindows (nested to any
    depth) are EGL work area surfaces inside them;
  - GLUT menus are Wimp menus (Menu opens them, Adjust keeps them open),
    also in the ES build;
  - key releases, special keys (and Shift/Ctrl/Alt on their own), mouse
    motion, entry, the scroll wheel, pointer hiding and warping;
  - single-buffered programs are shown after each display callback and
    while idle or timer callbacks draw;
  - full screen and game mode are borderless windows covering the desktop
    in the current screen mode.
- **Worked port and guide:** freeglut's demos (`!Shapes`, `!One`,
  `!Subwin`, `!Lorenz`, `!Fractals`, `!Resizer`, `!View3D`, `!Keyboard`)
  in the new `riscos-mesa-glut` zip; [docs/porting/glut.md](docs/porting/glut.md).
- **EGL:** work area surfaces in a window now stack in creation order
  (later ones on top), and showing one replots those above it. Before,
  the newest was drawn first, so a surface created inside another was
  hidden by it.
- **SDL2 sound:** a RISC OS audio driver (from riscos-openttd, the shared
  SDL overlay), playing through SharedSoundBuffer/StreamManager so SDL
  programs' sound mixes with other programs'. SDL falls back to its `dsp`
  driver (DigitalRenderer) if the modules aren't there. New `!LoopWave`
  (SDL's loopwave test) in the ports zip.
- **Tests:** the EGL host harness has 265 checks (stacking). Its fake Wimp
  can now click, drag, release, turn the wheel and choose from menus.
  `tests/host-harness/glut/run.sh` drives freeglut's demos (23 checks).

## 20.3.5-5: OpenGL ES, porting aids and porting guides

All tests pass on a Raspberry Pi 4.

- **OpenGL ES 1.1 and 2.0** through the native RISC OS EGL types (Wimp
  window, `EGL_RISCOS_SCREEN_WINDOW`, sprite, pbuffer), and through SDL2
  (`SDL_GL_CONTEXT_PROFILE_ES`, major version 1 or 2).
  - It comes from a small patch that gives OSMesa ES profiles.
  - ES 3.x is refused.
- **EGL:**
  - The initial API is now OpenGL ES, as the spec says. Desktop GL code
    must call `eglBindAPI(EGL_OPENGL_API)` (a change from 20.3.5-4).
  - Every config supports GL, ES 1 and ES 2.
- **Mesa patch:** fragment shaders with no default float precision compile
  (mediump, with a warning), as they did on the Pi's VideoCore compiler.
- **DispmanX compatibility (`libbcm_host`), a porting aid** for existing
  Raspberry Pi 1–3 Khronos code. It isn't the RISC OS API; native EGL
  comes first and is unchanged.
  - DispmanX elements, `EGL_DISPMANX_WINDOW_T`, and empty `libGLESv2` /
    `libvcos` / … so Pi link lines work.
  - Also `eglSaneChooseConfigBRCM` and a minimal `interface/vcos/vcos.h`.
  - **Window mode**, the default in the desktop: the program's "display"
    is a desktop window (640 pixels wide). libEGL plots into it, and the
    library polls the Wimp after each `eglSwapBuffers`, so unchanged Pi
    programs multitask.
  - `<App>$Display` / `DispmanX$Display` choose a size, or `full` for the
    Pi's whole-screen behaviour.
- **The Pi's own examples, rebuilt:** `!HelloTriangle`, `!HelloTriangle2` and
  `!HelloTeapot` (`riscos-mesa-hello_pi` zip).
- **Porting guides** (`docs/porting/`), each with a worked port in `ports/`:
  - Mesa's EGL demos, over a RISC OS back end for eglut: `!EGLGears`,
    `!ES1Gears`, `!ES1Torus`, `!ES2Gears`, `!EGLTri`;
  - the OpenGL ES 2.0 Programming Guide samples (`esUtil_RISCOS.c`; the
    samples are fetched at build time, as they have no licence);
  - SDL 2 GL programs (SDL's `testgl2`, `testgles`, `testgles2`);
  - Raspberry Pi code.

  The Mesa demos and SDL programs are in the new `riscos-mesa-ports` zip.
  `ports/common/riscos_wimpwin.c` is a small Wimp window + loop for your
  own ports.
- **High resolution (EX0 EY0, 180 dpi) desktops:**
  - SDL2 windows are shown 2x, with the mouse scaled to match.
    `SDL$WindowScale` overrides this.
  - EGL window and full screen sprites now match the screen's dpi. They
    were doubled and cropped before.
- **SDL2:** each program's task name, icon bar sprite and menu title come
  from its own application directory (`!App`, or the generic application
  sprite).
  - The global `SDL$IconSprite`, which leaked OpenTTD's icon and name into
    other programs, is no longer read.
  - Every desktop SDL program gets an icon bar icon with a Quit menu.
- **Tests:**
  - New `glestest` (ES on the native types) and `dmxtest` (DispmanX, full
    screen and window mode).
  - The host harness has 262 checks and can run whole Wimp programs from a
    script (`portrun.c`).
- **Licences:** patches keep the licence of the files they change. Ports
  keep their own licences (userland BSD, mesa-demos MIT, SDL zlib).

## 20.3.5-4: EGL extensions

- **Display extensions:**
  - `EGL_KHR_surfaceless_context`;
  - fence, reusable and wait syncs;
  - `EGL_EXT_buffer_age`;
  - swap with damage (KHR and EXT);
  - `EGL_KHR_partial_update`;
  - `EGL_KHR_lock_surface` 1–3;
  - `EGL_KHR_context_flush_control`.
- **Client extensions:** `EGL_EXT_client_extensions`, `EGL_EXT_platform_base`
  with `EGL_RISCOS_platform_wimp`,
  `EGL_KHR_client_get_all_proc_addresses` and `EGL_KHR_debug`.
- **Tests:**
  - `egl-damage`: 507 fps with the middle half shown, against 225 fps for
    the whole window, on a Pi 4.
  - The host harness has 206 checks.
- **Docs:** the EGL guide covers every extension, with worked examples.

## 20.3.5-3: EGL 1.4

- **`libEGL.a`: EGL 1.4 over OSMesa.**
  - The native window is a Wimp window handle, or -1 for the whole screen.
  - Pixmaps are sprites; pbuffers are supported.
  - Work area surfaces and `eglRedrawWindowRISCOS` for the Wimp.
- **Full screen:**
  - By default a sprite is plotted after the vsync wait.
  - Rendering straight into screen memory is optional.
  - Screen banks are opt-in (experimental: they tear on the Pi 4).
- **Window surface sprites** are padded to at least 1 MB: small sprites
  froze on the Pi 4.
- **Tests:** `egltest` and its `egl-*` Obey files. The EGL host harness
  runs against a fake Wimp, screen and SpriteOp.
- **Docs:** the programming guide (`docs/EGL-GUIDE.md`), and `LICENCES.txt`
  with every component's licence.

## 20.3.5-2: stack crash fix, faster build, multitasking SDL

- **Build with `-fstack-clash-protection`:**
  - Mesa's big stack frames were jumping past the ELF stack's guard page,
    which caused crashes that looked random.
  - `tools/check-stack-probes.py` checks a binary for this.
- **Flags:** `-O3 -mtune=cortex-a72 -mfpu=vfpv4`, chosen by benchmark on a
  Pi 4 (NEON gave nothing).
- **SDL driver multitasks:** `SDL_Delay`, `SDL_WaitEvent` and windowed
  vsync yield to the desktop.
- **Tools and tests:** `glbench` and a high resolution timer. Zips store
  RISC OS filetypes (`tools/mkrozip.py`).

## 20.3.5: first release

- **Mesa 20.3.5 classic OSMesa** as one static `libOSMesa.a` (OpenGL 2.1,
  GLSL 1.20), plus GLU 9.0.1.
- **SDL 2.26:** an OpenGL context for its RISC OS driver (overlay shared
  with riscos-openttd). GL windows work as desktop windows and full screen.
- **Build:** cross-built with GCCSDK GCC 10; source downloads are SHA-256
  checked.
- **Tested on a Raspberry Pi 4:** a lit 640x480 cube at about 160 fps in a
  desktop window.
