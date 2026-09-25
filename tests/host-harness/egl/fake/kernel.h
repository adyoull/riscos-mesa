/* Host stand-in for UnixLib's kernel.h (EGL host harness). */
#ifndef FAKE_KERNEL_H
#define FAKE_KERNEL_H
typedef struct { int errnum; char errmess[252]; } _kernel_oserror;
typedef struct { int r[10]; } _kernel_swi_regs;
_kernel_oserror *_kernel_swi(int no, _kernel_swi_regs *in, _kernel_swi_regs *out);
int _kernel_osbyte(int op, int x, int y);
int _kernel_oswrch(int c);
#endif
