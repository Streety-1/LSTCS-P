/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 * Copyright (C) 2024      Jan Smialkowski
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/glutil.h"

#include <reimpl/gl.h>

#include "utils/utils.h"
#include "utils/dialog.h"
#include "utils/logger.h"

#include <stdio.h>
#include <malloc.h>
#include <string.h>

_Thread_local _egl_context* current_context = NULL;

// Helpers for our handling of shaders
_Thread_local GLboolean skip_next_compile = GL_FALSE;
_Thread_local char next_shader_fname[256];
_Thread_local char current_shader_name[40];

/*
 * Placeholder shaders (valid Cg), used when a real shader is missing or fails to
 * compile. The vertex one collapses geometry to a point so the object just doesn't
 * appear; the fragment one draws plain white. Either way the game keeps running:
 * a shader with no compiled program makes glLinkProgram crash inside SceGxm.
 */
static const char * const PLACEHOLDER_VS = "float4 main() : POSITION { return float4(0.0, 0.0, 0.0, 1.0); }";
static const char * const PLACEHOLDER_FS = "float4 main() : COLOR { return float4(1.0, 1.0, 1.0, 1.0); }";

static void set_placeholder_source(GLuint shader) {
    GLint type = 0;
    glGetShaderiv(shader, GL_SHADER_TYPE, &type);
    const char *src = type == GL_FRAGMENT_SHADER ? PLACEHOLDER_FS : PLACEHOLDER_VS;
    GLint len = (GLint) strlen(src);
    glShaderSource(shader, 1, &src, &len);
}

GLint uniforms[GLUTIL_MAX_PROGRAMS * GLUTIL_MAX_UNIFORMS];

void load_shader(GLuint shader, const char * string, size_t length);

/*
 * Current vitaGL decides GLSL vs Cg from the shader *type*: GL_VERTEX_SHADER /
 * GL_FRAGMENT_SHADER mean GLSL, which it may compile only at link time through its
 * GLSL translator. This port always supplies Cg (the translated shaders/cg files),
 * so create shaders with vitaGL's explicit Cg types. Without this, compiles report
 * success but produce no program, and glLinkProgram crashes inside SceGxm.
 * GL_SHADER_TYPE queries still return GL_VERTEX_SHADER / GL_FRAGMENT_SHADER.
 */
GLuint glCreateShader_soloader(GLenum shaderType) {
#if defined(USE_CG_SHADERS) && defined(GL_CG_VERTEX_SHADER_EXT)   // only newer vitaGL needs this
    if (shaderType == GL_VERTEX_SHADER) shaderType = GL_CG_VERTEX_SHADER_EXT;
    else if (shaderType == GL_FRAGMENT_SHADER) shaderType = GL_CG_FRAGMENT_SHADER_EXT;
#endif
    return glCreateShader(shaderType);
}

void glCompileShader_soloader(GLuint shader) {
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glCompileShader<%p>(shader: %i)\n", __builtin_return_address(0), shader);
#endif

#ifndef USE_GXP_SHADERS
    if (!skip_next_compile) {
        glCompileShader(shader);

        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled != GL_TRUE) {
            lstcs_log("Shader %s (id %u) failed to compile; using a placeholder", current_shader_name, shader);
            l_error("Shader %s failed to compile; using a placeholder", current_shader_name);
            set_placeholder_source(shader);
            glCompileShader(shader);
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (compiled != GL_TRUE) {
                lstcs_log("  placeholder for %s ALSO failed: the shader compiler isn't working", current_shader_name);
                fatal_error("The Vita's shader compiler (libshacccg.suprx) failed to work. "
                            "Reinstall it with ShaRKBR33D, then reboot and try again.");
            }
#ifdef DUMP_COMPILED_SHADERS
            next_shader_fname[0] = '\0';
#endif
        }
#ifdef DUMP_COMPILED_SHADERS
        // Cache the compiled shader, but only when it really compiled (shaders are created
        // as Cg, so a program exists right after glCompileShader) and we know which real
        // shader it is. Written to a temp file and renamed, so a power-off mid-write can
        // never leave a half-written cache file that would be loaded next time.
        if (compiled == GL_TRUE && next_shader_fname[0] != '\0') {
            const GLsizei cap = 256 * 1024;   // vitaGL writes without a size check
            void *bin = vglMalloc(cap);
            GLsizei len = 0;
            if (bin) {
                vglGetShaderBinary(shader, cap, &len, bin);
                if (len > 0 && len <= cap && file_mkpath(next_shader_fname, 0777)) {
                    char tmp[272];
                    snprintf(tmp, sizeof(tmp), "%s.tmp", next_shader_fname);
                    if (file_save(tmp, bin, len))
                        rename(tmp, next_shader_fname);
                }
                vglFree(bin);
            }
        }
        next_shader_fname[0] = '\0';
#endif
    }
    skip_next_compile = GL_FALSE;
