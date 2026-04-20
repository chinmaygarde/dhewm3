/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

/*
** QGL_PROC_ES3.H
**
** OpenGL ES 3.0 core function pointer declarations. Loaded at runtime via
** SDL_GL_GetProcAddress — no libGLESv2 linkage required.
**
** Only contains functions absent from qgl_proc.h (which covers desktop GL 1.x)
** or whose GLES3 core names differ from the ARB-suffixed variants in qgl.h.
**
** Include pattern (same as qgl_proc.h):
**   #define QGLPROC(name, rettype, args)  ...use name, rettype, args...
**   #include "renderer/qgl_proc_es3.h"
**   // QGLPROC is #undef'd at the bottom of this file
*/

#ifndef QGLPROC
#error "define QGLPROC before including this file"
#endif

/* ---- Shader management ---- */
QGLPROC(glCreateShader, GLuint, (GLenum type))
QGLPROC(glShaderSource, void, (GLuint shader, GLsizei count, const GLchar **string, const GLint *length))
QGLPROC(glCompileShader, void, (GLuint shader))
QGLPROC(glDeleteShader, void, (GLuint shader))
QGLPROC(glIsShader, GLboolean, (GLuint shader))
QGLPROC(glGetShaderiv, void, (GLuint shader, GLenum pname, GLint *params))
QGLPROC(glGetShaderInfoLog, void, (GLuint shader, GLsizei bufsize, GLsizei *length, GLchar *infolog))
QGLPROC(glGetShaderSource, void, (GLuint shader, GLsizei bufsize, GLsizei *length, GLchar *source))
QGLPROC(glReleaseShaderCompiler, void, (void))
QGLPROC(glShaderBinary, void, (GLsizei n, const GLuint *shaders, GLenum binaryformat, const GLvoid *binary, GLsizei length))
QGLPROC(glGetShaderPrecisionFormat, void, (GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision))

/* ---- Program management ---- */
QGLPROC(glCreateProgram, GLuint, (void))
QGLPROC(glAttachShader, void, (GLuint program, GLuint shader))
QGLPROC(glDetachShader, void, (GLuint program, GLuint shader))
QGLPROC(glLinkProgram, void, (GLuint program))
QGLPROC(glUseProgram, void, (GLuint program))
QGLPROC(glDeleteProgram, void, (GLuint program))
QGLPROC(glIsProgram, GLboolean, (GLuint program))
QGLPROC(glValidateProgram, void, (GLuint program))
QGLPROC(glGetProgramiv, void, (GLuint program, GLenum pname, GLint *params))
QGLPROC(glGetProgramInfoLog, void, (GLuint program, GLsizei bufsize, GLsizei *length, GLchar *infolog))
QGLPROC(glGetAttachedShaders, void, (GLuint program, GLsizei maxcount, GLsizei *count, GLuint *shaders))

/* ---- Uniforms ---- */
QGLPROC(glGetUniformLocation, GLint, (GLuint program, const GLchar *name))
QGLPROC(glGetActiveUniform, void, (GLuint program, GLuint index, GLsizei bufsize, GLsizei *length, GLint *size, GLenum *type, GLchar *name))
QGLPROC(glGetUniformfv, void, (GLuint program, GLint location, GLfloat *params))
QGLPROC(glGetUniformiv, void, (GLuint program, GLint location, GLint *params))
QGLPROC(glUniform1f, void, (GLint location, GLfloat x))
QGLPROC(glUniform1fv, void, (GLint location, GLsizei count, const GLfloat *v))
QGLPROC(glUniform1i, void, (GLint location, GLint x))
QGLPROC(glUniform1iv, void, (GLint location, GLsizei count, const GLint *v))
QGLPROC(glUniform2f, void, (GLint location, GLfloat x, GLfloat y))
QGLPROC(glUniform2fv, void, (GLint location, GLsizei count, const GLfloat *v))
QGLPROC(glUniform2i, void, (GLint location, GLint x, GLint y))
QGLPROC(glUniform2iv, void, (GLint location, GLsizei count, const GLint *v))
QGLPROC(glUniform3f, void, (GLint location, GLfloat x, GLfloat y, GLfloat z))
QGLPROC(glUniform3fv, void, (GLint location, GLsizei count, const GLfloat *v))
QGLPROC(glUniform3i, void, (GLint location, GLint x, GLint y, GLint z))
QGLPROC(glUniform3iv, void, (GLint location, GLsizei count, const GLint *v))
QGLPROC(glUniform4f, void, (GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w))
QGLPROC(glUniform4fv, void, (GLint location, GLsizei count, const GLfloat *v))
QGLPROC(glUniform4i, void, (GLint location, GLint x, GLint y, GLint z, GLint w))
QGLPROC(glUniform4iv, void, (GLint location, GLsizei count, const GLint *v))
QGLPROC(glUniformMatrix2fv, void, (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value))
QGLPROC(glUniformMatrix3fv, void, (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value))
QGLPROC(glUniformMatrix4fv, void, (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value))

