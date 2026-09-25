# Porting Mesa's EGL demos (and other eglut programs)

**Worked port:** `ports/mesa-demos`, built by `build/build-ports.sh`. It
has five demos from mesa-demos 9.0.0, all unchanged: `eglgears` (OpenGL
1.x), `gears` and `torus` (OpenGL ES 1.1), `es2gears` (ES 2.0) and
`egltri` (OpenGL). Each runs in a resizable, multitasking desktop window.

## How the demos are built

mesa-demos' `src/egl` programs don't talk to X11 themselves. They use
**eglut**, a tiny GLUT-like library of three files:

- `eglut.c` does all the EGL work: it chooses a config, creates the
  context (`eglBindAPI` + `EGL_CONTEXT_CLIENT_VERSION`), creates the
  window surface, makes it current and swaps.
- `eglutint.h` declares five native-window functions.
- `eglut_x11.c` / `eglut_wayland.c` implement those five for one window
  system.

So only the native layer needs replacing. riscos-mesa's EGL is ordinary
EGL whose native window is a Wimp window handle, so `eglut.c` itself
compiles unchanged.

| eglut native function | X11 does | `eglut_riscos.c` does |
| --- | --- | --- |
| `_eglutNativeInitDisplay` | `XOpenDisplay` | `Wimp_Initialise`; the EGL display is `EGL_DEFAULT_DISPLAY`; optionally redirect output |
| `_eglutNativeInitWindow` | `XCreateWindow` with the config's visual | `Wimp_CreateWindow` + `Wimp_OpenWindow`, centred; the native window is the handle |
| `_eglutNativeFiniWindow` / `FiniDisplay` | destroy the window, close the display | `Wimp_DeleteWindow`, `Wimp_CloseDown` (also at exit) |
| `_eglutNativeEventLoop` | `XNextEvent`: Expose, ConfigureNotify, KeyPress | `Wimp_Poll`: the redraw goes to `eglRedrawWindowRISCOS`; null events drive the idle callback; keys are translated; resizes are found by querying the surface size after each swap |

The whole back end is `ports/mesa-demos/eglut_riscos.c` (about 150
lines), on top of `ports/common/riscos_wimpwin.c`.

## Step by step

1. **Take the sources unchanged:**
   - `src/egl/eglut/eglut.c`, `eglut.h`, `eglutint.h`;
   - the demo;
   - anything from `src/util` it uses (`es2gears` needs `matrix.c`;
     `gl_wrap.h` just includes `GL/gl.h` and `GL/glu.h`, both in the
     devkit).
2. **Replace `eglut_x11.c`** with `eglut_riscos.c` and
   `riscos_wimpwin.c`.
3. **Compile with `-D_GNU_SOURCE`.** `es2gears` calls `sincos`, which
   UnixLib only declares for GNU code (Linux gets it by default).
4. **Link** with `-lEGL -lOSMesa -lstdc++ -lz -lm`:

   ```sh
   $CC $RO_CFLAGS -D_GNU_SOURCE -I$STAGE/include -Iports/common \
       -Iupstream/eglut -Iupstream/util -static \
       upstream/eglut/eglut.c eglut_riscos.c ../common/riscos_wimpwin.c \
       upstream/opengles2/es2gears.c upstream/util/matrix.c \
       -o '!ES2Gears/!RunImage,e1f' -L$STAGE/lib -lEGL -lOSMesa -lstdc++ -lz -lm
   ```

5. **Write a `!Run` file** that sets the output variable and runs the
   program:

   ```
   Set ES2Gears$Dir <Obey$Dir>
   Set EGLUT$Output /|<ES2Gears$Dir>/Output
   Run <ES2Gears$Dir>.!RunImage
   ```

   The demos print (the gears report FPS every 5 seconds), and printing
   from a Wimp task opens a command window. With `EGLUT$Output` set, the
   back end sends stdout and stderr to that file and shows the newest line
   in the title bar. The `|<` keeps `<ES2Gears$Dir>` unexpanded, so
   UnixLib resolves `/<ES2Gears$Dir>/Output` as a path when the program
   opens it.

