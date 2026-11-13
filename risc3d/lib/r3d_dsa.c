/* r3d_dsa.c: GL_EXT_direct_state_access's functions that touch state the
   library keeps or the program's memory, as the bound object's own
   function with the object bound for the call (included by r3d_gl.c,
   before the generated functions). The others are sent as they are and
   done by the renderer's own (rol-3d: Mesa's, with the compatibility
   profile).

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */

/* a texture or buffer object bound for the commands until temp_end, in
   rol-3d (its binding put back after) */
static void
temp_bind (uint32_t kind, GLenum target, GLuint name)
{
  uint32_t *a = r3d_put (OP_TEMP_BIND, 3);
  if (!a)
    return;
  a[0] = kind;
  a[1] = target;
  a[2] = name;
}

static void
temp_texture (GLenum target, GLuint texture)
{
  used_name (&cur->next_texture, texture);
  temp_bind (0, target, texture);
}

static void
temp_buffer (GLuint buffer)
{
  used_name (&cur->next_buffer, buffer);
  temp_bind (1, GL_COPY_WRITE_BUFFER, buffer);
}

static void temp_end (void) { r3d_put (OP_TEMP_END, 0); }

/* a texture unit made active, or the client's active unit, for the call */
static int
unit_begin (GLenum texunit)
{
  int was = cur->active;
  glActiveTexture (texunit);
  return was;
}

static void unit_end (int was) { glActiveTexture (GL_TEXTURE0 + (GLenum) was); }

static int
client_unit_begin (GLenum texunit)
{
  int was = cur->client_unit;
  glClientActiveTexture (texunit);
  return was;
}

static void client_unit_end (int was) { glClientActiveTexture (GL_TEXTURE0 + (GLenum) was); }

/* a matrix stack (GL_TEXTUREi: that unit's) made the current one */
struct mat_was { GLenum mode; int active, unit; };

static struct mat_was
matrix_begin (GLenum mode)
{
  struct mat_was w = { cur->matrix_mode, cur->active, 0 };
  if (mode >= GL_TEXTURE0 && mode < GL_TEXTURE0 + 32)
    {
      w.unit = 1;
      glActiveTexture (mode);
      mode = GL_TEXTURE;
    }
  if (mode != w.mode)
    glMatrixMode (mode);
  return w;
}

static void
matrix_end (struct mat_was w)
{
  if (cur->matrix_mode != w.mode)
    glMatrixMode (w.mode);
  if (w.unit)
    glActiveTexture (GL_TEXTURE0 + (GLenum) w.active);
}

/* a vertex array object bound, and a buffer as GL_ARRAY_BUFFER */
struct vao_was { GLuint vao, array_buffer; };

static struct vao_was
vao_begin (GLuint vaobj, GLuint buffer)
{
  struct vao_was w = { cur->vao, cur->array_buffer };
  if (vaobj != w.vao)
    glBindVertexArray (vaobj);
  if (buffer != cur->array_buffer)
    glBindBuffer (GL_ARRAY_BUFFER, buffer);
  return w;
}

static void
vao_end (struct vao_was w)
{
  if (cur->vao != w.vao)
    glBindVertexArray (w.vao);
  if (cur->array_buffer != w.array_buffer)
    glBindBuffer (GL_ARRAY_BUFFER, w.array_buffer);
}

/* --- textures by name: bound for the call (OP_TEMP_BIND) ------------- */

void
glTextureImage1DEXT (GLuint texture, GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const void * pixels)
{
  temp_texture (target, texture);
  glTexImage1D (target, level, internalformat, width, border, format, type, pixels);
  temp_end ();
}

void
glTextureImage2DEXT (GLuint texture, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void * pixels)
{
  temp_texture (target, texture);
  glTexImage2D (target, level, internalformat, width, height, border, format, type, pixels);
  temp_end ();
}

void
glTextureImage3DEXT (GLuint texture, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void * pixels)
{
  temp_texture (target, texture);
  glTexImage3D (target, level, internalformat, width, height, depth, border, format, type, pixels);
  temp_end ();
}

void
glTextureSubImage1DEXT (GLuint texture, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const void * pixels)
{
  temp_texture (target, texture);
  glTexSubImage1D (target, level, xoffset, width, format, type, pixels);
  temp_end ();
}

void
glTextureSubImage2DEXT (GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void * pixels)
{
  temp_texture (target, texture);
  glTexSubImage2D (target, level, xoffset, yoffset, width, height, format, type, pixels);
  temp_end ();
}

