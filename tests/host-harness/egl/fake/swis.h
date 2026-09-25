/* Host stand-in for UnixLib's swis.h (EGL host harness). */
#ifndef FAKE_SWIS_H
#define FAKE_SWIS_H
#define OS_Byte             0x06
#define OS_SpriteOp         0x2E
#define OS_ReadVduVariables 0x31
#define OS_ReadModeVariable 0x35
#define OS_ScreenMode       0x65
#define Wimp_RedrawWindow   0x400C8
#define Wimp_UpdateWindow   0x400C9
#define Wimp_GetRectangle   0x400CA
#define Wimp_GetWindowState 0x400CB
#endif
