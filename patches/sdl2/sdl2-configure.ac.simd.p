--- configure.ac
+++ configure.ac
@@ -4554,6 +4554,10 @@
         CheckOSS
         CheckPTHREAD
         CheckClockGettime
+        # 2026: SDL's ARM SIMD and NEON blitters (--enable-arm-simd,
+        # --enable-arm-neon); SDL_cpuinfo.c checks the CPU at run time
+        CheckARM
+        CheckNEON
 
         # Set up files for the misc library
         if test x$enable_misc = xyes; then
