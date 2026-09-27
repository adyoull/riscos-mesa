#ifndef _TCURISCOSPLATFORM_HPP
#define _TCURISCOSPLATFORM_HPP
/*
 * dEQP platform for riscos-mesa's EGL on the host test harness's fake
 * RISC OS (tests/host-harness/egl). Native display: EGL_DEFAULT_DISPLAY;
 * native window: a (fake) Wimp window handle; native pixmap: a 32bpp
 * sprite. Part of riscos-mesa, MIT licence.
 */
#include "tcuDefs.hpp"
#include "tcuPlatform.hpp"
#include "egluPlatform.hpp"
#include "gluPlatform.hpp"

namespace tcu
{
namespace riscos
{

class Platform : public tcu::Platform, private eglu::Platform, private glu::Platform
{
public:
    Platform(void);
    virtual ~Platform(void);

    virtual const glu::Platform &getGLPlatform(void) const
    {
        return static_cast<const glu::Platform &>(*this);
    }
    virtual const eglu::Platform &getEGLPlatform(void) const
    {
        return static_cast<const eglu::Platform &>(*this);
    }
};

} // namespace riscos
} // namespace tcu

#endif
