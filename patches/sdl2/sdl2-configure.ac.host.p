--- configure.ac
+++ configure.ac
@@ -3747,7 +3747,7 @@
 
 dnl Set up the configuration based on the host platform!
 case "$host" in
-    *-*-linux*|*-*-uclinux*|*-*-gnu*|*-*-k*bsd*-gnu|*-*-bsdi*|*-*-freebsd*|*-*-dragonfly*|*-*-netbsd*|*-*-openbsd*|*-*-sysv5*|*-*-solaris*|*-*-hpux*|*-*-aix*|*-*-minix*|*-*-nto*)
+    *-*-linux*|*-*-uclinux*|*-*-gnu|*-*-gnu-*|*-*-k*bsd*-gnu|*-*-bsdi*|*-*-freebsd*|*-*-dragonfly*|*-*-netbsd*|*-*-openbsd*|*-*-sysv5*|*-*-solaris*|*-*-hpux*|*-*-aix*|*-*-minix*|*-*-nto*)
         case "$host" in
             *-*-android*)
                 # Android
@@ -4540,7 +4540,7 @@
             have_locale=yes
         fi
         ;;
-    *-*-riscos*)
+    *-*-riscos*|*-riscos*)
         ARCH=riscos
         CheckVisibilityHidden
         CheckWerror
