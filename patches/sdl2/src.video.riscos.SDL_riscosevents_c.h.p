diff --git src/video/riscos/SDL_riscosevents_c.h src/video/riscos/SDL_riscosevents_c.h
--- src/video/riscos/SDL_riscosevents_c.h
+++ src/video/riscos/SDL_riscosevents_c.h
@@ -29,6 +29,16 @@
 extern int RISCOS_InitEvents(_THIS);
 extern void RISCOS_PumpEvents(_THIS);
 extern void RISCOS_QuitEvents(_THIS);
+extern SDL_bool RISCOS_WimpDelay(_THIS, Uint32 ms);
+extern int RISCOS_WaitEventTimeout(_THIS, int timeout);
+extern void RISCOS_SendWakeupEvent(_THIS, SDL_Window *window);
+/* 2026: desktop shutdown (Message_PreQuit): see SDL_riscosevents.c */
+extern void RISCOS_RestartShutdown(void);
+/* 2026: the Wimp's input focus (caret) is SDL's keyboard focus */
+extern void RISCOS_SetCaretFocus(_THIS, SDL_bool has);
+/* 2026: SDL_WarpMouseInWindow and relative mouse mode (SDL_riscosmouse.c) */
+extern int RISCOS_WarpPointer(_THIS, SDL_Window *window, int x, int y);
+extern void RISCOS_CentrePointer(_THIS, SDL_Window *window);
 
 #endif /* SDL_riscosevents_c_h_ */
 
