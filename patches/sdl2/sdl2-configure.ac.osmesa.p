--- configure.ac
+++ configure.ac
@@ -4566,6 +4566,28 @@
             SOURCES="$SOURCES $srcdir/src/video/riscos/*.c"
             have_video=yes
             SUMMARY_video="${SUMMARY_video} riscos"
+            # 2026: software OpenGL through Mesa's OSMesa (static libOSMesa)
+            AC_ARG_ENABLE(video-riscos-osmesa,
+[AS_HELP_STRING([--enable-video-riscos-osmesa], [use Mesa OSMesa for software OpenGL in the RISC OS driver [default=no]])],
+                          , enable_video_riscos_osmesa=no)
+            if test x$enable_video_riscos_osmesa = xyes; then
+                AC_MSG_CHECKING(for OSMesa)
+                save_LIBS="$LIBS"
+                LIBS="$LIBS -lOSMesa -lstdc++ -lz -lm"
+                AC_LINK_IFELSE([AC_LANG_PROGRAM([[extern void *OSMesaGetProcAddress(const char *);]],
+                                                [[OSMesaGetProcAddress("glFinish");]])],
+                               [have_osmesa=yes], [have_osmesa=no])
+                LIBS="$save_LIBS"
+                AC_MSG_RESULT($have_osmesa)
+                if test x$have_osmesa = xyes; then
+                    AC_DEFINE(SDL_VIDEO_OPENGL, 1, [ ])
+                    AC_DEFINE(SDL_VIDEO_OPENGL_OSMESA, 1, [ ])
+                    SUMMARY_video="${SUMMARY_video} opengl(osmesa)"
+                    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -lOSMesa -lstdc++ -lz -lm"
+                else
+                    AC_MSG_ERROR([--enable-video-riscos-osmesa given but libOSMesa was not found])
+                fi
+            fi
         fi
         # Set up files for the filesystem library
         if test x$enable_filesystem = xyes; then