#endif
}

void glShaderSource_soloader(GLuint handle, GLsizei count, const GLchar *const *string, const GLint *length) {
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glShaderSource<%p>(shader: %i, count: %i, string: %p, length: %p)\n", __builtin_return_address(0), handle, count, string, length);
#endif
    if (!string) {
        l_error("<%p> Shader source string is NULL, count: %i",
                   __builtin_return_address(0), count);
        lstcs_log("Shader %u given no source; using a placeholder", handle);
        snprintf(current_shader_name, sizeof(current_shader_name), "(no source)");
        set_placeholder_source(handle);
        skip_next_compile = GL_FALSE;
        return;
    } else if (!*string) {
        l_error("<%p> Shader source *string is NULL, count: %i",
                   __builtin_return_address(0), count);
        lstcs_log("Shader %u given no source; using a placeholder", handle);
        snprintf(current_shader_name, sizeof(current_shader_name), "(no source)");
        set_placeholder_source(handle);
        skip_next_compile = GL_FALSE;
        return;
    }

    size_t total_length = 0;

    for (int i = 0; i < count; ++i) {
        if (!length) {
            total_length += strlen(string[i]);
        } else {
            total_length += length[i];
        }
    }

    char * str = malloc(total_length+1);
    size_t l = 0;

    for (int i = 0; i < count; ++i) {
        if (!length) {
            memcpy(str + l, string[i], strlen(string[i]));
            l += strlen(string[i]);
        } else {
            memcpy(str + l, string[i], length[i]);
            l += length[i];
        }
    }
    str[total_length] = '\0';

    load_shader(handle, str, total_length);

    free(str);
}

void gl_preload() {
    lstcs_log("libshacccg.suprx: ur0:data=%s ur0:data/external=%s",
              file_exists("ur0:/data/libshacccg.suprx") ? "yes" : "no",
              file_exists("ur0:/data/external/libshacccg.suprx") ? "yes" : "no");
    if (!file_exists("ur0:/data/libshacccg.suprx")
        && !file_exists("ur0:/data/external/libshacccg.suprx")) {
        fatal_error("Error: libshacccg.suprx is not installed. "
                    "Google \"ShaRKBR33D\" for quick installation.");
        }

    memset(uniforms, -1, sizeof(uniforms));

#ifdef USE_GLSL_SHADERS
    vglSetSemanticBindingMode(VGL_MODE_POSTPONED);
#endif
}

