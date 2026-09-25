diff --git src/audio/SDL_audio.c src/audio/SDL_audio.c
index 0888878..6d20280 100644
--- src/audio/SDL_audio.c
+++ src/audio/SDL_audio.c
@@ -117,6 +117,9 @@ static const AudioBootStrap *const bootstrap[] = {
 #if SDL_AUDIO_DRIVER_PIPEWIRE
     &PIPEWIRE_bootstrap,
 #endif
+#if SDL_AUDIO_DRIVER_RISCOS
+    &RISCOSAUDIO_bootstrap,
+#endif
 #if SDL_AUDIO_DRIVER_OSS
     &DSP_bootstrap,
 #endif
