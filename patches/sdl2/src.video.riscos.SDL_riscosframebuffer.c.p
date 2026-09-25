diff --git src/video/riscos/SDL_riscosframebuffer.c src/video/riscos/SDL_riscosframebuffer.c
index 5984199..dd2d735 100644
--- src/video/riscos/SDL_riscosframebuffer.c
+++ src/video/riscos/SDL_riscosframebuffer.c
@@ -40,6 +40,11 @@ int RISCOS_CreateWindowFramebuffer(_THIS, SDL_Window * window, Uint32 * format,
     SDL_DisplayMode mode;
     int size;
 
+    /* 2026: an OpenGL window's sprite belongs to the GL context. */
+    if (driverdata->gl_active) {
+        return SDL_SetError("Window uses OpenGL; it has no framebuffer surface");
+    }
+
     /* Free the old framebuffer surface */
     RISCOS_DestroyWindowFramebuffer(_this, window);
 
@@ -53,6 +58,26 @@ int RISCOS_CreateWindowFramebuffer(_THIS, SDL_Window * window, Uint32 * format,
         sprite_mode = (1 | (90 << 1) | (90 << 14) | (6 << 27));
     }
 
+    /* 2026: full screen with a matching pixel format: hand SDL the screen
+       memory itself, so presenting a frame needs no extra copy or sprite plot.
+       VDU variables: 6 = LineLength, 9 = Log2BPP, 11/12 = X/YWindLimit,
+       148 = ScreenStart.  */
+    if (((SDL_VideoData *) _this->driverdata)->wimp_sdl_window != window &&
+        SDL_BYTESPERPIXEL(*format) == 4) {
+        int vars[6] = { 148, 6, 9, 11, 12, -1 };
+        int vals[5];
+        regs.r[0] = (int)vars;
+        regs.r[1] = (int)vals;
+        if (_kernel_swi(OS_ReadVduVariables, &regs, &regs) == NULL &&
+            vals[0] != 0 && vals[2] == 5 &&
+            window->w <= vals[3] + 1 && window->h <= vals[4] + 1) {
+            driverdata->fb_direct = 1;
+            *pixels = (void *)vals[0];
+            *pitch = vals[1];
+            return 0;
+        }
+    }
+
     /* Calculate pitch */
     *pitch = (((window->w * SDL_BYTESPERPIXEL(*format)) + 3) & ~3);
 
@@ -88,32 +113,175 @@ int RISCOS_CreateWindowFramebuffer(_THIS, SDL_Window * window, Uint32 * format,
     return 0;
 }
 
-int RISCOS_UpdateWindowFramebuffer(_THIS, SDL_Window * window, const SDL_Rect * rects, int numrects)
+static void RISCOS_vdu_word(int v)
+{
+    _kernel_oswrch(v & 0xff);
+    _kernel_oswrch((v >> 8) & 0xff);
+}
+
+/* 2026: the eigen factors a sprite plots with: a mode word's dpi (180 = 0,
+   90 = 1, 45 = 2), otherwise (a mode number or the screen's own mode
+   specifier) the screen's. */
+static void
+RISCOS_SpriteEigs(const sprite_header *spr, int xeig, int yeig, int *sxe, int *sye)
+{
+    unsigned int mode = (unsigned int) spr->mode;
+    *sxe = xeig;
+    *sye = yeig;
+    if ((mode & 1) && (mode >> 27) != 0) {
+        int xdpi = (mode >> 1) & 0x1FFF, ydpi = (mode >> 14) & 0x1FFF;
+        *sxe = xdpi >= 180 ? 0 : xdpi >= 90 ? 1 : 2;
+        *sye = ydpi >= 180 ? 0 : ydpi >= 90 ? 1 : 2;
+    }
+}
+
+/* 2026: plot the framebuffer sprite for each rectangle of a Wimp redraw or
+   update loop.  block is the Wimp_RedrawWindow/UpdateWindow block.  Each
+   SDL pixel covers wscale_x x wscale_y screen pixels (2x2 in EX0 EY0
+   modes, see RISCOS_ChooseWindowScale). */
+void
+RISCOS_WimpPlotWindow(_THIS, SDL_Window *window, int *block, int more)
 {
     SDL_WindowData *driverdata = (SDL_WindowData *) window->driverdata;
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    int xeig = vdata->xeig, yeig = vdata->yeig;
+    int sx = vdata->wscale_x > 0 ? vdata->wscale_x : 1;
+    int sy = vdata->wscale_y > 0 ? vdata->wscale_y : 1;
+    int scale[4], sxe, sye;
     _kernel_swi_regs regs;
-    _kernel_oserror *error;
 
-    regs.r[0] = 512+52;
-    regs.r[1] = (int)driverdata->fb_area;
-    regs.r[2] = (int)driverdata->fb_sprite;
-    regs.r[3] = 0; /* window->x << 1; */
-    regs.r[4] = 0; /* window->y << 1; */
-    regs.r[5] = 0x50;
-    regs.r[6] = 0;
-    regs.r[7] = 0;
-    error = _kernel_swi(OS_SpriteOp, &regs, &regs);
+    while (more) {
+        if (driverdata && driverdata->fb_sprite) {
+            int ox = block[1] - block[5];
+            int oy = block[4] - block[6];
+            /* wanted size / the sprite's own size (it may be a 90 dpi sprite
+               in a 180 dpi mode, which SpriteExtend already doubles) */
+            RISCOS_SpriteEigs(driverdata->fb_sprite, xeig, yeig, &sxe, &sye);
+            scale[0] = sx << xeig; scale[1] = sy << yeig;
+            scale[2] = 1 << sxe;   scale[3] = 1 << sye;
+            regs.r[1] = (int)driverdata->fb_area;
+            regs.r[2] = (int)driverdata->fb_sprite;
+            regs.r[3] = ox;
+            regs.r[4] = oy - ((window->h * sy) << yeig);
+            regs.r[5] = 0;
+            if (scale[0] == scale[2] && scale[1] == scale[3]) {
+                regs.r[0] = 512+34;     /* plain plot */
+            } else {
+                regs.r[0] = 512+52;     /* PutSpriteScaled */
+                regs.r[6] = (int)scale;
+                regs.r[7] = 0;          /* no translation table: SpriteExtend converts true colour */
+            }
+            _kernel_swi(OS_SpriteOp, &regs, &regs);
+        }
+        regs.r[1] = (int)block;
+        if (_kernel_swi(Wimp_GetRectangle, &regs, &regs) != NULL)
+            break;
+        more = regs.r[0];
+    }
+}
+
+static int
+RISCOS_WimpUpdateFramebuffer(_THIS, SDL_Window * window, const SDL_Rect * rects, int numrects)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    int xeig = vdata->xeig, yeig = vdata->yeig;
+    int sx = vdata->wscale_x > 0 ? vdata->wscale_x : 1;
+    int sy = vdata->wscale_y > 0 ? vdata->wscale_y : 1;
+    int block[11], i;
+    _kernel_swi_regs regs;
+
+    for (i = 0; i < numrects || (numrects == 0 && i == 0); i++) {
+        int l = 0, t = 0, w = window->w, h = window->h;
+        if (numrects > 0) {
+            l = rects[i].x; t = rects[i].y; w = rects[i].w; h = rects[i].h;
+            if (w <= 0 || h <= 0) continue;
+        }
+        block[0] = vdata->wimp_window;
+        block[1] = (l * sx) << xeig;
+        block[2] = -(((t + h) * sy) << yeig);
+        block[3] = ((l + w) * sx) << xeig;
+        block[4] = -((t * sy) << yeig);
+        regs.r[1] = (int)block;
+        if (_kernel_swi(Wimp_UpdateWindow, &regs, &regs) != NULL)
+            continue;
+        RISCOS_WimpPlotWindow(_this, window, block, regs.r[0]);
+    }
+    return 0;
+}
+
+static int
+RISCOS_FullscreenUpdateFramebuffer(_THIS, SDL_Window * window, const SDL_Rect * rects, int numrects)
+{
+    /* 2026: plot only the dirty rectangles (by clipping the graphics window,
+       which SpriteExtend honours) and align the framebuffer with the top of
+       the screen so it matches the mouse coordinates from RISCOS_PollMouse. */
+    SDL_WindowData *driverdata = (SDL_WindowData *) window->driverdata;
+    _kernel_swi_regs regs;
+    _kernel_oserror *error = NULL;
+    SDL_Rect bounds;
+    int xeig = 1, yeig = 1, y0 = 0, i;
+
+    xeig = ((SDL_VideoData *) _this->driverdata)->xeig;
+    yeig = ((SDL_VideoData *) _this->driverdata)->yeig;
+    if (SDL_GetDisplayBounds(0, &bounds) == 0 && bounds.h > window->h) {
+        y0 = (bounds.h - window->h) << yeig;
+    }
+
+    for (i = 0; i < numrects || (numrects == 0 && i == 0); i++) {
+        if (numrects > 0) {
+            int l = rects[i].x, t = rects[i].y, w = rects[i].w, h = rects[i].h;
+            if (w <= 0 || h <= 0) continue;
+            _kernel_oswrch(24);
+            RISCOS_vdu_word(l << xeig);
+            RISCOS_vdu_word(y0 + ((window->h - (t + h)) << yeig));
+            RISCOS_vdu_word(((l + w) << xeig) - 1);
+            RISCOS_vdu_word(y0 + ((window->h - t) << yeig) - 1);
+        }
+
+        /* Plain unscaled, untranslated plot (PutSpriteUserCoords, GCOL 0):
+           the sprite is already in the screen's pixel format. */
+        regs.r[0] = 512+34;
+        regs.r[1] = (int)driverdata->fb_area;
+        regs.r[2] = (int)driverdata->fb_sprite;
+        regs.r[3] = 0;
+        regs.r[4] = y0;
+        regs.r[5] = 0;
+        error = _kernel_swi(OS_SpriteOp, &regs, &regs);
+        if (error != NULL) break;
+    }
+    if (numrects > 0) {
+        _kernel_oswrch(26); /* restore default graphics window */
+    }
     if (error != NULL) {
-        return SDL_SetError("OS_SpriteOp 52 failed: %s (%i)", error->errmess, error->errnum);
+        return SDL_SetError("OS_SpriteOp 34 failed: %s (%i)", error->errmess, error->errnum);
     }
 
     return 0;
 }
 
+int RISCOS_UpdateWindowFramebuffer(_THIS, SDL_Window * window, const SDL_Rect * rects, int numrects)
+{
+    SDL_WindowData *driverdata = (SDL_WindowData *) window->driverdata;
+
+    if (((SDL_VideoData *) _this->driverdata)->wimp_window != 0 &&
+        ((SDL_VideoData *) _this->driverdata)->wimp_sdl_window == window) {
+        return RISCOS_WimpUpdateFramebuffer(_this, window, rects, numrects);
+    }
+    if (driverdata && driverdata->fb_direct) {
+        /* Already drawn straight into screen memory. */
+        return 0;
+    }
+    return RISCOS_FullscreenUpdateFramebuffer(_this, window, rects, numrects);
+}
+
 void RISCOS_DestroyWindowFramebuffer(_THIS, SDL_Window * window)
 {
     SDL_WindowData *driverdata = (SDL_WindowData *) window->driverdata;
 
+    if (driverdata->gl_active)
+        return;             /* 2026: the GL sprite is freed by the GL code */
+    driverdata->fb_direct = 0;
+
     if (driverdata->fb_area) {
         SDL_free(driverdata->fb_area);
         driverdata->fb_area = NULL;
