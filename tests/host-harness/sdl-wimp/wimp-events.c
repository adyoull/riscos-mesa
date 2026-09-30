/*
 * wimp-events.c - host test of the SDL RISC OS driver's Wimp event
 * handling (SDL_riscosevents.c): quitting from the desktop
 * (Message_PreQuit, Message_Quit), the close icon, the icon bar menu and
 * the keys handed to the Wimp.
 *
 * The driver file is compiled in here (#include), so its static functions
 * can be given Wimp_Poll blocks directly. The SWIs it calls are recorded by
 * a fake _kernel_swi, and the SDL functions it calls are small stubs that
 * record what was sent. See run.sh. Part of riscos-mesa, MIT licence.
 */
#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>
#include <sys/mman.h>

/* The driver calls exit() when Message_Quit arrives and the program won't
   quit: catch it here instead. */
static jmp_buf exit_jump;
static int exit_called, exit_status;
static void test_exit(int status)
{
    exit_called = 1;
    exit_status = status;
    longjmp(exit_jump, 1);
}
#define exit test_exit
#include "SDL_riscosevents.c"
#undef exit

/* ---- what the driver did ---- */

static int checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define MAX_CALLS 64
static struct { int swi; _kernel_swi_regs in; RISCOS_Message message; } calls[MAX_CALLS];
static int ncalls;
static int n_sdl_quit, n_app_terminating, n_close_events, n_sdl_Quit;
static SDL_Window *close_window;
static Uint32 ticks;

static void reset(void)
{
    ncalls = 0;
    n_sdl_quit = n_app_terminating = n_close_events = n_sdl_Quit = 0;
    close_window = NULL;
    exit_called = 0;
    SDL_zero(riscos_quit);
}

/* The calls made to one SWI since reset(), and the last one's registers */
static int calls_to(int swi, _kernel_swi_regs *last, RISCOS_Message *message)
{
    int i, n = 0;
    for (i = 0; i < ncalls; i++) {
        if (calls[i].swi == swi) {
            n++;
            if (last) *last = calls[i].in;
            if (message) *message = calls[i].message;
        }
    }
    return n;
}

/* ---- fake RISC OS ---- */

#define WINDOW_H 0x1234
static int fake_ptr_on, fake_ptr[2];

_kernel_oserror *_kernel_swi(int no, _kernel_swi_regs *in, _kernel_swi_regs *out)
{
    if (ncalls < MAX_CALLS) {
        calls[ncalls].swi = no;
        calls[ncalls].in = *in;
        if (no == Wimp_SendMessage)
            memcpy(&calls[ncalls].message, (void *)(intptr_t)in->r[1], sizeof(RISCOS_Message));
        ncalls++;
    }
    if (out != in) *out = *in;
    switch (no) {
    case Wimp_Poll:
    case Wimp_PollIdle:
        out->r[0] = 0;          /* a null event */
        break;
    case Wimp_GetPointerInfo:
        memset((void *)(intptr_t)in->r[1], 0, sizeof(RISCOS_Pointer));
        if (fake_ptr_on) {
            RISCOS_Pointer *p = (RISCOS_Pointer *)(intptr_t)in->r[1];
            p->x = fake_ptr[0]; p->y = fake_ptr[1]; p->window = WINDOW_H;
        }
        break;
    case Wimp_GetWindowState:
        memset((char *)(intptr_t)in->r[1] + 4, 0, sizeof(RISCOS_WindowState) - 4);
        if (fake_ptr_on) {
            RISCOS_WindowState *w = (RISCOS_WindowState *)(intptr_t)in->r[1];
            w->open.visible.x0 = 100; w->open.visible.y1 = 900;     /* top left, OS units */
        }
        break;
    case OS_Mouse:
        out->r[0] = fake_ptr[0]; out->r[1] = fake_ptr[1]; out->r[2] = 0;
        break;
    case OS_ReadMonotonicTime:
        out->r[0] = (int)(ticks / 10);
        break;
    default:
        break;
    }
    return NULL;
}
_kernel_oserror *_kernel_swi_c(int no, _kernel_swi_regs *in, _kernel_swi_regs *out, int *carry)
{
    *carry = 0;
    return _kernel_swi(no, in, out);
}
int _kernel_osbyte(int op, int x, int y) { (void)op; (void)x; (void)y; return 0xff; }

