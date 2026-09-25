/* es2tri.c - a GLUT program over OpenGL ES 2.0 (freeglut built with
   -DFREEGLUT_GLES), for the host test run.sh. MIT licence (riscos-mesa). */
#include <GL/freeglut.h>
#include <stdio.h>
static GLuint prog;
static const char *vs = "attribute vec2 p; void main(){ gl_Position = vec4(p,0.0,1.0); }";
static const char *fs = "precision mediump float; void main(){ gl_FragColor = vec4(1.0,0.5,0.0,1.0); }";
static GLuint sh(GLenum t, const char *s){ GLuint h=glCreateShader(t); glShaderSource(h,1,&s,0); glCompileShader(h); return h; }
static void display(void){
  static const GLfloat v[] = { -0.8f,-0.8f, 0.8f,-0.8f, 0.0f,0.8f };
  glClearColor(0,0,0.4f,1); glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(prog); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,v); glEnableVertexAttribArray(0);
  glDrawArrays(GL_TRIANGLES,0,3); glutSwapBuffers(); }
int main(int argc, char **argv){
  glutInit(&argc, argv); glutInitContextVersion(2,0); glutInitDisplayMode(GLUT_RGB|GLUT_DOUBLE);
  glutInitWindowSize(320,240); glutCreateWindow("es2tri");
  prog=glCreateProgram(); glAttachShader(prog,sh(GL_VERTEX_SHADER,vs)); glAttachShader(prog,sh(GL_FRAGMENT_SHADER,fs));
  glBindAttribLocation(prog,0,"p"); glLinkProgram(prog);
  printf("GL_VERSION %s\n", glGetString(GL_VERSION));
  glutDisplayFunc(display); glutMainLoop(); return 0; }
