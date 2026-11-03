diff --git src/audio/SDL_audio.c src/audio/SDL_audio.c
--- src/audio/SDL_audio.c
+++ src/audio/SDL_audio.c
@@ -123,6 +123,9 @@ static const AudioBootStrap *const bootstrap[] = {
 #ifdef SDL_AUDIO_DRIVER_PIPEWIRE
     &PIPEWIRE_bootstrap,
 #endif
+#ifdef SDL_AUDIO_DRIVER_RISCOS
+    &RISCOSAUDIO_bootstrap,
+#endif
 #ifdef SDL_AUDIO_DRIVER_OSS
     &DSP_bootstrap,
 #endif
