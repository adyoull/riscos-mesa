/* Host stand-in for UnixLib's unixlib/local.h (EGL host harness): Unix to
   RISC OS path conversion, enough for program names. */
#ifndef FAKE_UNIXLIB_LOCAL_H
#define FAKE_UNIXLIB_LOCAL_H
#include <stdio.h>
#include <string.h>
static inline char *__riscosify_std(const char *name, int create_dir, char *buf,
                                    size_t len, int *filetype)
{
    char *p;
    (void) create_dir; (void) filetype;
    snprintf(buf, len, "%s", name[0] == '/' ? name + 1 : name);
    for (p = buf; *p; p++) if (*p == '/') *p = '.';
    return buf;
}
#endif
