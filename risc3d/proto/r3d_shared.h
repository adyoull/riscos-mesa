/* r3d_shared.h: the memory a RISC3D context shares with its renderer.
   The program's GL library writes commands into the ring and moves head;
   the renderer reads them and moves tail. Neither end makes a system call
   to do so. Answers (query results, a finished frame's pixels) come back
   in the reply area. The same layout for 32-bit ARM (RISC OS) and 64-bit
   (the Linux helper).

   A command is words: opcode, length in words (with these two), then its
   arguments. It is never split across the end of the ring: opcode 0
   (OP_WRAP) means "the next command is at the start of the ring".

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#ifndef R3D_SHARED_H
#define R3D_SHARED_H
#include <stdint.h>

#define R3D_MAGIC 0x31443352u		/* "R3D1" */
#define R3D_RING_WORDS (1u << 20)	/* 4 MB */
#define R3D_REPLY_BYTES (32u << 20)	/* a 4K frame */
#define R3D_AREA_BYTES (4096u + R3D_RING_WORDS * 4 + R3D_REPLY_BYTES)
#define OP_WRAP 0
#define OP_BLOB 0x7fff			/* data for the next command marked R3D_WITH_BLOB */
#define R3D_WITH_BLOB 0x80000000u

struct r3d_area
{
  uint32_t magic, ring_words, ring_offset, reply_offset, reply_bytes;
  volatile uint32_t head;		/* words written, ever */
  volatile uint32_t tail;		/* words read, ever */
  volatile uint32_t sleeping;		/* the renderer is waiting to be woken */
  volatile uint32_t reply_len;		/* the last answer's length in bytes */
  volatile uint32_t asking;		/* the program is about to wait for an
					   answer (so the renderer, if awake,
					   goes to the socket at once) */
  volatile uint32_t frame_done;		/* overlapped frames: the last one read
					   back into its half of the reply area */
  uint32_t pad[5];
};

/* what the program sends down the context's socket (through RISC3D_Sync) */
#define R3D_WAKE 'W'			/* there are commands */
#define R3D_WAIT 'S'			/* finish them all, then answer one byte */

#endif
