/* r3d_gl.c: OpenGL (1.0 to 2.1, compatibility) and OpenGL ES 2 for RISC
   OS programs, as commands for the RISC3D module (which hands them to its
   backend). Functions that are only numbers in and out, or that take
   small arrays, are generated (out/gen/client_gen.c, from gl.xml); the
   ones with strings, pixels and vertex arrays are here.

   Commands are written straight into memory shared with the renderer
   (proto/r3d_shared.h): no SWI and no system call for a command. The
   module is called only to wake a renderer that has gone to sleep and to
   wait for an answer (a query, the end of a frame).

   So that common queries need no wait, this side keeps what it can of
   GL's state: the matrix stacks (computed as Mesa does), the matrix mode,
   the viewport, the list base, the active texture unit, buffer bindings,
   pixel store settings, and object names (glGen* answer at once). Display
   lists record the state they change, and calling one replays that here.

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <swis.h>
#include <kernel.h>
#include "r3d_gl.h"
#include "r3d.h"
#include "r3d_ops.h"
#include "r3d_shared.h"
#include "r3d_gl_names.h"

#define MAX_ATTRIBS 16
#define MAX_UNITS 8
#define MAX_ATTRIB_DEPTH 16
#define MAX_LIST_NESTING 64

struct attrib
{
  int enabled, client;			/* client: its pointer is the program's memory */
  int in_buffer;			/* its pointer is into a buffer object */
  GLint size;
  GLenum type;
  GLboolean normalized;
  GLsizei stride;
  const void *pointer;
};

/* fixed-function arrays: vertex 0, normal 1, colour 2, index 3, edge
   flag 4, fog coordinate 5, secondary colour 6, texture coordinates
   8 + unit (the slots the renderer uses too) */
#define FIXED_SLOTS (8 + MAX_UNITS)

struct pixelstore
{
  GLint swap, lsb, row_length, skip_rows, skip_pixels, alignment, image_height, skip_images;
};

struct stack
{
  int depth, max;
  GLfloat (*m)[16];			/* m[0 .. depth - 1]; the top is m[depth - 1] */
};

struct saved_attrib
{
  GLbitfield mask;
  GLenum matrix_mode;
  GLint viewport[4];
  GLuint list_base;
  int active;
};

struct saved_client
{
  GLbitfield mask;
  struct attrib attribs[MAX_ATTRIBS];
  struct attrib fixed[FIXED_SLOTS];
  int client_unit;
  GLuint array_buffer;
  struct pixelstore pack, unpack;
  GLuint pack_buffer, unpack_buffer;
};

/* a display list's record of the state it changes (words: op, length,
   arguments, as in the ring) */
struct list
{
  GLuint name;
  uint32_t *w;
  size_t n, cap;
};

struct mapping
{
  GLenum target;
  void *ptr;
  GLint size;
  GLenum access;
};

/* One context: its RISC3D handle, the shared memory, and what the library
   has to remember of GL's state. The gl functions work on the current one. */
struct r3d_context
{
  int handle;
  struct r3d_area *area;
  uint32_t *ring;
  uint32_t ring_words;
  uint32_t local_head;
  uint32_t published;			/* the head the renderer has been shown */
  uint32_t tail_seen;			/* the renderer's tail when last read */
  int desktop;				/* OpenGL (compatibility), not GLES */

  struct attrib attribs[MAX_ATTRIBS];
  struct attrib fixed[FIXED_SLOTS];
  int client_unit;
  GLuint array_buffer, element_buffer, pack_buffer, unpack_buffer;
  struct pixelstore pack, unpack;

  /* names: the next to give; above every name the program has used */
  GLuint next_texture, next_buffer, next_list;

  /* kept state */
  struct stack modelview, projection, texture[MAX_UNITS];
  int units;
  GLenum matrix_mode;
  int active;				/* active texture unit */
  GLint viewport[4];
  int viewport_known;
  GLuint list_base;
  int in_begin;
  struct saved_attrib attrib_stack[MAX_ATTRIB_DEPTH];
  int attrib_depth;
  struct saved_client client_stack[MAX_ATTRIB_DEPTH];
  int client_depth;

  /* display lists */
  struct list *lists;
  size_t nlists, caplists;
  GLuint compiling;			/* the list being made, or 0 */
  GLenum compile_mode;
  struct list *recording;
  int replaying;

  /* feedback and selection: the program's buffers */
  GLfloat *feedback;
  GLsizei feedback_size;
  GLuint *select;
  GLsizei select_size;

  struct mapping maps[4];
  char *strings[6];

  /* overlapped frames: the surface's size, the last frame sent */
  int surface_w, surface_h;
  uint32_t frame;
  int overlay;				/* frames go to the screen (r3d_overlay) */
  int after_overlay;			/* the last frame sent was one of those */

  /* immediate mode being collected (r3d_imm) */
  int imm_on;
  GLenum imm_mode;
  uint32_t imm_specified, imm_layout;
  GLfloat imm_cur[6][4];		/* colour, normal, texture coordinates 0-3 */
  GLfloat *imm_v;			/* gathered here (strips and the like) */
  size_t imm_count, imm_cap, imm_stride;
  size_t imm_capv;			/* room for this many vertices */
  GLfloat *imm_w;			/* where the next one goes */
  uint32_t *imm_cmd;			/* the draw being filled in the ring */
  uint32_t imm_cmd_head;		/* where it starts */
};
static struct r3d_context none = { .handle = -1 };
static struct r3d_context *cur = &none;
static const char *last_error;
static char error_text[256];

static uint32_t r3d_f2u (float f) { union { float f; uint32_t u; } v = { f }; return v.u; }
static float r3d_u2f (uint32_t u) { union { uint32_t u; float f; } v = { u }; return v.f; }
static void r3d_d2w (uint32_t *w, double d) { memcpy (w, &d, 8); }
static long r3d_nonneg (long n) { return n < 0 ? 0 : n; }

/* the module: 0 wakes the renderer, 1 waits until it has done everything */
static int
kick (int wait)
{
  if (wait)
    __atomic_store_n (&cur->area->asking, 1, __ATOMIC_SEQ_CST);
  _kernel_oserror *e = _swix (RISC3D_Sync, _INR (0, 1), cur->handle, wait);
  if (wait)
    __atomic_store_n (&cur->area->asking, 0, __ATOMIC_RELAXED);
  if (e)
    {
      strncpy (error_text, e->errmess, sizeof error_text - 1);
      last_error = error_text;
      return -1;
    }
  return 0;
}

/* Commands are written at local_head and shown to the renderer (the
   shared head moved) in batches: when a sixteenth of the ring is waiting,
   at glFlush, at r3d_flush and before any wait. It is woken only if it
   has gone to sleep. */

static void
publish (void)
{
  if (cur->published == cur->local_head)
    return;
  cur->published = cur->local_head;
  __atomic_store_n (&cur->area->head, cur->local_head, __ATOMIC_SEQ_CST);
  if (__atomic_load_n (&cur->area->sleeping, __ATOMIC_SEQ_CST))
    kick (0);
}

void
r3d_flush (void)
{
  if (cur->handle >= 0)
    publish ();
}

/* room for a command of n argument words: its arguments, or 0 */
static uint32_t *r3d_put_slow (uint32_t op, size_t n);

static inline __attribute__ ((always_inline)) uint32_t *
r3d_put (uint32_t op, size_t n)
{
  /* the usual case, in each function: room, no batch to show yet */
  struct r3d_context *c = cur;
  uint32_t need = (uint32_t) n + 2, h = c->local_head, at = h & (c->ring_words - 1);
  if (__builtin_expect (c->handle >= 0 && !c->imm_on && op != OP_glFlush && op != OP_glFinish && n < 4096
			&& h - c->published <= c->ring_words / 16 && at + need <= c->ring_words
			&& c->ring_words - (h - c->tail_seen) >= need, 1))
    {
      uint32_t *a = c->ring + at;
      a[0] = op;
      a[1] = need;
      c->local_head = h + need;
      return a + 2;
    }
  return r3d_put_slow (op, n);
}

static void imm_fallback (void);

static uint32_t *
r3d_put_slow (uint32_t op, size_t n)
{
  /* any other command between glBegin and glEnd: what was collected
     goes as the calls themselves, then this one */
  if (cur->imm_on)
    imm_fallback ();
  if (cur->handle < 0 || n + 2 > cur->ring_words / 2)
    return 0;
  /* the renderer is shown commands a batch at a time (and always before a
     wait): shown one by one, a renderer faster than the program goes to
     sleep between them and has to be woken for each */
  /* (nothing here touches the shared memory but the command itself: the
     head is published, and the tail read, only now and then. Each would
     be a memory barrier on ARM, and immediate mode is a call a vertex) */
  if (cur->local_head - cur->published > cur->ring_words / 16 || op == OP_glFlush || op == OP_glFinish)
    publish ();
  uint32_t need = (uint32_t) n + 2;
  uint32_t at = cur->local_head & (cur->ring_words - 1);
  uint32_t skip = at + need > cur->ring_words ? cur->ring_words - at : 0;
  if (cur->ring_words - (cur->local_head - cur->tail_seen) < need + skip)
    {
      cur->tail_seen = __atomic_load_n (&cur->area->tail, __ATOMIC_SEQ_CST);
      if (cur->ring_words - (cur->local_head - cur->tail_seen) < need + skip)
	{
	  if (kick (1) < 0)		/* full: let the renderer catch up */
	    return 0;
	  cur->tail_seen = __atomic_load_n (&cur->area->tail, __ATOMIC_SEQ_CST);
	}
    }
  if (skip)
    {
      cur->ring[at] = OP_WRAP;
      cur->local_head += skip;
      at = 0;
    }
  uint32_t *a = cur->ring + at;
  a[0] = op;
  a[1] = need;
  cur->local_head += need;
  return a + 2;
}

/* a command with fixed words and then data of any size (padded to words).
   Data too big for one command goes ahead of it in pieces (OP_BLOB), and
   the renderer puts it after the fixed words. */
static void
r3d_put_data (uint32_t op, const uint32_t *fixed, size_t nfixed, const void *data, size_t bytes)
{
  size_t dw = data ? (bytes + 3) / 4 : 0;
  size_t piece = cur->ring_words / 8;
  if (cur->handle < 0)
    return;
  if (nfixed + dw <= piece)
    {
      uint32_t *a = r3d_put (op, nfixed + dw);
      if (!a)
	return;
      memcpy (a, fixed, nfixed * 4);
      if (dw)
	{
	  a[nfixed + dw - 1] = 0;
	  memcpy (a + nfixed, data, bytes);
	}
      return;
    }
  for (size_t at = 0; at < bytes; )
    {
      size_t n = bytes - at < piece * 4 ? bytes - at : piece * 4;
      uint32_t *a = r3d_put (OP_BLOB, 1 + (n + 3) / 4);
      if (!a)
	return;
      a[0] = (uint32_t) n;
      a[(n + 3) / 4] = 0;
      memcpy (a + 1, (const char *) data + at, n);
      at += n;
    }
  uint32_t *a = r3d_put (op | R3D_WITH_BLOB, nfixed);
  if (a)
    memcpy (a, fixed, nfixed * 4);
}

/* waits for everything to be done, and takes the last command's answer */
static int
r3d_sync (void *reply, size_t max)
{
  if (cur->handle < 0)
    return -1;
  cur->area->reply_len = 0;
  if (cur->published != cur->local_head)
    {
      cur->published = cur->local_head;
      __atomic_store_n (&cur->area->head, cur->local_head, __ATOMIC_SEQ_CST);
    }
  if (kick (1) < 0)
    return -1;
  cur->tail_seen = __atomic_load_n (&cur->area->tail, __ATOMIC_SEQ_CST);
  uint32_t n = cur->area->reply_len;
  if (reply && n)
    memcpy (reply, (const char *) cur->area + cur->area->reply_offset, n < max ? n : max);
  return (int) n;
}

/* the answer itself, in the shared memory (valid until the next command) */
static const void *
reply_area (void)
{
  return (const char *) cur->area + cur->area->reply_offset;
}

const char *
r3d_error (void)
{
  return last_error;
}

/* RISC3D has a driver, and the program hasn't been told to do without
   (the system variable RISC3D$Disable, for comparing with software) */
int
r3d_available (void)
{
  const char *drv = 0;
  unsigned flags = 0;
  if (getenv ("RISC3D$Disable"))
    return 0;
  if (_swix (RISC3D_Info, _OUT (3) | _OUT (4), &drv, &flags))
    return 0;
  /* a DRM device (V3DRender) takes no command stream: Mesa's own driver
     for it is used instead (RISC3D_Info r5, r6) */
  return drv != 0 && !(flags & 1);
}

/* --- kept state: matrices ------------------------------------------------

   As Mesa computes them (src/mesa/math/m_matrix.c), in floats, column
   first. */

static const GLfloat identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

static void
mat_mul (GLfloat *p, const GLfloat *a, const GLfloat *b)
{
  GLfloat r[16];
  for (int i = 0; i < 4; i++)
    {
      const GLfloat ai0 = a[i], ai1 = a[4 + i], ai2 = a[8 + i], ai3 = a[12 + i];
      r[i] = ai0 * b[0] + ai1 * b[1] + ai2 * b[2] + ai3 * b[3];
      r[4 + i] = ai0 * b[4] + ai1 * b[5] + ai2 * b[6] + ai3 * b[7];
      r[8 + i] = ai0 * b[8] + ai1 * b[9] + ai2 * b[10] + ai3 * b[11];
      r[12 + i] = ai0 * b[12] + ai1 * b[13] + ai2 * b[14] + ai3 * b[15];
    }
  memcpy (p, r, sizeof r);
}

static int
stack_init (struct stack *s, int max)
{
  if (max < 2)
    max = 2;
  s->m = malloc ((size_t) max * sizeof *s->m);
  if (!s->m)
    return 0;
  s->max = max;
  s->depth = 1;
  memcpy (s->m[0], identity, sizeof identity);
  return 1;
}

/* the stack the matrix functions work on now, or 0 */
static struct stack *
current_stack (void)
{
  switch (cur->matrix_mode)
    {
    case GL_MODELVIEW: return &cur->modelview;
    case GL_PROJECTION: return &cur->projection;
    case GL_TEXTURE: return cur->active < cur->units ? &cur->texture[cur->active] : 0;
    }
  return 0;
}

static GLfloat *
top (void)
{
  struct stack *s = current_stack ();
  return s && s->m ? s->m[s->depth - 1] : 0;
}

static void
st_load (const GLfloat *m)
{
  GLfloat *t = top ();
  if (t)
    memcpy (t, m, 16 * sizeof *m);
}

