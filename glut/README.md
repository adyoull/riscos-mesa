# freeglut for RISC OS

A native RISC OS back end for [freeglut](https://github.com/freeglut/freeglut)
3.8.0, the open source GLUT, over riscos-mesa's EGL.
`build/build-freeglut.sh` fetches freeglut at its pinned commit, applies
`patches/freeglut/`, adds this back end as `src/riscos` and builds:

- `libglut.a` (OpenGL 2.1) and `GL/glut.h`, `GL/freeglut*.h`:
  link `-lglut -lGLU -lEGL -lOSMesa -lstdc++ -lz -lm`;
- `libfreeglut-gles.a` (OpenGL ES 1.1/2.0, compile with `-DFREEGLUT_GLES`):
  link `-lfreeglut-gles -lEGL -lOSMesa -lstdc++ -lz -lm`.

How to use it, what works and what doesn't: [docs/porting/glut.md](../docs/porting/glut.md).

## The back end (`riscos/`)

| File | What it does |
| --- | --- |
| `fg_internal_riscos.h` | the platform types: a window handle is a Wimp window handle; per-window EGL surface and state |
| `fg_init_riscos.c` | Wimp_Initialise (task name from the `!App` directory), EGL set-up, game mode, stubs for devices RISC OS lacks |
| `fg_window_riscos.c` | Wimp windows and EGL surfaces (visible area for a window, a work area rectangle for each subwindow), contexts, swaps, full screen, titles, the pointer |
| `fg_main_riscos.c` | the Wimp_Poll loop, keys (releases found with OS_Byte 121), the mouse (buttons, motion and entry from Wimp_GetPointerInfo, the wheel from OS_Pointer 2) |
| `fg_menu_riscos.c` | GLUT menus shown as Wimp menus |
| `fg_state_riscos.c` | `glutGet` / `glutDeviceGet` |
| `fg_gles_riscos.c` | the ES build: font stubs, and freeglut's menu code with its GL drawing turned off |

The patch to freeglut itself is small: it recognises `__riscos__`
(`TARGET_HOST_RISCOS`, tested before the Unix check since GCCSDK defines
`__unix__`), includes the header above, and lets single-buffered windows
be shown after each display callback.

Subwindows rely on riscos-mesa's EGL stacking work area surfaces in
creation order (a subwindow's subwindow is drawn over it).

## Testing

`tests/host-harness/glut/run.sh` builds the library for Linux against the
fake RISC OS of `tests/host-harness/egl` and runs freeglut's demos with
scripted keys, clicks, drags, menu choices, the wheel and resizing.

## Licence

The back end is MIT (the same terms as freeglut, see the file headers);
freeglut is MIT/X-style (`LICENCES.txt`, section 10).
