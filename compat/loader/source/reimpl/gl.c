/*
 * Copyright (C) 2024 Jan Smialkowski
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/gl.h"

#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/utils.h"

#include <psp2/kernel/clib.h>
#include <stdlib.h>
#include <psp2/kernel/processmgr.h>

#define PROGRAM_UNIFORM_SPEEDHACK
//#define SKIP_ERROR_HANDLING

extern _Thread_local _egl_context *current_context;
extern GLint uniforms[GLUTIL_MAX_PROGRAMS * GLUTIL_MAX_UNIFORMS];

GLint find_uniform(GLuint p, GLint u);

void glActiveTexture_soloader(GLenum texture) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_context] glActiveTexture<%p>(texture: %u)\n", __builtin_return_address(0), texture);
#endif
        return;
    }
#endif

    current_context->texture = texture - GL_TEXTURE0;
    if (current_context->is_pbuffer == EGL_FALSE) {
        glActiveTexture(texture);
    }
}

void glBindBuffer_soloader(GLenum target, GLuint buffer) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_context] glBindBuffer<%p>(target: %u, buffer: %u)\n", __builtin_return_address(0),
                      target, buffer);
#endif
        return;
    }
#endif

    current_context->buffers[target - GL_ARRAY_BUFFER] = buffer;
    if (current_context->is_pbuffer == EGL_FALSE) {
        glBindBuffer(target, buffer);
    }
}

void glBindTexture_soloader(GLenum target, GLuint texture) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_context] glBindTexture<%p>(target: %u, texture: %u)\n", __builtin_return_address(0),
                      target, texture);
#endif
        return;
    }
#endif

    const size_t i = target == GL_TEXTURE_2D ? 0 : 1;
    current_context->textures[current_context->texture * 2 + i] = texture;
    if (current_context->is_pbuffer == EGL_FALSE) {
        glBindTexture(target, texture);
    }
}

void glBufferData_soloader(GLenum target, GLsizei size, const void *data, GLenum usage) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_context] glBufferData<%p>(target: %u, size: %i, data: %p, usage: %u)\n",
                      __builtin_return_address(0), target, size, data, usage);
#endif
        return;
    }
#endif

    glNamedBufferData(current_context->buffers[target - GL_ARRAY_BUFFER], size, data, usage);
}

void glBufferSubData_soloader(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_context] glBufferSubData<%p>(target: %u, offset: %i, size: %i, data: %p)\n",
                      __builtin_return_address(0), target, offset, size, data);
#endif
        return;
    }
#endif

    glNamedBufferSubData(current_context->buffers[target - GL_ARRAY_BUFFER], offset, size, data);
}

/*
 * Current vitaGL's direct-state texture functions (glTextureImage2D & co.) take only a
 * texture id and handle plain 2D textures. The original port was built against a vitaGL
 * that also took a target, so cube-map faces could be uploaded the same way. For cube
 * maps we briefly bind the texture, use the classic call with the face target, and then
 * restore the previous binding so the loader's own texture tracking is undisturbed.
 */
#define WITH_CUBE_MAP_BOUND(tex, call) do { \
        GLint prev_cube_ = 0; \
        glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &prev_cube_); \
        glBindTexture(GL_TEXTURE_CUBE_MAP, (tex)); \
        call; \
        glBindTexture(GL_TEXTURE_CUBE_MAP, (GLuint) prev_cube_); \
    } while (0)

void glCompressedTexImage2D_soloader(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height,
                                     GLint border, GLsizei imageSize, const void *data) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf(
            "[gl_dbg][no_context] glCompressedTexImage2D<%p>(target: %u, level: %i, internalformat: %u, width: %i, height: %i, border: %i, imageSize: %i, data: %p)\n",
            __builtin_return_address(0), target, level, internalformat, width, height, border, imageSize, data);
#endif
        return;
    }
#endif

    const size_t i = target == GL_TEXTURE_2D ? 0 : 1;
    const GLuint tex = current_context->textures[current_context->texture * 2 + i];
    if (i == 0)
        glCompressedTextureImage2D(tex, level, internalformat, width, height, border, imageSize, data);
    else
        WITH_CUBE_MAP_BOUND(tex, glCompressedTexImage2D(target, level, internalformat, width, height, border,
                                                        imageSize, data));
}

void glCopyTexImage2D_soloader(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width,
                               GLsizei height, GLint border) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf(
            "[gl_dbg][no_context] glCopyTexImage2D<%p>(target: %u, level: %i, internalformat: %u, x: %i, y: %i, width: %i, height: %i, border: %i)\n",
            __builtin_return_address(0), target, level, internalformat, x, y, width, height, border);
