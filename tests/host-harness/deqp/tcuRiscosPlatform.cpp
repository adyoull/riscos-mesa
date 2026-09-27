/*
 * dEQP platform for riscos-mesa's EGL on the host test harness's fake
 * RISC OS (tests/host-harness/egl/fake_riscos.c).
 *
 * - Display: EGL_DEFAULT_DISPLAY, or eglGetPlatformDisplayEXT with
 *   EGL_PLATFORM_RISCOS.
 * - Windows: fake Wimp windows on a 1920x1080 32bpp (0x00BBGGRR) screen,
 *   opened at the top left; the native window is the window handle (for
 *   the platform call, a pointer to it). Screen pixels can be read back,
 *   and windows can be resized.
 * - Pixmaps: 32bpp sprites in the config's colour order.
 * - The EGL library is the one linked in (egl_riscos.c): every function
 *   comes from its eglGetProcAddress (EGL_KHR_get_all_proc_addresses).
 *
 * Part of riscos-mesa, MIT licence.
 */
#include "tcuRiscosPlatform.hpp"
#include "egluNativeDisplay.hpp"
#include "egluNativeWindow.hpp"
#include "egluNativePixmap.hpp"
#include "egluGLContextFactory.hpp"
#include "eglwLibrary.hpp"
#include "eglwEnums.hpp"
#include "tcuTexture.hpp"
#include "tcuTextureUtil.hpp"
#include "deMemory.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

extern "C"
{
#include "fake_riscos.h"
typedef void (*riscos_proc)(void);
riscos_proc eglGetProcAddress(const char *name);
}

tcu::Platform *createPlatform(void)
{
    return new tcu::riscos::Platform();
}

namespace tcu
{
namespace riscos
{

using namespace eglw;

enum
{
    SCREEN_WIDTH          = 1920,
    SCREEN_HEIGHT         = 1080,
    DEFAULT_WINDOW_WIDTH  = 400,
    DEFAULT_WINDOW_HEIGHT = 300,
    EGL_PLATFORM_RISCOS   = 0x3FF5, // provisional: EGL/eglext_riscos.h
    VISUAL_TRGB           = 0x4000  // EGL_RISCOS_VISUAL_TRGB
};

// ---- the EGL library: everything through eglGetProcAddress ----

class Loader : public FunctionLoader
{
public:
    GenericFuncType get(const char *name) const
    {
        return (GenericFuncType)eglGetProcAddress(name);
    }
};

class Library : public FuncPtrLibrary
{
public:
    Library(void)
    {
        const Loader loader;
        initCore(&m_egl, &loader);
        initExtensions(&m_egl, &loader);
    }
};

static const Library &library(void)
{
    static const Library lib;
    return lib;
}

// ---- display ----

static const eglu::NativeDisplay::Capability DISPLAY_CAPABILITIES = (eglu::NativeDisplay::Capability)(
    eglu::NativeDisplay::CAPABILITY_GET_DISPLAY_LEGACY | eglu::NativeDisplay::CAPABILITY_GET_DISPLAY_PLATFORM_EXT);

class Display : public eglu::NativeDisplay
{
public:
    Display(void) : eglu::NativeDisplay(DISPLAY_CAPABILITIES, EGL_PLATFORM_RISCOS, "EGL_RISCOS_platform_wimp")
    {
    }
    EGLNativeDisplayType getLegacyNative(void)
    {
        return EGL_DEFAULT_DISPLAY;
    }
    void *getPlatformNative(void)
    {
        return nullptr; // the only display
    }
    const EGLAttrib *getPlatformAttributes(void) const
    {
        return nullptr;
    }
    const eglw::Library &getLibrary(void) const
    {
        return library();
    }
};

// ---- windows ----

static const eglu::NativeWindow::Capability WINDOW_CAPABILITIES = (eglu::NativeWindow::Capability)(
    eglu::NativeWindow::CAPABILITY_CREATE_SURFACE_LEGACY |
    eglu::NativeWindow::CAPABILITY_CREATE_SURFACE_PLATFORM_EXTENSION | eglu::NativeWindow::CAPABILITY_GET_SURFACE_SIZE |
    eglu::NativeWindow::CAPABILITY_SET_SURFACE_SIZE | eglu::NativeWindow::CAPABILITY_GET_SCREEN_SIZE |
    eglu::NativeWindow::CAPABILITY_READ_SCREEN_PIXELS);

class Window : public eglu::NativeWindow
{
public:
    // Tests make and destroy windows in several threads at once: the
    // fake Wimp's window list is changed under its SWI lock
    Window(int width, int height) : eglu::NativeWindow(WINDOW_CAPABILITIES), m_handle(s_next++), m_size(width, height)
    {
        open();
    }
    ~Window(void)
    {
        fake_lock();
        for (int i = 0; i < FAKE_MAX_WINDOWS; i++)
            if (fake_windows[i].handle == m_handle)
                fake_windows[i].handle = 0;
        fake_unlock();
    }
    EGLNativeWindowType getLegacyNative(void)
    {
        return (EGLNativeWindowType)(intptr_t)m_handle;
    }
    void *getPlatformExtension(void)
    {
        return &m_handle; // EGL_RISCOS_platform_wimp: a pointer to the handle
    }
    IVec2 getSurfaceSize(void) const
    {
        return m_size;
    }
    IVec2 getScreenSize(void) const
    {
        return m_size;
    }
    void setSurfaceSize(IVec2 size)
    {
        m_size = size;
        open();
    }
    void readScreenPixels(tcu::TextureLevel *dst) const
    {
        const int w = m_size.x(), h = m_size.y();
        const LockGuard lock;
        dst->setStorage(TextureFormat(TextureFormat::RGBA, TextureFormat::UNORM_INT8), w, h);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                const uint32_t p = fake_screen_pixel(x, y); // 0x00BBGGRR screen
                dst->getAccess().setPixel(
                    IVec4((int)(p & 0xff), (int)((p >> 8) & 0xff), (int)((p >> 16) & 0xff), 255), x, y);
            }
    }

private:
    // A window at the top left of the screen; OS units are 2 per pixel
    void open(void)
    {
        const int top = SCREEN_HEIGHT * 2;
        const LockGuard lock;
        fake_open_window(m_handle, 0, top - m_size.y() * 2, m_size.x() * 2, top, 0, 0);
    }

