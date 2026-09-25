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
- `sdl2-configure.ac.host.p`: OpenTTD's triplet fix (arm-riscos-gnueabihf
  is not Linux). `sdl2-configure.ac.osmesa.p`: the OSMesa option.

The older `sdl2-riscos-framebuffer.p` from the buildkit is superseded by
`src.video.riscos.SDL_riscosframebuffer.c.p` and must not be applied.

Regenerate after editing: in a git tree of pristine SDL + these patches,
`git diff --no-prefix <pristine> HEAD -- <file> > src.video.riscos.<file>.p`.