#endif
        return;
    }
#endif

    if (current_context->is_pbuffer == EGL_FALSE) {
        glCopyTexImage2D(target, level, internalformat, x, y, width, height, border);
    } else {
        l_error("glCopyTexImage2D called from a pbuffer context");
        //glCopyTexImage2D(target, level, internalformat, x, y, width, height, border);
    }
}

void glGenerateMipmap_soloader(GLenum target) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_context] glGenerateMipmap<%p>(target: %u)\n", __builtin_return_address(0), target);
#endif
        return;
    }
#endif

    const size_t i = target == GL_TEXTURE_2D ? 0 : 1;
    glGenerateTextureMipmap(current_context->textures[current_context->texture * 2 + i]);
}

void glGetIntegerv_soloader(GLenum pname, GLint *data) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_context] glGetIntegerv<%p>(pname: %u, data: %p)\n", __builtin_return_address(0),
                      pname, data);
#endif
        return;
    }
#endif

    switch (pname) {
        case GL_ACTIVE_TEXTURE:
            *data = current_context->texture + GL_TEXTURE0;
            return;
        case GL_ARRAY_BUFFER_BINDING:
            *data = current_context->buffers[0];
            return;
        case GL_CURRENT_PROGRAM:
            *data = current_context->program + 1;
            return;
        case GL_ELEMENT_ARRAY_BUFFER_BINDING:
            *data = current_context->buffers[1];
            return;
        case GL_TEXTURE_BINDING_2D:
            *data = current_context->textures[current_context->texture * 2 + 0];
            return;
        case GL_TEXTURE_BINDING_CUBE_MAP:
            *data = current_context->textures[current_context->texture * 2 + 1];
            return;
        default:
            glGetIntegerv(pname, data);
            return;
    }
}

const GLubyte *glGetString_soloader(GLenum name) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_context] glGetString<%p>(name: %u)\n", __builtin_return_address(0), name);
#endif
        return NULL;
    }
#endif

    switch (name) {
        case GL_VENDOR:
            return "Imagination Technologies";
        case GL_RENDERER:
            return "PowerVR SGX 544MP";
        default:
            return glGetString(name);
    }
}

GLint glGetUniformLocation_soloader(GLuint program, const GLchar *name) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_context] glGetUniformLocation<%p>(program: %u, name: %s)\n",
                      __builtin_return_address(0), program, name);
#endif
        return -1;
    }
#endif

#ifdef DEBUG_OPENGL
    const GLint original = glGetUniformLocation(program, name);
    const GLint mapped = find_uniform(program, original);
    sceClibPrintf("[gl_dbg] glGetUniformLocation<%p>(program: %u, name: %s): %p => %d", __builtin_return_address(0),
                  program, name, (void *) original, mapped);
    return mapped;
#else
    return find_uniform(program, glGetUniformLocation(program, name));
#endif
}

void glLinkProgram_soloader(GLuint program) {
    // This is kinda crude, but it fixes for double linking of some shaders
    if (__builtin_return_address(0) == (void *)0x98576668) {
        return;
    }
    glLinkProgram(program);
}

/*
 * Texture memory: the game ships many textures only uncompressed (their ETC1 versions
 * are missing, so it falls back to raw pixels). On a phone that fits; on the Vita the
 * first level used up every byte of graphics memory. Large power-of-two textures are
 * now stored as DXT, which vitaGL compresses as they're uploaded: DXT5 (with alpha) is
 * a quarter the size of RGBA8888, DXT1 an eighth.
 *
 * Two vitaGL limitations shape this, both of which garbled UI/text in a first attempt:
 *  - Non-power-of-two textures are padded before compressing and then given their
 *    original size, so rows land in the wrong place. Those are left uncompressed.
 *  - Only level 0 is converted to RGBA8888 before compressing; mip levels are passed
 *    through as-is. So every level is converted here first, and mip levels smaller
 *    than a 4x4 DXT block are padded to one.
 * A texture's mip levels follow whatever was chosen for its level 0.
 */
#define TEX_COMPRESS_MIN_PIXELS (128 * 128)
#define TEX_TRACKED 65536
static uint8_t tex_is_compressed[TEX_TRACKED / 8];
lstcs_tex_stats_t lstcs_tex_stats;

static int is_pot(GLsizei v) { return v > 0 && (v & (v - 1)) == 0; }

