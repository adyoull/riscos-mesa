diff --git src/cpuinfo/SDL_cpuinfo.c src/cpuinfo/SDL_cpuinfo.c
--- src/cpuinfo/SDL_cpuinfo.c
+++ src/cpuinfo/SDL_cpuinfo.c
@@ -675,7 +675,12 @@ int SDL_GetCPUCount(void)
 {
     if (!SDL_CPUCount) {
 #ifndef SDL_CPUINFO_DISABLED
-#if defined(HAVE_SYSCONF) && defined(_SC_NPROCESSORS_ONLN)
+/* RISC OS (riscos-mesa): not through sysconf. UnixLib 5.0.3.11's headers
+   name _SC_NPROCESSORS_ONLN and _SC_PHYS_PAGES at last, and asking would
+   make the shared SDL need sysconf from the program, which mixed programs
+   linked before don't give (build/build-shared.sh's check). UnixLib
+   answers 1 processor anyway, as the count falls back to here. */
+#if defined(HAVE_SYSCONF) && defined(_SC_NPROCESSORS_ONLN) && !defined(__riscos__)
         if (SDL_CPUCount <= 0) {
             SDL_CPUCount = (int)sysconf(_SC_NPROCESSORS_ONLN);
         }
@@ -1103,7 +1108,7 @@ int SDL_GetSystemRAM(void)
 {
     if (!SDL_SystemRAM) {
 #ifndef SDL_CPUINFO_DISABLED
-#if defined(HAVE_SYSCONF) && defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
+#if defined(HAVE_SYSCONF) && defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE) && !defined(__riscos__)
         if (SDL_SystemRAM <= 0) {
             SDL_SystemRAM = (int)((Sint64)sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE) / (1024 * 1024));
         }