    struct LockGuard
    {
        LockGuard(void)
        {
            fake_lock();
        }
        ~LockGuard(void)
        {
            fake_unlock();
        }
    };
    static std::atomic<int> s_next;
    int m_handle;
    IVec2 m_size;
};

std::atomic<int> Window::s_next(0x1001);

class WindowFactory : public eglu::NativeWindowFactory
{
public:
    WindowFactory(void) : eglu::NativeWindowFactory("wimp", "Wimp window", WINDOW_CAPABILITIES)
    {
    }
    eglu::NativeWindow *createWindow(eglu::NativeDisplay *, const eglu::WindowParams &params) const
    {
        const int w = params.width != eglu::WindowParams::SIZE_DONT_CARE ? params.width : DEFAULT_WINDOW_WIDTH;
        const int h = params.height != eglu::WindowParams::SIZE_DONT_CARE ? params.height : DEFAULT_WINDOW_HEIGHT;
        return new Window(w, h);
    }
};

// ---- pixmaps: 32bpp sprites ----

static const eglu::NativePixmap::Capability PIXMAP_CAPABILITIES = (eglu::NativePixmap::Capability)(
    eglu::NativePixmap::CAPABILITY_CREATE_SURFACE_LEGACY |
    eglu::NativePixmap::CAPABILITY_CREATE_SURFACE_PLATFORM_EXTENSION | eglu::NativePixmap::CAPABILITY_READ_PIXELS);

class Pixmap : public eglu::NativePixmap
{
public:
    Pixmap(int width, int height, bool trgb) : eglu::NativePixmap(PIXMAP_CAPABILITIES), m_w(width), m_h(height)
    {
        // A mode selector for 0x00RRGGBB (ModeFlags bit 14), 32bpp; must
        // stay in memory (static) as the sprite refers to it
        static int trgb_selector[] = {1, 0, 0, 5, -1, 0, 0x4000, -1};
        m_area    = (int *)calloc(1, 16 + 44 + (size_t)width * height * 4);
        m_sprite  = m_area + 4;
        m_area[0] = 16 + 44 + width * height * 4;
        m_area[1] = 1;
        m_area[2] = 16;
        m_area[3] = m_area[0];
        m_sprite[0]  = 44 + width * height * 4;
        m_sprite[4]  = width - 1;
        m_sprite[5]  = height - 1;
        m_sprite[6]  = 0;
        m_sprite[7]  = 31;
        m_sprite[8]  = 44;
        m_sprite[9]  = 44;
        m_sprite[10] = trgb ? (int)(intptr_t)trgb_selector : (1 | (90 << 1) | (90 << 14) | (6 << 27));
        m_trgb       = trgb;
    }
    ~Pixmap(void)
    {
        free(m_area);
    }
    EGLNativePixmapType getLegacyNative(void)
    {
        return (EGLNativePixmapType)m_sprite;
    }
    void *getPlatformExtension(void)
    {
        return m_sprite;
    }
    void readPixels(tcu::TextureLevel *dst)
    {
        const uint32_t *pix = (const uint32_t *)((char *)m_sprite + 44);
        dst->setStorage(TextureFormat(TextureFormat::RGBA, TextureFormat::UNORM_INT8), m_w, m_h);
        for (int y = 0; y < m_h; y++)
            for (int x = 0; x < m_w; x++)
            {
                const uint32_t p = pix[y * m_w + x]; // row 0 = top
                const int lo = (int)(p & 0xff), mid = (int)((p >> 8) & 0xff), hi = (int)((p >> 16) & 0xff);
                dst->getAccess().setPixel(m_trgb ? IVec4(hi, mid, lo, 255) : IVec4(lo, mid, hi, 255), x, y);
            }
    }

private:
    int m_w, m_h;
    bool m_trgb;
    int *m_area, *m_sprite;
};

class PixmapFactory : public eglu::NativePixmapFactory
{
public:
    PixmapFactory(void) : eglu::NativePixmapFactory("sprite", "32bpp sprite", PIXMAP_CAPABILITIES)
    {
    }
    eglu::NativePixmap *createPixmap(eglu::NativeDisplay *, int width, int height) const
    {
        return new Pixmap(width, height, false);
    }
    eglu::NativePixmap *createPixmap(eglu::NativeDisplay *display, EGLDisplay eglDisplay, EGLConfig config,
                                     const EGLAttrib *, int width, int height) const
    {
        EGLint visual = 0;
        display->getLibrary().getConfigAttrib(eglDisplay, config, EGL_NATIVE_VISUAL_ID, &visual);
        return new Pixmap(width, height, visual == VISUAL_TRGB);
    }
};

class DisplayFactory : public eglu::NativeDisplayFactory
{
public:
    DisplayFactory(void)
        : eglu::NativeDisplayFactory("riscos", "RISC OS (fake, host)", DISPLAY_CAPABILITIES, EGL_PLATFORM_RISCOS,
                                     "EGL_RISCOS_platform_wimp")
    {
        m_nativeWindowRegistry.registerFactory(new WindowFactory());
        m_nativePixmapRegistry.registerFactory(new PixmapFactory());
    }
    eglu::NativeDisplay *createDisplay(const EGLAttrib *) const
    {
        return new Display();
    }
};

Platform::Platform(void)
{
    fake_set_screen(SCREEN_WIDTH, SCREEN_HEIGHT, 0, 5);
    fake_reset_clip();
    m_nativeDisplayFactoryRegistry.registerFactory(new DisplayFactory());
    m_contextFactoryRegistry.registerFactory(new eglu::GLContextFactory(m_nativeDisplayFactoryRegistry));
}

Platform::~Platform(void)
{
}

} // namespace riscos
} // namespace tcu
