# Registering the RISC OS EGL extensions with Khronos

riscos-mesa's EGL has two RISC OS extensions, `EGL_RISCOS_wimp_window` and
`EGL_RISCOS_platform_wimp` (`egl/include/EGL/eglext_riscos.h`). They aren't
registered yet, so their enum values (0x3FF0 to 0x3FF5) are **provisional**.
Those values fall inside the range Khronos keeps for future allocations
(0x35B0 to 0x3FFF), so another vendor could be given them one day.
A third, `EGL_RISCOS_overlay` (hardware overlays, render size: 0x3FF6 to
0x3FF8, and `eglCheckOverlaysRISCOS`, `eglSwapWouldWaitRISCOS`), is newer
and not part of this registration yet: it waits for Pi testing.

This folder holds a ready-made registration, to be submitted by the project
owner as a pull request to the Khronos EGL registry:

| File | What it is |
|---|---|
| `EGL_RISCOS_platform_wimp.txt` | The spec for the client extension (`EGL_PLATFORM_RISCOS`), extension #156 |
| `EGL_RISCOS_wimp_window.txt` | The spec for the native types, work area surfaces, screen banks and the two redraw functions, extension #157 |
| `0001-Add-EGL_RISCOS_platform_wimp-and-EGL_RISCOS_wimp_win.patch` | The whole pull request as one commit (`git am`), made against EGL-Registry `main` at db3425b (2026-09-21) |

The patch does the following:

- Reserves the enum block **0x35A0 to 0x35AF** (vendor `RISCOS`). This is the
  lowest free block, as the registry's README asks.
- Adds both extensions to `api/egl.xml`, with their enums, the two
  commands, and `EGL_RISCOS_SCREEN_WINDOW` and the visual IDs as special
  numbers.
- Gives the extensions numbers 156 and 157 in `registry.tcl`, and links
  them from `index.php`.
- Adds a `__riscos__` branch to `api/EGL/eglplatform.h`. It sits before
  `__unix__`, because GCCSDK defines both.
- Regenerates `EGL/egl.h` and `EGL/eglext.h`.

Checked here:

- `make` in `api/` regenerates the headers without errors.
- `make tests` passes with gcc and g++, and also with `-D__riscos__`.
- `egl.xml` validates against `registry.rnc`, using Python's
  rnc2rng and lxml instead of jing.

## How to submit

1. Fork https://github.com/KhronosGroup/EGL-Registry on GitHub, then clone
   the fork on the Mac. Don't use the HIKVISION drive for this.
2. Apply the patch and check that it still fits:
   ```sh
   git checkout -b riscos-extensions origin/main
   git am /path/to/riscos-mesa/docs/khronos/0001-*.patch
   cd api && make clobber && make && make tests
   ```
   If others have registered anything since 2026-09-21, the `git am` may
   conflict, or the block or the numbers may already be taken. In that
   case, take the next free 16-value block (search egl.xml for
   "Reservable for future use") and the next extension number (search
   registry.tcl for "Next free extension number"). Then change the two
   `.txt` files to match.
3. Push the branch to the fork and open a pull request against `main`. A
   title such as "Register EGL_RISCOS_wimp_window and
   EGL_RISCOS_platform_wimp" will do; the commit message works as the
   description.
4. Khronos reviews it, usually through comments on the PR. **The values
   aren't ours until the PR is merged.**

Registering extensions and enum values is free. It's separate from Khronos
conformance certification, which needs membership or an Adopter fee.

## After the pull request is merged

Change riscos-mesa as follows:

- **`egl/include/EGL/eglext_riscos.h`:**
  - Change the values to the registered ones (0x35A0 to 0x35A5 if the
    block above is granted).
  - Remove "PROVISIONAL" from the header comment.
- **The library:** keep accepting the old values 0x3FF0 to 0x3FF5 as
  aliases, so programs built against the old header keep working. A
  `case` label for each is enough.
- **Tests:**
  - `tests/host-harness/deqp/tcuRiscosPlatform.cpp` has its own copy of
    `EGL_PLATFORM_RISCOS`. Update it.
  - Add harness checks that the old values are still accepted.
- **Docs:**
  - Put a CHANGELOG entry saying that programs should be rebuilt against
    the new header, although the old values still work.
  - Mention the registration in `docs/EGL-GUIDE.md`.
