# Registered RISC OS names

The names riscos-mesa uses that RISC OS Open has reserved for it, and the
ones still to ask for.

## Reserved

- 7 November 2026: `EGL$Overlay`, `SDL$WindowScale`, `SDL$PointerMargin`
  (system variables).
- 18 November 2026: application name `MesaLibs`; system variables
  `EGL$Overlapped`, `EGL$OverlayStatus`, `EGL$SwapInterval`,
  `EGL$WindowScale`, `FreeGLUT$PointerMargin`, `FreeGLUT$TaskName`,
  `OpenAL$NoNEON`, `OpenAL$PeriodSize`, `SDL$IconSprite`, `V3DM$Dump`,
  `V3DM$DumpJobs`, `V3DM$Fake`, `V3DM$NoImport`, `V3DM$Stats`,
  `V3DM$Tiled`, `V3DM$Trace`, `V3DM$Uncached`.

## Not yet reserved

- `V3DM$Why` (from 20.3.5-20).
- `DispmanX$Display` (the DispmanX compatibility library).

The `SDL_RISCOS_...` hints, which can also be set as system variables,
and Mesa's own `LIBGL_ALWAYS_SOFTWARE` are not RISC OS-style names and
aren't requested. `RISC3D$...` names belong to RISC3D.

New names go in a request before the release that uses them.
