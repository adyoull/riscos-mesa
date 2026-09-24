#include <stdio.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
static unsigned char buf[8*8*4];
static void try(int prof,int maj,int min){
  int a[]={OSMESA_FORMAT,OSMESA_RGBA,OSMESA_DEPTH_BITS,24,OSMESA_PROFILE,prof,
           OSMESA_CONTEXT_MAJOR_VERSION,maj,OSMESA_CONTEXT_MINOR_VERSION,min,0};
  OSMesaContext c=OSMesaCreateContextAttribs(a,NULL);
  if(!c){printf("%s %d.%d: NULL\n",prof==OSMESA_CORE_PROFILE?"core":"compat",maj,min);return;}
  OSMesaMakeCurrent(c,buf,GL_UNSIGNED_BYTE,8,8);
  printf("%s %d.%d: %s\n",prof==OSMESA_CORE_PROFILE?"core":"compat",maj,min,glGetString(GL_VERSION));
  OSMesaDestroyContext(c);
}
int main(void){try(OSMESA_COMPAT_PROFILE,2,1);try(OSMESA_COMPAT_PROFILE,3,0);
 try(OSMESA_CORE_PROFILE,3,1);try(OSMESA_CORE_PROFILE,3,2);try(OSMESA_CORE_PROFILE,3,3);return 0;}
