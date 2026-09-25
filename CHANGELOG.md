# Changelog

Releases are numbered after the Mesa version they contain; `-N` is the Nth
riscos-mesa build of it. Each release's full notes are on the GitHub
[Releases](../../releases) page.

## Unreleased (20.3.5-6): freeglut

Host-tested; not yet tested on a Raspberry Pi.

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
