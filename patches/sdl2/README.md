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
  with `--enable-video-riscos-osmesa`. Desktop GL 2.1, and (2026-09-25)
  OpenGL ES 1.1 / 2.0 with `SDL_GL_CONTEXT_PROFILE_ES`, which needs
  riscos-mesa's OSMesa patch (`OSMESA_ES1_PROFILE`/`OSMESA_ES2_PROFILE`). Without that flag the library has no
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
- High resolution desktops (2026-09-25, from riscos-openttd commit
  a062b36): in an EX0 EY0 ("180 dpi") mode a desktop window is shown with
  each SDL pixel as 2x2 screen pixels, as a 90 dpi mode would show it, and
  the mouse position is scaled to match. It falls back to 1:1 if the
  doubled window wouldn't fit. `SDL$WindowScale` (1 = off, 2-4) overrides
  it. The plot works out the scale from the sprite's own resolution too, so
  a 90 dpi sprite (non-16M-colour screens) isn't doubled twice. Full screen
  is unaffected. GL windows are scaled the same way (GL renders at the
  window's SDL size).
- `sdl2-configure.ac.host.p`: OpenTTD's triplet fix (arm-riscos-gnueabihf
  is not Linux). `sdl2-configure.ac.osmesa.p`: the OSMesa option.

The older `sdl2-riscos-framebuffer.p` from the buildkit is superseded by
`src.video.riscos.SDL_riscosframebuffer.c.p` and must not be applied.

Regenerate after editing: in a git tree of pristine SDL + these patches,
`git diff --no-prefix <pristine> HEAD -- <file> > src.video.riscos.<file>.p`.

## Licence
These patches change SDL files, so they are under SDL's zlib licence, like
the files they change (including the new SDL_riscosopengl.c/.h, which
carry SDL's notice). The rest of riscos-mesa is MIT: see LICENCES.txt.
