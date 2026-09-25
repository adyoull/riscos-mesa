# Porting the OpenGL ES 2.0 Programming Guide samples (esUtil)

**Worked port:** `ports/esbook`, built by `build/build-ports.sh`. All ten
LinuxX11 samples from the *OpenGL ES 2.0 Programming Guide* (Munshi,
Ginsburg, Shreiner) build unchanged and run in desktop windows: Chapter 2
Hello_Triangle to Chapter 13 ParticleSystem.

**Licence:** the samples' repository
([danginsburg/opengles-book-samples](https://github.com/danginsburg/opengles-book-samples))
has no licence, so the samples can't be redistributed. The repository
holds only riscos-mesa's own `esUtil_RISCOS.c` (MIT).
`build/build-ports.sh` fetches the samples at a pinned commit (604a02cc)
and builds them into `stage/ports/esbook` for your own use; they aren't in
release zips. (The ES 3.0 book's samples are MIT, but they need OpenGL ES
3.0, which riscos-mesa doesn't have.)

## How the samples are built

Every sample calls the book's framework, **esUtil**:

- `esInitContext`, `esCreateWindow(ctx, title, w, h, flags)`;
- `esRegisterDrawFunc` / `UpdateFunc` / `KeyFunc`;
- `esMainLoop`.

The framework has four files:

- `esShader.c`, `esShapes.c`, `esTransform.c`: portable (GL ES and
  maths only).
- `esUtil.c`: the only file that knows about X11. It opens the display
  and window, creates the EGL context, runs the loop, logs, and loads
  TGA files.

`ports/esbook/esUtil_RISCOS.c` replaces `esUtil.c`. It implements the same
functions from `esUtil.h` over `ports/common/riscos_wimpwin.c`:

- **`esCreateWindow`:** opens a Wimp window, then does the book's EGL
  set-up with the window handle as the native window. It turns the
  `ES_WINDOW_*` flags into config attributes, and creates an ES 2.0
  context (`EGL_CONTEXT_CLIENT_VERSION` 2).
- **`esMainLoop`:** a Wimp_Poll loop. On each null event it runs update,
  draw and `eglSwapBuffers`, then updates `esContext->width/height` if
  the window was resized. Redraws go to `eglRedrawWindowRISCOS`. It
  prints FPS every 2 seconds, as the X11 version does.
- **`esLoadTGA`:** the same uncompressed 24/32-bit loader. It also looks
  in `<ESUtil$Dir>`, so a sample finds its `.tga` files in its
  application directory whatever the current directory is.

## Step by step

1. Take `Common/esUtil.h`, `esShader.c`, `esShapes.c`, `esTransform.c`
   and the sample, all unchanged.
2. Replace `esUtil.c` with `esUtil_RISCOS.c` + `riscos_wimpwin.c`.
3. Link with `-lEGL -lOSMesa -lstdc++ -lz -lm` (instead of `-lGLESv2
   -lEGL -lX11`).
4. In `!Run`, set `ESUtil$Dir` to the application directory, and
   `ESUtil$Output` to a file for the text output:

   ```
   Set ESUtil$Dir <Obey$Dir>
   Set ESUtil$Output /|<ESUtil$Dir>/Output
   Run <ESUtil$Dir>.!RunImage
   ```

5. Copy the sample's `.tga` files into the application directory.

## Differences you'll see

- **Multisampling:** `ES_WINDOW_MULTISAMPLE` is ignored, with a message.
  riscos-mesa has no multisample configs.
- **The Multisample sample:** it shows one yellow square instead of four
  coloured ones. That's the sample, not the port. It's a copy of the
  stencil test that asks only for `ES_WINDOW_RGB`, so it gets no stencil
  buffer. On Linux it only works if the driver happens to hand out a
  stencil buffer anyway.
- **ParticleSystem:** it draws nothing for its first second. The sample
  sets its uniforms in the first update, before any `glUseProgram`
  (Mesa reports `GL_INVALID_OPERATION`), so the first burst has no
  colour. It also calls `glEnable(GL_TEXTURE_2D)`, which isn't valid in
  ES 2.0. Both happen on Linux too.
- **MultiTexture, Stencil_Test and ParticleSystem** call `eglSwapBuffers`
  in their draw function as well as in the main loop, so each frame is
  shown twice. The book's X11 framework does the same.
- **Escape or the close icon quits.** On X11 the samples only stop when
  the window is closed.

## Going native

esUtil is a teaching framework. For your own program, keep your drawing
code and write the set-up and loop directly, as in `esCreateWindow` and
`esMainLoop` in `esUtil_RISCOS.c`. That is the native RISC OS EGL pattern
described in [mesa-demos.md](mesa-demos.md#going-native) and
`docs/EGL-GUIDE.md`.

## Results on the host test rig

All ten samples ran on the fake RISC OS (`tests/host-harness/egl`), and
their pictures were checked against the book's descriptions: triangle,
vertex shader cube, textures, mipmaps, cube map, wrap modes, the light-mapped
multitexture, stencil test and point-sprite particles. They haven't been
run on RISC OS hardware yet.
