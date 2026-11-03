diff --git src/thread/pthread/SDL_syssem.c src/thread/pthread/SDL_syssem.c
--- src/thread/pthread/SDL_syssem.c
+++ src/thread/pthread/SDL_syssem.c
@@ -97,6 +97,29 @@ int SDL_SemWait(SDL_sem *sem)
     return retval;
 }
 
+#ifdef __riscos__
+/* 2026: UnixLib before 5.0.3.6 has sem_timedwait only as a stub that always
+   fails with ENOSYS. This SDL may have been built with a newer UnixLib
+   (so it calls sem_timedwait) and then linked into a program with an older
+   one, so when sem_timedwait says it doesn't exist, wait as SDL does
+   without it: try, then sleep a millisecond, until the time is up.
+   Otherwise SDL_SemWaitTimeout would fail at once and SDL's timer thread
+   would never sleep. */
+static int RISCOS_SemWaitTimeoutPolling(SDL_sem *sem, Uint32 timeout)
+{
+    Uint32 end = SDL_GetTicks() + timeout;
+    int retval;
+
+    while ((retval = SDL_SemTryWait(sem)) == SDL_MUTEX_TIMEDOUT) {
+        if (SDL_TICKS_PASSED(SDL_GetTicks(), end)) {
+            break;
+        }
+        SDL_Delay(1);
+    }
+    return retval;
+}
+#endif
+
 int SDL_SemWaitTimeout(SDL_sem *sem, Uint32 timeout)
 {
     int retval = 0;
@@ -154,6 +177,11 @@ int SDL_SemWaitTimeout(SDL_sem *sem, Uint32 timeout)
     if (retval < 0) {
         if (errno == ETIMEDOUT) {
             retval = SDL_MUTEX_TIMEDOUT;
+#ifdef __riscos__
+        } else if (errno == ENOSYS) {
+            /* the stub in an older UnixLib: see above */
+            retval = RISCOS_SemWaitTimeoutPolling(sem, timeout);
+#endif
         } else {
             SDL_SetError("sem_timedwait returned an error: %s", strerror(errno));
         }
