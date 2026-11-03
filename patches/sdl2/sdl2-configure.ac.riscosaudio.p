--- configure.ac
+++ configure.ac
@@ -4573,6 +4573,13 @@
             SOURCES="$SOURCES $srcdir/src/misc/riscos/*.c"
             have_misc=yes
         fi
+        # 2026: audio through SharedSoundBuffer
+        if test x$enable_audio = xyes; then
+            AC_DEFINE(SDL_AUDIO_DRIVER_RISCOS, 1, [ ])
+            SOURCES="$SOURCES $srcdir/src/audio/riscos/*.c"
+            SUMMARY_audio="${SUMMARY_audio} riscos"
+            have_audio=yes
+        fi
         # Set up files for the video library
         if test x$enable_video = xyes; then
             AC_DEFINE(SDL_VIDEO_DRIVER_RISCOS, 1, [ ])
