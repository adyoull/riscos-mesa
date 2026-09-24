#include <stdio.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/osmesa.h>
static unsigned char buf[32*32*4];
int main(void){
  OSMesaContext c=OSMesaCreateContextExt(OSMESA_RGBA,16,0,0,NULL);
  GLUquadric *q; GLdouble m[16];
  OSMesaMakeCurrent(c,buf,GL_UNSIGNED_BYTE,32,32);
  glMatrixMode(GL_PROJECTION); gluPerspective(60,1,1,10);
  glGetDoublev(GL_PROJECTION_MATRIX,m);
  q=gluNewQuadric(); glClear(GL_COLOR_BUFFER_BIT); glTranslatef(0,0,-3); gluSphere(q,1,12,12); glFinish();
  printf("GLU %s, proj[0]=%.3f, centre px=%02x\n",gluGetString(GLU_VERSION),m[0],buf[(16*32+16)*4]);
  return buf[(16*32+16)*4]==0;
}
