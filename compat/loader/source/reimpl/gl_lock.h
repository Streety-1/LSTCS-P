#ifndef SOLOADER_GL_LOCK_H
#define SOLOADER_GL_LOCK_H

#include <vitaGL.h>

void gl_lock(void);
void gl_unlock(void);

void lk_glActiveTexture(GLenum texture);
void lk_glAttachShader(GLuint prog, GLuint shad);
void lk_glBindAttribLocation(GLuint program, GLuint index, const GLchar *name);
void lk_glBindBuffer(GLenum target, GLuint buffer);
void lk_glBindFramebuffer(GLenum target, GLuint framebuffer);
void lk_glBindTexture(GLenum target, GLuint texture);
void lk_glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);
void lk_glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
void lk_glBufferData(GLenum target, GLsizei size, const GLvoid *data, GLenum usage);
void lk_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
GLenum lk_glCheckFramebufferStatus(GLenum target);
void lk_glClear(GLbitfield mask);
void lk_glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void lk_glCompileShader(GLuint shader);
void lk_glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data);
void lk_glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
GLuint lk_glCreateProgram(void);
GLuint lk_glCreateShader(GLenum shaderType);
void lk_glCullFace(GLenum mode);
void lk_glDeleteBuffers(GLsizei n, const GLuint *gl_buffers);
void lk_glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers);
void lk_glDeleteProgram(GLuint prog);
void lk_glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers);
void lk_glDeleteShader(GLuint shad);
void lk_glDeleteTextures(GLsizei n, const GLuint *textures);
void lk_glDepthFunc(GLenum func);
void lk_glDepthMask(GLboolean flag);
void lk_glDisable(GLenum cap);
void lk_glDisableVertexAttribArray(GLuint index);
void lk_glDrawArrays(GLenum mode, GLint first, GLsizei count);
void lk_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void lk_glEnable(GLenum cap);
void lk_glEnableVertexAttribArray(GLuint index);
void lk_glFinish(void);
void lk_glFlush(void);
void lk_glFrontFace(GLenum mode);
void lk_glGenBuffers(GLsizei n, GLuint *buffers);
void lk_glGenerateMipmap(GLenum target);
void lk_glGenFramebuffers(GLsizei n, GLuint *framebuffers);
void lk_glGenTextures(GLsizei n, GLuint *textures);
void lk_glGetActiveAttrib(GLuint prog, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
void lk_glGetActiveUniform(GLuint prog, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
void lk_glGetAttachedShaders(GLuint prog, GLsizei maxCount, GLsizei *count, GLuint *shads);
GLint lk_glGetAttribLocation(GLuint prog, const GLchar *name);
GLenum lk_glGetError(void);
void lk_glGetIntegerv(GLenum pname, GLint *data);
void lk_glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
void lk_glGetProgramiv(GLuint program, GLenum pname, GLint *params);
void lk_glGetShaderSource(GLuint handle, GLsizei bufSize, GLsizei *length, GLchar *source);
void lk_glGetShaderiv(GLuint handle, GLenum pname, GLint *params);
const GLubyte * lk_glGetString(GLenum name);
GLint lk_glGetUniformLocation(GLuint prog, const GLchar *name);
void lk_glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params);
void lk_glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer);
void lk_glLinkProgram(GLuint progr);
void lk_glReleaseShaderCompiler(void);
void lk_glShaderSource(GLuint handle, GLsizei count, const GLchar *const *string, const GLint *length);
void lk_glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *data);
void lk_glTexParameteri(GLenum target, GLenum pname, GLint param);
void lk_glUniform1fv(GLint location, GLsizei count, const GLfloat *value);
void lk_glUniform1i(GLint location, GLint v0);
void lk_glUniform2fv(GLint location, GLsizei count, const GLfloat *value);
void lk_glUniform3fv(GLint location, GLsizei count, const GLfloat *value);
void lk_glUniform4fv(GLint location, GLsizei count, const GLfloat *value);
void lk_glUseProgram(GLuint program);
void lk_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
void lk_glViewport(GLint x, GLint y, GLsizei width, GLsizei height);

#endif