static void
st_mult (const GLfloat *m)
{
  GLfloat *t = top ();
  if (t)
    mat_mul (t, t, m);
}

static void
st_translate (GLfloat x, GLfloat y, GLfloat z)
{
  GLfloat *m = top ();
  if (!m)
    return;
  m[12] = m[0] * x + m[4] * y + m[8] * z + m[12];
  m[13] = m[1] * x + m[5] * y + m[9] * z + m[13];
  m[14] = m[2] * x + m[6] * y + m[10] * z + m[14];
  m[15] = m[3] * x + m[7] * y + m[11] * z + m[15];
}

static void
st_scale (GLfloat x, GLfloat y, GLfloat z)
{
  GLfloat *m = top ();
  if (!m)
    return;
  m[0] *= x; m[4] *= y; m[8] *= z;
  m[1] *= x; m[5] *= y; m[9] *= z;
  m[2] *= x; m[6] *= y; m[10] *= z;
  m[3] *= x; m[7] *= y; m[11] *= z;
}

static void
st_rotate (GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
  if (angle == 0.0F || !top ())
    return;
  GLfloat s = (GLfloat) sin (angle * M_PI / 180.0), c = (GLfloat) cos (angle * M_PI / 180.0);
  GLfloat mag = sqrtf (x * x + y * y + z * z);
  if (mag <= 1.0e-4F)
    return;
  x /= mag; y /= mag; z /= mag;
  GLfloat xx = x * x, yy = y * y, zz = z * z, xy = x * y, yz = y * z, zx = z * x;
  GLfloat xs = x * s, ys = y * s, zs = z * s, one_c = 1.0F - c;
  GLfloat m[16];
#define M(row, col) m[(col) * 4 + (row)]
  M (0, 0) = (one_c * xx) + c; M (0, 1) = (one_c * xy) - zs; M (0, 2) = (one_c * zx) + ys; M (0, 3) = 0.0F;
  M (1, 0) = (one_c * xy) + zs; M (1, 1) = (one_c * yy) + c; M (1, 2) = (one_c * yz) - xs; M (1, 3) = 0.0F;
  M (2, 0) = (one_c * zx) - ys; M (2, 1) = (one_c * yz) + xs; M (2, 2) = (one_c * zz) + c; M (2, 3) = 0.0F;
  M (3, 0) = 0.0F; M (3, 1) = 0.0F; M (3, 2) = 0.0F; M (3, 3) = 1.0F;
#undef M
  st_mult (m);
}

static void
st_ortho (GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f)
{
  if (l == r || b == t || n == f)
    return;				/* GL_INVALID_VALUE */
  GLfloat m[16] = { 2.0F / (r - l), 0, 0, 0,
		    0, 2.0F / (t - b), 0, 0,
		    0, 0, -2.0F / (f - n), 0,
		    -(r + l) / (r - l), -(t + b) / (t - b), -(f + n) / (f - n), 1.0F };
  st_mult (m);
}

static void
st_frustum (GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f)
{
  if (n <= 0.0F || f <= 0.0F || n == f || l == r || t == b)
    return;				/* GL_INVALID_VALUE */
  GLfloat x = (2.0F * n) / (r - l), y = (2.0F * n) / (t - b);
  GLfloat a = (r + l) / (r - l), bb = (t + b) / (t - b);
  GLfloat c = -(f + n) / (f - n), d = -(2.0F * f * n) / (f - n);
  GLfloat m[16] = { x, 0, 0, 0, 0, y, 0, 0, a, bb, c, -1.0F, 0, 0, d, 0 };
  st_mult (m);
}

static void
st_push (void)
{
  struct stack *s = current_stack ();
  if (s && s->m && s->depth < s->max)
    {
      memcpy (s->m[s->depth], s->m[s->depth - 1], sizeof s->m[0]);
      s->depth++;
    }
}

static void
st_pop (void)
{
  struct stack *s = current_stack ();
  if (s && s->m && s->depth > 1)
    s->depth--;
}

static void
st_matrix_mode (GLenum mode)
{
  if (mode == GL_MODELVIEW || mode == GL_PROJECTION || mode == GL_TEXTURE)
    cur->matrix_mode = mode;
}

static void
st_active (GLenum texture)
{
  if (texture >= GL_TEXTURE0 && texture < GL_TEXTURE0 + 32)
    cur->active = (int) (texture - GL_TEXTURE0);
}

static void
st_viewport (GLint x, GLint y, GLsizei w, GLsizei h)
{
  if (w < 0 || h < 0)
    return;
  cur->viewport[0] = x;
  cur->viewport[1] = y;
  cur->viewport[2] = w;
  cur->viewport[3] = h;
  cur->viewport_known = 1;
}

static void
st_push_attrib (GLbitfield mask)
{
  if (cur->attrib_depth >= MAX_ATTRIB_DEPTH)
    return;
  struct saved_attrib *a = &cur->attrib_stack[cur->attrib_depth++];
  a->mask = mask;
  a->matrix_mode = cur->matrix_mode;
  memcpy (a->viewport, cur->viewport, sizeof a->viewport);
  a->list_base = cur->list_base;
  a->active = cur->active;
}

static void
st_pop_attrib (void)
{
  if (cur->attrib_depth <= 0)
    return;
  struct saved_attrib *a = &cur->attrib_stack[--cur->attrib_depth];
  if (a->mask & GL_TRANSFORM_BIT)
    cur->matrix_mode = a->matrix_mode;
  if (a->mask & GL_VIEWPORT_BIT)
    memcpy (cur->viewport, a->viewport, sizeof a->viewport);
  if (a->mask & GL_LIST_BIT)
    cur->list_base = a->list_base;
  if (a->mask & GL_TEXTURE_BIT)
    cur->active = a->active;
}

/* --- display lists: what each one changes of the kept state ------------- */

static struct list *
find_list (GLuint name, int make)
{
  for (size_t i = 0; i < cur->nlists; i++)
    if (cur->lists[i].name == name)
      return &cur->lists[i];
  if (!make)
    return 0;
  if (cur->nlists == cur->caplists)
    {
      size_t cap = cur->caplists ? cur->caplists * 2 : 64;
      struct list *l = realloc (cur->lists, cap * sizeof *l);
      if (!l)
	return 0;
      cur->lists = l;
      cur->caplists = cap;
    }
  struct list *l = &cur->lists[cur->nlists++];
  memset (l, 0, sizeof *l);
  l->name = name;
  return l;
}

static void
drop_list (GLuint name)
{
  for (size_t i = 0; i < cur->nlists; i++)
    if (cur->lists[i].name == name)
      {
	free (cur->lists[i].w);
	cur->lists[i] = cur->lists[--cur->nlists];
	return;
      }
}

/* records a state change in the list being made: whether to make it now
   too (GL_COMPILE_AND_EXECUTE, or no list) */
static int
record (uint32_t op, const uint32_t *args, size_t n)
{
  if (cur->replaying || !cur->compiling)
    return !cur->in_begin;
  struct list *l = cur->recording;
  if (l && l->n + n + 2 > l->cap)
    {
      size_t cap = (l->cap + n + 2) * 2 + 32;
      uint32_t *w = realloc (l->w, cap * 4);
      if (!w)
	return cur->compile_mode == GL_COMPILE_AND_EXECUTE;
      l->w = w;
      l->cap = cap;
    }
  if (l)
    {
      l->w[l->n] = op;
      l->w[l->n + 1] = (uint32_t) n;
      memcpy (l->w + l->n + 2, args, n * 4);
      l->n += n + 2;
    }
  return cur->compile_mode == GL_COMPILE_AND_EXECUTE;
}

static void replay (GLuint name, int depth);

static void
replay_words (const uint32_t *w, size_t n, int depth)
{
  for (size_t i = 0; i + 2 <= n; i += 2 + w[i + 1])
    {
      const uint32_t *a = w + i + 2;
      GLfloat f[16];
      for (uint32_t k = 0; k < w[i + 1] && k < 16; k++)
	f[k] = r3d_u2f (a[k]);
      switch (w[i])
	{
	case OP_glMatrixMode: st_matrix_mode (a[0]); break;
	case OP_glLoadIdentity: st_load (identity); break;
	case OP_glLoadMatrixf: st_load (f); break;
	case OP_glMultMatrixf: st_mult (f); break;
	case OP_glPushMatrix: st_push (); break;
	case OP_glPopMatrix: st_pop (); break;
	case OP_glTranslatef: st_translate (f[0], f[1], f[2]); break;
	case OP_glScalef: st_scale (f[0], f[1], f[2]); break;
	case OP_glRotatef: st_rotate (f[0], f[1], f[2], f[3]); break;
	case OP_glOrtho: st_ortho (f[0], f[1], f[2], f[3], f[4], f[5]); break;
	case OP_glFrustum: st_frustum (f[0], f[1], f[2], f[3], f[4], f[5]); break;
	case OP_glActiveTexture: st_active (a[0]); break;
	case OP_glViewport: st_viewport ((GLint) a[0], (GLint) a[1], (GLsizei) a[2], (GLsizei) a[3]); break;
	case OP_glListBase: cur->list_base = a[0]; break;
	case OP_glPushAttrib: st_push_attrib (a[0]); break;
	case OP_glPopAttrib: st_pop_attrib (); break;
	case OP_glCallList: replay (a[0], depth + 1); break;
	case OP_glCallLists:
	  /* the names, less the list base (which is the one at the call) */
	  for (uint32_t k = 0; k < w[i + 1]; k++)
	    replay (cur->list_base + a[k], depth + 1);
	  break;
	}
    }
}

static void
replay (GLuint name, int depth)
{
  if (depth >= MAX_LIST_NESTING)
    return;
  struct list *l = find_list (name, 0);
  if (!l || !l->n)
    return;
  /* (a list can call itself: copy, as its record may move) */
  size_t n = l->n;
  uint32_t *w = malloc (n * 4);
  if (!w)
    return;
  memcpy (w, l->w, n * 4);
  int was = cur->replaying;
  cur->replaying = 1;
  replay_words (w, n, depth);
  cur->replaying = was;
  free (w);
}

/* --- what the library has to remember ---------------------------------- */

static void
used_name (GLuint *next, GLuint name)
{
  if (name >= *next && name < 0x7fff0000u)
    *next = name + 1;
}

static void
r3d_track_glBindBuffer (GLenum target, GLuint buffer)
{
  used_name (&cur->next_buffer, buffer);
  switch (target)
    {
    case GL_ARRAY_BUFFER: cur->array_buffer = buffer; break;
    case GL_ELEMENT_ARRAY_BUFFER: cur->element_buffer = buffer; break;
    case GL_PIXEL_PACK_BUFFER: cur->pack_buffer = buffer; break;
    case GL_PIXEL_UNPACK_BUFFER: cur->unpack_buffer = buffer; break;
    }
}

static void
r3d_track_glBindTexture (GLenum target, GLuint texture)
{
  (void) target;
  used_name (&cur->next_texture, texture);
}

static void
r3d_track_glDeleteBuffers (GLsizei n, const GLuint *buffers)
{
  for (GLsizei i = 0; buffers && i < n; i++)
    {
      if (buffers[i] == cur->array_buffer)
	cur->array_buffer = 0;
      if (buffers[i] == cur->element_buffer)
	cur->element_buffer = 0;
      if (buffers[i] == cur->pack_buffer)
	cur->pack_buffer = 0;
      if (buffers[i] == cur->unpack_buffer)
	cur->unpack_buffer = 0;
    }
}

static void
r3d_track_glEnableVertexAttribArray (GLuint index)
{
  if (index < MAX_ATTRIBS)
    cur->attribs[index].enabled = 1;
}

static void
r3d_track_glDisableVertexAttribArray (GLuint index)
{
  if (index < MAX_ATTRIBS)
    cur->attribs[index].enabled = 0;
}

static void
r3d_track_glPixelStorei (GLenum pname, GLint param)
{
  struct pixelstore *p = pname >= GL_PACK_SWAP_BYTES && pname <= GL_PACK_ALIGNMENT ? &cur->pack
			 : pname == GL_PACK_IMAGE_HEIGHT || pname == GL_PACK_SKIP_IMAGES ? &cur->pack : &cur->unpack;
  switch (pname)
    {
    case GL_PACK_SWAP_BYTES: case GL_UNPACK_SWAP_BYTES: p->swap = param; break;
    case GL_PACK_LSB_FIRST: case GL_UNPACK_LSB_FIRST: p->lsb = param; break;
    case GL_PACK_ROW_LENGTH: case GL_UNPACK_ROW_LENGTH: if (param >= 0) p->row_length = param; break;
    case GL_PACK_SKIP_ROWS: case GL_UNPACK_SKIP_ROWS: if (param >= 0) p->skip_rows = param; break;
    case GL_PACK_SKIP_PIXELS: case GL_UNPACK_SKIP_PIXELS: if (param >= 0) p->skip_pixels = param; break;
    case GL_PACK_ALIGNMENT: case GL_UNPACK_ALIGNMENT:
      if (param == 1 || param == 2 || param == 4 || param == 8)
	p->alignment = param;
      break;
    case GL_PACK_IMAGE_HEIGHT: case GL_UNPACK_IMAGE_HEIGHT: if (param >= 0) p->image_height = param; break;
    case GL_PACK_SKIP_IMAGES: case GL_UNPACK_SKIP_IMAGES: if (param >= 0) p->skip_images = param; break;
    }
}

static void
r3d_track_glPixelStoref (GLenum pname, GLfloat param)
{
  r3d_track_glPixelStorei (pname, (GLint) lrintf (param));
}

/* the state the kept functions change: recorded in a list being made,
   and done here unless only recorded */
#define REC(op, ...) do { uint32_t r_[] = { 0, __VA_ARGS__ }; if (!record (op, r_ + 1, sizeof r_ / 4 - 1)) return; } while (0)
#define REC0(op) do { if (!record (op, 0, 0)) return; } while (0)

static void r3d_track_glMatrixMode (GLenum mode) { if (!cur->desktop) return; REC (OP_glMatrixMode, mode); st_matrix_mode (mode); }
static void r3d_track_glLoadIdentity (void) { if (!cur->desktop) return; REC0 (OP_glLoadIdentity); st_load (identity); }
static void r3d_track_glPushMatrix (void) { if (!cur->desktop) return; REC0 (OP_glPushMatrix); st_push (); }
static void r3d_track_glPopMatrix (void) { if (!cur->desktop) return; REC0 (OP_glPopMatrix); st_pop (); }

static void
load_or_mult (int mult, const GLfloat *m)
{
  uint32_t w[16];
  for (int i = 0; i < 16; i++)
    w[i] = r3d_f2u (m[i]);
  if (!record (mult ? OP_glMultMatrixf : OP_glLoadMatrixf, w, 16))
    return;
  if (mult)
    st_mult (m);
  else
    st_load (m);
}

