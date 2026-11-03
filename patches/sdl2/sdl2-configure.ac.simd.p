--- configure.ac
+++ configure.ac
@@ -4558,6 +4558,10 @@
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
