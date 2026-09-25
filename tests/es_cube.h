/* es_cube - a spinning cube in OpenGL ES 1.1 or 2.0 (see es_cube.c). */
#ifndef ES_CUBE_H
#define ES_CUBE_H
int es_cube_init(int es2, int w, int h);    /* current context; 0 = shaders failed */
void es_cube_resize(int w, int h);
void es_cube_draw(float angle);
#endif