static void
load_or_mult_d (int mult, int transpose, const void *src, int dbl)
{
  GLfloat m[16];
  if (!cur->desktop || !src)
    return;
  for (int i = 0; i < 16; i++)
    {
      int k = transpose ? (i % 4) * 4 + i / 4 : i;
      m[i] = dbl ? (GLfloat) ((const GLdouble *) src)[k] : ((const GLfloat *) src)[k];
    }
  load_or_mult (mult, m);
}

static void r3d_track_glLoadMatrixf (const GLfloat *m) { load_or_mult_d (0, 0, m, 0); }
static void r3d_track_glLoadMatrixd (const GLdouble *m) { load_or_mult_d (0, 0, m, 1); }
static void r3d_track_glMultMatrixf (const GLfloat *m) { load_or_mult_d (1, 0, m, 0); }
static void r3d_track_glMultMatrixd (const GLdouble *m) { load_or_mult_d (1, 0, m, 1); }
static void r3d_track_glLoadTransposeMatrixf (const GLfloat *m) { load_or_mult_d (0, 1, m, 0); }
static void r3d_track_glLoadTransposeMatrixd (const GLdouble *m) { load_or_mult_d (0, 1, m, 1); }
static void r3d_track_glMultTransposeMatrixf (const GLfloat *m) { load_or_mult_d (1, 1, m, 0); }
static void r3d_track_glMultTransposeMatrixd (const GLdouble *m) { load_or_mult_d (1, 1, m, 1); }

static void
r3d_track_glTranslatef (GLfloat x, GLfloat y, GLfloat z)
{
  if (!cur->desktop)
    return;
  REC (OP_glTranslatef, r3d_f2u (x), r3d_f2u (y), r3d_f2u (z));
  st_translate (x, y, z);
}

static void r3d_track_glTranslated (GLdouble x, GLdouble y, GLdouble z) { r3d_track_glTranslatef ((GLfloat) x, (GLfloat) y, (GLfloat) z); }

static void
r3d_track_glScalef (GLfloat x, GLfloat y, GLfloat z)
{
  if (!cur->desktop)
    return;
  REC (OP_glScalef, r3d_f2u (x), r3d_f2u (y), r3d_f2u (z));
  st_scale (x, y, z);
}

static void r3d_track_glScaled (GLdouble x, GLdouble y, GLdouble z) { r3d_track_glScalef ((GLfloat) x, (GLfloat) y, (GLfloat) z); }

static void
r3d_track_glRotatef (GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
  if (!cur->desktop)
    return;
  REC (OP_glRotatef, r3d_f2u (angle), r3d_f2u (x), r3d_f2u (y), r3d_f2u (z));
  st_rotate (angle, x, y, z);
}

static void r3d_track_glRotated (GLdouble a, GLdouble x, GLdouble y, GLdouble z) { r3d_track_glRotatef ((GLfloat) a, (GLfloat) x, (GLfloat) y, (GLfloat) z); }

static void
r3d_track_glOrtho (GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
  if (!cur->desktop)
    return;
  REC (OP_glOrtho, r3d_f2u ((GLfloat) l), r3d_f2u ((GLfloat) r), r3d_f2u ((GLfloat) b),
       r3d_f2u ((GLfloat) t), r3d_f2u ((GLfloat) n), r3d_f2u ((GLfloat) f));
  st_ortho ((GLfloat) l, (GLfloat) r, (GLfloat) b, (GLfloat) t, (GLfloat) n, (GLfloat) f);
}

static void
r3d_track_glFrustum (GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
  if (!cur->desktop)
    return;
  REC (OP_glFrustum, r3d_f2u ((GLfloat) l), r3d_f2u ((GLfloat) r), r3d_f2u ((GLfloat) b),
       r3d_f2u ((GLfloat) t), r3d_f2u ((GLfloat) n), r3d_f2u ((GLfloat) f));
  st_frustum ((GLfloat) l, (GLfloat) r, (GLfloat) b, (GLfloat) t, (GLfloat) n, (GLfloat) f);
}

static void r3d_track_glActiveTexture (GLenum texture) { REC (OP_glActiveTexture, texture); st_active (texture); }

static void
r3d_track_glViewport (GLint x, GLint y, GLsizei w, GLsizei h)
{
  REC (OP_glViewport, (uint32_t) x, (uint32_t) y, (uint32_t) w, (uint32_t) h);
  st_viewport (x, y, w, h);
}

static void r3d_track_glListBase (GLuint base) { if (!cur->desktop) return; REC (OP_glListBase, base); cur->list_base = base; }
static void r3d_track_glPushAttrib (GLbitfield mask) { if (!cur->desktop) return; REC (OP_glPushAttrib, mask); st_push_attrib (mask); }
static void r3d_track_glPopAttrib (void) { if (!cur->desktop) return; REC0 (OP_glPopAttrib); st_pop_attrib (); }

static void
r3d_track_glCallList (GLuint list)
{
  if (!cur->desktop)
    return;
  REC (OP_glCallList, list);
  replay (list, 0);
}

static void
r3d_track_glNewList (GLuint list, GLenum mode)
{
  if (!cur->desktop || cur->compiling || !list || (mode != GL_COMPILE && mode != GL_COMPILE_AND_EXECUTE))
    return;
  used_name (&cur->next_list, list);
  drop_list (list);
  cur->compiling = list;
  cur->compile_mode = mode;
  cur->recording = find_list (list, 1);
}

static void
r3d_track_glEndList (void)
{
  cur->compiling = 0;
  cur->recording = 0;
}

static void
r3d_track_glDeleteLists (GLuint list, GLsizei range)
{
  if (range <= 0)
    return;
  for (size_t i = cur->nlists; i-- > 0; )
    if (cur->lists[i].name >= list && cur->lists[i].name - list < (GLuint) range)
      drop_list (cur->lists[i].name);
}

static void
r3d_track_glBegin (GLenum mode)
{
  (void) mode;
  if (!cur->compiling || cur->compile_mode == GL_COMPILE_AND_EXECUTE)
    cur->in_begin = 1;
}

static void
r3d_track_glEnd (void)
{
  if (!cur->compiling || cur->compile_mode == GL_COMPILE_AND_EXECUTE)
    cur->in_begin = 0;
}

/* how many values a glGet...v, glLight...v, glMaterial...v (and the
   like) with this name takes or gives */
static long
r3d_count (GLenum pname)
{
  switch (pname)
    {
    case GL_MODELVIEW_MATRIX: case GL_PROJECTION_MATRIX: case GL_TEXTURE_MATRIX:
    case GL_TRANSPOSE_MODELVIEW_MATRIX: case GL_TRANSPOSE_PROJECTION_MATRIX:
    case GL_TRANSPOSE_TEXTURE_MATRIX:
      return 16;
    case GL_VIEWPORT: case GL_SCISSOR_BOX: case GL_COLOR_CLEAR_VALUE:
    case GL_COLOR_WRITEMASK: case GL_BLEND_COLOR: case GL_CURRENT_VERTEX_ATTRIB:
    case GL_CURRENT_COLOR: case GL_CURRENT_TEXTURE_COORDS: case GL_CURRENT_RASTER_COLOR:
    case GL_CURRENT_RASTER_POSITION: case GL_CURRENT_RASTER_TEXTURE_COORDS:
    case GL_CURRENT_SECONDARY_COLOR: case GL_FOG_COLOR: case GL_LIGHT_MODEL_AMBIENT:
    case GL_ACCUM_CLEAR_VALUE: case GL_TEXTURE_ENV_COLOR: case GL_TEXTURE_BORDER_COLOR:
    case GL_MAP2_GRID_DOMAIN:
    case GL_AMBIENT: case GL_DIFFUSE: case GL_SPECULAR: case GL_POSITION:
    case GL_EMISSION: case GL_AMBIENT_AND_DIFFUSE:
    case GL_OBJECT_PLANE: case GL_EYE_PLANE:
      return 4;
    case GL_CURRENT_NORMAL: case GL_POINT_DISTANCE_ATTENUATION: case GL_SPOT_DIRECTION:
    case GL_COLOR_INDEXES:
      return 3;
    case GL_DEPTH_RANGE: case GL_ALIASED_POINT_SIZE_RANGE: case GL_ALIASED_LINE_WIDTH_RANGE:
    case GL_MAX_VIEWPORT_DIMS: case GL_POLYGON_MODE: case GL_POINT_SIZE_RANGE:
    case GL_LINE_WIDTH_RANGE: case GL_MAP1_GRID_DOMAIN: case GL_MAP2_GRID_SEGMENTS:
      return 2;
    }
  return 1;
}

static size_t
type_bytes (GLenum type)
{
  switch (type)
    {
    case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
    case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
    case GL_DOUBLE: return 8;
    }
  return 4;
}

/* --- pixels ----------------------------------------------------------- */

static int
components (GLenum format)
{
  switch (format)
    {
    case GL_RGBA: case GL_BGRA: return 4;
    case GL_RGB: case GL_BGR: return 3;
    case GL_LUMINANCE_ALPHA: return 2;
    }
  return 1;
}

/* bytes in a pixel, and in its element (for row alignment) */
static size_t
pixel_bytes (GLenum format, GLenum type, size_t *elem)
{
  size_t e;
  switch (type)
    {
    case GL_UNSIGNED_BYTE_3_3_2: case GL_UNSIGNED_BYTE_2_3_3_REV:
      *elem = 1;
      return 1;
    case GL_UNSIGNED_SHORT_5_6_5: case GL_UNSIGNED_SHORT_5_6_5_REV:
    case GL_UNSIGNED_SHORT_4_4_4_4: case GL_UNSIGNED_SHORT_4_4_4_4_REV:
    case GL_UNSIGNED_SHORT_5_5_5_1: case GL_UNSIGNED_SHORT_1_5_5_5_REV:
      *elem = 2;
      return 2;
    case GL_UNSIGNED_INT_8_8_8_8: case GL_UNSIGNED_INT_8_8_8_8_REV:
    case GL_UNSIGNED_INT_10_10_10_2: case GL_UNSIGNED_INT_2_10_10_10_REV:
    case GL_UNSIGNED_INT_24_8:
      *elem = 4;
      return 4;
    case 0x140B:			/* GL_HALF_FLOAT */
      e = 2;
      break;
    default:
      e = type_bytes (type);
      break;
    }
  *elem = e;
  return e * (size_t) components (format);
}

struct extent
{
  size_t start, row, image, pixel, total;
};

/* where an image of w x h x d lies in the program's memory under these
   pixel store settings (images: 1 for 1D and 2D) */
static struct extent
image_extent (const struct pixelstore *ps, GLsizei w, GLsizei h, GLsizei d, GLenum format, GLenum type, int three)
{
  struct extent x = { 0, 0, 0, 0, 0 };
  if (w <= 0 || h <= 0 || d <= 0)
    return x;
  size_t align = (size_t) (ps->alignment ? ps->alignment : 4);
  size_t row_len = ps->row_length > 0 ? (size_t) ps->row_length : (size_t) w;
  if (type == GL_BITMAP)
    {
      size_t rb = ((row_len + 7) / 8 + align - 1) / align * align;
      x.row = rb;
      x.image = rb * (size_t) h;
      x.start = (size_t) ps->skip_rows * rb + (size_t) ps->skip_pixels / 8;
      x.total = x.start + (size_t) (h - 1) * rb + ((size_t) ps->skip_pixels % 8 + (size_t) w + 7) / 8;
      return x;
    }
  size_t elem, px = pixel_bytes (format, type, &elem);
  size_t rb = row_len * px;
  if (elem < align)
    rb = (rb + align - 1) / align * align;
  size_t img_h = three && ps->image_height > 0 ? (size_t) ps->image_height : (size_t) h;
  x.pixel = px;
  x.row = rb;
  x.image = rb * img_h;
  x.start = (three ? (size_t) ps->skip_images * x.image : 0) + (size_t) ps->skip_rows * rb + (size_t) ps->skip_pixels * px;
  x.total = x.start + (size_t) (d - 1) * x.image + (size_t) (h - 1) * rb + (size_t) w * px;
  return x;
}

/* an image the program gives: from a pixel unpack buffer (an offset), or
   its memory. f: the fixed words, which end with three for it */
static void
put_image (uint32_t op, uint32_t *f, size_t nf, const void *pixels, size_t bytes)
{
  if (cur->unpack_buffer)
    {
      f[nf - 3] = 1;
      f[nf - 2] = (uint32_t) (uintptr_t) pixels;
      f[nf - 1] = 0;
      r3d_put_data (op, f, nf, 0, 0);
      return;
    }
  f[nf - 3] = 0;
  f[nf - 2] = 0;
  f[nf - 1] = pixels ? (uint32_t) bytes : 0;
  r3d_put_data (op, f, nf, pixels, pixels ? bytes : 0);
}

static size_t
unpack_bytes (GLsizei w, GLsizei h, GLsizei d, GLenum format, GLenum type, int three)
{
  return image_extent (&cur->unpack, w, h, d, format, type, three).total;
}

/* tight pixels from the renderer, laid out under the pack settings */
static void
scatter (void *dst, const char *src, GLsizei w, GLsizei h, GLsizei d, GLenum format, GLenum type, int three)
{
  struct extent x = image_extent (&cur->pack, w, h, d, format, type, three);
  size_t line = (size_t) w * x.pixel;
  if (!dst || !x.total || !line)
    return;
  for (GLsizei z = 0; z < d; z++)
    for (GLsizei y = 0; y < h; y++)
      memcpy ((char *) dst + x.start + (size_t) z * x.image + (size_t) y * x.row,
	      src + ((size_t) z * (size_t) h + (size_t) y) * line, line);
}

void
glTexImage1D (GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const void *pixels)
{
  uint32_t f[10] = { target, (uint32_t) level, (uint32_t) internalformat, (uint32_t) width, (uint32_t) border, format, type };
  put_image (OP_glTexImage1D, f, 10, pixels, unpack_bytes (width, 1, 1, format, type, 0));
}

void
glTexImage2D (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels)
{
  uint32_t f[11] = { target, (uint32_t) level, (uint32_t) internalformat, (uint32_t) width,
		     (uint32_t) height, (uint32_t) border, format, type };
  put_image (OP_glTexImage2D, f, 11, pixels, unpack_bytes (width, height, 1, format, type, 0));
}

void
glTexImage3D (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void *pixels)
{
  uint32_t f[12] = { target, (uint32_t) level, (uint32_t) internalformat, (uint32_t) width,
		     (uint32_t) height, (uint32_t) depth, (uint32_t) border, format, type };
  put_image (OP_glTexImage3D, f, 12, pixels, unpack_bytes (width, height, depth, format, type, 1));
}