// Returns the input's bytes per pixel, and whether it has alpha, or 0 if unsupported.
static int texel_info(GLenum format, GLenum type, int *alpha) {
    if (format == GL_RGBA && type == GL_UNSIGNED_BYTE) { *alpha = 1; return 4; }
    if (format == GL_RGB && type == GL_UNSIGNED_BYTE) { *alpha = 0; return 3; }
    if (format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5) { *alpha = 0; return 2; }
    if (format == GL_RGBA && type == GL_UNSIGNED_SHORT_4_4_4_4) { *alpha = 1; return 2; }
    if (format == GL_RGBA && type == GL_UNSIGNED_SHORT_5_5_5_1) { *alpha = 1; return 2; }
    return 0;
}

// Converts any supported input to tightly packed RGBA8888 with rows padded to
// (aw x ah) by repeating edge pixels. Source rows follow GL's default 4-byte unpack
// alignment (the game never changes it). Returns a malloc'd buffer or NULL.
static uint8_t *to_rgba8888(const uint8_t *src, GLsizei w, GLsizei h, GLsizei aw, GLsizei ah, GLenum type, int bpp) {
    uint8_t *dst = malloc((size_t) aw * ah * 4);
    if (!dst) return NULL;
    const size_t stride = ((size_t) w * bpp + 3) & ~(size_t) 3;
    for (GLsizei y = 0; y < ah; y++) {
        const uint8_t *row = src + stride * (size_t) (y < h ? y : h - 1);
        uint8_t *out = dst + (size_t) y * aw * 4;
        for (GLsizei x = 0; x < aw; x++) {
            const GLsizei sx = x < w ? x : w - 1;
            uint8_t r, g, b, a = 255;
            if (bpp == 4) {
                const uint8_t *p = row + sx * 4; r = p[0]; g = p[1]; b = p[2]; a = p[3];
            } else if (bpp == 3) {
                const uint8_t *p = row + sx * 3; r = p[0]; g = p[1]; b = p[2];
            } else {
                const uint16_t v = (uint16_t) (row[sx * 2] | (row[sx * 2 + 1] << 8));
                if (type == GL_UNSIGNED_SHORT_5_6_5) {
                    r = (uint8_t) (((v >> 11) & 31) * 255 / 31); g = (uint8_t) (((v >> 5) & 63) * 255 / 63);
                    b = (uint8_t) ((v & 31) * 255 / 31);
                } else if (type == GL_UNSIGNED_SHORT_4_4_4_4) {
                    r = (uint8_t) (((v >> 12) & 15) * 17); g = (uint8_t) (((v >> 8) & 15) * 17);
                    b = (uint8_t) (((v >> 4) & 15) * 17); a = (uint8_t) ((v & 15) * 17);
                } else { // 5_5_5_1
                    r = (uint8_t) (((v >> 11) & 31) * 255 / 31); g = (uint8_t) (((v >> 6) & 31) * 255 / 31);
                    b = (uint8_t) (((v >> 1) & 31) * 255 / 31); a = (v & 1) ? 255 : 0;
                }
            }
            out[x * 4 + 0] = r; out[x * 4 + 1] = g; out[x * 4 + 2] = b; out[x * 4 + 3] = a;
        }
    }
    return dst;
}

// Uploads a 2D texture level, compressing it to DXT when that's safe. Returns 1 if it
// handled the upload, 0 if the caller should upload it unchanged.
static int upload_compressed(GLuint tex, GLint level, GLsizei width, GLsizei height, GLenum format, GLenum type,
                             const void *pixels) {
    const int tracked = tex < TEX_TRACKED;
    const uint8_t bit = (uint8_t) (1u << (tex & 7));
    int alpha = 0;
    const int bpp = texel_info(format, type, &alpha);

    lstcs_tex_stats.uploads++;
    if (!is_pot(width) || !is_pot(height)) lstcs_tex_stats.npot++;
    if (bpp == 0) lstcs_tex_stats.other_format++;
    lstcs_tex_stats.raw_bytes += (uint64_t) width * height * (bpp ? bpp : 4);

    int compress;
    if (level == 0) {
        compress = bpp && pixels && is_pot(width) && is_pot(height) && width >= 4 && height >= 4 &&
                   (int64_t) width * height >= TEX_COMPRESS_MIN_PIXELS;
        if (tracked) {
            if (compress) tex_is_compressed[tex >> 3] |= bit;
            else tex_is_compressed[tex >> 3] &= ~bit;
        }
    } else {
        compress = bpp && pixels && tracked && (tex_is_compressed[tex >> 3] & bit);
    }
    if (!compress) {
        lstcs_tex_stats.stored_bytes += (uint64_t) width * height * (bpp ? bpp : 4);
        return 0;
    }

    const GLsizei aw = (width + 3) & ~3, ah = (height + 3) & ~3;
    const uint8_t *src = pixels;
    uint8_t *conv = NULL;
    if (bpp != 4 || aw != width || ah != height) {
        conv = to_rgba8888(pixels, width, height, aw, ah, type, bpp);
        if (!conv) {
            lstcs_tex_stats.stored_bytes += (uint64_t) width * height * bpp;
            return 0;   // out of memory: fall back to the normal upload
        }
        src = conv;
    }
    const uint64_t t0 = sceKernelGetProcessTimeWide();
    glTextureImage2D(tex, level, alpha ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
                     width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, src);
    lstcs_tex_stats.compress_us += sceKernelGetProcessTimeWide() - t0;
    free(conv);
    lstcs_tex_stats.compressed++;
    lstcs_tex_stats.stored_bytes += (uint64_t) aw * ah / (alpha ? 1 : 2);
    return 1;
}