## Keys

eglut hands out plain characters and `EGLUT_KEY_F1`…`F12`,
`EGLUT_KEY_LEFT` etc. The back end maps the RISC OS key codes (0x181–0x189,
0x1CA–0x1CC and 0x18C–0x18F) to those. Keys the demo has no callback for
go back to the Wimp, so hot keys keep working. Escape reaches the keyboard
callback as 27, and eglut's default callback quits on it, as on Linux.

## Porting other eglut programs

Anything written against eglut builds the same way. Of mesa-demos' other
eglut programs:

- **`clear` and `tri` (ES 1.1):** also work unchanged (checked on the host
  rig).
- **`drawtex`:** needs `GL_OES_draw_texture`, which Mesa's software
  renderer doesn't have; it says so and exits.

Watch for:

- **Programs that only draw on demand** (no idle callback; `egltri`,
  `clear`, `tri`). On X11 they draw their first frame on the first Expose
  event. On RISC OS, EGL answers the Wimp's redraw requests from the
  surface itself, so the back end draws the first frame when the loop
  starts, and draws again whenever the window is opened, moved or
  resized.
- **Programs that use X11 as well:** `bindtex`, `msaa`, `pbuffer`,
  `render_tex`, `two_win`, `es1_info`, `es2tri`, `xeglgears`,
  `xeglthreads` and the `texture_from_pixmap` demos call X11 directly
  and don't use eglut. Replace their window code with a Wimp window, as
  in [Going native](#going-native) below.
- **Multisampling:** `msaa` asks for `EGL_SAMPLES` 1 and
  `EGL_SAMPLE_BUFFERS` 1. riscos-mesa has no multisample configs, so
  remove those attributes when porting it.
- **Pixmaps:** X11 pixmaps have no RISC OS equivalent, but riscos-mesa's
  EGL pixmaps are sprites. A program that renders into a pixmap can make
  a 32bpp sprite instead: see `run_pixmap` in `tests/glestest.c`.

## Going native

eglut is a convenience. A RISC OS program can do the same in its own
Wimp_Poll loop:

```c
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext_riscos.h>

EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
eglInitialize(dpy, NULL, NULL);
eglChooseConfig(dpy, attribs, &cfg, 1, &n);          /* EGL_RENDERABLE_TYPE as needed */
eglBindAPI(EGL_OPENGL_API);                          /* or EGL_OPENGL_ES_API (the default) */
ctx  = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);
surf = eglCreateWindowSurface(dpy, cfg, window_handle, NULL);
eglMakeCurrent(dpy, surf, surf, ctx);

for (;;) {
    Wimp_Poll (null events on while animating)
    case Null_Reason_Code:        draw(); eglSwapBuffers(dpy, surf); break;
    case Redraw_Window_Request:   eglRedrawWindowRISCOS(dpy, block); break;
    case Open_Window_Request:     Wimp_OpenWindow; break;
    case Close_Window_Request:    quit
}
```

`tests/glestest.c` is a complete example; `docs/EGL-GUIDE.md` covers the
rest (work area views, full screen with `EGL_RISCOS_SCREEN_WINDOW`,
sprites).

## Results

**Raspberry Pi 4 (20.3.5-5):** the five demos in the ports zip work in
multitasking desktop windows, with their frame rates in the title bar.

**Host test rig:** on the fake RISC OS (`tests/host-harness/egl`, with the runner in
`tests/host-harness/egl/portrun.c`), all five demos, plus `clear` and
`tri`, drew correctly in a 300x300 window. The rig also checked redraw,
key handling, a window resize (`egltri` redrawn at 500x350) and the FPS
line in the title bar. `es2gears` ran at 357 fps there (x86).