void
glTexSubImage1D (GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const void *pixels)
{
  uint32_t f[9] = { target, (uint32_t) level, (uint32_t) xoffset, (uint32_t) width, format, type };
  put_image (OP_glTexSubImage1D, f, 9, pixels, unpack_bytes (width, 1, 1, format, type, 0));
}

void
glTexSubImage2D (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels)
{
  uint32_t f[11] = { target, (uint32_t) level, (uint32_t) xoffset, (uint32_t) yoffset,
		     (uint32_t) width, (uint32_t) height, format, type };
  put_image (OP_glTexSubImage2D, f, 11, pixels, unpack_bytes (width, height, 1, format, type, 0));
}

void
glTexSubImage3D (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels)
{
  uint32_t f[13] = { target, (uint32_t) level, (uint32_t) xoffset, (uint32_t) yoffset, (uint32_t) zoffset,
		     (uint32_t) width, (uint32_t) height, (uint32_t) depth, format, type };
  put_image (OP_glTexSubImage3D, f, 13, pixels, unpack_bytes (width, height, depth, format, type, 1));
}

void
glCompressedTexImage1D (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const void *data)
{
  uint32_t f[9] = { target, (uint32_t) level, internalformat, (uint32_t) width, (uint32_t) border, (uint32_t) imageSize };
  put_image (OP_glCompressedTexImage1D, f, 9, data, (size_t) r3d_nonneg (imageSize));
}

void
glCompressedTexImage2D (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data)
{
  uint32_t f[10] = { target, (uint32_t) level, internalformat, (uint32_t) width,
		     (uint32_t) height, (uint32_t) border, (uint32_t) imageSize };
  put_image (OP_glCompressedTexImage2D, f, 10, data, (size_t) r3d_nonneg (imageSize));
}

void
glCompressedTexImage3D (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void *data)
{
  uint32_t f[11] = { target, (uint32_t) level, internalformat, (uint32_t) width, (uint32_t) height,
		     (uint32_t) depth, (uint32_t) border, (uint32_t) imageSize };
  put_image (OP_glCompressedTexImage3D, f, 11, data, (size_t) r3d_nonneg (imageSize));
}

void
glCompressedTexSubImage1D (GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void *data)
{
  uint32_t f[9] = { target, (uint32_t) level, (uint32_t) xoffset, (uint32_t) width, format, (uint32_t) imageSize };
  put_image (OP_glCompressedTexSubImage1D, f, 9, data, (size_t) r3d_nonneg (imageSize));
}

void
glCompressedTexSubImage2D (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data)
{
  uint32_t f[11] = { target, (uint32_t) level, (uint32_t) xoffset, (uint32_t) yoffset,
		     (uint32_t) width, (uint32_t) height, format, (uint32_t) imageSize };
  put_image (OP_glCompressedTexSubImage2D, f, 11, data, (size_t) r3d_nonneg (imageSize));
}

void
glCompressedTexSubImage3D (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *data)
{
  uint32_t f[13] = { target, (uint32_t) level, (uint32_t) xoffset, (uint32_t) yoffset, (uint32_t) zoffset,
		     (uint32_t) width, (uint32_t) height, (uint32_t) depth, format, (uint32_t) imageSize };
  put_image (OP_glCompressedTexSubImage3D, f, 13, data, (size_t) r3d_nonneg (imageSize));
}

void
glDrawPixels (GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels)
{
  uint32_t f[7] = { (uint32_t) width, (uint32_t) height, format, type };
  put_image (OP_glDrawPixels, f, 7, pixels, unpack_bytes (width, height, 1, format, type, 0));
}

void
glBitmap (GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap)
{
  uint32_t f[9] = { (uint32_t) width, (uint32_t) height, r3d_f2u (xorig), r3d_f2u (yorig), r3d_f2u (xmove), r3d_f2u (ymove) };
  put_image (OP_glBitmap, f, 9, bitmap, unpack_bytes (width, height, 1, GL_COLOR_INDEX, GL_BITMAP, 0));
}

void
glPolygonStipple (const GLubyte *mask)
{
  uint32_t f[3];
  put_image (OP_glPolygonStipple, f, 3, mask, unpack_bytes (32, 32, 1, GL_COLOR_INDEX, GL_BITMAP, 0));
}

void
glReadPixels (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels)
{
  uint32_t *a = r3d_put (OP_glReadPixels, 8);
  if (!a)
    return;
  a[0] = (uint32_t) x;
  a[1] = (uint32_t) y;
  a[2] = (uint32_t) width;
  a[3] = (uint32_t) height;
  a[4] = format;
  a[5] = type;
  a[6] = cur->pack_buffer != 0;
  a[7] = (uint32_t) (uintptr_t) pixels;
  if (cur->pack_buffer)
    return;
  if (r3d_sync (0, 0) < 0)
    return;
  scatter (pixels, reply_area (), width, height, 1, format, type, 0);
}

void
glGetTexImage (GLenum target, GLint level, GLenum format, GLenum type, void *pixels)
{
  uint32_t *a = r3d_put (OP_glGetTexImage, 6);
  if (!a)
    return;
  a[0] = target;
  a[1] = (uint32_t) level;
  a[2] = format;
  a[3] = type;
  a[4] = cur->pack_buffer != 0;
  a[5] = (uint32_t) (uintptr_t) pixels;
  if (cur->pack_buffer || r3d_sync (0, 0) <= 0)
    return;
  const GLint *wh = reply_area ();
  scatter (pixels, (const char *) (wh + 3), wh[0], wh[1], wh[2], format, type, target == GL_TEXTURE_3D);
}

void
glGetCompressedTexImage (GLenum target, GLint level, void *img)
{
  uint32_t *a = r3d_put (OP_glGetCompressedTexImage, 4);
  if (!a)
    return;
  a[0] = target;
  a[1] = (uint32_t) level;
  a[2] = cur->pack_buffer != 0;
  a[3] = (uint32_t) (uintptr_t) img;
  if (cur->pack_buffer || r3d_sync (0, 0) < 4)
    return;
  const uint32_t *r = reply_area ();
  if (img)
    memcpy (img, r + 1, r[0]);
}

void
glGetPolygonStipple (GLubyte *mask)
{
  uint32_t *a = r3d_put (OP_glGetPolygonStipple, 2);
  if (!a)
    return;
  a[0] = cur->pack_buffer != 0;
  a[1] = (uint32_t) (uintptr_t) mask;
  if (!cur->pack_buffer)
    r3d_sync (mask, 128);
}

/* --- strings ----------------------------------------------------------- */

static void
put_named (uint32_t op, GLuint a0, GLuint a1, int two, const GLchar *name)
{
  uint32_t f[3];
  size_t n = 0, l = strlen (name);
  f[n++] = a0;
  if (two)
    f[n++] = a1;
  f[n++] = (uint32_t) l;
  r3d_put_data (op, f, n, name, l);
}

void
glBindAttribLocation (GLuint program, GLuint index, const GLchar *name)
{
  put_named (OP_glBindAttribLocation, program, index, 1, name);
}

GLint
glGetAttribLocation (GLuint program, const GLchar *name)
{
  int32_t r = -1;
  put_named (OP_glGetAttribLocation, program, 0, 0, name);
  r3d_sync (&r, 4);
  return r;
}

GLint
glGetUniformLocation (GLuint program, const GLchar *name)
{
  int32_t r = -1;
  put_named (OP_glGetUniformLocation, program, 0, 0, name);
  r3d_sync (&r, 4);
  return r;
}

void
glShaderSource (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)
{
  size_t total = 0;
  for (GLsizei i = 0; i < count; i++)
    total += length && length[i] >= 0 ? (size_t) length[i] : strlen (string[i]);
  char *all = malloc (total + 1);
  if (!all)
    return;
  size_t at = 0;
  for (GLsizei i = 0; i < count; i++)
    {
      size_t l = length && length[i] >= 0 ? (size_t) length[i] : strlen (string[i]);
      memcpy (all + at, string[i], l);
      at += l;
    }
  uint32_t f[2] = { shader, (uint32_t) total };
  r3d_put_data (OP_glShaderSource, f, 2, all, total);
  free (all);
}

/* an answer of a length word and then text */
static void
get_text (uint32_t op, GLuint obj, GLsizei bufSize, GLsizei *length, GLchar *text)
{
  if (length)
    *length = 0;
  if (bufSize <= 0)
    return;
  uint32_t *a = r3d_put (op, 2);
  uint32_t *r = malloc ((size_t) bufSize + 8);
  if (!a || !r)
    {
      free (r);
      return;
    }
  a[0] = obj;
  a[1] = (uint32_t) bufSize;
  r[0] = 0;
  r3d_sync (r, (size_t) bufSize + 4);
  size_t l = r[0] < (uint32_t) bufSize ? r[0] : (uint32_t) bufSize - 1;
  if (text)
    {
      memcpy (text, r + 1, l);
      text[l] = 0;
    }
  if (length)
    *length = (GLsizei) l;
  free (r);
}

void
glGetShaderInfoLog (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
  get_text (OP_glGetShaderInfoLog, shader, bufSize, length, infoLog);
}

void
glGetProgramInfoLog (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
  get_text (OP_glGetProgramInfoLog, program, bufSize, length, infoLog);
}

void
glGetShaderSource (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source)
{
  get_text (OP_glGetShaderSource, shader, bufSize, length, source);
}

static void
get_active (uint32_t op, GLuint program, GLuint index, GLsizei bufSize, GLsizei *length,
	    GLint *size, GLenum *type, GLchar *name)
{
  uint32_t r[3 + 64];
  uint32_t *a = r3d_put (op, 3);
  if (!a)
    return;
  memset (r, 0, sizeof r);
  a[0] = program;
  a[1] = index;
  a[2] = 255;
  r3d_sync (r, sizeof r);
  size_t l = r[0] < 255 ? r[0] : 255;
  if (bufSize > 0 && l >= (size_t) bufSize)
    l = (size_t) bufSize - 1;
  if (size)
    *size = (GLint) r[1];
  if (type)
    *type = r[2];
  if (name && bufSize > 0)
    {
      memcpy (name, r + 3, l);
      name[l] = 0;
    }
  if (length)
    *length = (GLsizei) l;
}

void
glGetActiveAttrib (GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name)
{
  get_active (OP_glGetActiveAttrib, program, index, bufSize, length, size, type, name);
}

void
glGetActiveUniform (GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name)
{
  get_active (OP_glGetActiveUniform, program, index, bufSize, length, size, type, name);
}

void
glGetAttachedShaders (GLuint program, GLsizei maxCount, GLsizei *count, GLuint *shaders)
{
  uint32_t r[1 + 32];
  uint32_t *a = r3d_put (OP_glGetAttachedShaders, 2);
  if (!a)
    return;
  memset (r, 0, sizeof r);
  a[0] = program;
  a[1] = maxCount > 32 ? 32 : (uint32_t) maxCount;
  r3d_sync (r, sizeof r);
  if (count)
    *count = (GLsizei) r[0];
  if (shaders)
    memcpy (shaders, r + 1, r[0] * 4);
}

void
glGetShaderPrecisionFormat (GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision)
{
  uint32_t r[3] = { 0, 0, 0 };
  uint32_t *a = r3d_put (OP_glGetShaderPrecisionFormat, 2);
  if (!a)
    return;
  a[0] = shadertype;
  a[1] = precisiontype;
  r3d_sync (r, sizeof r);
  range[0] = (GLint) r[0];
  range[1] = (GLint) r[1];
  *precision = (GLint) r[2];
}

void
glShaderBinary (GLsizei count, const GLuint *shaders, GLenum binaryFormat, const void *binary, GLsizei length)
{
  (void) count; (void) shaders; (void) binaryFormat; (void) binary; (void) length;
}

#include "r3d_exts.h"

static int
ext_cmp (const void *k, const void *e)
{
  return strcmp ((const char *) k, *(const char *const *) e);
}

/* What the context is, not what Linux made: the stream carries OpenGL 2.1
   (compatibility) or GLES 2.0, whatever version the renderer's context
   has, so a program that saw 4.6 would call functions that aren't here.
   The renderer's own version is kept in brackets (its "Mesa x.y.z" part
   where it has one). GL_EXTENSIONS: only those whose functions the stream
   carries. */
static char *
own_string (int slot, const char *linux_s)
{
  size_t n = strlen (linux_s);
  char *s;
  if (slot == 2)
    {
      const char *m = strstr (linux_s, "Mesa ");
      const char *v = m ? m : linux_s;
      if (!m && !strncmp (v, "OpenGL ES ", 10))
	v += 10;
      s = malloc (strlen (v) + 32);
      if (s)
	sprintf (s, "%s RISC3D (%s)", cur->desktop ? "2.1" : "OpenGL ES 2.0", v);
      return s;
    }
  if (slot == 3)
    return strdup (cur->desktop ? "1.20" : "OpenGL ES GLSL ES 1.00");
  /* slot 4 */
  const char *const *list = cur->desktop ? r3d_exts_gl : r3d_exts_gles2;
  size_t count = cur->desktop ? sizeof r3d_exts_gl / sizeof *r3d_exts_gl
			      : sizeof r3d_exts_gles2 / sizeof *r3d_exts_gles2;
  s = malloc (n + 1);
  if (!s)
    return 0;
  char *o = s, word[128];
  for (const char *p = linux_s; *p;)
    {
      while (*p == ' ')
	p++;
      size_t l = strcspn (p, " ");
      if (l && l < sizeof word)
	{
	  memcpy (word, p, l);
	  word[l] = 0;
	  if (bsearch (word, list, count, sizeof *list, ext_cmp))
	    {
	      if (o != s)
		*o++ = ' ';
	      memcpy (o, word, l);
	      o += l;
	    }
	}
      p += l;
    }
  *o = 0;
  return s;
}

const GLubyte *
glGetString (GLenum name)
{
  int slot = name == GL_VENDOR ? 0 : name == GL_RENDERER ? 1 : name == GL_VERSION ? 2
	     : name == GL_SHADING_LANGUAGE_VERSION ? 3 : name == GL_EXTENSIONS ? 4 : 5;
  uint32_t *a = r3d_put (OP_glGetString, 1);
  if (!a)
    return 0;
  a[0] = name;
  int n = r3d_sync (0, 0);
  if (n <= 0)
    return 0;
  /* (the program may keep the pointer: one copy per name, kept) */
  char *s = malloc ((size_t) n + 1);
  if (!s)
    return 0;
  memcpy (s, reply_area (), (size_t) n);
  s[n] = 0;
  if (slot >= 2 && slot <= 4)
    {
      char *own = own_string (slot, s);
      free (s);
      if (!own)
	return 0;
      s = own;
    }
  if (cur->strings[slot] && !strcmp (cur->strings[slot], s))
    {
      free (s);
      return (const GLubyte *) cur->strings[slot];
    }
  /* the old copy is kept too: a program may still hold it */
  cur->strings[slot] = s;
  return (const GLubyte *) s;
}