#if defined(USE_GLSL_SHADERS) && defined(DUMP_COMPILED_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* xxh_name = str_xxhsum(string, length);
    snprintf(current_shader_name, sizeof(current_shader_name), "%s", xxh_name);

    char gxp_path[256];
    snprintf(gxp_path, sizeof(gxp_path), "%sshadercache/%c/%s.gxp", data_path(), xxh_name[0], xxh_name);

    if (file_exists(gxp_path)) {
        uint8_t *buffer;
        size_t size;

        file_load(gxp_path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
        skip_next_compile = GL_TRUE;
    } else {
        glShaderSource(shader, 1, &string, &length);
        strcpy(next_shader_fname, gxp_path);
    }

    free(xxh_name);
}
#elif defined(USE_GLSL_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    glShaderSource(shader, 1, &string, &length);
}
#elif defined(USE_CG_SHADERS) && defined(DUMP_COMPILED_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* xxh_name = str_xxhsum(string, length);
    snprintf(current_shader_name, sizeof(current_shader_name), "%s", xxh_name);

    char gxp_path[256];
    char cg_path[256];
    snprintf(gxp_path, sizeof(gxp_path), "%sshadercache/%c/%s.gxp", data_path(), xxh_name[0], xxh_name);
    snprintf(cg_path, sizeof(cg_path), "%scg/%c/%s.cg", data_path(), xxh_name[0], xxh_name);

    if (file_exists(gxp_path)) {
        uint8_t *buffer;
        size_t size;

        file_load(gxp_path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
        skip_next_compile = GL_TRUE;
    } else if (file_exists(cg_path)) {
        char *buffer;
        size_t size;

        file_load(cg_path, (uint8_t **) &buffer, &size);

        // Fixed: previously passed the game's GLSL text with the .cg file's length.
        GLint cg_len = (GLint) size;
        glShaderSource(shader, 1, (const GLchar * const *) &buffer, &cg_len);
        strcpy(next_shader_fname, gxp_path);

        free(buffer);
        skip_next_compile = GL_FALSE;
    } else {
        l_warn("Encountered an untranslated shader %s, saving GLSL "
               "and using a dummy shader.", xxh_name);
        next_shader_fname[0] = '\0'; // never cache the placeholder

        char glsl_path[256];
        snprintf(glsl_path, sizeof(glsl_path), "%sglsl/%c/%s.glsl", data_path(), xxh_name[0], xxh_name);
        file_mkpath(glsl_path, 0777);
        file_save(glsl_path, (const uint8_t *) string, length);

        lstcs_log("Shader %s has no translation in cg/ ; using a placeholder (GLSL saved to glsl/)", xxh_name);
        set_placeholder_source(shader);

        skip_next_compile = GL_FALSE;
    }

    free(xxh_name);
}
#elif defined(USE_CG_SHADERS) || defined(USE_GXP_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* xxh_name = str_xxhsum(string, length);
    snprintf(current_shader_name, sizeof(current_shader_name), "%s", xxh_name);

    char path[256];
#ifdef USE_CG_SHADERS
    snprintf(path, sizeof(path), "%scg/%c/%s.cg", data_path(), xxh_name[0], xxh_name);
#else
    snprintf(path, sizeof(path), "app0:gxp/%c/%s.gxp", xxh_name[0], xxh_name);
#endif

    if (file_exists(path)) {
#ifdef USE_CG_SHADERS
        char *buffer;
        size_t size;

        file_load(path, (uint8_t **) &buffer, &size);

        // Fixed: previously passed the game's GLSL text with the .cg file's length.
        GLint cg_len = (GLint) size;
        glShaderSource(shader, 1, (const GLchar * const *) &buffer, &cg_len);

        free(buffer);
#else
        uint8_t *buffer;
        size_t size;

        file_load(path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
#endif
    } else {
        l_warn("Encountered an untranslated shader %s, saving GLSL "
               "and using a dummy shader.", xxh_name);
        next_shader_fname[0] = '\0'; // never cache the placeholder

        char glsl_path[256];
        snprintf(glsl_path, sizeof(glsl_path), "%sglsl/%c/%s.glsl", data_path(), xxh_name[0], xxh_name);
        file_mkpath(glsl_path, 0777);
        file_save(glsl_path, (const uint8_t *) string, length);

        lstcs_log("Shader %s has no precompiled/translated version; using a placeholder (GLSL saved to glsl/)", xxh_name);
        set_placeholder_source(shader);
#ifdef USE_GXP_SHADERS
        glCompileShader(shader);   // in GXP mode the game's own compile call is skipped
#endif
    }

    free(xxh_name);
}
#else
#error "Define one of (USE_GLSL_SHADERS, USE_CG_SHADERS, USE_GXP_SHADERS)"
#endif
