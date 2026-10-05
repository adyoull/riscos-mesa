# Changelog

Releases are numbered after the Mesa version they contain; `-N` is the Nth
riscos-mesa build of it. Each release's full notes are on the GitHub
[Releases](../../releases) page.

## 20.3.5-12 (in development)

- **Faster with no change to the picture** (new patch `riscos-exact-speed`;
  every rendering check and glbench scene is bit-for-bit the same).
  Measured as ARM instructions per frame under qemu:
  - clearing a 24-bit depth buffer (what EGL, SDL and GLUT use) takes
    half the work: glbench's clear scene -53%, cube -19%;
  - 2D and orthographic textured drawing -5%;
  - lit drawing with `GL_COLOR_MATERIAL` and a colour per polygon -11%
    (the light/material values are worked out again only when the colour
    changes);
  - fog -32%: the integer fog read and wrote pixels with `memcpy`, which
    is a library call on ARM, so it gained less on a Pi than on a PC.
- **GLUT game mode draws at the resolution the program asks for**
  (`glutGameModeString`), stretched over the screen, instead of always at
  the desktop's: a program asking for 640x480 on a 1920x1080 desktop draws
  a seventh of the pixels it did. The program sees that size, and the
  pointer is mapped to it. Game mode asks for a hardware overlay
  (VideoOverlay, on a Raspberry Pi), which stretches the frame and saves
  copying it to the screen; other machines plot it, stretched, as before.
- **GLUT single-buffered windows aren't copied to the screen again and
  again while nothing draws**: with a timer set, they used to be shown
  every 20 ms whether it had gone off or not (50 copies a second for a
  program with a one-second timer). They're now shown after a timer runs,
  and what a timer draws is shown before the wait for the next one.
- GLUT: closing the game mode window with
  `GLUT_ACTION_GLUTMAINLOOP_RETURNS` set no longer frees it twice.
