diff --git configure.ac configure.ac
index cc30f9a..b442454 100644
--- configure.ac
+++ configure.ac
@@ -4560,6 +4560,13 @@ dnl BeOS support removed after SDL 2.0.1. Haiku still works.  --ryan.
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