/* ---- SDL, as far as the driver uses it ---- */

static SDL_Mouse mouse;
SDL_Mouse *SDL_GetMouse(void) { return &mouse; }
SDL_Window *SDL_GetMouseFocus_REAL(void) { return NULL; }
void SDL_SetMouseFocus(SDL_Window *w) { (void)w; }
static int motion_x, motion_y;
int SDL_SendMouseMotion(SDL_Window *w, SDL_MouseID id, int rel, int x, int y)
{ motion_x = x; motion_y = y; mouse.x = x; mouse.y = y; return 0; }
int SDL_SendMouseButton(SDL_Window *w, SDL_MouseID id, Uint8 s, Uint8 b) { return 0; }
int SDL_SendMouseWheel(SDL_Window *w, SDL_MouseID id, float x, float y, SDL_MouseWheelDirection d) { return 0; }
int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode sc) { return 0; }
int SDL_SendKeyboardText(const char *t) { return 0; }
void SDL_ToggleModState(const SDL_Keymod m, const SDL_bool on) { }
int SDL_GetDisplayBounds_REAL(int i, SDL_Rect *r) { r->x = r->y = 0; r->w = 1920; r->h = 1080; return 0; }
SDL_threadID SDL_ThreadID_REAL(void) { return 1; }
Uint32 SDL_GetTicks_REAL(void) { return ticks; }
size_t SDL_strlcpy_REAL(char *d, const char *s, size_t n) { snprintf(d, n, "%s", s); return strlen(s); }
size_t SDL_strlen_REAL(const char *s) { return strlen(s); }
void *SDL_memset_REAL(void *d, int c, size_t n) { return memset(d, c, n); }
void *SDL_memcpy_REAL(void *d, const void *s, size_t n) { return memcpy(d, s, n); }
int SDL_SendQuit(void) { n_sdl_quit++; return 1; }
int SDL_SendAppEvent(SDL_EventType t) { if (t == SDL_APP_TERMINATING) n_app_terminating++; return 1; }
int SDL_SendWindowEvent(SDL_Window *w, Uint8 e, int d1, int d2)
{
    if (e == SDL_WINDOWEVENT_CLOSE) { n_close_events++; close_window = w; }
    return 1;
}
/* The event queue: SDL_QUIT / SDL_APP_TERMINATING are queued until the
   program takes them (take_events) */
static SDL_bool queued;
SDL_bool SDL_HasEvent_REAL(Uint32 type) { return queued && (type == SDL_QUIT || type == SDL_APP_TERMINATING); }
void SDL_Quit_REAL(void) { n_sdl_Quit++; }
const char *RISCOS_AppName(void) { return "Test"; }
void RISCOS_ApplyPointerVisibility(_THIS) { }
static int n_update_eigs, n_full_mode_changed;
void RISCOS_UpdateEigs(_THIS) { n_update_eigs++; }
void RISCOS_FullWindowModeChanged(_THIS) { n_full_mode_changed++; }
void RISCOS_WimpPlotWindow(_THIS, SDL_Window *w, RISCOS_Redraw *r, int more) { }

/* ---- the tests ---- */

#define WINDOW 0x1234
#define ICON 5
#define TASK_MANAGER 0x5a5a
static SDL_VideoDevice device;
static SDL_VideoData vdata;
static SDL_Window window;

static void wimp_event(int reason, RISCOS_PollBlock *event)
{
    RISCOS_WimpHandleEvent(&device, reason, event);
}

static void prequit(int size, int flags)
{
    RISCOS_PollBlock event;
    SDL_zero(event);
    event.message.size = size;
    event.message.sender = TASK_MANAGER;
    event.message.my_ref = 777;
    event.message.action = 8;
    event.message.data[0] = flags;
    wimp_event(18, &event);     /* PreQuit is sent recorded */
}

static void menu_quit(void)
{
    RISCOS_PollBlock event;
    SDL_zero(event);
    event.menu[0] = 0;
    event.menu[1] = -1;
    wimp_event(9, &event);
}

static void close_icon(void)
{
    RISCOS_PollBlock event;
    SDL_zero(event);
    event.window = WINDOW;
    wimp_event(3, &event);
}

