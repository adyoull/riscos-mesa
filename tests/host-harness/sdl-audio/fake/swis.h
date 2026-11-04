/* Host stand-in for UnixLib's swis.h: the SWIs SDL_riscosaudio.c uses
   (numbers as on RISC OS; the module SWIs are in the driver itself). */
#ifndef FAKE_SWIS_H
#define FAKE_SWIS_H
#define OS_SWINumberFromString 0x39
#endif