/* --- gets: from the kept state where it can ---------------------------- */

/* 1 and the values (as doubles, n of them), if kept here; float: the
   state is floating point (integers are rounded from it) */
static int
kept (GLenum pname, double *v, int *n, int *is_float)
{
  struct stack *s = 0;
  int transpose = 0;
  *is_float = 0;
  *n = 1;
  switch (pname)
    {
    case GL_TRANSPOSE_MODELVIEW_MATRIX: transpose = 1; /* fall through */
    case GL_MODELVIEW_MATRIX: s = &cur->modelview; break;
    case GL_TRANSPOSE_PROJECTION_MATRIX: transpose = 1; /* fall through */
    case GL_PROJECTION_MATRIX: s = &cur->projection; break;
    case GL_TRANSPOSE_TEXTURE_MATRIX: transpose = 1; /* fall through */
    case GL_TEXTURE_MATRIX:
      if (cur->active >= cur->units)
	return 0;
      s = &cur->texture[cur->active];
      break;
    case GL_MODELVIEW_STACK_DEPTH: if (!cur->desktop) return 0; v[0] = cur->modelview.depth; return 1;
    case GL_PROJECTION_STACK_DEPTH: if (!cur->desktop) return 0; v[0] = cur->projection.depth; return 1;
    case GL_TEXTURE_STACK_DEPTH:
      if (!cur->desktop || cur->active >= cur->units)
	return 0;
      v[0] = cur->texture[cur->active].depth;
      return 1;
    case GL_MAX_MODELVIEW_STACK_DEPTH: if (!cur->desktop) return 0; v[0] = cur->modelview.max; return 1;
    case GL_MAX_PROJECTION_STACK_DEPTH: if (!cur->desktop) return 0; v[0] = cur->projection.max; return 1;
    case GL_MATRIX_MODE: if (!cur->desktop) return 0; v[0] = cur->matrix_mode; return 1;
    case GL_LIST_BASE: if (!cur->desktop) return 0; v[0] = cur->list_base; return 1;
    case GL_LIST_INDEX: if (!cur->desktop) return 0; v[0] = cur->compiling; return 1;
    case GL_LIST_MODE: if (!cur->desktop) return 0; v[0] = cur->compiling ? cur->compile_mode : 0; return 1;
    case GL_ACTIVE_TEXTURE: v[0] = GL_TEXTURE0 + cur->active; return 1;
    case GL_CLIENT_ACTIVE_TEXTURE: if (!cur->desktop) return 0; v[0] = GL_TEXTURE0 + cur->client_unit; return 1;
    case GL_ARRAY_BUFFER_BINDING: v[0] = cur->array_buffer; return 1;
    case GL_ELEMENT_ARRAY_BUFFER_BINDING: v[0] = cur->element_buffer; return 1;
    case GL_PIXEL_PACK_BUFFER_BINDING: if (!cur->desktop) return 0; v[0] = cur->pack_buffer; return 1;
    case GL_PIXEL_UNPACK_BUFFER_BINDING: if (!cur->desktop) return 0; v[0] = cur->unpack_buffer; return 1;
    case GL_PACK_ALIGNMENT: v[0] = cur->pack.alignment; return 1;
    case GL_UNPACK_ALIGNMENT: v[0] = cur->unpack.alignment; return 1;
    case GL_VIEWPORT:
      if (!cur->viewport_known)
	return 0;
      for (int i = 0; i < 4; i++)
	v[i] = cur->viewport[i];
      *n = 4;
      return 1;
    default:
      return 0;
    }
  if (!cur->desktop || !s->m)
    return 0;
  const GLfloat *m = s->m[s->depth - 1];
  for (int i = 0; i < 16; i++)
    v[i] = m[transpose ? (i % 4) * 4 + i / 4 : i];
  *n = 16;
  *is_float = 1;
  return 1;
}

/* the renderer's answer: up to 1024 words */
static const uint32_t *
asked (uint32_t op, GLenum pname)
{
  uint32_t *a = r3d_put (op, 1);
  if (!a)
    return 0;
  a[0] = pname;
  if (r3d_sync (0, 0) <= 0)
    return 0;
  return reply_area ();
}

static size_t
values_of (GLenum pname)
{
  GLenum count_name = pname == GL_COMPRESSED_TEXTURE_FORMATS ? GL_NUM_COMPRESSED_TEXTURE_FORMATS
		      : pname == GL_SHADER_BINARY_FORMATS ? GL_NUM_SHADER_BINARY_FORMATS : 0;
  if (count_name)
    {
      const uint32_t *r = asked (OP_glGetIntegerv, count_name);
      size_t n = r ? r[0] : 0;
      return n > 60 ? 60 : n;
    }
  return (size_t) r3d_count (pname);
}

void
glGetIntegerv (GLenum pname, GLint *data)
{
  double v[16];
  int n, f;
  if (kept (pname, v, &n, &f))
    {
      /* (as Mesa: matrices are scaled as colours are, not rounded) */
      for (int i = 0; i < n; i++)
	{
	  double x = f ? 2147483647.0 * v[i] : v[i];
	  data[i] = x >= 2147483647.0 ? 2147483647 : x <= -2147483648.0 ? (GLint) -2147483647 - 1 : (GLint) x;
	}
      return;
    }
  size_t c = values_of (pname);
  const uint32_t *r = asked (OP_glGetIntegerv, pname);
  if (r && data)
    memcpy (data, r, 4 * c);
  if (r && pname == GL_VIEWPORT)
    {
      memcpy (cur->viewport, r, sizeof cur->viewport);
      cur->viewport_known = 1;
    }
}

void
glGetFloatv (GLenum pname, GLfloat *data)
{
  double v[16];
  int n, f;
  if (kept (pname, v, &n, &f))
    {
      for (int i = 0; i < n; i++)
	data[i] = (GLfloat) v[i];
      return;
    }
  size_t c = values_of (pname);
  const uint32_t *r = asked (OP_glGetFloatv, pname);
  if (r && data)
    memcpy (data, r, 4 * c);
}

void
glGetDoublev (GLenum pname, GLdouble *data)
{
  double v[16];
  int n, f;
  if (kept (pname, v, &n, &f))
    {
      for (int i = 0; i < n; i++)
	data[i] = v[i];
      return;
    }
  size_t c = values_of (pname);
  const uint32_t *r = asked (OP_glGetDoublev, pname);
  if (r && data)
    memcpy (data, r, 8 * c);
}

void
glGetBooleanv (GLenum pname, GLboolean *data)
{
  double v[16];
  int n, f;
  if (kept (pname, v, &n, &f))
    {
      for (int i = 0; i < n; i++)
	data[i] = v[i] != 0.0;
      return;
    }
  size_t c = values_of (pname);
  const uint32_t *r = asked (OP_glGetBooleanv, pname);
  if (r && data)
    memcpy (data, r, c);
}

/* --- names: given here, at once ----------------------------------------- */

static void
gen_names (GLuint *next, GLsizei n, GLuint *names)
{
  for (GLsizei i = 0; names && i < n; i++)
    names[i] = (*next)++;
}

void glGenTextures (GLsizei n, GLuint *textures) { gen_names (&cur->next_texture, n, textures); }
void glGenBuffers (GLsizei n, GLuint *buffers) { gen_names (&cur->next_buffer, n, buffers); }

GLuint
glGenLists (GLsizei range)
{
  if (range <= 0 || cur->handle < 0)
    return 0;
  GLuint base = cur->next_list;
  cur->next_list += (GLuint) range;
  return base;
}

GLboolean
glAreTexturesResident (GLsizei n, const GLuint *textures, GLboolean *residences)
{
  (void) textures;
  for (GLsizei i = 0; residences && i < n; i++)
    residences[i] = GL_TRUE;
  return GL_TRUE;
}

/* --- display lists, called --------------------------------------------- */

static GLuint
list_value (GLenum type, const void *lists, GLsizei i)
{
  const unsigned char *b = lists;
  switch (type)
    {
    case GL_BYTE: return (GLuint) (GLint) ((const GLbyte *) lists)[i];
    case GL_UNSIGNED_BYTE: return b[i];
    case GL_SHORT: return (GLuint) (GLint) ((const GLshort *) lists)[i];
    case GL_UNSIGNED_SHORT: return ((const GLushort *) lists)[i];
    case GL_INT: return (GLuint) ((const GLint *) lists)[i];
    case GL_UNSIGNED_INT: return ((const GLuint *) lists)[i];
    case GL_FLOAT: return (GLuint) ((const GLfloat *) lists)[i];
    case GL_2_BYTES: return (GLuint) b[2 * i] * 256 + b[2 * i + 1];
    case GL_3_BYTES: return ((GLuint) b[3 * i] << 16) + ((GLuint) b[3 * i + 1] << 8) + b[3 * i + 2];
    case GL_4_BYTES: return ((GLuint) b[4 * i] << 24) + ((GLuint) b[4 * i + 1] << 16) + ((GLuint) b[4 * i + 2] << 8) + b[4 * i + 3];
    }
  return 0;
}

static size_t
list_bytes (GLenum type)
{
  switch (type)
    {
    case GL_2_BYTES: return 2;
    case GL_3_BYTES: return 3;
    case GL_4_BYTES: return 4;
    }
  return type_bytes (type);
}

void
glCallLists (GLsizei n, GLenum type, const void *lists)
{
  if (n <= 0 || !lists)
    return;
  size_t bytes = (size_t) n * list_bytes (type);
  uint32_t f[3] = { (uint32_t) n, type, (uint32_t) bytes };
  r3d_put_data (OP_glCallLists, f, 3, lists, bytes);
  if (!cur->desktop || (!cur->nlists && !cur->compiling))
    return;
  /* what the lists change here, and their record in a list being made */
  uint32_t *names = malloc ((size_t) n * 4);
  if (!names)
    return;
  for (GLsizei i = 0; i < n; i++)
    names[i] = list_value (type, lists, i);
  if (record (OP_glCallLists, names, (size_t) n))
    for (GLsizei i = 0; i < n; i++)
      replay (cur->list_base + names[i], 0);
  free (names);
}

/* --- evaluators: the control points packed, as the stride is the
   program's ------------------------------------------------------------ */

static int
map_k (GLenum target)
{
  static const int k[9] = { 4, 1, 3, 1, 2, 3, 4, 3, 4 };
  GLenum t = target >= GL_MAP2_COLOR_4 ? target - (GL_MAP2_COLOR_4 - GL_MAP1_COLOR_4) : target;
  return t >= GL_MAP1_COLOR_4 && t <= GL_MAP1_VERTEX_4 ? k[t - GL_MAP1_COLOR_4] : 0;
}

static void
put_map (uint32_t op, uint32_t *f, size_t nf, const void *points, int dbl, GLint ustride, GLint uorder,
	 GLint vstride, GLint vorder, int k, size_t ui, size_t vi)
{
  size_t es = dbl ? 8 : 4, n = (size_t) uorder * (size_t) vorder * (size_t) k;
  if (!k || uorder < 1 || vorder < 1 || ustride < k || vstride < k || !points)
    {
      f[ui + 1] = 0;			/* an order of 0: GL_INVALID_VALUE there */
      f[nf - 1] = 0;
      r3d_put_data (op, f, nf, 0, 0);
      return;
    }
  char *p = malloc (n * es);
  if (!p)
    return;
  for (GLint j = 0; j < vorder; j++)
    for (GLint i = 0; i < uorder; i++)
      memcpy (p + (((size_t) j * (size_t) uorder + (size_t) i) * (size_t) k) * es,
	      (const char *) points + ((size_t) i * (size_t) ustride + (size_t) j * (size_t) vstride) * es,
	      (size_t) k * es);
  f[ui] = (uint32_t) k;
  if (vi)
    f[vi] = (uint32_t) (uorder * k);
  f[nf - 1] = (uint32_t) (n * es);
  r3d_put_data (op, f, nf, p, n * es);
  free (p);
}

void
glMap1d (GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points)
{
  uint32_t f[8] = { target };
  r3d_d2w (f + 1, u1);
  r3d_d2w (f + 3, u2);
  f[6] = (uint32_t) order;
  put_map (OP_glMap1d, f, 8, points, 1, stride, order, map_k (target), 1, map_k (target), 5, 0);
}

void
glMap1f (GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points)
{
  uint32_t f[6] = { target, r3d_f2u (u1), r3d_f2u (u2), 0, (uint32_t) order };
  put_map (OP_glMap1f, f, 6, points, 0, stride, order, map_k (target), 1, map_k (target), 3, 0);
}

void
glMap2d (GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points)
{
  uint32_t f[14] = { target };
  r3d_d2w (f + 1, u1);
  r3d_d2w (f + 3, u2);
  f[6] = (uint32_t) uorder;
  r3d_d2w (f + 7, v1);
  r3d_d2w (f + 9, v2);
  f[12] = (uint32_t) vorder;
  put_map (OP_glMap2d, f, 14, points, 1, ustride, uorder, vstride, vorder, map_k (target), 5, 11);
}

void
glMap2f (GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points)
{
  uint32_t f[10] = { target, r3d_f2u (u1), r3d_f2u (u2), 0, (uint32_t) uorder, r3d_f2u (v1), r3d_f2u (v2), 0, (uint32_t) vorder };
  put_map (OP_glMap2f, f, 10, points, 0, ustride, uorder, vstride, vorder, map_k (target), 3, 7);
}

/* --- feedback and selection: the renderer keeps a buffer, and leaving
   the mode copies it into the program's ----------------------------------- */

void
glFeedbackBuffer (GLsizei size, GLenum type, GLfloat *buffer)
{
  uint32_t *a = r3d_put (OP_glFeedbackBuffer, 2);
  if (!a)
    return;
  a[0] = (uint32_t) size;
  a[1] = type;
  cur->feedback = buffer;
  cur->feedback_size = size;
}

void
glSelectBuffer (GLsizei size, GLuint *buffer)
{
  uint32_t *a = r3d_put (OP_glSelectBuffer, 1);
  if (!a)
    return;
  a[0] = (uint32_t) size;
  cur->select = buffer;
  cur->select_size = size;
}

