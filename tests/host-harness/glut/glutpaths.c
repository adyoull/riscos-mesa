/*
 * glutpaths.c - checks of the RISC OS freeglut back end's drawing paths,
 * run through portrun on the fake RISC OS (see run.sh). argv[1] picks one:
 *
 *   gamemode    glutGameModeString("640x360") on the fake 1280x720 screen:
 *               the program sees 640x360 (glutGameModeGet, glutGet, the
 *               reshape callback), the frame is stretched over the whole
 *               screen, and the pointer is mapped to the render size
 *               (a click in the middle of the screen is 320,180). With
 *               OVL=1 (a fake VideoOverlay, as on a Pi) the frame goes
 *               through a 640x360 overlay instead of being plotted.
 *   slowtimer   a single-buffered window and a one-second timer that draws
 *               nothing: the window isn't copied to the screen again and
 *               again while it waits (it used to be, every 20 ms).
 *   drawtimer   a single-buffered window drawn from a 100 ms timer without
 *               glutPostRedisplay: each frame is shown.
 *   scaled      run with EIG0=1 (a high resolution, EX0 EY0 screen): a
 *               320x180 window is shown 640x360 screen pixels (window
 *               scale 2), the program still sees 320x180 and a half size
 *               screen, and pointer positions are in its pixels.
 *
 * Time in the fake runs at 20 ms per Wimp_Poll, so the counts are exact.
 * Part of riscos-mesa, MIT licence.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/freeglut.h>

unsigned int fake_screen_pixel(int x, int y);
/* the fake VideoOverlay (../ovl/fake_ovl.c), linked in for OVL=1 */
int fake_ovl_info(int *w, int *h, int *banks, int *flags, int *shown, int *scale_w, int *scale_h)
    __attribute__((weak));
static const char *mode;
static int ticks;

static void quad(float x0, float y0, float x1, float y1)
{
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
    glEnd();
}

/* -- gamemode -- */
static void gm_display(void)
{
    glViewport(0, 0, glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 640, 360, 0, -1, 1);     /* y down, as GLUT's pointer */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1, 0, 0);
    quad(0, 0, 320, 180);               /* the top left quarter red */
    glutSwapBuffers();
    if (getenv("OVL"))
        glutPostRedisplay();            /* EGL starts an overlay for an animation */
}
static void gm_reshape(int w, int h) { printf("reshape %dx%d\n", w, h); }
static void gm_mouse(int b, int s, int x, int y)
{
    if (s == GLUT_DOWN)
        printf("click %d,%d\n", x, y);
}
static void gm_timer(int v)
{
    unsigned tl = fake_screen_pixel(10, 10) & 0xFFFFFF, br = fake_screen_pixel(1270, 710) & 0xFFFFFF;
    unsigned mid = fake_screen_pixel(630, 350) & 0xFFFFFF;
    int ow, oh, shown;
    (void) v;
    if (getenv("OVL") && fake_ovl_info && fake_ovl_info(&ow, &oh, NULL, NULL, &shown, NULL, NULL))
        printf("overlay %dx%d, showing buffer %d\n", ow, oh, shown);
    else
        printf("screen top left %06x, bottom right %06x, just above and left of the middle %06x\n",
               tl, br, mid);
}

/* -- slowtimer / drawtimer -- */
static void sb_display(void)
{
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
}
static void sb_timer(int v)
{
    ticks++;
    if (!strcmp(mode, "drawtimer")) {
        glClearColor((ticks & 1) ? 1.0f : 0.0f, 0.5f, 0, 1);   /* drawn here, no PostRedisplay */
        glClear(GL_COLOR_BUFFER_BIT);
        glFlush();
    }
    if (ticks * v >= 2000) {
        printf("%s: %d timer calls\n", mode, ticks);
        glutLeaveMainLoop();
        return;
    }
    glutTimerFunc(v, sb_timer, v);
}

/* -- scaled -- */
static void sc_display(void)
{
    glViewport(0, 0, glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 320, 180, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1, 0, 0);
    quad(0, 0, 160, 90);                /* the top left quarter red */
    glutSwapBuffers();
}
static void sc_timer(int v)
{
    /* the window's top left on the screen: GLUT pixels, 2x2 screen pixels each */
    int x = glutGet(GLUT_WINDOW_X) * 2, y = glutGet(GLUT_WINDOW_Y) * 2;
    (void) v;
    printf("screen %dx%d, window %dx%d\n", glutGet(GLUT_SCREEN_WIDTH), glutGet(GLUT_SCREEN_HEIGHT),
           glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT));
    printf("pixels %06x %06x %06x %06x, outside %06x\n",
           fake_screen_pixel(x + 1, y + 1) & 0xFFFFFF, fake_screen_pixel(x + 318, y + 178) & 0xFFFFFF,
           fake_screen_pixel(x + 321, y + 181) & 0xFFFFFF, fake_screen_pixel(x + 638, y + 358) & 0xFFFFFF,
           fake_screen_pixel(x + 641, y + 358) & 0xFFFFFF);
}

int main(int argc, char **argv)
{
    glutInit(&argc, argv);
    mode = argc > 1 ? argv[1] : "gamemode";
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
    if (!strcmp(mode, "gamemode")) {
        glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
        glutGameModeString("640x360");
        glutEnterGameMode();
        printf("game mode %dx%d, window %dx%d\n", glutGameModeGet(GLUT_GAME_MODE_WIDTH),
               glutGameModeGet(GLUT_GAME_MODE_HEIGHT), glutGet(GLUT_WINDOW_WIDTH),
               glutGet(GLUT_WINDOW_HEIGHT));
        glutDisplayFunc(gm_display);
        glutReshapeFunc(gm_reshape);
        glutMouseFunc(gm_mouse);
        glutTimerFunc(200, gm_timer, 0);
    } else if (!strcmp(mode, "scaled")) {
        glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
        glutInitWindowSize(320, 180);
        glutCreateWindow(mode);
        glutDisplayFunc(sc_display);
        glutReshapeFunc(gm_reshape);
        glutMouseFunc(gm_mouse);
        glutTimerFunc(200, sc_timer, 0);
    } else {
        glutInitDisplayMode(GLUT_SINGLE | GLUT_RGB);
        glutInitWindowSize(200, 150);
        glutCreateWindow(mode);
        glutDisplayFunc(sb_display);
        glutTimerFunc(!strcmp(mode, "drawtimer") ? 100 : 1000, sb_timer,
                      !strcmp(mode, "drawtimer") ? 100 : 1000);
    }
    glutMainLoop();
    return 0;
}
