diff --git src/timer/unix/SDL_systimer.c src/timer/unix/SDL_systimer.c
index 406bd48..48b3a13 100644
--- src/timer/unix/SDL_systimer.c
+++ src/timer/unix/SDL_systimer.c
@@ -183,6 +183,14 @@ SDL_GetPerformanceFrequency(void)
     return 1000000;
 }
 
+#ifdef __riscos__
+/* 2026: set by the RISC OS video driver. While the program is a Wimp task
+   with a desktop window, SDL_Delay must yield to the other tasks instead of
+   UnixLib's busy-wait (which freezes the desktop). Returns non-zero if it
+   did the waiting. */
+int (*SDL_RISCOS_DelayHook)(Uint32 ms) = NULL;
+#endif
+
 void
 SDL_Delay(Uint32 ms)
 {
@@ -203,6 +211,12 @@ SDL_Delay(Uint32 ms)
     }
 #endif
 
+#ifdef __riscos__
+    if (SDL_RISCOS_DelayHook && SDL_RISCOS_DelayHook(ms)) {
+        return;
+    }
+#endif
+
     /* Set the timeout interval */
 #if HAVE_NANOSLEEP
     elapsed.tv_sec = ms / 1000;