GLint
glRenderMode (GLenum mode)
{
  uint32_t *a = r3d_put (OP_glRenderMode, 1);
  if (!a)
    return 0;
  a[0] = mode;
  if (r3d_sync (0, 0) < 12)
    return 0;
  const uint32_t *r = reply_area ();
  void *dst = r[1] == GL_FEEDBACK ? (void *) cur->feedback : r[1] == GL_SELECT ? (void *) cur->select : 0;
  GLsizei size = r[1] == GL_FEEDBACK ? cur->feedback_size : cur->select_size;
  if (dst && r[2])
    memcpy (dst, r + 3, 4 * (r[2] < (uint32_t) size ? r[2] : (uint32_t) size));
  return (GLint) r[0];
}

/* --- buffer objects ------------------------------------------------------ */

void
glBufferData (GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
  uint32_t f[4] = { target, (uint32_t) size, usage, data != 0 };
  r3d_put_data (OP_glBufferData, f, 4, data, data ? (size_t) size : 0);
}

void
glBufferSubData (GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
  uint32_t f[3] = { target, (uint32_t) offset, (uint32_t) size };
  r3d_put_data (OP_glBufferSubData, f, 3, data, (size_t) size);
}

void
glGetBufferSubData (GLenum target, GLintptr offset, GLsizeiptr size, void *data)
{
  uint32_t *a = r3d_put (OP_glGetBufferSubData, 3);
  if (!a)
    return;
  a[0] = target;
  a[1] = (uint32_t) offset;
  a[2] = (uint32_t) size;
  r3d_sync (data, (size_t) size);
}

/* mapping: a copy here, fetched unless only written, and sent back at
   unmapping unless only read */
static struct mapping *
mapping_of (GLenum target, int make)
{
  struct mapping *free_one = 0;
  for (int i = 0; i < 4; i++)
    {
      if (cur->maps[i].ptr && cur->maps[i].target == target)
	return &cur->maps[i];
      if (!cur->maps[i].ptr && !free_one)
	free_one = &cur->maps[i];
    }
  return make ? free_one : 0;
}

void *
glMapBuffer (GLenum target, GLenum access)
{
  struct mapping *m = mapping_of (target, 1);
  if (!m || mapping_of (target, 0) || cur->handle < 0)
    return 0;
  GLint size = 0;
  glGetBufferParameteriv (target, GL_BUFFER_SIZE, &size);
  if (size <= 0)
    return 0;
  void *p = malloc ((size_t) size);
  if (!p)
    return 0;
  if (access != GL_WRITE_ONLY)
    glGetBufferSubData (target, 0, size, p);
  m->target = target;
  m->ptr = p;
  m->size = size;
  m->access = access;
  return p;
}

GLboolean
glUnmapBuffer (GLenum target)
{
  struct mapping *m = mapping_of (target, 0);
  if (!m)
    return GL_FALSE;
  if (m->access != GL_READ_ONLY)
    glBufferSubData (target, 0, m->size, m->ptr);
  free (m->ptr);
  m->ptr = 0;
  return GL_TRUE;
}

void
glGetBufferPointerv (GLenum target, GLenum pname, void **params)
{
  struct mapping *m = mapping_of (target, 0);
  if (params)
    *params = pname == GL_BUFFER_MAP_POINTER && m ? m->ptr : 0;
}

void
glBindFramebuffer (GLenum target, GLuint framebuffer)
{
  uint32_t *a = r3d_put (OP_glBindFramebuffer, 2);
  if (!a)
    return;
  a[0] = target;
  a[1] = framebuffer;
}

/* --- vertex arrays ------------------------------------------------------

   An array in a buffer object is the renderer's already. One in the
   program's own memory is sent before each draw that uses it: the vertices
   from 0 to the last one the draw needs. */

void
glVertexAttribPointer (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)
{
  if (index >= MAX_ATTRIBS)
    return;
  struct attrib *t = &cur->attribs[index];
  t->size = size;
  t->type = type;
  t->normalized = normalized;
  t->stride = stride;
  t->pointer = pointer;
  t->client = cur->array_buffer == 0;
  t->in_buffer = !t->client;
  if (t->client)
    return;
  uint32_t *a = r3d_put (OP_glVertexAttribPointer, 6);
  if (!a)
    return;
  a[0] = index;
  a[1] = (uint32_t) size;
  a[2] = type;
  a[3] = normalized;
  a[4] = (uint32_t) stride;
  a[5] = (uint32_t) (uintptr_t) pointer;
}

void
glGetVertexAttribPointerv (GLuint index, GLenum pname, void **pointer)
{
  (void) pname;
  *pointer = index < MAX_ATTRIBS ? (void *) cur->attribs[index].pointer : 0;
}

static int
fixed_slot (GLenum cap)
{
  switch (cap)
    {
    case GL_VERTEX_ARRAY: return 0;
    case GL_NORMAL_ARRAY: return 1;
    case GL_COLOR_ARRAY: return 2;
    case GL_INDEX_ARRAY: return 3;
    case GL_EDGE_FLAG_ARRAY: return 4;
    case GL_FOG_COORD_ARRAY: return 5;
    case GL_SECONDARY_COLOR_ARRAY: return 6;
    case GL_TEXTURE_COORD_ARRAY: return cur->client_unit < MAX_UNITS ? 8 + cur->client_unit : -1;
    }
  return -1;
}

static void
client_state (uint32_t op, GLenum cap, int on)
{
  int s = fixed_slot (cap);
  if (s >= 0)
    cur->fixed[s].enabled = on;
  uint32_t *a = r3d_put (op, 1);
  if (a)
    a[0] = cap;
}

void glEnableClientState (GLenum cap) { client_state (OP_glEnableClientState, cap, 1); }
void glDisableClientState (GLenum cap) { client_state (OP_glDisableClientState, cap, 0); }

void
glClientActiveTexture (GLenum texture)
{
  if (texture >= GL_TEXTURE0 && texture < GL_TEXTURE0 + MAX_UNITS)
    cur->client_unit = (int) (texture - GL_TEXTURE0);
  uint32_t *a = r3d_put (OP_glClientActiveTexture, 1);
  if (a)
    a[0] = texture;
}

static void
fixed_pointer (uint32_t op, int slot, GLint size, GLenum type, GLsizei stride, const void *pointer)
{
  if (slot < 0)
    return;
  struct attrib *t = &cur->fixed[slot];
  t->size = size;
  t->type = type;
  t->stride = stride;
  t->pointer = pointer;
  t->client = cur->array_buffer == 0;
  t->in_buffer = !t->client;
  if (t->client)
    return;
  uint32_t *a = r3d_put (op, 4);
  if (!a)
    return;
  a[0] = (uint32_t) size;
  a[1] = type;
  a[2] = (uint32_t) stride;
  a[3] = (uint32_t) (uintptr_t) pointer;
}

void glVertexPointer (GLint size, GLenum type, GLsizei stride, const void *pointer) { fixed_pointer (OP_glVertexPointer, 0, size, type, stride, pointer); }
void glNormalPointer (GLenum type, GLsizei stride, const void *pointer) { fixed_pointer (OP_glNormalPointer, 1, 3, type, stride, pointer); }
void glColorPointer (GLint size, GLenum type, GLsizei stride, const void *pointer) { fixed_pointer (OP_glColorPointer, 2, size, type, stride, pointer); }
void glIndexPointer (GLenum type, GLsizei stride, const void *pointer) { fixed_pointer (OP_glIndexPointer, 3, 1, type, stride, pointer); }
void glEdgeFlagPointer (GLsizei stride, const void *pointer) { fixed_pointer (OP_glEdgeFlagPointer, 4, 1, GL_UNSIGNED_BYTE, stride, pointer); }
void glFogCoordPointer (GLenum type, GLsizei stride, const void *pointer) { fixed_pointer (OP_glFogCoordPointer, 5, 1, type, stride, pointer); }
void glSecondaryColorPointer (GLint size, GLenum type, GLsizei stride, const void *pointer) { fixed_pointer (OP_glSecondaryColorPointer, 6, size, type, stride, pointer); }

void
glTexCoordPointer (GLint size, GLenum type, GLsizei stride, const void *pointer)
{
  fixed_pointer (OP_glTexCoordPointer, cur->client_unit < MAX_UNITS ? 8 + cur->client_unit : -1, size, type, stride, pointer);
}

/* (OpenGL 2.1 section 2.8, table 2.5) */
void
glInterleavedArrays (GLenum format, GLsizei stride, const void *pointer)
{
  const GLsizei f = sizeof (GLfloat), c = 4;
  int et = 0, ec = 0, en = 0, st = 0, sc = 0, sv = 0;
  GLenum tc = GL_FLOAT;
  GLsizei pc = 0, pn = 0, pv = 0, s = 0;
  switch (format)
    {
    case GL_V2F: sv = 2; s = 2 * f; break;
    case GL_V3F: sv = 3; s = 3 * f; break;
    case GL_C4UB_V2F: ec = 1; sc = 4; sv = 2; tc = GL_UNSIGNED_BYTE; pv = c; s = c + 2 * f; break;
    case GL_C4UB_V3F: ec = 1; sc = 4; sv = 3; tc = GL_UNSIGNED_BYTE; pv = c; s = c + 3 * f; break;
    case GL_C3F_V3F: ec = 1; sc = 3; sv = 3; pv = 3 * f; s = 6 * f; break;
    case GL_N3F_V3F: en = 1; sv = 3; pv = 3 * f; s = 6 * f; break;
    case GL_C4F_N3F_V3F: ec = en = 1; sc = 4; sv = 3; pn = 4 * f; pv = 7 * f; s = 10 * f; break;
    case GL_T2F_V3F: et = 1; st = 2; sv = 3; pv = 2 * f; s = 5 * f; break;
    case GL_T4F_V4F: et = 1; st = 4; sv = 4; pv = 4 * f; s = 8 * f; break;
    case GL_T2F_C4UB_V3F: et = ec = 1; st = 2; sc = 4; sv = 3; tc = GL_UNSIGNED_BYTE; pc = 2 * f; pv = c + 2 * f; s = c + 5 * f; break;
    case GL_T2F_C3F_V3F: et = ec = 1; st = 2; sc = 3; sv = 3; pc = 2 * f; pv = 5 * f; s = 8 * f; break;
    case GL_T2F_N3F_V3F: et = en = 1; st = 2; sv = 3; pn = 2 * f; pv = 5 * f; s = 8 * f; break;
    case GL_T2F_C4F_N3F_V3F: et = ec = en = 1; st = 2; sc = 4; sv = 3; pc = 2 * f; pn = 6 * f; pv = 9 * f; s = 12 * f; break;
    case GL_T4F_C4F_N3F_V4F: et = ec = en = 1; st = 4; sc = 4; sv = 4; pc = 4 * f; pn = 8 * f; pv = 11 * f; s = 15 * f; break;
    default:
      return;				/* GL_INVALID_ENUM */
    }
  if (stride < 0)
    return;
  if (!stride)
    stride = s;
  const char *p = pointer;
  glDisableClientState (GL_EDGE_FLAG_ARRAY);
  glDisableClientState (GL_INDEX_ARRAY);
  glDisableClientState (GL_SECONDARY_COLOR_ARRAY);
  glDisableClientState (GL_FOG_COORD_ARRAY);
  if (et)
    {
      glEnableClientState (GL_TEXTURE_COORD_ARRAY);
      glTexCoordPointer (st, GL_FLOAT, stride, p);
    }
  else
    glDisableClientState (GL_TEXTURE_COORD_ARRAY);
  if (ec)
    {
      glEnableClientState (GL_COLOR_ARRAY);
      glColorPointer (sc, tc, stride, p + pc);
    }
  else
    glDisableClientState (GL_COLOR_ARRAY);
  if (en)
    {
      glEnableClientState (GL_NORMAL_ARRAY);
      glNormalPointer (GL_FLOAT, stride, p + pn);
    }
  else
    glDisableClientState (GL_NORMAL_ARRAY);
  glEnableClientState (GL_VERTEX_ARRAY);
  glVertexPointer (sv, GL_FLOAT, stride, p + pv);
}

void
glGetPointerv (GLenum pname, void **params)
{
  int s = -1;
  switch (pname)
    {
    case GL_VERTEX_ARRAY_POINTER: s = 0; break;
    case GL_NORMAL_ARRAY_POINTER: s = 1; break;
    case GL_COLOR_ARRAY_POINTER: s = 2; break;
    case GL_INDEX_ARRAY_POINTER: s = 3; break;
    case GL_EDGE_FLAG_ARRAY_POINTER: s = 4; break;
    case GL_FOG_COORD_ARRAY_POINTER: s = 5; break;
    case GL_SECONDARY_COLOR_ARRAY_POINTER: s = 6; break;
    case GL_TEXTURE_COORD_ARRAY_POINTER: s = 8 + cur->client_unit; break;
    case GL_FEEDBACK_BUFFER_POINTER: *params = cur->feedback; return;
    case GL_SELECTION_BUFFER_POINTER: *params = cur->select; return;
    }
  *params = s >= 0 && s < FIXED_SLOTS ? (void *) cur->fixed[s].pointer : 0;
}

static size_t
array_elem (const struct attrib *t)
{
  GLint size = t->size == GL_BGRA ? 4 : t->size;
  return (size_t) size * type_bytes (t->type);
}

static void
send_client_arrays (size_t vertices)
{
  if (!vertices)
    return;
  for (GLuint i = 0; i < MAX_ATTRIBS; i++)
    {
      struct attrib *t = &cur->attribs[i];
      if (!t->enabled || !t->client || !t->pointer)
	continue;
      size_t elem = array_elem (t);
      size_t stride = t->stride ? (size_t) t->stride : elem;
      size_t bytes = stride * (vertices - 1) + elem;
      uint32_t f[6] = { i, (uint32_t) t->size, t->type, t->normalized, (uint32_t) stride, (uint32_t) bytes };
      r3d_put_data (OP_CLIENT_ARRAY, f, 6, t->pointer, bytes);
    }
  for (int s = 0; s < FIXED_SLOTS; s++)
    {
      struct attrib *t = &cur->fixed[s];
      if (!t->enabled || !t->client || !t->pointer || s == 7)
	continue;
      size_t elem = array_elem (t);
      size_t stride = t->stride ? (size_t) t->stride : elem;
      size_t bytes = stride * (vertices - 1) + elem;
      uint32_t f[6] = { s < 7 ? (uint32_t) s : 8, s < 8 ? 0 : (uint32_t) (s - 8), (uint32_t) t->size, t->type,
			(uint32_t) stride, (uint32_t) bytes };
      r3d_put_data (OP_FIXED_ARRAY, f, 6, t->pointer, bytes);
    }
}

static int
any_client_arrays (void)
{
  for (int i = 0; i < MAX_ATTRIBS; i++)
    if (cur->attribs[i].enabled && cur->attribs[i].client && cur->attribs[i].pointer)
      return 1;
  for (int s = 0; s < FIXED_SLOTS; s++)
    if (cur->fixed[s].enabled && cur->fixed[s].client && cur->fixed[s].pointer)
      return 1;
  return 0;
}

