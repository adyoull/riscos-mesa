diff --git src/video/riscos/SDL_riscoswimp.h src/video/riscos/SDL_riscoswimp.h
new file mode 100644
index 0000000..920a3bc
--- /dev/null
+++ src/video/riscos/SDL_riscoswimp.h
@@ -0,0 +1,174 @@
+/*
+  Simple DirectMedia Layer
+  Copyright (C) 1997-2022 Sam Lantinga <slouken@libsdl.org>
+
+  This software is provided 'as-is', without any express or implied
+  warranty.  In no event will the authors be held liable for any damages
+  arising from the use of this software.
+
+  Permission is granted to anyone to use this software for any purpose,
+  including commercial applications, and to alter it and redistribute it
+  freely, subject to the following restrictions:
+
+  1. The origin of this software must not be misrepresented; you must not
+     claim that you wrote the original software. If you use this software
+     in a product, an acknowledgment in the product documentation would be
+     appreciated but is not required.
+  2. Altered source versions must be plainly marked as such, and must not be
+     misrepresented as being the original software.
+  3. This notice may not be removed or altered from any source distribution.
+*/
+
+#ifndef SDL_riscoswimp_h_
+#define SDL_riscoswimp_h_
+
+/* 2026: the Window Manager (Wimp) data blocks the RISC OS driver passes to
+   and gets from Wimp SWIs and Wimp_Poll, laid out as in the RISC OS PRM
+   (volume 3, The Window Manager). Coordinates are OS units; y goes up. */
+
+#include "SDL_stdinc.h"
+
+/* A rectangle: bottom left (x0, y0) inclusive, top right (x1, y1) exclusive */
+typedef struct RISCOS_Box
+{
+    int x0, y0, x1, y1;
+} RISCOS_Box;
+
+/* Wimp_OpenWindow, and Wimp_Poll reason 2 (Open_Window_Request) */
+typedef struct RISCOS_WindowOpen
+{
+    int window;                 /* window handle */
+    RISCOS_Box visible;         /* visible area, screen coordinates */
+    int scroll_x, scroll_y;     /* scroll offsets */
+    int behind;                 /* handle to open behind; -1 top, -2 bottom */
+} RISCOS_WindowOpen;
+
+/* Wimp_GetWindowState: the open block followed by the window flags, so it
+   can be handed back to Wimp_OpenWindow as it is */
+typedef struct RISCOS_WindowState
+{
+    RISCOS_WindowOpen open;
+    unsigned int flags;
+} RISCOS_WindowState;
+
+/* Wimp_RedrawWindow, Wimp_UpdateWindow and Wimp_GetRectangle; Wimp_Poll
+   reason 1 (Redraw_Window_Request) gives the window handle. For
+   Wimp_UpdateWindow, box is the work area rectangle to update on entry;
+   on return (as for the others) it is the window's visible area. */
+typedef struct RISCOS_Redraw
+{
+    int window;
+    RISCOS_Box box;
+    int scroll_x, scroll_y;
+    RISCOS_Box clip;            /* the rectangle to draw now, screen coordinates */
+} RISCOS_Redraw;
+
+/* Wimp_GetPointerInfo, and Wimp_Poll reason 6 (Mouse_Click) */
+typedef struct RISCOS_Pointer
+{
+    int x, y;                   /* screen coordinates */
+    int buttons;                /* bit 0 Adjust, 1 Menu, 2 Select (click type bits for Mouse_Click) */
+    int window;                 /* -1 the background, -2 the icon bar */
+    int icon;                   /* icon handle; -1 the work area */
+} RISCOS_Pointer;
+
+/* Wimp_GetCaretPosition, and Wimp_Poll reasons 11 and 12 (Lose_Caret,
+   Gain_Caret) */
+typedef struct RISCOS_Caret
+{
+    int window, icon;
+    int x, y;                   /* offset from the work area origin */
+    int height;                 /* height and flags */
+    int index;                  /* index into the icon's string */
+} RISCOS_Caret;
+
+/* Wimp_Poll reason 8 (Key_Pressed): the caret block and the key */
+typedef struct RISCOS_KeyPressed
+{
+    RISCOS_Caret caret;
+    int code;                   /* character code, or &180+ for function keys */
+} RISCOS_KeyPressed;
+
+/* Wimp_Poll reasons 17-19 (User_Message, User_Message_Recorded,
+   User_Message_Acknowledge) and Wimp_SendMessage */
+typedef struct RISCOS_Message
+{
+    int size;                   /* length of the message in bytes */
+    int sender;                 /* task handle of the sender */
+    int my_ref, your_ref;
+    int action;                 /* the message number */
+    int data[59];               /* message specific */
+} RISCOS_Message;
+
+/* Wimp_CreateIcon */
+typedef struct RISCOS_IconCreate
+{
+    int window;                 /* -1 the right hand side of the icon bar */
+    RISCOS_Box box;             /* work area coordinates */
+    unsigned int flags;
+    char data[12];              /* e.g. a sprite name */
+} RISCOS_IconCreate;
+
+/* Put a sprite name in a (non-indirected) icon's data. Sprite names are
+   up to 12 characters, and the Wimp takes all 12 bytes when there is no
+   terminator, so a 12-character name such as "!Warzone2100" fits exactly
+   (a string copy with a 12-byte limit would cut it to 11). */
+SDL_FORCE_INLINE void
+RISCOS_IconSpriteName(RISCOS_IconCreate *icon, const char *name)
+{
+    size_t n = SDL_strlen(name);
+    if (n > sizeof(icon->data))
+        n = sizeof(icon->data);
+    SDL_memset(icon->data, 0, sizeof(icon->data));
+    SDL_memcpy(icon->data, name, n);
+}
+
+/* Wimp_CreateWindow: the window block, with no icons following */
+typedef struct RISCOS_WindowDef
+{
+    RISCOS_Box visible;         /* screen coordinates */
+    int scroll_x, scroll_y;
+    int behind;
+    unsigned int flags;
+    Uint8 title_fg, title_bg;   /* Wimp colours; 255 = transparent */
+    Uint8 work_fg, work_bg;
+    Uint8 scroll_outer, scroll_inner;
+    Uint8 title_focus_bg;
+    Uint8 extra_flags;
+    RISCOS_Box extent;          /* work area extent */
+    unsigned int title_flags;   /* icon flags of the title bar */
+    unsigned int work_flags;    /* work area button type in bits 12-15 */
+    int sprite_area;            /* 1 = the Wimp sprite area */
+    Sint16 min_width, min_height;
+    int title_data[3];          /* title bar icon data: indirected text */
+    int icon_count;
+} RISCOS_WindowDef;
+
+/* The block Wimp_Poll fills in, as each event reason uses it */
+typedef union RISCOS_PollBlock
+{
+    int window;                 /* reasons 1-4: the window the event is for */
+    RISCOS_Redraw redraw;       /* 1 Redraw_Window_Request */
+    RISCOS_WindowOpen open;     /* 2 Open_Window_Request */
+    RISCOS_Pointer click;       /* 6 Mouse_Click */
+    RISCOS_KeyPressed key;      /* 8 Key_Pressed */
+    int menu[64];               /* 9 Menu_Selection: item per level, -1 ended */
+    RISCOS_Caret caret;         /* 11, 12 Lose_Caret, Gain_Caret */
+    RISCOS_Message message;     /* 17-19 User_Message... */
+} RISCOS_PollBlock;
+
+SDL_COMPILE_TIME_ASSERT(riscos_box, sizeof(RISCOS_Box) == 16);
+SDL_COMPILE_TIME_ASSERT(riscos_window_open, sizeof(RISCOS_WindowOpen) == 32);
+SDL_COMPILE_TIME_ASSERT(riscos_window_state, sizeof(RISCOS_WindowState) == 36);
+SDL_COMPILE_TIME_ASSERT(riscos_redraw, sizeof(RISCOS_Redraw) == 44);
+SDL_COMPILE_TIME_ASSERT(riscos_pointer, sizeof(RISCOS_Pointer) == 20);
+SDL_COMPILE_TIME_ASSERT(riscos_caret, sizeof(RISCOS_Caret) == 24);
+SDL_COMPILE_TIME_ASSERT(riscos_key_pressed, sizeof(RISCOS_KeyPressed) == 28);
+SDL_COMPILE_TIME_ASSERT(riscos_message, sizeof(RISCOS_Message) == 256);
+SDL_COMPILE_TIME_ASSERT(riscos_icon_create, sizeof(RISCOS_IconCreate) == 36);
+SDL_COMPILE_TIME_ASSERT(riscos_window_def, sizeof(RISCOS_WindowDef) == 88);
+SDL_COMPILE_TIME_ASSERT(riscos_poll_block, sizeof(RISCOS_PollBlock) == 256);
+
+#endif /* SDL_riscoswimp_h_ */
+
+/* vi: set ts=4 sw=4 expandtab: */
