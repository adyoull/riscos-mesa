diff --git src/video/SDL_blit_A.c src/video/SDL_blit_A.c
--- src/video/SDL_blit_A.c
+++ src/video/SDL_blit_A.c
@@ -1452,6 +1452,16 @@ SDL_CalculateBlitA(SDL_Surface * surface)
                 }
 #endif /* __MMX__ || __3dNOW__ */
                 if (sf->Amask == 0xff000000) {
+#if SDL_ARM_NEON_BLITTERS || SDL_ARM_SIMD_BLITTERS
+                    /* riscos-mesa: the ARM routines leave the destination's
+                       alpha as it was, where BlitRGBtoRGBPixelAlpha
+                       blends it too. So they're only used for destinations
+                       without alpha (the window surface, say). Their
+                       colours are within half a step of the exact blend;
+                       the C code's are up to 2 steps off
+                       (tests/host-harness/sdl-arm). */
+                    if (df->Amask == 0) {
+#endif
 #if SDL_ARM_NEON_BLITTERS
                     if (SDL_HasNEON())
                         return BlitRGBtoRGBPixelAlphaARMNEON;
@@ -1459,6 +1469,9 @@ SDL_CalculateBlitA(SDL_Surface * surface)
 #if SDL_ARM_SIMD_BLITTERS
                     if (SDL_HasARMSIMD())
                         return BlitRGBtoRGBPixelAlphaARMSIMD;
+#endif
+#if SDL_ARM_NEON_BLITTERS || SDL_ARM_SIMD_BLITTERS
+                    }
 #endif
                     return BlitRGBtoRGBPixelAlpha;
                 }