static double
component (GLenum type, const void *p, int i, int normalized)
{
  switch (type)
    {
    case GL_BYTE: { double v = ((const GLbyte *) p)[i]; return normalized ? (2 * v + 1) / 255 : v; }
    case GL_UNSIGNED_BYTE: { double v = ((const GLubyte *) p)[i]; return normalized ? v / 255 : v; }
    case GL_SHORT: { double v = ((const GLshort *) p)[i]; return normalized ? (2 * v + 1) / 65535 : v; }
    case GL_UNSIGNED_SHORT: { double v = ((const GLushort *) p)[i]; return normalized ? v / 65535 : v; }
    case GL_INT: { double v = ((const GLint *) p)[i]; return normalized ? (2 * v + 1) / 4294967295.0 : v; }
    case GL_UNSIGNED_INT: { double v = ((const GLuint *) p)[i]; return normalized ? v / 4294967295.0 : v; }
    case GL_DOUBLE: return ((const GLdouble *) p)[i];
    }
  return ((const GLfloat *) p)[i];
}

/* glArrayElement: the arrays in the program's memory as immediate-mode
   calls, here (all in buffer objects: the renderer's own glArrayElement) */
void
glArrayElement (GLint i)
{
  int client = 0, vbo = 0;
  for (int s = 0; s < FIXED_SLOTS; s++)
    if (cur->fixed[s].enabled && cur->fixed[s].pointer)
      {
	if (cur->fixed[s].client)
	  client = 1;
	else
	  vbo = 1;
      }
  if (!client || vbo)
    {
      uint32_t *a = r3d_put (OP_glArrayElement, 1);
      if (a)
	a[0] = (uint32_t) i;
      return;
    }
  double v[4];
  for (int s = 0; s < FIXED_SLOTS; s++)
    {
      static const int order[FIXED_SLOTS] = { 4, 8, 9, 10, 11, 12, 13, 14, 15, 2, 6, 5, 1, 3, 0, 7 };
      int k = order[s];
      struct attrib *t = &cur->fixed[k];
      if (!t->enabled || !t->pointer || k == 7)
	continue;
      size_t stride = t->stride ? (size_t) t->stride : array_elem (t);
      const char *p = (const char *) t->pointer + stride * (size_t) i;
      int n = t->size == GL_BGRA ? 4 : t->size;
      v[0] = v[1] = v[2] = 0;
      v[3] = 1;
      for (int c = 0; c < n && c < 4; c++)
	v[c] = component (t->type, p, c, k == 2 || k == 6);
      if (t->size == GL_BGRA)
	{
	  double r = v[0];
	  v[0] = v[2];
	  v[2] = r;
	}
      switch (k)
	{
	case 0: glVertex4d (v[0], v[1], v[2], n < 4 ? 1.0 : v[3]); break;
	case 1: glNormal3d (v[0], v[1], v[2]); break;
	case 2: glColor4d (v[0], v[1], v[2], n < 4 ? 1.0 : v[3]); break;
	case 3: glIndexd (v[0]); break;
	case 4: glEdgeFlag (*(const GLboolean *) p); break;
	case 5: glFogCoordd (v[0]); break;
	case 6: glSecondaryColor3d (v[0], v[1], v[2]); break;
	default: glMultiTexCoord4d (GL_TEXTURE0 + (GLenum) (k - 8), v[0], v[1], v[2], n < 4 ? 1.0 : v[3]); break;
	}
    }
}

void
glDrawArrays (GLenum mode, GLint first, GLsizei count)
{
  if (count <= 0 || first < 0)
    return;
  send_client_arrays ((size_t) first + (size_t) count);
  uint32_t *a = r3d_put (OP_glDrawArrays, 3);
  if (!a)
    return;
  a[0] = mode;
  a[1] = (uint32_t) first;
  a[2] = (uint32_t) count;
}

static size_t
max_index (GLenum type, const void *indices, GLsizei count)
{
  size_t max = 0;
  for (GLsizei i = 0; i < count; i++)
    {
      size_t v = type == GL_UNSIGNED_BYTE ? ((const uint8_t *) indices)[i]
		 : type == GL_UNSIGNED_SHORT ? ((const uint16_t *) indices)[i]
		 : ((const uint32_t *) indices)[i];
      if (v > max)
	max = v;
    }
  return max;
}

/* range: the last vertex the draw needs, if known (else (size_t) -1) */
static void
draw_elements (GLenum mode, GLsizei count, GLenum type, const void *indices, size_t last)
{
  if (count <= 0)
    return;
  size_t eb = type_bytes (type);
  if (cur->element_buffer)
    {
      /* indices in a buffer object: arrays in the program's memory are
	 sized from the indices, read back (OpenGL) */
      if (any_client_arrays ())
	{
	  if (last == (size_t) -1 && cur->desktop)
	    {
	      void *ix = malloc ((size_t) count * eb);
	      if (ix)
		{
		  glGetBufferSubData (GL_ELEMENT_ARRAY_BUFFER, (GLintptr) (uintptr_t) indices, (GLsizeiptr) ((size_t) count * eb), ix);
		  last = max_index (type, ix, count);
		  free (ix);
		}
	    }
	  if (last != (size_t) -1)
	    send_client_arrays (last + 1);
	}
      uint32_t *a = r3d_put (OP_glDrawElements, 4);
      if (!a)
	return;
      a[0] = mode;
      a[1] = (uint32_t) count;
      a[2] = type;
      a[3] = (uint32_t) (uintptr_t) indices;
      return;
    }
  if (!indices)
    return;
  send_client_arrays ((last != (size_t) -1 ? last : max_index (type, indices, count)) + 1);
  uint32_t f[4] = { mode, (uint32_t) count, type, (uint32_t) ((size_t) count * eb) };
  r3d_put_data (OP_DRAW_ELEMENTS_INLINE, f, 4, indices, (size_t) count * eb);
}

void
glDrawElements (GLenum mode, GLsizei count, GLenum type, const void *indices)
{
  draw_elements (mode, count, type, indices, (size_t) -1);
}

void
glDrawRangeElements (GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices)
{
  (void) start;
  if (end < start)
    return;				/* GL_INVALID_VALUE */
  draw_elements (mode, count, type, indices, end);
}

void
glMultiDrawArrays (GLenum mode, const GLint *first, const GLsizei *count, GLsizei drawcount)
{
  for (GLsizei i = 0; i < drawcount; i++)
    glDrawArrays (mode, first[i], count[i]);
}

void
glMultiDrawElements (GLenum mode, const GLsizei *count, GLenum type, const void *const *indices, GLsizei drawcount)
{
  for (GLsizei i = 0; i < drawcount; i++)
    glDrawElements (mode, count[i], type, indices[i]);
}

void
glPushClientAttrib (GLbitfield mask)
{
  uint32_t *a = r3d_put (OP_glPushClientAttrib, 1);
  if (!a)
    return;
  a[0] = mask;
  if (cur->client_depth >= MAX_ATTRIB_DEPTH)
    return;
  struct saved_client *s = &cur->client_stack[cur->client_depth++];
  s->mask = mask;
  memcpy (s->attribs, cur->attribs, sizeof s->attribs);
  memcpy (s->fixed, cur->fixed, sizeof s->fixed);
  s->client_unit = cur->client_unit;
  s->array_buffer = cur->array_buffer;
  s->pack = cur->pack;
  s->unpack = cur->unpack;
  s->pack_buffer = cur->pack_buffer;
  s->unpack_buffer = cur->unpack_buffer;
}

void
glPopClientAttrib (void)
{
  if (!r3d_put (OP_glPopClientAttrib, 0) || cur->client_depth <= 0)
    return;
  struct saved_client *s = &cur->client_stack[--cur->client_depth];
  if (s->mask & GL_CLIENT_VERTEX_ARRAY_BIT)
    {
      memcpy (cur->attribs, s->attribs, sizeof s->attribs);
      memcpy (cur->fixed, s->fixed, sizeof s->fixed);
      cur->client_unit = s->client_unit;
      cur->array_buffer = s->array_buffer;
    }
  if (s->mask & GL_CLIENT_PIXEL_STORE_BIT)
    {
      cur->pack = s->pack;
      cur->unpack = s->unpack;
      cur->pack_buffer = s->pack_buffer;
      cur->unpack_buffer = s->unpack_buffer;
    }
}

/* --- immediate mode, collected -------------------------------------------

   Between glBegin and glEnd the vertex, colour, normal and texture
   coordinate calls are collected, and sent as draws from an array
   (OP_IMMEDIATE), as Mesa itself does with immediate mode; at glEnd the
   current colour, normal and texture coordinates are set to the last ones
   given, as the calls would have left them. The vertices are written
   straight into the ring, into a command made room for at the first
   vertex and finished when it is full or at glEnd: a long primitive of
   points, lines, triangles or quads goes a draw at a time, so the
   renderer draws while the program is still calling; a long strip, fan,
   loop or polygon is gathered here first (it can't be cut).

   Not while a display list is made, nor for anything else between glBegin
   and glEnd (a glMaterial, an edge flag...): then what was collected goes
   as the calls themselves, and the rest of the primitive too. An
   attribute is in the array only if it was given before the first
   vertex; one first given later does the same. */

enum { IMM_VERTEX, IMM_COLOR, IMM_NORMAL, IMM_TEX };
#define IMM_UNITS 4
#define IMM_SMALL 8			/* fewer vertices than this go as calls */
#define IMM_CHUNK 1024			/* vertices a draw, for those that can be cut */
#define IMM_MAX_FLOATS (4u << 20)	/* gathered here, at most */

static const int imm_per[GL_POLYGON + 1] = { 1, 2, 0, 0, 3, 0, 0, 4, 0, 0 };

static int
r3d_imm_begin (GLenum mode)
{
  if (!cur->desktop || cur->compiling || mode > GL_POLYGON || cur->handle < 0)
    return 0;
  cur->imm_on = 1;
  cur->imm_mode = mode;
  cur->imm_specified = 0;
  cur->imm_count = 0;
  cur->imm_capv = 0;			/* no room yet: the first vertex makes it */
  cur->imm_cmd = 0;
  return 1;
}

static size_t
imm_stride (uint32_t layout)
{
  size_t n = 4;
  if (layout & 1)
    n += 4;
  if (layout & 2)
    n += 3;
  for (int u = 0; u < IMM_UNITS; u++)
    if (layout & (4u << u))
      n += 4;
  return n;
}

/* the program's own arrays: none enabled or in a buffer object, so the
   renderer needn't save and restore them around ours */
static int
arrays_untouched (void)
{
  for (int i = 0; i < MAX_ATTRIBS; i++)
    if (cur->attribs[i].enabled || cur->attribs[i].in_buffer)
      return 0;
  for (int s = 0; s < FIXED_SLOTS; s++)
    if (cur->fixed[s].enabled || cur->fixed[s].in_buffer)
      return 0;
  return 1;
}

/* room in the ring for a draw of up to cap vertices: 1, or 0 */
static int
imm_room (size_t cap)
{
  size_t words = 4 + cap * cur->imm_stride;
  cur->imm_on = 0;			/* (making room is a command) */
  uint32_t *a = r3d_put (OP_IMMEDIATE, words);
  cur->imm_on = 1;
  if (!a)
    return 0;
  cur->imm_cmd = a;
  cur->imm_cmd_head = cur->local_head - (uint32_t) (words + 2);
  cur->imm_capv = cap;
  cur->imm_w = (GLfloat *) (a + 4);
  return 1;
}

/* the draw in the ring finished with the vertices it has */
static void
imm_finish (void)
{
  uint32_t *a = cur->imm_cmd;
  size_t words = 4 + cur->imm_count * cur->imm_stride;
  a[-1] = (uint32_t) (words + 2);
  a[0] = cur->imm_mode;
  a[1] = (uint32_t) cur->imm_count;
  a[2] = cur->imm_layout;
  a[3] = (uint32_t) arrays_untouched ();
  cur->local_head = cur->imm_cmd_head + (uint32_t) (words + 2);
  cur->imm_cmd = 0;
}

/* what the ring holds of it moved here (the room given back) */
static int
imm_to_heap (size_t cap)
{
  size_t floats = cur->imm_count * cur->imm_stride, want = cap * cur->imm_stride;
  if (want < floats)
    want = floats;
  if (want > cur->imm_cap)
    {
      GLfloat *p = realloc (cur->imm_v, want * sizeof *p);
      if (!p)
	return 0;
      cur->imm_v = p;
      cur->imm_cap = want;
    }
  if (cur->imm_cmd)
    {
      memcpy (cur->imm_v, cur->imm_cmd + 4, floats * sizeof (GLfloat));
      cur->local_head = cur->imm_cmd_head;
      cur->imm_cmd = 0;
    }
  cur->imm_capv = cur->imm_cap / cur->imm_stride;
  cur->imm_w = cur->imm_v + floats;
  return 1;
}

/* the current values given so far, as calls */
static void
imm_current (void)
{
  const GLfloat (*c)[4] = (const GLfloat (*)[4]) cur->imm_cur;
  if (cur->imm_specified & 1)
    glColor4f (c[0][0], c[0][1], c[0][2], c[0][3]);
  if (cur->imm_specified & 2)
    glNormal3f (c[1][0], c[1][1], c[1][2]);
  for (int u = 0; u < IMM_UNITS; u++)
    if (cur->imm_specified & (4u << u))
      glMultiTexCoord4f (GL_TEXTURE0 + (GLenum) u, c[2 + u][0], c[2 + u][1], c[2 + u][2], c[2 + u][3]);
}

/* what was collected, as the calls themselves (glBegin, then each
   vertex with its attributes, then the current values); collecting
   ends */
static void
imm_as_calls (void)
{
  if (cur->imm_cmd && !imm_to_heap (0))
    cur->imm_count = 0;			/* (no memory: the vertices are lost) */
  cur->imm_on = 0;
  uint32_t *a = r3d_put (OP_glBegin, 1);
  if (!a)
    return;
  a[0] = cur->imm_mode;
  const GLfloat *all = cur->imm_v;
  for (size_t i = 0; i < cur->imm_count; i++)
    {
      const GLfloat *v = all + i * cur->imm_stride, *p = v + 4;
      if (cur->imm_layout & 1)
	{
	  glColor4f (p[0], p[1], p[2], p[3]);
	  p += 4;
	}
      if (cur->imm_layout & 2)
	{
	  glNormal3f (p[0], p[1], p[2]);
	  p += 3;
	}
      for (int u = 0; u < IMM_UNITS; u++)
	if (cur->imm_layout & (4u << u))
	  {
	    glMultiTexCoord4f (GL_TEXTURE0 + (GLenum) u, p[0], p[1], p[2], p[3]);
	    p += 4;
	  }
      glVertex4f (v[0], v[1], v[2], v[3]);
    }
  imm_current ();
}

