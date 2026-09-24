/* Checks OSMESA_Y_UP=0 puts GL's top edge in buffer row 0 and that the
   byte order is R,G,B,A (== RISC OS 0x00BBGGRR). */
#include <stdio.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
static unsigned char buf[16*16*4];
int main(void){
  OSMesaContext c=OSMesaCreateContextExt(OSMESA_RGBA,16,0,0,NULL);
  OSMesaMakeCurrent(c,buf,GL_UNSIGNED_BYTE,16,16);
  OSMesaPixelStore(OSMESA_Y_UP,0);
  glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT);           /* blue */
  glColor3f(1,0,0); glRectf(-1,0,1,1);                            /* red top half */
  glFinish();
  printf("row0 px: %02x %02x %02x %02x  row15 px: %02x %02x %02x %02x\n",
    buf[0],buf[1],buf[2],buf[3], buf[15*64],buf[15*64+1],buf[15*64+2],buf[15*64+3]);
  return !(buf[0]==255&&buf[2]==0&&buf[15*64+2]==255);
}