- **Fixes from a code audit** (2026-10-04):
  - **Drawing into a framebuffer object could land in the window**
    (depth-tested, untextured drawing, with any EGL, SDL or GLUT context),
    and crashed with one larger than the window. OSMesa's own fast
    triangles are now only used for the window, and not with
    GL_DEPTH_CLAMP, which they ignored. GL_CLAMP with a GL_LINEAR filter
    under GL_FASTEST lost the border colour. New Mesa patch
    `riscos-audit-fixes`, checked by the new `render-paths`.
  - **EGL:** when a window surface's buffer couldn't be re-made (a resize
    or mode change with memory short, or eglMakeCurrent whose read
    surface's window had gone) the current context drew into freed
    memory. A new sprite is now made before the old one is freed, and a
    context is never left on a freed buffer. Work area surfaces (and
    DispmanX windows) larger than 4096 are refused: their size maths
    overflowed into a sprite far too small. eglMakeCurrent with the same
    context on other surfaces now flushes first, so what it drew lands in
    the old surface.
  - **SDL:** `sdl2-config` and `sdl2.pc` now give `-lEGL`, so ports built
    with pkg-config, CMake or autotools link. In a window, clicks and drags
    on the title bar and border icons no longer reach the program, and
    the pointer's row is no longer one out (the top row can be reached).
    Relink to get these.
- **SDL: a key press no longer repeats on every frame** (reported by the
  fheroes2 port). The RISC OS driver sent every held key again at each
  poll, so SDL gave a repeat (`event.key.repeat`) each frame: a 100 ms tap
  at 60 fps arrived as a press and about 6 repeats, and programs that act
  on every KEYDOWN acted several times (fheroes2's quit dialog closed
  straight away). Now a press is sent once, and a held key repeats as on
  the desktop, after the keyboard's auto-repeat delay and at its rate
  (none after `*FX 11,0`). New test `sdlkeys` (Obey file `sdl-keys`)
  counts the events; the host harness checks the timing. Relink to get
  the fix.
- **Fog is faster** (new patch `riscos-fog-span`). Fog on triangles used
  to be blended in floating point for every pixel, with a division and
  six conversions. Now it's done in integers, and with the default fog
  hint the fog amount is worked out exactly every 8 pixels and
  interpolated in between. Pictures change by at most 1 in a colour
  channel. Asked for by the YSFlight port, whose fogged sky and ground
  spent 15-17% of a frame in fog. On the host, Mesa's fog work is 14%
  less and glbench's new `fog` scene 4% faster; the Pi, whose floating
  point division and conversions cost more, hasn't been measured yet.
  New check `render-fog` compares fog with the expected value worked out
  from each pixel's depth; `render-tex`'s fogged cases have new hashes.
- SDL overlay: `sdl2-configure.ac.simd.p` didn't apply after the GCCSDK
  autobuilder's own `configure.ac.p`, which adds lines at the same place,
  so riscos-openttd's SDL build stopped (reported by riscos-openttd). The
  ARM blitter checks now go earlier in the RISC OS section, and
  `tools/sdl-overlay-regen.sh --check` checks the configure patches apply
  after the autobuilder's too. No change to what is built.
- **glPushMatrix after glBegin/glEnd drew with the wrong matrix** (a Mesa
  classic swrast bug, new patch `riscos-push-flush`). Found by the Warzone
  2100 port, whose skirmish screen lost its buttons; any OpenGL 1.x program
  that pushes a matrix right after immediate-mode drawing could lose what
  it drew. Relink to get the fix.
- **The output helpers lost stderr on RISC OS** (`riscos_output.c`,
  `riscos_wimpwin.c`): RISC OS lets a file be open for writing only once,
  so the second `freopen` failed and closed stderr. stdout now shares the
  file with `dup2`. `riscos_output.c` can also send output to a default
  (`-DOUTPUT_DEFAULT='"/dev/null"'`). Found by the Warzone 2100 and Freeciv
  ports.
- **No build paths in released programs:** `-ffile-prefix-map` in the
  build flags, and `build/package.sh` refuses to package a program or
  library that names the build machine's directories (6 programs did).
  Changing the compiler flags now rebuilds Mesa, SDL and GLU from fresh.
- **ARM checks with RISC OS's alignment rules:** `tests/host-harness/qemu`
  builds a QEMU that traps unaligned accesses as RISC OS does (from the
  riscos-ffmpeg port); `QEMU_ALIGN=` runs the Mesa checks under it.
- **Porting guide:** what other ports learnt: open a file for writing
  once, the 128 MB dynamic area limit, keeping the desktop alive while
  loading, the "EMT trap" at start-up (an earlier program's leftover
  ARMEABISupport record: restart), crash reports, CMake flags, old C++
  and `-fno-delete-null-pointer-checks`, AIF for TaskWindows.
- **Fixes from a code review of 20.3.5-10 and -11:**
  - EGL overlays: a window uncovered after frames were swapped while it
    was covered no longer shows the overlay's older frame again; an
    overlay whose window has been deleted is removed (it stayed over
    everything); `EGL$Overlay` is read with OS_ReadVarVal instead of
    `getenv`, which isn't thread safe in UnixLib for names with a `$`;
    an overlay is only used at sizes VideoOverlay says it can show; a
    menu or window over the parent of a child window now hides the
    child's overlay.
  - SDL: a desktop mode change between 90 and 180 dpi keeps a window's
    picture and mouse right (it fitted only full windows before), and
    SDL's desktop mode follows the change, so a later desktop-size full
    screen gets the right size; a full window that can't be made falls
    back to single tasking full screen; on the EGL path the mouse is
    right in a window taller than the screen; frames are held only on
    the event loop's thread.
  - Build and tests: `build-sdl2.sh` stops if a `.p` file doesn't apply
    (it carried on); `tools/sdl-overlay-regen.sh --check` also finds
    source changes no `.p` covers (after a `.p` was removed), says when it
    falls back to the tree's git history for pristine SDL, and copes with
    a file that is back to pristine; the `sdl-arm` test checks which CPU
    features each emulated CPU reports, and rebuilds when its options
    change; `tests/host-setup.sh` no longer lets the host build see the
    RISC OS stage's pkg-config files; CI keeps all the logs; the devkit
    examples' `make zip` works from the repository too.

## 20.3.5-11: UnixLib 5.0.3.1, so SDL's sound thread runs

Every program is relinked with UnixLib 5.0.3.1. Its most important fix
for riscos-mesa: threads now run in desktop programs that poll often,
so SDL's sound and OpenAL get time. riscos-mesa's own code is unchanged.

- **Every program is linked with UnixLib 5.0.3.1** (was 5.0.1). The fix
  that matters most here: threads now run in programs that poll the Wimp
  often. Before, a desktop program calling Wimp_Poll more often than every
  2 cs (SDL programs do) never switched threads, so SDL's sound thread
  and OpenAL's mixing got no time. Also: `ctime()` and `asctime()`
  returned a bad pointer, `read()` into a stack buffer could stop a
  program with "EMT trap", child processes tore down their parent's
  thread timer and sound, long sleeps ended early; the heap can grow past
  128 MB, and files over 2GB work for programs built with
  `-D_FILE_OFFSET_BITS=64`. riscos-mesa's libraries don't contain UnixLib,
  so the devkit's libraries work as before; relink your own programs to
  get the fixes. `build/TOOLCHAIN.md` says how to install it.
- **PThreadTicker 0.03** (from UnixLib 5.0.3.1) replaces 0.01 in the
  devkit and the test programs; programs linked with 5.0.3.1 use only
  0.03. The `RMEnsure ... 0.01` lines stay, as its ReadMe advises: with
  an older copy already loaded, a program runs its own copy of the code,
  which also works.

## 20.3.5-10: SDL gets EGL's overlay, a full screen that multitasks, and faster 2D

SDL programs can now use what 20.3.5-9 gave EGL: GL windows can draw at a
smaller size and go through the Pi's hardware overlay. Desktop-size full
screen keeps the desktop multitasking, and SDL's 2D drawing uses the
CPU's NEON or SIMD instructions. It's also the first release with 20.3.5-9's
overlays run on a Pi.

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
- **Fixes found by a code review:**
  - EGL: a TRGB config on a TBGR screen now remakes its sprite when the
    screen changes between 90 and 180 dpi at the same pixel size (it kept
    plotting at the old scale).
  - EGL: looking up a sprite image for a texture takes the lock, so another
    thread creating or destroying images can't race it.
  - EGL: `EGL_LARGEST_PBUFFER` reads back as given; invalid sprite modes
    for pixmaps are refused (the SWI's error flag is now checked); an
    overlay forgets its last buffer when it's destroyed.
  - SDL: the driver listens for Message_ModeChange, so mouse positions
    and plotting follow a desktop mode change between 90 and 180 dpi.
  - Ported programs (the eglut and esUtil helpers) ask EGL whether their
    window has an overlay instead of reading `EGL$Overlay` themselves,
    which missed "ON".
- **SDL full screen can keep multitasking** (asked for by the Freeciv
  port, whose server runs in a TaskWindow). `SDL_WINDOW_FULLSCREEN_DESKTOP`
  now gives a "full window", as RDPClient's: a borderless desktop window
  covering the screen, so other programs keep running, the icon bar pops
  up and other windows can come in front (a click brings the game back).
  `SDL_WINDOW_FULLSCREEN` still owns the screen. The hint or system
  variable `SDL_RISCOS_FULLSCREEN_WINDOW` = `"1"` makes both full windows,
  `"0"` neither (the old behaviour).
- **EGL makes no overlay bigger than 2048x1200 pixels** (or 2048 each
  way); bigger surfaces are plotted. riscos-ffmpeg's Reel found that a 4K
  overlay on a Pi 4 ran the GPU short of memory and blanked the whole
  screen. The EGL guide also says to draw text into an overlay at the
  surface's own pixels: shrinking screen-size text into a smaller frame
  makes it unreadable.
- **SDL uses its ARM NEON and SIMD routines for 2D drawing** (suggested
  by the Freeciv port). SDL 2.26 has them for see-through blits, filling
  rectangles and two pixel conversions, but only switched them on for
  Linux. They're now on for RISC OS too, with SDL checking the CPU when a
  blit is set up (NEON, else ARMv6 SIMD, else the C code as before).
  They're used for sprites onto surfaces without alpha, such as the
  window surface; onto a surface with its own alpha SDL's C code runs as
  before, because the ARM routines leave that alpha alone. Colours come
  out within half a step of the exact blend (the C code is up to 2 steps
  off). Checked through SDL's API on emulated NEON and SIMD-only CPUs
  (`tests/host-harness/sdl-arm`). On a Pi 4 (`sdlblitbench`, a 1024x768
  frame, ms per frame, average of three interleaved runs each):

  | Scene | Before | After | |
  | --- | --- | --- | --- |
  | tiles (soft-edged map tiles) | 6.20 | 3.83 | 1.62x |
  | units (300 round sprites) | 7.38 | 6.42 | 1.15x |
  | glass (see-through sprites) | 8.57 | 5.31 | 1.61x |
  | fill | 0.79 | 0.77 | same |
  | tiles onto a surface with alpha (C code in both) | 6.15 | 5.92 | 1.04x |
  | copy, no blending (same code in both) | 3.41 | 3.40 | same |

  The routines are pixman's (MIT and zlib):
  `LICENCES.txt` has the notices. riscos-openttd gets them only if it
  configures SDL with `--enable-arm-simd --enable-arm-neon`.
- **Smaller changes:**
  - `sdlblitbench`: how fast SDL draws a 2D game's frame (tiles, sprites,
    see-through sprites, fills), for comparing SDL builds.
  - EGL: a surface whose render size equals the size it's shown at uses
    the ordinary plot, not a scaled one.
  - sdlgltest: `-S WxH`, `-V`/`-N` and key D (desktop-size full screen),
    with Obey files `sdl-scaled`, `sdl-scaled-plot` and `sdl-plain`.
  - Host tests: the SDL GL harness runs both routes (the EGL one against
    a fake VideoOverlay), and the examples harness drives example 2's
    menu.
  - SDL's core has one small hook (`src.video.SDL_video.c.p`) so full
    screen keeps the render size.
  - Khronos's dEQP-EGL tests re-run against this release: 858 pass, 12
    fail (one known pbuffer-copy case), no change from 20.3.5-8. Their
    build needed an include path fix.

## 20.3.5-9: hardware overlays and a scaled render size for EGL

EGL can show a window through the display's video overlay (the
VideoOverlay module on a Raspberry Pi), and a program can render at a
smaller size stretched to fill its window or the screen. Both are opt-in.
(First run on a Pi in 20.3.5-10's testing.)

- Hardware overlays for window surfaces (`EGL_RISCOS_overlay`,
  `EGL_OVERLAY_RISCOS`), with fallbacks to plotting, hidden while a window
  or menu overlaps, back to plotting while paused
  (`eglCheckOverlaysRISCOS`); `EGL$Overlay` lets the user force them on
  or off.
- A render size (`EGL_RENDER_WIDTH_RISCOS`/`_HEIGHT_RISCOS`), stretched by
  the overlay or by the sprite plot.
- Screen banks wait only when a vsync hasn't passed yet;
  `eglSwapWouldWaitRISCOS` says whether a swap would block.
- `ovltest`: Pi tests of the VideoOverlay module itself.

Full notes: [docs/history](docs/history/releases-20.3.5-7-to-9.md#2035-9-hardware-overlays-and-a-scaled-render-size-for-egl).

## 20.3.5-8: EGL follows the Khronos rules, Cortex-A8/A9, a devkit guide

EGL was run against Khronos's dEQP-EGL tests and made to follow the EGL
1.4 rules where it didn't: 858 pass (842 before).

- Depth and stencil belong to surfaces; draw and read surfaces can
  differ; one current context per API; per-thread state and a lock.
- Stricter attribute checks; `EGL_CONFORMANT` is 0 (not certified);
  a Khronos registration of the RISC OS extensions ready in `docs/khronos/`.
- Built for VFPv3, so it runs on Cortex-A8/A9 machines too.
- Every program linked with UnixLib 5.0.1, with the PThreadTicker module.
- The devkit gains a beginner's guide and six example programs.
- GLUT: programs with a menu no longer stop drawing after one frame.

Full notes: [docs/history](docs/history/releases-20.3.5-7-to-9.md#2035-8-egl-follows-the-khronos-rules-cortex-a8a9-a-devkit-guide).

## 20.3.5-7: faster rendering, OpenAL, video textures

Rendering is up to 2.9 times as fast as 20.3.5-6 with the same picture
(Pi 4, glbench, 640x480, ms per frame):

| Scene | 20.3.5-6 | 20.3.5-7 | |
| --- | --- | --- | --- |
| lit cube | 5.38 | 3.11 | 1.7x |
| full-screen bilinear texture | 44.04 | 23.47 | 1.9x |
| 12288 lit triangles | 25.79 | 11.06 | 2.3x |
| GLSL per-pixel shaded cube | 108.91 | 38.05 | 2.9x |
| 4 blended full-screen quads | 28.29 | 20.45 | 1.4x |

- A rebuilt GLSL interpreter (programs decoded once, fragment shaders run
  32 pixels at a time); faster depth testing, texturing, blending and
  common 2D drawing; faster start-up and 13 MB less memory per context.
- Video frames as textures with no copy (EGL images of sprites).
- OpenAL Soft in the devkit.
- SDL: quits properly from the desktop, keeps short mouse clicks, icon
  bar icons for 12-character names, a window scale hint.
- A TaskWindow start says what's wrong instead of failing silently.
- `tests/run-all.sh` runs every host test.

Full notes: [docs/history](docs/history/releases-20.3.5-7-to-9.md#2035-7-faster-rendering-openal-video-textures).

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