static void
imm_fallback (void)
{
  imm_as_calls ();
}

/* the vertices gathered here, as one draw */
static void
imm_draw_heap (void)
{
  uint32_t f[4] = { cur->imm_mode, (uint32_t) cur->imm_count, cur->imm_layout, (uint32_t) arrays_untouched () };
  cur->imm_on = 0;
  r3d_put_data (OP_IMMEDIATE, f, 4, cur->imm_v, cur->imm_count * cur->imm_stride * 4);
  cur->imm_on = 1;
}

static void
r3d_imm_end (void)
{
  if (cur->imm_count < IMM_SMALL)
    {
      imm_as_calls ();
      r3d_put (OP_glEnd, 0);
      return;
    }
  if (cur->imm_cmd)
    imm_finish ();
  else
    imm_draw_heap ();
  cur->imm_on = 0;
  imm_current ();
}

/* the room is full (or there is none yet): 1 if the vertex can go on */
static int
imm_full (void)
{
  int k = imm_per[cur->imm_mode];
  if (!cur->imm_capv && !cur->imm_count)
    {
      /* the first vertex: its layout, and room in the ring */
      cur->imm_layout = cur->imm_specified;
      cur->imm_stride = imm_stride (cur->imm_layout);
      return imm_room (k ? IMM_CHUNK / (size_t) k * (size_t) k : IMM_CHUNK) || imm_to_heap (IMM_CHUNK);
    }
  if (cur->imm_cmd && k)
    {
      /* whole primitives: this draw goes, the next one has room */
      imm_finish ();
      publish ();
      cur->imm_count = 0;
      return imm_room (cur->imm_capv);
    }
  /* gathered here: more room */
  size_t floats = cur->imm_count * cur->imm_stride;
  if (floats * 2 > IMM_MAX_FLOATS)
    {
      if (k && !cur->imm_cmd)
	{
	  imm_draw_heap ();
	  cur->imm_count = 0;
	  cur->imm_w = cur->imm_v;
	  return 1;
	}
      return 0;
    }
  return imm_to_heap (cur->imm_count * 2);
}

static int
imm_attr_late (void)
{
  imm_fallback ();			/* (sends this one's value too) */
  return 1;
}

/* one call between glBegin and glEnd: 1 if collected. Inlined into each
   call, with kind, unit and n known there. */
static inline __attribute__ ((always_inline)) int
r3d_imm (int kind, int unit, int n, const GLfloat *val)
{
  struct r3d_context *c = cur;
  if (kind != IMM_VERTEX)
    {
      if (kind == IMM_TEX && (unsigned) unit >= IMM_UNITS)
	{
	  imm_fallback ();
	  return 0;
	}
      int at = kind == IMM_COLOR ? 0 : kind == IMM_NORMAL ? 1 : 2 + unit;
      GLfloat *d = c->imm_cur[at];
      d[0] = val[0];
      d[1] = n > 1 ? val[1] : 0;
      d[2] = n > 2 ? val[2] : 0;
      d[3] = n > 3 ? val[3] : 1;
      c->imm_specified |= 1u << at;
      if (__builtin_expect (c->imm_count && !(c->imm_layout & (1u << at)), 0))
	return imm_attr_late ();
      return 1;
    }
  if (__builtin_expect (c->imm_count >= c->imm_capv, 0) && !imm_full ())
    {
      imm_fallback ();
      return 0;
    }
  c = cur;
  GLfloat *d = c->imm_w;
  uint32_t l = c->imm_layout;
  d[0] = val[0];
  d[1] = n > 1 ? val[1] : 0;
  d[2] = n > 2 ? val[2] : 0;
  d[3] = n > 3 ? val[3] : 1;
  d += 4;
  if (l & 1)
    {
      d[0] = c->imm_cur[0][0]; d[1] = c->imm_cur[0][1]; d[2] = c->imm_cur[0][2]; d[3] = c->imm_cur[0][3];
      d += 4;
    }
  if (l & 2)
    {
      d[0] = c->imm_cur[1][0]; d[1] = c->imm_cur[1][1]; d[2] = c->imm_cur[1][2];
      d += 3;
    }
  if (__builtin_expect (l & 0x3c, 0))
    for (int u = 0; u < IMM_UNITS; u++)
      if (l & (4u << u))
	{
	  memcpy (d, c->imm_cur[2 + u], 16);
	  d += 4;
	}
  c->imm_w = d;
  c->imm_count++;
  return 1;
}

/* --- contexts ----------------------------------------------------------- */

/* the contexts open, closed when the program ends however it ends (a
   program that isn't a Wimp task, run from an Obey file or the command
   line, isn't seen ending by RISC3D: its contexts would stay open, each
   with a Linux process and 36 MB mapped) */
#define MAX_OPEN 16
static struct r3d_context *open_ctx[MAX_OPEN];
static int atexit_set;

static void
close_all (void)
{
  for (int i = 0; i < MAX_OPEN; i++)
    if (open_ctx[i])
      {
	_swix (RISC3D_Close, _IN (0), open_ctx[i]->handle);
	open_ctx[i] = 0;
      }
}

static void
remember (struct r3d_context *c, int on)
{
  for (int i = 0; i < MAX_OPEN; i++)
    if (on ? !open_ctx[i] : open_ctx[i] == c)
      {
	open_ctx[i] = on ? c : 0;
	break;
      }
  if (on && !atexit_set)
    atexit_set = atexit (close_all) == 0;
}

static void
free_context (struct r3d_context *c)
{
  free (c->modelview.m);
  free (c->projection.m);
  for (int i = 0; i < MAX_UNITS; i++)
    free (c->texture[i].m);
  for (size_t i = 0; i < c->nlists; i++)
    free (c->lists[i].w);
  free (c->lists);
  for (int i = 0; i < 4; i++)
    free (c->maps[i].ptr);
  free (c->imm_v);
  free (c);
}

/* a context of OpenGL (desktop: the compatibility profile) or GLES 2:
   the renderer makes it now, so that a failure is seen here */
static struct r3d_context *
create (int desktop)
{
  struct r3d_context *c = calloc (1, sizeof *c);
  unsigned bytes = 0;
  if (!c)
    {
      last_error = "No memory for a RISC3D context";
      return 0;
    }
  _kernel_oserror *e = _swix (RISC3D_Open, _IN (0) | _OUTR (0, 2), 0, &c->handle, &c->area, &bytes);
  if (e)
    {
      strncpy (error_text, e->errmess, sizeof error_text - 1);
      last_error = error_text;
      free (c);
      return 0;
    }
  if (!c->area || c->area->magic != R3D_MAGIC)
    {
      _swix (RISC3D_Close, _IN (0), c->handle);
      last_error = "RISC3D's driver gave no shared memory";
      free (c);
      return 0;
    }
  c->ring = (uint32_t *) ((char *) c->area + c->area->ring_offset);
  c->ring_words = c->area->ring_words;
  c->local_head = c->published = c->tail_seen = c->area->head;
  c->unpack.alignment = c->pack.alignment = 4;
  c->next_texture = c->next_buffer = c->next_list = 1;
  c->matrix_mode = GL_MODELVIEW;

  struct r3d_context *was = cur;
  if (was != &none)
    publish ();
  cur = c;
  int ok = 0;
  uint32_t *a = r3d_put (OP_CONTEXT, 1);
  if (a)
    {
      a[0] = (uint32_t) desktop;
      int n = r3d_sync (0, 0);
      const uint32_t *r = reply_area ();
      if (n >= 8 && r[0])
	ok = 1;
      else
	{
	  snprintf (error_text, sizeof error_text, "%s", n > 8 ? (const char *) (r + 2) : "the renderer made no context");
	  last_error = error_text;
	}
    }
  if (ok && desktop)
    {
      /* what is kept here: the stacks, as deep as the renderer's */
      c->desktop = 1;
      a = r3d_put (OP_LIMITS, 0);
      GLint lim[6] = { 32, 32, 10, 8, 8, 16 };
      if (!a || r3d_sync (lim, sizeof lim) < (int) sizeof lim)
	ok = 0;
      c->units = lim[3] < 1 ? 1 : lim[3] > MAX_UNITS ? MAX_UNITS : lim[3];
      ok = ok && stack_init (&c->modelview, lim[0]) && stack_init (&c->projection, lim[1]);
      for (int i = 0; ok && i < c->units; i++)
	ok = stack_init (&c->texture[i], lim[2]);
      if (!ok && !last_error)
	last_error = "No memory for a RISC3D context";
    }
  cur = was;
  if (!ok)
    {
      _swix (RISC3D_Close, _IN (0), c->handle);
      free_context (c);
      return 0;
    }
  remember (c, 1);
  return c;
}

struct r3d_context *r3d_create (void) { return create (0); }
struct r3d_context *r3d_create_gl (void) { return create (1); }

/* the context the gl functions draw with from now (0: none) */
void
r3d_make_current (struct r3d_context *c)
{
  if (cur != &none)
    publish ();
  cur = c ? c : &none;
}

void
r3d_destroy (struct r3d_context *c)
{
  if (!c)
    return;
  if (cur == c)
    {
      publish ();
      cur = &none;
    }
  remember (c, 0);
  _swix (RISC3D_Close, _IN (0), c->handle);
  free_context (c);
}

/* the simple form: one context, made current */
static struct r3d_context *only;

static const char *
open_one (int desktop)
{
  if (!only)
    only = create (desktop);
  if (!only)
    return last_error;
  r3d_make_current (only);
  return 0;
}

const char *r3d_open (void) { return open_one (0); }
const char *r3d_open_gl (void) { return open_one (1); }

void
r3d_close (void)
{
  r3d_destroy (only);
  only = 0;
}

void
r3d_surface (int width, int height)
{
  uint32_t *a = r3d_put (OP_SURFACE, 2);
  if (!a)
    return;
  a[0] = (uint32_t) width;
  a[1] = (uint32_t) height;
  cur->surface_w = width;
  cur->surface_h = height;
  /* the first surface sets the viewport (and scissor box) to fit */
  if (!cur->viewport_known && width > 0 && height > 0)
    st_viewport (0, 0, width, height);
}

/* for measuring: the renderer's time since the last call, in
   nanoseconds: working through commands, of that reading frames back;
   then times it slept, and commands. 0, or -1 */
int
r3d_stats (unsigned long long v[4])
{
  uint64_t r[4] = { 0, 0, 0, 0 };
  if (!r3d_put (OP_STATS, 0) || r3d_sync (r, sizeof r) < (int) sizeof r)
    return -1;
  for (int i = 0; i < 4; i++)
    v[i] = r[i];
  return 0;
}

/* Overlapped frames: this frame is finished and read back by the renderer
   while the program goes on with the next one; what comes back is the
   frame before (the first time, this one). So the program never waits for
   the GPU to finish a frame, at the cost of a frame of latency. Each frame
   must be at most half the reply area (16 MB: 2048 x 2048), else this is
   r3d_swap. Don't mix the two in one context. Returns the bytes copied,
   or -1. */
int
r3d_swap_overlapped (void *pixels, size_t bytes)
{
  size_t half = cur->handle >= 0 ? cur->area->reply_bytes / 2 : 0;
  if ((size_t) cur->surface_w * (size_t) cur->surface_h * 4 > half)
    return r3d_swap (pixels, bytes);
  uint32_t n = ++cur->frame;
  uint32_t *a = r3d_put (OP_PRESENT_SLOT, 1);
  if (!a)
    return -1;
  a[0] = n;
  publish ();
  /* (after zero-copy frames, the one before was never read back) */
  uint32_t want = n > 1 && !cur->after_overlay ? n - 1 : n;
  cur->after_overlay = 0;
  /* (usually there already: the renderer read it back during this frame) */
  for (unsigned i = 0; (int32_t) (__atomic_load_n (&cur->area->frame_done, __ATOMIC_ACQUIRE) - want) < 0; i++)
    if (i > 200000)
      {
	if (r3d_sync (0, 0) < 0)
	  return -1;
	break;
      }
  size_t size = (size_t) cur->surface_w * (size_t) cur->surface_h * 4;
  if (size > bytes)
    size = bytes;
  if (pixels)
    memcpy (pixels, (const char *) cur->area + cur->area->reply_offset + (want & 1) * half, size);
  return (int) size;
}

/* Zero-copy frames: see r3d.h */
int
r3d_overlay (int x, int y, int width, int height, unsigned *key)
{
  if (!cur || cur->handle < 0)
    return -1;
  uint32_t *a = r3d_put (OP_OVERLAY, 4);
  if (!a)
    return -1;
  a[0] = (uint32_t) x;
  a[1] = (uint32_t) y;
  a[2] = (uint32_t) (width > 0 ? width : 0);
  a[3] = (uint32_t) (height > 0 ? height : 0);
  int32_t r[3] = { 0, 0, 0 };
  /* (a renderer older than this answers nothing: no overlay) */
  if (r3d_sync (r, sizeof r) < (int) sizeof r || r[0] != 1)
    {
      cur->overlay = 0;
      return 0;
    }
  cur->overlay = 1;
  if (key)
    *key = (unsigned) r[1];
  return 1;
}

int
r3d_swap_overlay (void)
{
  if (!cur || cur->handle < 0 || !cur->overlay)
    return -1;
  uint32_t n = ++cur->frame;
  uint32_t *a = r3d_put (OP_PRESENT_OVERLAY, 1);
  if (!a)
    return -1;
  a[0] = n;
  publish ();
  uint32_t want = n > 1 ? n - 1 : n;
  /* the frame before is shown at the display's next frame, up to 20 ms
     away; waiting for it with r3d_sync would wait for this one as well */
  clock_t t0 = clock ();
  for (unsigned i = 0; (int32_t) (__atomic_load_n (&cur->area->frame_done, __ATOMIC_ACQUIRE) - want) < 0; i++)
    if (!(i & 1023) && clock () - t0 > CLOCKS_PER_SEC / 5)
      {
	if (r3d_sync (0, 0) < 0)
	  return -1;
	break;
      }
  cur->after_overlay = 1;
  if (__atomic_load_n (&cur->area->overlay_error, __ATOMIC_ACQUIRE))
    {
      cur->overlay = 0;
      return -1;
    }
  return 0;
}

int
r3d_swap (void *pixels, size_t bytes)
{
  if (!r3d_put (OP_PRESENT, 0))
    return -1;
  return r3d_sync (pixels, bytes);
}

#include "client_gen.c"
