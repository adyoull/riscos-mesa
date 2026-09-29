diff --git src/audio/SDL_sysaudio.h src/audio/SDL_sysaudio.h
--- src/audio/SDL_sysaudio.h
+++ src/audio/SDL_sysaudio.h
@@ -187,6 +187,7 @@ extern AudioBootStrap ALSA_bootstrap;
 extern AudioBootStrap JACK_bootstrap;
 extern AudioBootStrap SNDIO_bootstrap;
 extern AudioBootStrap NETBSDAUDIO_bootstrap;
+extern AudioBootStrap RISCOSAUDIO_bootstrap;
 extern AudioBootStrap DSP_bootstrap;
 extern AudioBootStrap QSAAUDIO_bootstrap;
 extern AudioBootStrap SUNAUDIO_bootstrap;