void glTexImage2D_soloader(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                           GLint border, GLenum format, GLenum type, const void *pixels) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf(
            "[gl_dbg][no_context] glTexImage2D<%p>(target: %u, level: %i, internalformat: %i, width: %i, height: %i, border: %i, format: %u, type: %u, pixels: %p)\n",
            __builtin_return_address(0), target, level, internalformat, width, height, border, format, type, pixels);
#endif
        return;
    }
#endif

    const size_t i = target == GL_TEXTURE_2D ? 0 : 1;
    const GLuint tex = current_context->textures[current_context->texture * 2 + i];
    if (i == 0) {
        if (!upload_compressed(tex, level, width, height, format, type, pixels))
            glTextureImage2D(tex, level, internalformat, width, height, border, format, type, pixels);
    } else
        WITH_CUBE_MAP_BOUND(tex, glTexImage2D(target, level, internalformat, width, height, border, format, type,
                                              pixels));
}

void glTexParameteri_soloader(GLenum target, GLenum pname, GLint param) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_context] glTexParameteri<%p>(target: %u, pname: %u, param: %i)\n",
                      __builtin_return_address(0), target, pname, param);
#endif
        return;
    }
#endif

    const size_t i = target == GL_TEXTURE_2D ? 0 : 1;
    const GLuint tex = current_context->textures[current_context->texture * 2 + i];
    if (i == 0)
        glTextureParameteri(tex, pname, param);
    else
        WITH_CUBE_MAP_BOUND(tex, glTexParameteri(GL_TEXTURE_CUBE_MAP, pname, param));
}

void glUniform1fv_soloader(GLint location, GLsizei count, const GLfloat *value) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_context] glUniform1fv<%p>(location: %i, count: %i, value: %p)\n",
                      __builtin_return_address(0), location, count, value);
#endif
        return;
    }

    if (current_context->program == UINT32_MAX) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_program] glUniform1fv<%p>(location: %i, count: %i, value: %p)\n",
                      __builtin_return_address(0), location, count, value);
#endif
        return;
    }
#endif

#ifdef PROGRAM_UNIFORM_SPEEDHACK
    glUniform1fv(uniforms[current_context->program * GLUTIL_MAX_UNIFORMS + location], count, value);
#else
    glProgramUniform1fv(current_context->program + 1,
                        uniforms[current_context->program * GLUTIL_MAX_UNIFORMS + location], count, value);
#endif
}

void glUniform1i_soloader(GLint location, GLint v0) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_context] glUniform1i<%p>(location: %i, v0: %i)\n",
                      __builtin_return_address(0), location, v0);
#endif
        return;
    }

    if (current_context->program == UINT32_MAX) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_program] glUniform1i<%p>(location: %i, v0: %i)\n",
                      __builtin_return_address(0), location, v0);
#endif
        return;
    }
#endif

#ifdef PROGRAM_UNIFORM_SPEEDHACK
    glUniform1i(uniforms[current_context->program * GLUTIL_MAX_UNIFORMS + location], v0);
#else
    glProgramUniform1i(current_context->program + 1,
                       uniforms[current_context->program * GLUTIL_MAX_UNIFORMS + location], v0);
#endif
}

void glUniform2fv_soloader(GLint location, GLsizei count, const GLfloat *value) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_context] glUniform2fv<%p>(location: %i, count: %i, value: %p)\n",
                      __builtin_return_address(0), location, count, value);
