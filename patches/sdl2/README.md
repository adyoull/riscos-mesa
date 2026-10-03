# SDL 2.26.0 RISC OS overlay (authoritative copy)

**This directory is the master copy of the overlay.** riscos-openttd (and
any other project) uses a copy of it made with
`tools/sdl-overlay-export.sh DEST_DIR`, which also writes a `SOURCE` file
naming the riscos-mesa commit. Changes are made here, in riscos-mesa, and
then copied out; a change needed by another project comes here as a
request (a handoff), not as an edit to its copy.
`tools/sdl-overlay-check.sh DIR` reports whether a copy still matches.

One set of per-file patches, GCCSDK autobuilder style (`patch -p0`, in
name order: the configure.ac ones build on each other). The GL code is in these files, and the configure option decides
whether it is compiled:

- **riscos-mesa** (`build/build-sdl2.sh`) configures with
  `--enable-video-riscos-osmesa`: libSDL2 with OpenGL / OpenGL ES through
  OSMesa.
- **riscos-openttd** configures the same files without that option:
  libSDL2 with no GL code at all (no OSMesa needed to link).

It contains:

- OpenTTD's Wimp driver: desktop windows, icon bar icon + Quit menu,
  full screen single tasking, typing, eig caching, direct-to-screen full
  screen framebuffer (from openttd-riscos-buildkit.tgz).
- Fix 13: stay a Wimp task in full screen (no Wimp_CloseDown), become a
  task at VideoInit when the desktop is running, Wimp_SetMode instead of
  OS_ScreenMode while a task. Re-applied here from the OpenTTD port's notes;
  reconciled with riscos-openttd commit 9d90de1 (the same code).
