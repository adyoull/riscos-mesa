# SDL 2.26.0 RISC OS overlay (shared with riscos-openttd)

One set of per-file patches, GCCSDK autobuilder style (`patch -p0`, any
order), used by BOTH projects:

- OpenTTD's Wimp driver: desktop windows, icon bar icon + Quit menu,
  full screen single tasking, typing, eig caching, direct-to-screen full
  screen framebuffer (from openttd-riscos-buildkit.tgz).
- Fix 13: stay a Wimp task in full screen (no Wimp_CloseDown), become a
  task at VideoInit when the desktop is running, Wimp_SetMode instead of
  OS_ScreenMode while a task. Re-applied here from the START-HERE
  description: **compare with riscos-openttd commit 9d90de1 and keep one.**
- OpenGL via OSMesa (`SDL_riscosopengl.[ch]` + small hooks), compiled only
  with `--enable-video-riscos-osmesa`. Without that flag the library has no
  GL code at all (checked: no GL symbols), i.e. it is the OpenTTD driver.
- Scroll wheel (2026-09-25, from riscos-openttd commit 210ba99): read with
  `OS_Pointer 2` on every poll and sent as `SDL_MOUSEWHEEL`, in a window
  (while the pointer is over it) and in full screen. RISC OS 5 on the Pi
  doesn't send Wimp `Scroll_Request` events. It's in
  `src.video.riscos.SDL_riscosevents.c.p`; `scroll-wheel-only.diff` is the
  same change on its own, against the previous events patch.
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
- `sdl2-configure.ac.host.p`: OpenTTD's triplet fix (arm-riscos-gnueabihf
  is not Linux). `sdl2-configure.ac.osmesa.p`: the OSMesa option.

The older `sdl2-riscos-framebuffer.p` from the buildkit is superseded by
`src.video.riscos.SDL_riscosframebuffer.c.p` and must not be applied.

Regenerate after editing: in a git tree of pristine SDL + these patches,
`git diff --no-prefix <pristine> HEAD -- <file> > src.video.riscos.<file>.p`.
