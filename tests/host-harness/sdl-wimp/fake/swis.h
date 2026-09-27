/* Host stand-in for UnixLib's swis.h: the SWIs SDL_riscosevents.c uses
   (numbers as on RISC OS). */
#ifndef FAKE_SWIS_H
#define FAKE_SWIS_H
#define OS_Byte               0x06
#define OS_Mouse              0x1C
#define OS_ReadMonotonicTime  0x42
#define OS_Pointer            0x64
#define Wimp_OpenWindow       0x400C5
#define Wimp_Poll             0x400C7
#define Wimp_RedrawWindow     0x400C8
#define Wimp_GetRectangle     0x400CA
#define Wimp_GetWindowState   0x400CB
#define Wimp_GetPointerInfo   0x400CF
#define Wimp_SetCaretPosition 0x400D2
#define Wimp_CreateMenu       0x400D4
#define Wimp_ProcessKey       0x400DC
#define Wimp_CloseDown        0x400DD
#define Wimp_PollIdle         0x400E1
#define Wimp_SendMessage      0x400E7
#endif
