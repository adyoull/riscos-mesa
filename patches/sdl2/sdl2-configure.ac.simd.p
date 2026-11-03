--- configure.ac
+++ configure.ac
@@ -4566,6 +4566,10 @@
         CheckVisibilityHidden
         CheckWerror
         CheckDeclarationAfterStatement
+        # 2026: SDL's ARM SIMD and NEON blitters (--enable-arm-simd,
+        # --enable-arm-neon); SDL_cpuinfo.c checks the CPU at run time
+        CheckARM
+        CheckNEON
         CheckDummyVideo
         CheckOffscreenVideo
         CheckDiskAudio