#endif
        return;
    }

    if (current_context->program == UINT32_MAX) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_program] glUniform2fv<%p>(location: %i, count: %i, value: %p)\n",
                      __builtin_return_address(0), location, count, value);
#endif
        return;
    }
#endif

#ifdef PROGRAM_UNIFORM_SPEEDHACK
    glUniform2fv(uniforms[current_context->program * GLUTIL_MAX_UNIFORMS + location], count, value);
#else
    glProgramUniform2fv(current_context->program + 1,
                        uniforms[current_context->program * GLUTIL_MAX_UNIFORMS + location], count, value);
#endif
}

void glUniform3fv_soloader(GLint location, GLsizei count, const GLfloat *value) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_context] glUniform3fv<%p>(location: %i, count: %i, value: %p)\n",
                      __builtin_return_address(0), location, count, value);
#endif
        return;
    }

    if (current_context->program == UINT32_MAX) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_program] glUniform3fv<%p>(location: %i, count: %i, value: %p)\n",
                      __builtin_return_address(0), location, count, value);
#endif
        return;
    }
#endif

#ifdef PROGRAM_UNIFORM_SPEEDHACK
    glUniform3fv(uniforms[current_context->program * GLUTIL_MAX_UNIFORMS + location], count, value);
#else
    glProgramUniform3fv(current_context->program + 1,
                        uniforms[current_context->program * GLUTIL_MAX_UNIFORMS + location], count, value);
#endif
}

void glUniform4fv_soloader(GLint location, GLsizei count, const GLfloat *value) {
#ifndef SKIP_ERROR_HANDLING
    if (current_context == NULL) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_context] glUniform4fv<%p>(location: %i, count: %i, value: %p)\n",
                      __builtin_return_address(0), location, count, value);
#endif
        return;
    }

    if (current_context->program == UINT32_MAX) {
#ifdef DEBUG_OPENGL
        sceClibPrintf("[gl_dbg][no_program] glUniform4fv<%p>(location: %i, count: %i, value: %p)\n",
                      __builtin_return_address(0), location, count, value);
#endif
        return;
    }
#endif

#ifdef PROGRAM_UNIFORM_SPEEDHACK
    glUniform4fv(uniforms[current_context->program * GLUTIL_MAX_UNIFORMS + location], count, value);
#else
    glProgramUniform4fv(current_context->program + 1,
                        uniforms[current_context->program * GLUTIL_MAX_UNIFORMS + location], count, value);
#endif
}

void glUseProgram_soloader(GLuint program) {
    current_context->program = program == 0 ? UINT32_MAX : program - 1;
    if (current_context->is_pbuffer == EGL_FALSE) {
        glUseProgram(program);
    }
}

GLint find_uniform(const GLuint p, const GLint u) {
    const size_t begin = (p - 1) * GLUTIL_MAX_UNIFORMS;
    const size_t end = begin + GLUTIL_MAX_UNIFORMS;

    for (size_t i = begin; i < end; ++i) {
        if (uniforms[i] == -1) {
            uniforms[i] = u;
            return (GLint) (i - begin);
        }
        if (uniforms[i] == u) {
            return (GLint) (i - begin);
        }
    }

    l_error("MORE THEN %u UNIFORMS USED", GLUTIL_MAX_UNIFORMS);
    return -1;
}

/*
 * vitaGL can only render into a framebuffer object that has a colour *texture*
 * attached (glCheckFramebufferStatus reports that). Depth-only FBOs (e.g. shadow
 * maps) or colour renderbuffers give it no render target, so sceGxmBeginScene fails
 * and the next clear/draw writes through an invalid uniform-buffer pointer, crashing
 * inside SceGxm (seen when starting a level). Skip clears and draws aimed at such a
 * framebuffer: worst case that effect is missing, instead of the game crashing.
 */
volatile unsigned int lstcs_skipped_fbo_ops = 0;

static int fbo_unusable(void) {
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
        return 0;
    unsigned int n = ++lstcs_skipped_fbo_ops;
    if (n <= 5)
        lstcs_log("Skipping draw/clear on a framebuffer vitaGL can't render to (no colour texture)%s",
                  n == 5 ? " - further ones are only counted in the heartbeat" : "");
    return 1;
}

void glClear_soloader(GLbitfield mask) {
    if (fbo_unusable()) return;
    glClear(mask);
}

void glDrawArrays_soloader(GLenum mode, GLint first, GLsizei count) {
    if (fbo_unusable()) return;
    glDrawArrays(mode, first, count);
}

void glDrawElements_soloader(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    if (fbo_unusable()) return;
    glDrawElements(mode, count, type, indices);
}