static int restarted(void)
{
    _kernel_swi_regs r;
    ncalls = 0;
    RISCOS_RestartShutdown();
    return calls_to(Wimp_ProcessKey, &r, NULL) == 1 && r.r[0] == 0x1FC;
}

static void test_prequit_desktop(void)
{
    _kernel_swi_regs r;
    RISCOS_Message sent;

    /* a short PreQuit (no flag word): a desktop shutdown */
    reset();
    prequit(20, 0);
    CHECK(calls_to(Wimp_SendMessage, &r, &sent) == 1, "PreQuit acknowledged once");
    CHECK(r.r[0] == 19 && r.r[2] == TASK_MANAGER, "acknowledge (19) to the sender (r0 %d, r2 %x)", r.r[0], r.r[2]);
    CHECK(sent.your_ref == 777 && sent.action == 8, "your_ref = my_ref (%d)", sent.your_ref);
    CHECK(n_sdl_quit == 1 && n_app_terminating == 0, "SDL_QUIT posted (%d)", n_sdl_quit);
    CHECK(restarted(), "shutdown restarted with Ctrl-Shift-F12 when the program quits");
    CHECK(!restarted(), "and only once");

    /* with a flag word, bit 0 clear: also a desktop shutdown */
    reset();
    prequit(24, 0);
    CHECK(restarted(), "flags 0: shutdown restarted");
}

static void test_prequit_task_only(void)
{
    reset();
    prequit(24, 1);
    CHECK(calls_to(Wimp_SendMessage, NULL, NULL) == 1, "task-only PreQuit acknowledged");
    CHECK(n_sdl_quit == 1, "SDL_QUIT posted");
    CHECK(!restarted(), "no shutdown restarted when only this task is quit");
}

static void test_prequit_answered_no(void)
{
    /* the user said no, then quit the program another way */
    reset();
    prequit(20, 0);
    menu_quit();
    CHECK(!restarted(), "icon bar Quit after a PreQuit doesn't restart the shutdown");

    reset();
    prequit(20, 0);
    close_icon();
    CHECK(!restarted(), "close icon after a PreQuit doesn't restart the shutdown");

    /* ... or quit it much later */
    reset();
    ticks = 1000;
    prequit(20, 0);
    ticks += RISCOS_SHUTDOWN_ANSWER_MS + 1;
    CHECK(!restarted(), "quitting %d s after the PreQuit doesn't restart the shutdown",
          RISCOS_SHUTDOWN_ANSWER_MS / 1000);
    ticks = 1000;
    prequit(20, 0);
    ticks += RISCOS_SHUTDOWN_ANSWER_MS - 1000;
    CHECK(restarted(), "quitting within the time does");
}

static void test_close_and_menu(void)
{
    reset();
    close_icon();
    CHECK(n_close_events == 1 && close_window == &window, "close icon: SDL_WINDOWEVENT_CLOSE for our window");
    CHECK(n_sdl_quit == 0, "close icon: no SDL_QUIT from the driver (SDL sends it for the last window)");

    reset();
    {
        RISCOS_PollBlock event;
        SDL_zero(event);
        event.window = 0x9999;  /* someone else's window */
        wimp_event(3, &event);
    }
    CHECK(n_close_events == 0, "close request for another window ignored");

    reset();
    menu_quit();
    CHECK(n_sdl_quit == 1 && n_close_events == 0, "icon bar menu Quit: SDL_QUIT");
}

static void test_quit(void)
{
    reset();
    {
        RISCOS_PollBlock event;
        SDL_zero(event);
        event.message.size = 20;
        event.message.sender = TASK_MANAGER;
        event.message.action = 0;
        wimp_event(17, &event);
    }
    CHECK(n_app_terminating == 1 && n_sdl_quit == 1, "Message_Quit: SDL_APP_TERMINATING and SDL_QUIT");
    CHECK(calls_to(Wimp_SendMessage, NULL, NULL) == 0, "Message_Quit not acknowledged");

    /* the program hasn't taken the events yet: no exit */
    queued = SDL_TRUE;
    if (setjmp(exit_jump) == 0)
        RISCOS_PumpEvents(&device);
    CHECK(!exit_called, "no exit while SDL_QUIT is still queued");

    /* it took them and asked for more: the driver quits for it */
    queued = SDL_FALSE;
    if (setjmp(exit_jump) == 0)
        RISCOS_PumpEvents(&device);
    CHECK(exit_called && exit_status == 0 && n_sdl_Quit == 1, "then SDL_Quit and exit(0) (exit %d, SDL_Quit %d)",
          exit_called, n_sdl_Quit);

    /* a PreQuit the Quit follows is not restarted */
    reset();
    prequit(20, 0);
    {
        RISCOS_PollBlock event;
        SDL_zero(event);
        event.message.size = 20;
        event.message.action = 0;
        wimp_event(17, &event);
    }
    CHECK(!restarted(), "no restart after Message_Quit");
}

