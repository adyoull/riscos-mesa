diff --git src/video/riscos/SDL_riscosevents_c.h src/video/riscos/SDL_riscosevents_c.h
index 6b43411..f6cbee1 100644
--- src/video/riscos/SDL_riscosevents_c.h
+++ src/video/riscos/SDL_riscosevents_c.h
@@ -29,6 +29,11 @@
 extern int RISCOS_InitEvents(_THIS);
 extern void RISCOS_PumpEvents(_THIS);
 extern void RISCOS_QuitEvents(_THIS);
+extern SDL_bool RISCOS_WimpDelay(_THIS, Uint32 ms);
+extern int RISCOS_WaitEventTimeout(_THIS, int timeout);
+extern void RISCOS_SendWakeupEvent(_THIS, SDL_Window *window);
+/* 2026: desktop shutdown (Message_PreQuit): see SDL_riscosevents.c */
+extern void RISCOS_RestartShutdown(void);
 
 #endif /* SDL_riscosevents_c_h_ */
 
