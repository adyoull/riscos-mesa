typedef struct { int errnum; char errmess[252]; } _kernel_oserror;
typedef struct { int r[10]; } _kernel_swi_regs;
_kernel_oserror *_kernel_swi(int, _kernel_swi_regs *, _kernel_swi_regs *);