/* ---- Vertex attributes (core GLES2; qgl.h has ARB-suffixed variants only) ---- */
QGLPROC(glBindAttribLocation, void, (GLuint program, GLuint index, const GLchar *name))
QGLPROC(glGetAttribLocation, GLint, (GLuint program, const GLchar *name))
QGLPROC(glGetActiveAttrib, void, (GLuint program, GLuint index, GLsizei bufsize, GLsizei *length, GLint *size, GLenum *type, GLchar *name))
QGLPROC(glVertexAttrib1f, void, (GLuint indx, GLfloat x))
QGLPROC(glVertexAttrib1fv, void, (GLuint indx, const GLfloat *values))
QGLPROC(glVertexAttrib2f, void, (GLuint indx, GLfloat x, GLfloat y))
QGLPROC(glVertexAttrib2fv, void, (GLuint indx, const GLfloat *values))
QGLPROC(glVertexAttrib3f, void, (GLuint indx, GLfloat x, GLfloat y, GLfloat z))
QGLPROC(glVertexAttrib3fv, void, (GLuint indx, const GLfloat *values))
QGLPROC(glVertexAttrib4f, void, (GLuint indx, GLfloat x, GLfloat y, GLfloat z, GLfloat w))
QGLPROC(glVertexAttrib4fv, void, (GLuint indx, const GLfloat *values))
QGLPROC(glVertexAttribPointer, void, (GLuint indx, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *ptr))
QGLPROC(glEnableVertexAttribArray, void, (GLuint index))
QGLPROC(glDisableVertexAttribArray, void, (GLuint index))
QGLPROC(glGetVertexAttribfv, void, (GLuint index, GLenum pname, GLfloat *params))
QGLPROC(glGetVertexAttribiv, void, (GLuint index, GLenum pname, GLint *params))
QGLPROC(glGetVertexAttribPointerv, void, (GLuint index, GLenum pname, GLvoid **pointer))

/* ---- Buffer objects (core GLES2; qgl.h has ARB-suffixed variants only) ---- */
QGLPROC(glBindBuffer, void, (GLenum target, GLuint buffer))
QGLPROC(glGenBuffers, void, (GLsizei n, GLuint *buffers))
QGLPROC(glDeleteBuffers, void, (GLsizei n, const GLuint *buffers))
QGLPROC(glIsBuffer, GLboolean, (GLuint buffer))
QGLPROC(glBufferData, void, (GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage))
QGLPROC(glBufferSubData, void, (GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data))
QGLPROC(glGetBufferParameteriv, void, (GLenum target, GLenum pname, GLint *params))

/* ---- Framebuffer objects (GLES2 core) ---- */
QGLPROC(glBindFramebuffer, void, (GLenum target, GLuint framebuffer))
QGLPROC(glGenFramebuffers, void, (GLsizei n, GLuint *framebuffers))
QGLPROC(glDeleteFramebuffers, void, (GLsizei n, const GLuint *framebuffers))
QGLPROC(glFramebufferTexture2D, void, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level))
QGLPROC(glFramebufferRenderbuffer, void, (GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer))
QGLPROC(glCheckFramebufferStatus, GLenum, (GLenum target))
QGLPROC(glGetFramebufferAttachmentParameteriv, void, (GLenum target, GLenum attachment, GLenum pname, GLint *params))
QGLPROC(glIsFramebuffer, GLboolean, (GLuint framebuffer))
QGLPROC(glBindRenderbuffer, void, (GLenum target, GLuint renderbuffer))
QGLPROC(glGenRenderbuffers, void, (GLsizei n, GLuint *renderbuffers))
QGLPROC(glDeleteRenderbuffers, void, (GLsizei n, const GLuint *renderbuffers))
QGLPROC(glRenderbufferStorage, void, (GLenum target, GLenum internalformat, GLsizei width, GLsizei height))
QGLPROC(glGetRenderbufferParameteriv, void, (GLenum target, GLenum pname, GLint *params))
QGLPROC(glIsRenderbuffer, GLboolean, (GLuint renderbuffer))

/* ---- Texture (core GLES2 names; qgl.h has ARB-suffixed variants) ---- */
QGLPROC(glActiveTexture, void, (GLenum texture))
QGLPROC(glGenerateMipmap, void, (GLenum target))
QGLPROC(glCompressedTexImage2D, void, (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data))
QGLPROC(glCompressedTexSubImage2D, void, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data))

/* ---- Blend / stencil (GLES2 core; not in qgl_proc.h) ---- */
QGLPROC(glBlendFuncSeparate, void, (GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha))
QGLPROC(glBlendEquationSeparate, void, (GLenum modeRGB, GLenum modeAlpha))
QGLPROC(glBlendColor, void, (GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha))
QGLPROC(glSampleCoverage, void, (GLclampf value, GLboolean invert))
QGLPROC(glStencilFuncSeparate, void, (GLenum face, GLenum func, GLint ref, GLuint mask))
QGLPROC(glStencilMaskSeparate, void, (GLenum face, GLuint mask))

/* ---- GLES2 float variants (desktop GL uses double for these) ---- */
QGLPROC(glClearDepthf, void, (GLclampf depth))
QGLPROC(glDepthRangef, void, (GLclampf zNear, GLclampf zFar))

/* ---- GLES3 additions ---- */
/* Vertex Array Objects (core in ES 3.0; were OES extension in ES 2.0) */
QGLPROC(glGenVertexArrays, void, (GLsizei n, GLuint *arrays))
QGLPROC(glBindVertexArray, void, (GLuint array))
QGLPROC(glDeleteVertexArrays, void, (GLsizei n, const GLuint *arrays))
QGLPROC(glIsVertexArray, GLboolean, (GLuint array))
/* Multiple render targets */
QGLPROC(glDrawBuffers, void, (GLsizei n, const GLenum *bufs))
/* Buffer mapping (core in ES 3.0) */
QGLPROC(glMapBufferRange, void *, (GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access))
QGLPROC(glUnmapBuffer, GLboolean, (GLenum target))
QGLPROC(glFlushMappedBufferRange, void, (GLenum target, GLintptr offset, GLsizeiptr length))

#undef QGLPROC
