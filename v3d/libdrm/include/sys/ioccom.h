/* sys/ioccom.h for riscos-mesa's libdrm layer (v3d/libdrm): Linux's
   request number encoding, which V3DRender_Ioctl takes (its
   include/drm/drm.h). drm.h includes this header on systems that aren't
   Linux; UnixLib's sys/ioctl.h has the BSD encoding, which differs, so
   its macros are replaced here.
   Copyright (c) 2026 Andrew Youll. MIT licence. */
#ifndef RISCOS_MESA_SYS_IOCCOM_H
#define RISCOS_MESA_SYS_IOCCOM_H
#include <sys/ioctl.h>
#undef _IOC
#undef _IO
#undef _IOR
#undef _IOW
#undef _IOWR
#undef _IOC_NONE
#undef _IOC_READ
#undef _IOC_WRITE
#define _IOC_NRBITS 8
#define _IOC_TYPEBITS 8
#define _IOC_SIZEBITS 14
#define _IOC_NRSHIFT 0
#define _IOC_TYPESHIFT (_IOC_NRSHIFT + _IOC_NRBITS)
#define _IOC_SIZESHIFT (_IOC_TYPESHIFT + _IOC_TYPEBITS)
#define _IOC_DIRSHIFT (_IOC_SIZESHIFT + _IOC_SIZEBITS)
#define _IOC_NONE 0u
#define _IOC_WRITE 1u
#define _IOC_READ 2u
/* the BSD direction names (libdrm's xf86drm.h uses them off Linux) */
#undef IOC_VOID
#undef IOC_OUT
#undef IOC_IN
#undef IOC_INOUT
#define IOC_VOID _IOC_NONE
#define IOC_OUT _IOC_READ
#define IOC_IN _IOC_WRITE
#define IOC_INOUT (_IOC_READ | _IOC_WRITE)
#define _IOC(dir, type, nr, size) \
  ((unsigned long) (((dir) << _IOC_DIRSHIFT) | ((type) << _IOC_TYPESHIFT) | \
                    ((nr) << _IOC_NRSHIFT) | ((size) << _IOC_SIZESHIFT)))
#define _IO(type, nr) _IOC (_IOC_NONE, (type), (nr), 0)
#define _IOR(type, nr, t) _IOC (_IOC_READ, (type), (nr), sizeof (t))
#define _IOW(type, nr, t) _IOC (_IOC_WRITE, (type), (nr), sizeof (t))
#define _IOWR(type, nr, t) _IOC (_IOC_READ | _IOC_WRITE, (type), (nr), sizeof (t))
#endif
