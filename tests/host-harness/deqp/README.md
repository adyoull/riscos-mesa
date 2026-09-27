# Khronos dEQP-EGL tests on the host

This runs the EGL tests from Khronos's conformance suite, VK-GL-CTS, against
riscos-mesa's EGL. It runs on a Linux x86-64 host, using the fake RISC OS
from `../egl`: a Wimp, a 1920x1080 32bpp screen and OS_SpriteOp. It is
**not** a conformance submission. Only Khronos Adopters can make one, and it
would need the real hardware. It is a way of finding where we differ from
the specification.

## Files

| File | What it does |
| --- | --- |
| `tcuRiscosPlatform.cpp/.hpp` | The dEQP platform. <ul><li>Display: `EGL_DEFAULT_DISPLAY`, and `EGL_PLATFORM_RISCOS` through `EGL_EXT_platform_base`.</li><li>Windows: fake Wimp windows whose screen pixels can be read back.</li><li>Pixmaps: 32bpp sprites in either colour order.</li><li>The EGL library is linked in, and every function is reached through `eglGetProcAddress`.</li></ul> |
| `tcuRiscosMain.cpp` | Keeps all memory below 2 GB, because the fake SWIs pass pointers in 32-bit registers as RISC OS does. <ul><li>`malloc` stays on the brk heap.</li><li>dEQP's `main` runs on a low stack.</li><li>Every thread's stack comes from a pool between 1.75 and 2 GB.</li></ul> It does this with the linker's `--wrap` for `main`, `pthread_create` and `pthread_join`. |
| `riscos.cmake` | The CTS target file, `DEQP_TARGET=riscos`. |
| `build.sh` | Fetches VK-GL-CTS at a pinned commit together with the external sources it pins, adds the files above, and builds `deqp-egl` with our EGL and a host OSMesa. |
| `run.py` | Runs the cases one process per test group. It carries on after a crash or a hang, writes one line per case, and prints a summary. With `--expected` it compares with an earlier run. |

## Running it

You need the host Mesa tree with `patches/mesa` applied and built. It's the
same one `tests/run-all.sh` uses; see `../egl/README.md`.

```sh
M=~/host/mesa OUT=/tmp/deqp tests/host-harness/deqp/build.sh      # about 10 minutes the first time
python3 tests/host-harness/deqp/run.py /tmp/deqp/build/modules/egl/deqp-egl /tmp/deqp/results.txt \
    --timeout 20 --expected tests/host-harness/deqp/expected.txt
```

`expected.txt` is the result of the run below. `run.py` lists every case
whose result changed, and exits 1 if any case that passed no longer does.
Run it after changing `egl/` or `patches/mesa`, and update `expected.txt`
when a change is an improvement.

By default `run.py` runs `dEQP-EGL.info.*` and `dEQP-EGL.functional.*`, 2671
cases, which takes about 10 minutes. It leaves out
`functional.sharing.gles2.multithread.*`: that group has 1254 cases, each
of which runs threads for several seconds. `--exclude` with nothing after it
runs them as well.

## Results (2026-09-27, riscos-mesa 20.3.5-8 in development)

| | Before (20.3.5-7) | After |
| --- | --- | --- |
| Pass | 842 | 858 |
| Fail | 22 | 12 |
| Crash, Timeout, ResourceError | 6 | 0 |
| QualityWarning | 5 | 5 |
| NotSupported | 1796 | 1796 |

**Fixed as a result of these tests:**

- `eglChooseConfig` refuses invalid attribute values.
- `eglSetDamageRegionKHR` refuses surfaces that preserve their contents.
- The platform window pointer is read as an int.
- Depth and stencil belong to surfaces, not contexts (a new OSMesa patch,
  `riscos-osmesa-buffers`).
- Draw and read surfaces can differ.
- There is one current context per client API.
- EGL state is per thread, with locking.

**What's left:**

- **12 fails, `native_color_mapping` and `native_coord_mapping`
  `.pbuffer_to_native_pixmap.*`.** The test copies a pbuffer to a pixmap
  with `eglCopyBuffers`. It creates the pbuffer with no size, so 0x0, and
  expects the copy to fill the pixmap. We copy the overlap, which is nothing.
  Our reading is that the test depends on a driver that ignores the
  pbuffer's size.
- **5 quality warnings.**
  - `get_proc_address.core.gles3`: `eglGetProcAddress` returns a pointer
    for GLES 3 names too. EGL allows this: they are Mesa's dispatch stubs,
    which do nothing for functions the context doesn't have.
  - `resize.back_buffer.*`: when the window is resized, the surface's new
    buffer doesn't keep the old picture. EGL leaves the contents undefined
    after a resize; the test only warns about it.
- **NotSupported (most cases).** These are configs we don't have: every
  config is 32-bit RGBA, with no 16-bit, float, sRGB or multisampled ones.
  Extensions we don't have (robustness, ES 3, mutable render buffer,
  frame timestamps, and so on) are also counted here.

Threads were stress-tested: the `functional.multithread` group ran 150
times in a row (3600 cases) without a failure.
