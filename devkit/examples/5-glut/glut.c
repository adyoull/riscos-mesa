/*
 * Example 5: a lit, spinning teapot with GLUT.
 *
 * GLUT is the classic "OpenGL toolkit" used by textbooks and university
 * courses since the 1990s (the Red Book, NeHe's tutorials...). You give it
 * functions to call ("callbacks") - draw the picture, the window changed
 * size, a key was pressed - and it runs the loop for you. This devkit's
 * GLUT is freeglut with a RISC OS back end: real Wimp windows, and GLUT
 * menus become Wimp menus on the Menu button.
 *
 * What you learn here: callbacks, lighting, the depth buffer (so near
 * things hide far ones), and a menu.
 *
 * Build: make 5-glut   (see ../README.md)
 *
 * Part of the riscos-mesa devkit. MIT licence: copy it, change it, use it
 * as the start of your own program.
 */
#include <GL/freeglut.h>          /* GLUT plus freeglut's extras (glutLeaveMainLoop) */

static float angle;
static int spinning = 1;

/* Called whenever the window needs drawing. */
static void display(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0, 0, -3);              /* move the teapot away from the eye */
    glRotatef(20, 1, 0, 0);              /* tip it towards us a little */
    glRotatef(angle, 0, 1, 0);           /* and turn it */
    glutSolidTeapot(0.8);
    glutSwapBuffers();                   /* show the finished frame */
}

/* Called when the window changes size: keep the picture in proportion. */
static void reshape(int width, int height)
{
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(40, (double) width / (height ? height : 1), 0.5, 10);
    glMatrixMode(GL_MODELVIEW);
}

/* Called about 50 times a second (glutTimerFunc below): move and redraw. */
static void tick(int value)
{
    (void) value;
    if (!glutGetWindow())                /* the window has been closed */
        return;
    if (spinning)
        angle += 2;
    glutPostRedisplay();                 /* "please call display() soon" */
    glutTimerFunc(20, tick, 0);
}

static void keyboard(unsigned char key, int x, int y)
{
    (void) x; (void) y;
    if (key == 27)                       /* Escape */
        glutLeaveMainLoop();
    else if (key == ' ')
        spinning = !spinning;
}

/* Menu choices (click Menu over the window). */
static void menu(int choice)
{
    if (choice == 1)
        spinning = !spinning;
    else if (choice == 2)
        glutLeaveMainLoop();
}

int main(int argc, char **argv)
{
    static const GLfloat light_position[] = { 2, 3, 3, 0 };
    static const GLfloat teapot_colour[] = { 0.9f, 0.6f, 0.2f, 1 };

    glutInit(&argc, argv);
    /* Double buffered (draw hidden, then show), colour, and a depth buffer. */
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowSize(480, 360);
    /* When the window closes, return from glutMainLoop (below) instead of
     * exit()ing on the spot, so the program can save its work or tidy up
     * first. */
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
    glutCreateWindow("Teapot");

    glEnable(GL_DEPTH_TEST);             /* near surfaces hide far ones */
    glEnable(GL_LIGHTING);               /* shade by the angle to the light */
    glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, teapot_colour);
    glClearColor(0.1f, 0.1f, 0.3f, 1);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutTimerFunc(20, tick, 0);

    glutCreateMenu(menu);
    glutAddMenuEntry("Start/stop spinning", 1);
    glutAddMenuEntry("Quit", 2);
    /* GLUT names mouse buttons by position: left = Select, middle = Menu,
     * right = Adjust. Most GLUT programs put their menu on the right
     * button, so on RISC OS the Menu button also opens it, as users
     * expect. */
    glutAttachMenu(GLUT_RIGHT_BUTTON);

    glutMainLoop();                      /* runs until Escape, Quit or close */
    /* Save settings, free memory... here. */
    return 0;
}