/* The icon bar icon's sprite name: all 12 characters of the longest names
   (SDL_riscoswimp.h RISCOS_IconSpriteName; Warzone 2100's icon was blank
   when "!Warzone2100" was cut to 11) */
/* Message_ModeChange: the eig factors cached for mouse positions and
   plotting are read again (they change between 90 and 180 dpi modes). */
static void test_mode_change(void)
{
    RISCOS_PollBlock event;
    reset();
    n_update_eigs = 0;
    SDL_zero(event);
    event.message.size = 20;
    event.message.action = 0x400C1;
    wimp_event(17, &event);
    CHECK(n_update_eigs == 1, "Message_ModeChange: eig factors read again (%d)", n_update_eigs);
    CHECK(n_full_mode_changed == 1, "and a full window told to follow the new screen");
    CHECK(n_sdl_quit == 0 && calls_to(Wimp_SendMessage, NULL, NULL) == 0, "and nothing else");
}

/* A full window (full screen that multitasks) has no title bar: a click on
   it brings it back to the front; an ordinary window isn't moved. */
static void test_full_window_click(void)
{
    RISCOS_PollBlock event;
    _kernel_swi_regs r;
    RISCOS_WindowState sent;
    int wimp_window = vdata.wimp_window;

    reset();
    vdata.wimp_window = WINDOW_H;
    vdata.full_window = SDL_TRUE;
    SDL_zero(event);
    event.click.window = WINDOW_H;
    event.click.buttons = 4;
    wimp_event(6, &event);
    CHECK(calls_to(Wimp_OpenWindow, &r, NULL) == 1, "full window clicked: opened again (%d)",
          calls_to(Wimp_OpenWindow, NULL, NULL));
    memcpy(&sent, (void *)(intptr_t)r.r[1], sizeof sent);
    CHECK(sent.open.behind == -1 && sent.open.window == WINDOW_H, "at the front (behind %d)", sent.open.behind);
    CHECK(vdata.pending_clicks & 4, "and the click still reaches the program");

    reset();
    vdata.full_window = SDL_FALSE;
    vdata.pending_clicks = 0;
    wimp_event(6, &event);
    CHECK(calls_to(Wimp_OpenWindow, NULL, NULL) == 0, "ordinary window clicked: not moved");
    vdata.wimp_window = wimp_window;
    vdata.pending_clicks = 0;
}

static void test_icon_sprite_name(void)
{
    static const char *const names[] = { "!Warzone2100", "!OpenTTD", "application", "!TestGL2" };
    RISCOS_IconCreate icon;
    int i;

    for (i = 0; i < 4; i++) {
        size_t n = strlen(names[i]);
        memset(&icon, 0x55, sizeof(icon));
        RISCOS_IconSpriteName(&icon, names[i]);
        CHECK(memcmp(icon.data, names[i], n) == 0 && (n == 12 || icon.data[n] == 0),
              "sprite name \"%s\" (%zu characters) in full in the icon", names[i], n);
    }
    memset(&icon, 0x55, sizeof(icon));
    RISCOS_IconSpriteName(&icon, "!ThirteenChrs");
    CHECK(memcmp(icon.data, "!ThirteenChr", 12) == 0 && icon.box.x0 == 0x55555555,
          "a longer name is cut at 12, not past the data field");
}

