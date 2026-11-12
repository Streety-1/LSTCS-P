/*
 * Copyright (C) 2024 Jan Smialkowski
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SOLOADER_GL_H
#define SOLOADER_GL_H

#include <stdint.h>
#include <vitaGL.h>

#ifdef __cplusplus
extern "C" {
#endif

void glActiveTexture_soloader(GLenum texture);

void glBindBuffer_soloader(GLenum target, GLuint buffer);

void glBindTexture_soloader(GLenum target, GLuint texture);

void glBufferData_soloader(GLenum target, GLsizei size, const void *data, GLenum usage);

void glBufferSubData_soloader(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);

void glCompileShader_soloader(GLuint shader);
void glClear_soloader(GLbitfield mask);
void glDrawArrays_soloader(GLenum mode, GLint first, GLsizei count);
void glDrawElements_soloader(GLenum mode, GLsizei count, GLenum type, const void *indices);
extern volatile unsigned int lstcs_skipped_fbo_ops;

typedef struct {
    unsigned int uploads, compressed, npot, other_format;
    unsigned int mips_dropped;
    uint64_t raw_bytes, stored_bytes, compress_us;
} lstcs_tex_stats_t;
extern lstcs_tex_stats_t lstcs_tex_stats;
GLuint glCreateShader_soloader(GLenum shaderType);

void glCompressedTexImage2D_soloader(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height,
                                     GLint border, GLsizei imageSize, const void *data);

void glCopyTexImage2D_soloader(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width,
                               GLsizei height, GLint border);

void glGenerateMipmap_soloader(GLenum target);

void glGetIntegerv_soloader(GLenum pname, GLint *data);

const GLubyte *glGetString_soloader(GLenum name);

GLint glGetUniformLocation_soloader(GLuint program, const GLchar *name);

void glLinkProgram_soloader(GLuint program);

void glShaderSource_soloader(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);

void glTexImage2D_soloader(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                           GLint border, GLenum format, GLenum type, const void *pixels);

void glTexParameteri_soloader(GLenum target, GLenum pname, GLint param);

void glUniform1fv_soloader(GLint location, GLsizei count, const GLfloat *value);

void glUniform1i_soloader(GLint location, GLint v0);

void glUniform2fv_soloader(GLint location, GLsizei count, const GLfloat *value);

void glUniform3fv_soloader(GLint location, GLsizei count, const GLfloat *value);

void glUniform4fv_soloader(GLint location, GLsizei count, const GLfloat *value);

void glUseProgram_soloader(GLuint program);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_EGL_H
