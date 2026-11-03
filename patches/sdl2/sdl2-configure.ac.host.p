--- configure.ac
+++ configure.ac
@@ -3219,9 +3219,17 @@
             if test x$have_pthread_sem = xyes; then
                 AC_MSG_CHECKING(for sem_timedwait)
                 have_sem_timedwait=no
+                dnl A C library may have sem_timedwait only as a stub that
+                dnl always fails with ENOSYS (RISC OS UnixLib's does), and it
+                dnl says so with __stub_sem_timedwait, as AC_CHECK_FUNC knows.
+                dnl SDL_SemWaitTimeout would then fail at once (and SDL's
+                dnl timer thread would spin) instead of using its fallback.
                 AC_LINK_IFELSE([AC_LANG_PROGRAM([[
                   #include <pthread.h>
                   #include <semaphore.h>
+                  #if defined __stub_sem_timedwait || defined __stub___sem_timedwait
+                  #error sem_timedwait is a stub
+                  #endif
                 ]], [[
                   sem_timedwait(NULL, NULL);
                 ]])], [
@@ -3756,7 +3764,7 @@
 
 dnl Set up the configuration based on the host platform!
 case "$host" in
-    *-*-linux*|*-*-uclinux*|*-*-gnu*|*-*-k*bsd*-gnu|*-*-bsdi*|*-*-freebsd*|*-*-dragonfly*|*-*-netbsd*|*-*-openbsd*|*-*-sysv5*|*-*-solaris*|*-*-hpux*|*-*-aix*|*-*-minix*|*-*-nto*)
+    *-*-linux*|*-*-uclinux*|*-*-gnu|*-*-gnu-*|*-*-k*bsd*-gnu|*-*-bsdi*|*-*-freebsd*|*-*-dragonfly*|*-*-netbsd*|*-*-openbsd*|*-*-sysv5*|*-*-solaris*|*-*-hpux*|*-*-aix*|*-*-minix*|*-*-nto*)
         case "$host" in
             *-*-android*)
                 # Android
@@ -4553,7 +4561,7 @@
             have_locale=yes
         fi
         ;;
-    *-*-riscos*)
+    *-*-riscos*|*-riscos*)
         ARCH=riscos
         CheckVisibilityHidden
         CheckWerror