static void test_keys(void)
{
    static const int wimp_keys[] = { 0x1CC, 0x1DC, 0x1EC, 0x1FC };
    int i;
    for (i = 0; i < 4; i++) {
        RISCOS_PollBlock event;
        _kernel_swi_regs r;
        reset();
        SDL_zero(event);
        event.key.caret.window = WINDOW;
        event.key.code = wimp_keys[i];
        wimp_event(8, &event);
        CHECK(calls_to(Wimp_ProcessKey, &r, NULL) == 1 && r.r[0] == wimp_keys[i],
              "key &%X passed on to the Wimp", wimp_keys[i]);
    }
}

/* Pointer positions: screen pixels -> SDL pixels, with a 2x window scale
   and with a GL render size (SDL_RISCOS_GL_RENDER_SIZE) stretched over the
   window or the screen. */
static void test_mouse(void)
{
    SDL_WindowData wd;
    memset(&wd, 0, sizeof wd);
    fake_ptr_on = 1;
    vdata.xeig = vdata.yeig = 1;
    mouse.x = mouse.y = -1;
    fake_ptr[0] = 100 + 2 * 160; fake_ptr[1] = 900 - 2 * 120;       /* pixel 160,120 in */
    RISCOS_PollMouse(&device);
    CHECK(motion_x == 160 && motion_y == 120, "1:1 window: pixel 160,120 (%d,%d)", motion_x, motion_y);
    vdata.wscale_x = vdata.wscale_y = 2;
    RISCOS_PollMouse(&device);
    CHECK(motion_x == 80 && motion_y == 60, "2x window: 80,60 (%d,%d)", motion_x, motion_y);
    vdata.wscale_x = vdata.wscale_y = 1;
    window.driverdata = &wd;
    wd.render_w = 160; wd.render_h = 60; wd.disp_w = 320; wd.disp_h = 240;
    window.w = 160; window.h = 60;
    RISCOS_PollMouse(&device);
    CHECK(motion_x == 80 && motion_y == 30, "render size 160x60 in a 320x240 window: 80,30 (%d,%d)", motion_x, motion_y);
    fake_ptr[0] = 100 + 2 * 319; fake_ptr[1] = 900 - 2 * 239;       /* bottom right */
    RISCOS_PollMouse(&device);
    CHECK(motion_x == 159 && motion_y == 59, "bottom right corner: 159,59 (%d,%d)", motion_x, motion_y);
    /* full screen (1920x1080), a GL window rendering at 960x540 */
    vdata.wimp_window = 0;
    device.windows = &window;
    wd.gl_egl = 1;
    window.w = 960; window.h = 540;
    fake_ptr[0] = 2 * 1000; fake_ptr[1] = 2 * (1080 - 1 - 500);      /* pixel 1000,500 */
    RISCOS_PollMouse(&device);
    CHECK(motion_x == 500 && motion_y == 250, "full screen, render 960x540: 500,250 (%d,%d)", motion_x, motion_y);
    vdata.wimp_window = WINDOW;
    device.windows = NULL;
    window.driverdata = NULL;
    window.w = 320; window.h = 240;
    fake_ptr_on = 0;
}

static void *run(void *arg)
{
    (void)arg;
    device.driverdata = &vdata;
    vdata.wimp_task = 0x4000;
    vdata.wimp_window = WINDOW;
    vdata.wimp_sdl_window = &window;
    vdata.iconbar_icon = ICON;
    vdata.main_thread = 1;
    vdata.wscale_x = vdata.wscale_y = 1;
    window.w = 320;
    window.h = 240;

    test_prequit_desktop();
    test_prequit_task_only();
    test_prequit_answered_no();
    test_close_and_menu();
    test_mode_change();
    test_full_window_click();
    test_quit();
    test_keys();
    test_icon_sprite_name();
    test_mouse();
    return NULL;
}

int main(void)
{
    /* The driver passes blocks to SWIs as 32-bit addresses: run the tests
       on a stack below 2 GB (built -no-pie, so the statics are low too). */
    pthread_attr_t attr;
    pthread_t t;
    size_t stack_size = 1 << 20;
    void *stack = mmap((void *) 0x60000000, stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (stack == MAP_FAILED) { perror("mmap"); return 2; }
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, stack, stack_size);
    pthread_create(&t, &attr, run, NULL);
    pthread_join(t, NULL);

    printf("%d checks, %d failures: %s\n", checks, failures, failures ? "FAILED" : "ALL PASS");
    return failures != 0;
}