void
glTextureSubImage3DEXT (GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void * pixels)
{
  temp_texture (target, texture);
  glTexSubImage3D (target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
  temp_end ();
}

void
glGetTextureImageEXT (GLuint texture, GLenum target, GLint level, GLenum format, GLenum type, void * pixels)
{
  temp_texture (target, texture);
  glGetTexImage (target, level, format, type, pixels);
  temp_end ();
}

void
glCompressedTextureImage1DEXT (GLuint texture, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const void * bits)
{
  temp_texture (target, texture);
  glCompressedTexImage1D (target, level, internalformat, width, border, imageSize, bits);
  temp_end ();
}

void
glCompressedTextureImage2DEXT (GLuint texture, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void * bits)
{
  temp_texture (target, texture);
  glCompressedTexImage2D (target, level, internalformat, width, height, border, imageSize, bits);
  temp_end ();
}

void
glCompressedTextureImage3DEXT (GLuint texture, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void * bits)
{
  temp_texture (target, texture);
  glCompressedTexImage3D (target, level, internalformat, width, height, depth, border, imageSize, bits);
  temp_end ();
}

void
glCompressedTextureSubImage1DEXT (GLuint texture, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void * bits)
{
  temp_texture (target, texture);
  glCompressedTexSubImage1D (target, level, xoffset, width, format, imageSize, bits);
  temp_end ();
}

void
glCompressedTextureSubImage2DEXT (GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void * bits)
{
  temp_texture (target, texture);
  glCompressedTexSubImage2D (target, level, xoffset, yoffset, width, height, format, imageSize, bits);
  temp_end ();
}

void
glCompressedTextureSubImage3DEXT (GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void * bits)
{
  temp_texture (target, texture);
  glCompressedTexSubImage3D (target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, bits);
  temp_end ();
}

void
glGetCompressedTextureImageEXT (GLuint texture, GLenum target, GLint lod, void * img)
{
  temp_texture (target, texture);
  glGetCompressedTexImage (target, lod, img);
  temp_end ();
}

/* --- a texture unit's: that unit made active for the call ----------- */

void
glMultiTexImage1DEXT (GLenum texunit, GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const void * pixels)
{
  int was = unit_begin (texunit);
  glTexImage1D (target, level, internalformat, width, border, format, type, pixels);
  unit_end (was);
}

void
glMultiTexImage2DEXT (GLenum texunit, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void * pixels)
{
  int was = unit_begin (texunit);
  glTexImage2D (target, level, internalformat, width, height, border, format, type, pixels);
  unit_end (was);
}

void
glMultiTexImage3DEXT (GLenum texunit, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void * pixels)
{
  int was = unit_begin (texunit);
  glTexImage3D (target, level, internalformat, width, height, depth, border, format, type, pixels);
  unit_end (was);
}

void
glMultiTexSubImage1DEXT (GLenum texunit, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const void * pixels)
{
  int was = unit_begin (texunit);
  glTexSubImage1D (target, level, xoffset, width, format, type, pixels);
  unit_end (was);
}

void
glMultiTexSubImage2DEXT (GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void * pixels)
{
  int was = unit_begin (texunit);
  glTexSubImage2D (target, level, xoffset, yoffset, width, height, format, type, pixels);
  unit_end (was);
}

void
glMultiTexSubImage3DEXT (GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void * pixels)
{
  int was = unit_begin (texunit);
  glTexSubImage3D (target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
  unit_end (was);
}

void
glGetMultiTexImageEXT (GLenum texunit, GLenum target, GLint level, GLenum format, GLenum type, void * pixels)
{
  int was = unit_begin (texunit);
  glGetTexImage (target, level, format, type, pixels);
  unit_end (was);
}

void
glCompressedMultiTexImage1DEXT (GLenum texunit, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const void * bits)
{
  int was = unit_begin (texunit);
  glCompressedTexImage1D (target, level, internalformat, width, border, imageSize, bits);
  unit_end (was);
}

void
glCompressedMultiTexImage2DEXT (GLenum texunit, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void * bits)
{
  int was = unit_begin (texunit);
  glCompressedTexImage2D (target, level, internalformat, width, height, border, imageSize, bits);
  unit_end (was);
}

void
glCompressedMultiTexImage3DEXT (GLenum texunit, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void * bits)
{
  int was = unit_begin (texunit);
  glCompressedTexImage3D (target, level, internalformat, width, height, depth, border, imageSize, bits);
  unit_end (was);
}

void
glCompressedMultiTexSubImage1DEXT (GLenum texunit, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void * bits)
{
  int was = unit_begin (texunit);
  glCompressedTexSubImage1D (target, level, xoffset, width, format, imageSize, bits);
  unit_end (was);
}

void
glCompressedMultiTexSubImage2DEXT (GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void * bits)
{
  int was = unit_begin (texunit);
  glCompressedTexSubImage2D (target, level, xoffset, yoffset, width, height, format, imageSize, bits);
  unit_end (was);
}

void
glCompressedMultiTexSubImage3DEXT (GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void * bits)
{
  int was = unit_begin (texunit);
  glCompressedTexSubImage3D (target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, bits);
  unit_end (was);
}

void
glGetCompressedMultiTexImageEXT (GLenum texunit, GLenum target, GLint lod, void * img)
{
  int was = unit_begin (texunit);
  glGetCompressedTexImage (target, lod, img);
  unit_end (was);
}

/* --- matrices: the kept stacks (glMatrixMode for the call) ---------- */

void
glMatrixLoadfEXT (GLenum mode, const GLfloat * m)
{
  struct mat_was w = matrix_begin (mode);
  glLoadMatrixf (m);
  matrix_end (w);
}

void
glMatrixLoaddEXT (GLenum mode, const GLdouble * m)
{
  struct mat_was w = matrix_begin (mode);
  glLoadMatrixd (m);
  matrix_end (w);
}

void
glMatrixMultfEXT (GLenum mode, const GLfloat * m)
{
  struct mat_was w = matrix_begin (mode);
  glMultMatrixf (m);
  matrix_end (w);
}

void
glMatrixMultdEXT (GLenum mode, const GLdouble * m)
{
  struct mat_was w = matrix_begin (mode);
  glMultMatrixd (m);
  matrix_end (w);
}

void
glMatrixLoadIdentityEXT (GLenum mode)
{
  struct mat_was w = matrix_begin (mode);
  glLoadIdentity ();
  matrix_end (w);
}

void
glMatrixRotatefEXT (GLenum mode, GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
  struct mat_was w = matrix_begin (mode);
  glRotatef (angle, x, y, z);
  matrix_end (w);
}

void
glMatrixRotatedEXT (GLenum mode, GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
  struct mat_was w = matrix_begin (mode);
  glRotated (angle, x, y, z);
  matrix_end (w);
}

void
glMatrixScalefEXT (GLenum mode, GLfloat x, GLfloat y, GLfloat z)
{
  struct mat_was w = matrix_begin (mode);
  glScalef (x, y, z);
  matrix_end (w);
}

void
glMatrixScaledEXT (GLenum mode, GLdouble x, GLdouble y, GLdouble z)
{
  struct mat_was w = matrix_begin (mode);
  glScaled (x, y, z);
  matrix_end (w);
}

void
glMatrixTranslatefEXT (GLenum mode, GLfloat x, GLfloat y, GLfloat z)
{
  struct mat_was w = matrix_begin (mode);
  glTranslatef (x, y, z);
  matrix_end (w);
}

void
glMatrixTranslatedEXT (GLenum mode, GLdouble x, GLdouble y, GLdouble z)
{
  struct mat_was w = matrix_begin (mode);
  glTranslated (x, y, z);
  matrix_end (w);
}

void
glMatrixFrustumEXT (GLenum mode, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
  struct mat_was w = matrix_begin (mode);
  glFrustum (left, right, bottom, top, zNear, zFar);
  matrix_end (w);
}

void
glMatrixOrthoEXT (GLenum mode, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
  struct mat_was w = matrix_begin (mode);
  glOrtho (left, right, bottom, top, zNear, zFar);
  matrix_end (w);
}

void
glMatrixPopEXT (GLenum mode)
{
  struct mat_was w = matrix_begin (mode);
  glPopMatrix ();
  matrix_end (w);
}

void
glMatrixPushEXT (GLenum mode)
{
  struct mat_was w = matrix_begin (mode);
  glPushMatrix ();
  matrix_end (w);
}

void
glMatrixLoadTransposefEXT (GLenum mode, const GLfloat * m)
{
  struct mat_was w = matrix_begin (mode);
  glLoadTransposeMatrixf (m);
  matrix_end (w);
}

void
glMatrixLoadTransposedEXT (GLenum mode, const GLdouble * m)
{
  struct mat_was w = matrix_begin (mode);
  glLoadTransposeMatrixd (m);
  matrix_end (w);
}

void
glMatrixMultTransposefEXT (GLenum mode, const GLfloat * m)
{
  struct mat_was w = matrix_begin (mode);
  glMultTransposeMatrixf (m);
  matrix_end (w);
}

void
glMatrixMultTransposedEXT (GLenum mode, const GLdouble * m)
{
  struct mat_was w = matrix_begin (mode);
  glMultTransposeMatrixd (m);
  matrix_end (w);
}

/* --- a vertex array object's arrays: it bound, and the buffer -------- */

void
glVertexArrayVertexOffsetEXT (GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset)
{
  struct vao_was w = vao_begin (vaobj, buffer);
  glVertexPointer (size, type, stride, (const void *) offset);
  vao_end (w);
}

void
glVertexArrayColorOffsetEXT (GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset)
{
  struct vao_was w = vao_begin (vaobj, buffer);
  glColorPointer (size, type, stride, (const void *) offset);
  vao_end (w);
}

void
glVertexArrayEdgeFlagOffsetEXT (GLuint vaobj, GLuint buffer, GLsizei stride, GLintptr offset)
{
  struct vao_was w = vao_begin (vaobj, buffer);
  glEdgeFlagPointer (stride, (const void *) offset);
  vao_end (w);
}

void
glVertexArrayIndexOffsetEXT (GLuint vaobj, GLuint buffer, GLenum type, GLsizei stride, GLintptr offset)
{
  struct vao_was w = vao_begin (vaobj, buffer);
  glIndexPointer (type, stride, (const void *) offset);
  vao_end (w);
}

void
glVertexArrayNormalOffsetEXT (GLuint vaobj, GLuint buffer, GLenum type, GLsizei stride, GLintptr offset)
{
  struct vao_was w = vao_begin (vaobj, buffer);
  glNormalPointer (type, stride, (const void *) offset);
  vao_end (w);
}

void
glVertexArrayTexCoordOffsetEXT (GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset)
{
  struct vao_was w = vao_begin (vaobj, buffer);
  glTexCoordPointer (size, type, stride, (const void *) offset);
  vao_end (w);
}

void
glVertexArrayFogCoordOffsetEXT (GLuint vaobj, GLuint buffer, GLenum type, GLsizei stride, GLintptr offset)
{
  struct vao_was w = vao_begin (vaobj, buffer);
  glFogCoordPointer (type, stride, (const void *) offset);
  vao_end (w);
}

void
glVertexArraySecondaryColorOffsetEXT (GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset)
{
  struct vao_was w = vao_begin (vaobj, buffer);
  glSecondaryColorPointer (size, type, stride, (const void *) offset);
  vao_end (w);
}

void
glVertexArrayVertexAttribOffsetEXT (GLuint vaobj, GLuint buffer, GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, GLintptr offset)
{
  struct vao_was w = vao_begin (vaobj, buffer);
  glVertexAttribPointer (index, size, type, normalized, stride, (const void *) offset);
  vao_end (w);
}

void
glVertexArrayVertexAttribIOffsetEXT (GLuint vaobj, GLuint buffer, GLuint index, GLint size, GLenum type, GLsizei stride, GLintptr offset)
{
  struct vao_was w = vao_begin (vaobj, buffer);
  glVertexAttribIPointer (index, size, type, stride, (const void *) offset);
  vao_end (w);
}

void
glVertexArrayMultiTexCoordOffsetEXT (GLuint vaobj, GLuint buffer, GLenum texunit, GLint size, GLenum type, GLsizei stride, GLintptr offset)
{
  struct vao_was w = vao_begin (vaobj, buffer);
  int was = client_unit_begin (texunit);
  glTexCoordPointer (size, type, stride, (const void *) offset);
  client_unit_end (was);
  vao_end (w);
}

/* --- the client's arrays by unit, and their defaults ------------------- */

void
glMultiTexCoordPointerEXT (GLenum texunit, GLint size, GLenum type, GLsizei stride, const void *pointer)
{
  int was = client_unit_begin (texunit);
  glTexCoordPointer (size, type, stride, pointer);
  client_unit_end (was);
}

static void
client_state_indexed (GLenum array, GLuint index, int on)
{
  int was = -1;
  if (array == GL_TEXTURE_COORD_ARRAY)
    was = client_unit_begin (GL_TEXTURE0 + index);
  if (on)
    glEnableClientState (array);
  else
    glDisableClientState (array);
  if (was >= 0)
    client_unit_end (was);
}

void glEnableClientStateIndexedEXT (GLenum array, GLuint index) { client_state_indexed (array, index, 1); }
void glDisableClientStateIndexedEXT (GLenum array, GLuint index) { client_state_indexed (array, index, 0); }
void glEnableClientStateiEXT (GLenum array, GLuint index) { client_state_indexed (array, index, 1); }
void glDisableClientStateiEXT (GLenum array, GLuint index) { client_state_indexed (array, index, 0); }

void
glGetPointerIndexedvEXT (GLenum target, GLuint index, void **data)
{
  int was = client_unit_begin (GL_TEXTURE0 + index);
  glGetPointerv (target, data);
  client_unit_end (was);
}

void glGetPointeri_vEXT (GLenum pname, GLuint index, void **params) { glGetPointerIndexedvEXT (pname, index, params); }

/* back to the defaults: the state kept here too */
static void
r3d_track_glClientAttribDefaultEXT (GLbitfield mask)
{
  if (mask & GL_CLIENT_PIXEL_STORE_BIT)
    {
      memset (&cur->pack, 0, sizeof cur->pack);
      memset (&cur->unpack, 0, sizeof cur->unpack);
      cur->pack.alignment = cur->unpack.alignment = 4;
      cur->pack_buffer = cur->unpack_buffer = 0;
    }
  if (mask & GL_CLIENT_VERTEX_ARRAY_BIT)
    {
      array_defaults (cur->attribs, cur->fixed);
      cur->client_unit = 0;
      cur->array_buffer = 0;
    }
}

void
glPushClientAttribDefaultEXT (GLbitfield mask)
{
  glPushClientAttrib (mask);
  glClientAttribDefaultEXT (mask);
}

/* --- buffers by name: bound as GL_COPY_WRITE_BUFFER for the call; a
   mapping is kept under the name's own key ----------------------------- */

#define NAMED_MAP(b) (0x80000000u | (b))

void
glNamedBufferDataEXT (GLuint buffer, GLsizeiptr size, const void *data, GLenum usage)
{
  temp_buffer (buffer);
  glBufferData (GL_COPY_WRITE_BUFFER, size, data, usage);
  temp_end ();
}

void
glNamedBufferSubDataEXT (GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data)
{
  temp_buffer (buffer);
  glBufferSubData (GL_COPY_WRITE_BUFFER, offset, size, data);
  temp_end ();
}

void
glGetNamedBufferSubDataEXT (GLuint buffer, GLintptr offset, GLsizeiptr size, void *data)
{
  temp_buffer (buffer);
  if (!read_buffer (GL_COPY_WRITE_BUFFER, offset, (size_t) size, data))
    memset (data, 0, (size_t) (size > 0 ? size : 0));
  temp_end ();
}

void
glClearNamedBufferDataEXT (GLuint buffer, GLenum internalformat, GLenum format, GLenum type, const void *data)
{
  temp_buffer (buffer);
  glClearBufferData (GL_COPY_WRITE_BUFFER, internalformat, format, type, data);
  temp_end ();
}

void
glClearNamedBufferSubDataEXT (GLuint buffer, GLenum internalformat, GLsizeiptr offset, GLsizeiptr size, GLenum format,
			      GLenum type, const void *data)
{
  temp_buffer (buffer);
  glClearBufferSubData (GL_COPY_WRITE_BUFFER, internalformat, (GLintptr) offset, size, format, type, data);
  temp_end ();
}

/* (a mapping under GL_COPY_WRITE_BUFFER is renamed to the buffer's key) */
static void *
named_mapped (GLuint buffer, void *p)
{
  struct mapping *m = mapping_of (GL_COPY_WRITE_BUFFER, 0);
  if (p && m && m->ptr == p)
    m->target = NAMED_MAP (buffer);
  temp_end ();
  return p;
}

void *
glMapNamedBufferEXT (GLuint buffer, GLenum access)
{
  if (mapping_of (NAMED_MAP (buffer), 0) || mapping_of (GL_COPY_WRITE_BUFFER, 0))
    return 0;
  temp_buffer (buffer);
  return named_mapped (buffer, glMapBuffer (GL_COPY_WRITE_BUFFER, access));
}

void *
glMapNamedBufferRangeEXT (GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access)
{
  if (mapping_of (NAMED_MAP (buffer), 0) || mapping_of (GL_COPY_WRITE_BUFFER, 0))
    return 0;
  temp_buffer (buffer);
  return named_mapped (buffer, glMapBufferRange (GL_COPY_WRITE_BUFFER, offset, length, access));
}

void
glFlushMappedNamedBufferRangeEXT (GLuint buffer, GLintptr offset, GLsizeiptr length)
{
  struct mapping *m = mapping_of (NAMED_MAP (buffer), 0);
  if (!m || mapping_of (GL_COPY_WRITE_BUFFER, 0))
    return;
  m->target = GL_COPY_WRITE_BUFFER;
  temp_buffer (buffer);
  glFlushMappedBufferRange (GL_COPY_WRITE_BUFFER, offset, length);
  temp_end ();
  m->target = NAMED_MAP (buffer);
}

GLboolean
glUnmapNamedBufferEXT (GLuint buffer)
{
  struct mapping *m = mapping_of (NAMED_MAP (buffer), 0);
  if (!m || mapping_of (GL_COPY_WRITE_BUFFER, 0))
    return GL_FALSE;
  m->target = GL_COPY_WRITE_BUFFER;
  temp_buffer (buffer);
  GLboolean r = glUnmapBuffer (GL_COPY_WRITE_BUFFER);
  temp_end ();
  return r;
}

void
glGetNamedBufferPointervEXT (GLuint buffer, GLenum pname, void **params)
{
  struct mapping *m = mapping_of (NAMED_MAP (buffer), 0);
  if (params)
    *params = pname == GL_BUFFER_MAP_POINTER && m ? m->ptr : 0;
}

/* --- vertex array objects' state kept here ------------------------------ */

static void
vertex_array (GLuint vaobj, GLenum array, int on)
{
  struct vao_was w = vao_begin (vaobj, cur->array_buffer);
  if (array >= GL_TEXTURE0 && array < GL_TEXTURE0 + 32)
    {
      int was = client_unit_begin (array);
      client_state_indexed (GL_TEXTURE_COORD_ARRAY, array - GL_TEXTURE0, on);
      client_unit_end (was);
    }
  else if (on)
    glEnableClientState (array);
  else
    glDisableClientState (array);
  vao_end (w);
}

void glEnableVertexArrayEXT (GLuint vaobj, GLenum array) { vertex_array (vaobj, array, 1); }
void glDisableVertexArrayEXT (GLuint vaobj, GLenum array) { vertex_array (vaobj, array, 0); }

void
glEnableVertexArrayAttribEXT (GLuint vaobj, GLuint index)
{
  struct vao_was w = vao_begin (vaobj, cur->array_buffer);
  glEnableVertexAttribArray (index);
  vao_end (w);
}

void
glDisableVertexArrayAttribEXT (GLuint vaobj, GLuint index)
{
  struct vao_was w = vao_begin (vaobj, cur->array_buffer);
  glDisableVertexAttribArray (index);
  vao_end (w);
}

void
glVertexArrayVertexAttribDivisorEXT (GLuint vaobj, GLuint index, GLuint divisor)
{
  struct vao_was w = vao_begin (vaobj, cur->array_buffer);
  glVertexAttribDivisor (index, divisor);
  vao_end (w);
}

void
glGetVertexArrayPointervEXT (GLuint vaobj, GLenum pname, void **param)
{
  struct vao_was w = vao_begin (vaobj, cur->array_buffer);
  glGetPointerv (pname, param);
  vao_end (w);
}

void
glGetVertexArrayPointeri_vEXT (GLuint vaobj, GLuint index, GLenum pname, void **param)
{
  struct vao_was w = vao_begin (vaobj, cur->array_buffer);
  if (pname == GL_VERTEX_ATTRIB_ARRAY_POINTER)
    glGetVertexAttribPointerv (index, pname, param);
  else
    glGetPointerIndexedvEXT (pname, index, param);
  vao_end (w);
}

/* --- a named program's text, as long as it says ------------------------- */

void
glGetNamedProgramStringEXT (GLuint program, GLenum target, GLenum pname, void *string)
{
  GLint n = 0;
  glGetNamedProgramivEXT (program, target, 0x8627 /* GL_PROGRAM_LENGTH_ARB */, &n);
  if (n <= 0 || !string)
    return;
  uint32_t *a = r3d_put (OP_glGetNamedProgramStringEXT, 4);
  if (!a)
    return;
  a[0] = program;
  a[1] = target;
  a[2] = pname;
  a[3] = (uint32_t) n;
  r3d_sync (string, (size_t) n);
}