- OpenGL via OSMesa (`SDL_riscosopengl.[ch]` + small hooks; through
  riscos-mesa's EGL since 2026-09-29, see below), compiled only
  with `--enable-video-riscos-osmesa`. Desktop GL 2.1, and (2026-09-25)
  OpenGL ES 1.1 / 2.0 with `SDL_GL_CONTEXT_PROFILE_ES`, which needs
  riscos-mesa's OSMesa patch (`OSMESA_ES1_PROFILE`/`OSMESA_ES2_PROFILE`). Without that flag the library has no
  GL code at all (checked: no GL symbols), i.e. it is the OpenTTD driver.
- Scroll wheel (2026-09-25, from riscos-openttd commit 210ba99): read with
  `OS_Pointer 2` on every poll and sent as `SDL_MOUSEWHEEL`, in a window
  (while the pointer is over it) and in full screen. RISC OS 5 on the Pi
  doesn't send Wimp `Scroll_Request` events. It's in
  `src.video.riscos.SDL_riscosevents.c.p`.
- Cooperative multitasking (2026-09-25): nothing in the driver may stop
  other tasks while the program has a desktop window.
  - `SDL_Delay` yields with Wimp_PollIdle (whole centiseconds; the
    sub-centisecond rest is a short busy-wait) instead of UnixLib's
    busy-wait, which froze the desktop. Hook in
    `src.timer.unix.SDL_systimer.c.p`; main thread only.
  - `SDL_WaitEvent`/`SDL_WaitEventTimeout` block in Wimp_PollIdle with null
    events off, so an idle program uses no CPU. While the pointer is over
    the window it wakes every 2 cs to sample the mouse (the Wimp has no
    motion events). `SDL_SendWakeupEvent` sets an RMA pollword.
  - GL vsync in a window paces to the display rate with the same
    cooperative wait; `OS_Byte 19` is only used full screen.
  - Full screen stays single tasking by design (no Wimp_Poll).
- High resolution desktops (2026-09-25, from riscos-openttd commit
  a062b36): in an EX0 EY0 ("180 dpi") mode a desktop window is shown with
  each SDL pixel as 2x2 screen pixels, as a 90 dpi mode would show it, and
  the mouse position is scaled to match. It falls back to 1:1 if the
  doubled window wouldn't fit. The hint `SDL_RISCOS_WINDOW_SCALE` (1 = off,
  2-4; `SDL_SetHint` or a system variable of that name) overrides it, and
  so does the older `SDL$WindowScale` variable if the hint isn't set. The plot works out the scale from the sprite's own resolution too, so
  a 90 dpi sprite (non-16M-colour screens) isn't doubled twice. Full screen
  is unaffected. GL windows are scaled the same way (GL renders at the
  window's SDL size).
- The program's own name and icon (2026-09-25): the task name, icon bar
  sprite and icon bar menu title come from the program's application
  directory. A program run as `...!TestGL2.!RunImage` is "TestGL2" in the
  Task Manager and the menu, and its icon is the `!TestGL2` sprite if the
  Wimp sprite pool has it (the Filer or `IconSprites` in `!Run` loads it),
  otherwise the generic `application` sprite. `SDL_HINT_APP_NAME` (or the
  `SDL_APP_NAME` variable) overrides the name. Every desktop program now
  gets an icon bar icon with a Quit menu.
  - This replaces the global `SDL$IconSprite` variable, which isn't read
    any more. Once OpenTTD had set it, every SDL program run afterwards
    showed OpenTTD's sprite and name. OpenTTD's own icon is found the new
    way (`!OpenTTD`), so its `Set SDL$IconSprite` line is now unnecessary.
  - Before, the task name was always "SDL", because Wimp_Initialise
    happened before any window title was set.
- Sound (2026-09-26, from riscos-openttd commit a34e9bd): a RISC OS audio
  driver, `src/audio/riscos/SDL_riscosaudio.[ch]`, playing through the
  SharedSoundBuffer and StreamManager modules (over SharedSound, which is
  part of RISC OS; the other two are John Duffell's freeware, in the
  `ssb.zip` download on Andrew Sellors' RDPClient page,
  <https://orac.co.uk/software/rdpclient/rdpclient.html>; John Duffell's own site, now on the Internet Archive, has more
  details: <https://web.archive.org/web/20110920080106/http://www.duffell.riscos.me.uk/>),
  so SDL programs' sound mixes with other programs'. S16 stereo at the
  program's rate (SharedSoundBuffer resamples); about 60 ms queued; the
  audio thread sleeps rather than spins while the queue drains. It comes
  before SDL's `dsp` driver; if the modules aren't loaded it declines, and
  SDL falls back to `dsp` (UnixLib's `/dev/dsp` over DigitalRenderer).
  Registered by `sdl2-configure.ac.riscosaudio.p`,
  `include.SDL_config.h.in.p`, `src.audio.SDL_audio.c.p` and
  `src.audio.SDL_sysaudio.h.p`; configure reports
  `Audio drivers : disk dummy oss riscos`. Programs' `!Run` files should
  RMEnsure SSound, StreamMan and SSBuffer (see ports/sdl2-tests `!LoopWave`).
- Quitting from the desktop (2026-09-27, requested by riscos-openttd;
  written from the PRM/ROOL documentation):
  - Message_PreQuit (the Task Manager's Quit, or a desktop shutdown) is
    acknowledged, which stops the quit, and SDL_QUIT is posted so the
    program can quit its own way (confirm, save). For a desktop shutdown
    (flag bit 0 clear), when the program then quits, the shutdown is
    restarted with a Ctrl-Shift-F12 key press (Wimp_ProcessKey), before
    Wimp_CloseDown or at exit. Only if it quits within 30 s of the PreQuit
    and not by the icon bar menu or close icon, since SDL can't say
    whether the user answered "yes": quitting later, after "no", must not
    shut the desktop down.
  - Message_Quit posts SDL_APP_TERMINATING and SDL_QUIT; if the program
    takes them and carries on, the driver calls SDL_Quit and exits, as the
    Wimp requires.
  - The close icon sends SDL_WINDOWEVENT_CLOSE (SDL posts SDL_QUIT for the
    last window unless SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE is 0); the icon
    bar menu's Quit still sends SDL_QUIT.
  - SDL_ShowWindow / SDL_HideWindow open and close the desktop window
    (it reopens where it was); SDL_WINDOW_HIDDEN leaves it closed.
  - `SDL_riscoswimp.h`: the Wimp blocks the driver uses as structures
    (window state, redraw, pointer, caret/key, message...), from the PRM
    layouts with compile-time size checks, instead of int arrays with
    numeric offsets. The conversion changes no code (checked on the
    compiled objects).
  - Tested by `tests/host-harness/sdl-wimp`.
- Tidy-ups (2026-09-27, requested by riscos-openttd, from its 67f38d2; no
  change in behaviour): `RISCOS_IsWindowed()` instead of `wimp_window !=
  0` (21 places; the compiled code is identical); the window title and
  scroll wheel state are per device, in `SDL_VideoData`; the one desktop
  window limit is written down; the window scale is also an SDL hint
  (`SDL_HINT_RISCOS_WINDOW_SCALE`).
- Icon bar sprite names of 12 characters (2026-09-27, reported by the
  Warzone 2100 port): the name was copied into the icon with a 12-byte
  string copy, which keeps 11 characters, so `!Warzone2100` became
  `!Warzone210` and the icon was blank. `RISCOS_IconSpriteName`
  (`SDL_riscoswimp.h`) copies up to 12 characters with no terminator
  needed; checked by `tests/host-harness/sdl-wimp`.
- GL windows through EGL, opt-in (2026-09-29, requested by the Warzone
  2100 port; GL builds only, nothing changes without
  `--enable-video-riscos-osmesa`):
  - By default a GL window still renders into its sprite (the "sprite
    path", unchanged). With any of the hints below, `SDL_riscosopengl.c`
    uses the EGL path instead: the window is an EGL window surface
    (riscos-mesa's libEGL) on the desktop window, or on the screen (-1)
    full screen; the context is an EGL context in the screen's colour
    order; redraw requests go to `eglRedrawWindowRISCOS`.
    `SDL_RISCOS_GL_EGL` = "1" selects it on its own. **GL programs link
    `-lEGL`** (libSDL2 contains both paths): `-lSDL2 -lGLU -lEGL -lOSMesa
    ...`; build-sdl2.sh adds `-I egl/include`.
  - Render size: the hint `SDL_RISCOS_GL_RENDER_SIZE` = `"WxH"` (or a
    system variable of that name), read when a GL window is made. The
    program then sees a WxH window (`SDL_GetWindowSize`, drawable size,
    window events, mouse coordinates scaled from the desktop window or
    the screen), while the desktop window keeps the size it asked for
    (`SDL_riscoswindow.h`: `render_w/h`, `disp_w/h`, `RISCOS_ShownW/H`).
    EGL renders at WxH and stretches it. Full screen keeps WxH, stretched
    to the screen: `src.video.SDL_video.c.p` is a small hook in
    `SDL_UpdateFullscreenMode` (`RISCOS_KeepsRenderSize`) so SDL doesn't
    take the screen mode's size.
  - Overlay: `SDL_RISCOS_GL_OVERLAY` "1"/"0" asks for / refuses EGL's
    hardware overlay (`EGL_RISCOS_overlay`); unset, `EGL$Overlay` decides.
    A frame EGL would have to wait a vsync for is held and shown from
    PumpEvents (`RISCOS_GL_Idle`), which also keeps the overlay right
    while nothing is swapped, so a swap never blocks the desktop. Only
    the event loop's thread holds frames; a GL thread's swap waits (at
    most a vsync) instead.
  - On the EGL path the EX0 EY0 2x window scale is EGL's stretch too
    (the surface always renders at the SDL window size).
  - Checked by `tests/host-harness/harness.c` (the driver file, both
    paths, against the real EGL and the fake RISC OS and VideoOverlay) and
    `sdl-wimp` (mouse scaling).
- Full screen that multitasks: the "full window" (2026-09-30, requested by
  the Freeciv port, whose game server runs in a TaskWindow that full
  screen used to stop). `SDL_WINDOW_FULLSCREEN_DESKTOP` gives a borderless
  Wimp window the size of the screen, as RDPClient's full window mode: the
  program keeps polling the Wimp, so other tasks run, the icon bar pops
  up, other windows can come in front, and a click on the game brings it
  back to the front. It's drawn, sized and scaled like any desktop window
  (scale 1; a GL render size is stretched to the screen, through the
  overlay if asked). A desktop mode change resizes it and sends
  `SDL_WINDOWEVENT_RESIZED`. `SDL_WINDOW_FULLSCREEN` (with its mode
  change) still owns the screen, for speed. The hint or system variable
  `SDL_RISCOS_FULLSCREEN_WINDOW` = `"1"` makes that a full window too
  (after Wimp_SetMode), and `"0"` gives the single tasking kind for both,
  as before. `sdl-wimp` checks the click and the mode change.
- ARM SIMD and NEON blitters (2026-09-30, suggested by the Freeciv port):
  SDL 2.26 has ARM assembly for per-pixel alpha blits (32 bpp onto 32 bpp,
  and onto RGB565), filling rectangles and two pixel format conversions
  (from pixman, MIT licence, see LICENCES.txt), but its configure only
  turns them on for Linux. `sdl2-configure.ac.simd.p` turns them on for
  RISC OS, and `build/build-sdl2.sh` configures with `--enable-arm-simd
  --enable-arm-neon` (and checks they took). SDL checks the CPU when a
  blit is set up: NEON through VFPSupport, ARMv6 SIMD through
  OS_PlatformFeatures (`SDL_cpuinfo.c`, unchanged). The alpha routines
  leave the destination's alpha byte alone, where SDL's C code blends it,
  so `src.video.SDL_blit_A.c.p` uses them only for destinations without
  alpha, such as the window surface; onto a surface with alpha, the C code
  runs as before. Their colours are within half a step of the exact blend
  (the C code's are up to 2 steps off). Checked by
  `tests/host-harness/sdl-arm` (on emulated NEON and SIMD-only CPUs).
  On a Pi 4, sprites with soft edges or see-through all over draw about
  1.6 times as fast (`sdlblitbench`; figures in the CHANGELOG).
  riscos-openttd gets them only if it configures with the same two
  options; without them `SDL_blit_A.c` is the same as before.
- Desktop mode changes (2026-10-02, from a code review): on
  Message_ModeChange the driver reads the eig factors and SDL's desktop
  display mode again (`RISCOS_DesktopModeChanged`, new
  `src.video.riscos.SDL_riscosmodes.h.p`; not while SDL has set a mode of
  its own), and fits the desktop window to the new mode
  (`RISCOS_WindowModeChanged`): a window's scale and extent are worked
  out again, so its picture and the mouse stay right across a change
  between 90 and 180 dpi; a full window takes the new screen's size. A
  full window that can't be made falls back to single tasking full
  screen. On the EGL path the mouse is mapped across the window's visible
  area, which EGL stretches the frame over.
- `sdl2-configure.ac.host.p`: OpenTTD's triplet fix (arm-riscos-gnueabihf
  is not Linux). `sdl2-configure.ac.osmesa.p`: the OSMesa option.
  `sdl2-configure.ac.simd.p`: the ARM blitters (above).

The older `sdl2-riscos-framebuffer.p` from the buildkit is superseded by
`src.video.riscos.SDL_riscosframebuffer.c.p` and must not be applied.

## Changing the overlay

1. Edit the C files in the SDL tree that `build/build-sdl2.sh` makes
   (`src/SDL-release-2.26.0`), never the `.p` files themselves.
2. Rebuild (`build/build-sdl2.sh`) and run the host tests
   (`tests/run-all.sh`: the `sdl` and `sdl-wimp` steps).
3. Run `tools/sdl-overlay-regen.sh`. It rewrites each `.p` from the tree
   (a diff of pristine SDL against it, without `index` lines, so only real
   changes show) and then checks that pristine SDL plus the `.p` files
   gives the tree exactly. `--check` does only the check.
4. Commit the `.p` files. If anything OpenTTD's build compiles changed (all
   but the GL files), write the riscos-openttd handoff to re-export.

The four `sdl2-configure.ac.*.p` files patch the same file in turn (in
name order), so the script only checks them: edit those by hand.
--check also applies them after the GCCSDK autobuilder's own libsdl2
`configure.ac.p` (riscos-openttd builds with that recipe), when it finds the
autobuilder (`GCCSDK_AUTOBUILDER=<dir>`, or next to `GCCSDK_ENV`): that patch
changes the RISC OS section too, so a hunk whose context it touches fails
there although it applies to pristine SDL.

`build/build-sdl2.sh` notices when the `.p` files have changed since its
tree was made (after a `git pull`, say) and stops if the tree no longer
matches them, rather than building old code.

## Licence
These patches change SDL files, so they are under SDL's zlib licence, like
the files they change (including the new SDL_riscosopengl.c/.h, which
carry SDL's notice). The rest of riscos-mesa is MIT: see LICENCES.txt.
