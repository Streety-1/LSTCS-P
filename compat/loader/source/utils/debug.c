#include "utils/debug.h"
#include "utils/utils.h"

#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define MAX_AUTO_SHOTS     40     // start-up, levels and the once-a-minute ones
#define MAX_MANUAL_SHOTS   100
#define PERIODIC_SHOT_US   (60ull * 1000000)
#define LEVEL_SETTLE_US    (5ull * 1000000)   // no file opened for this long = loaded

static char g_debug_dir[96];
static char g_shot_dir[128];
static unsigned int g_shot_count = 0, g_auto_shots = 0, g_manual_shots = 0;

const char * debug_dir(void) {
    return g_debug_dir;
}

static void remove_dir_contents(const char * dir) {
    SceUID d = sceIoDopen(dir);
    if (d < 0) return;
    SceIoDirent ent;
    char path[256];
    while (sceIoDread(d, &ent) > 0) {
        if (SCE_S_ISDIR(ent.d_stat.st_mode)) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent.d_name);
        sceIoRemove(path);
    }
    sceIoDclose(d);
}

void debug_init(void) {
    snprintf(g_debug_dir, sizeof(g_debug_dir), "%sdebug/", data_path());
    sceIoMkdir(g_debug_dir, 0777);

    char a[160], b[160];
    // Keep the previous session's log and screenshots, drop the ones before that.
    snprintf(a, sizeof(a), "%slstcs-log.txt", g_debug_dir);
    snprintf(b, sizeof(b), "%slstcs-log-previous.txt", g_debug_dir);
    sceIoRemove(b);
    sceIoRename(a, b);

    snprintf(a, sizeof(a), "%sscreenshots", g_debug_dir);
    snprintf(b, sizeof(b), "%sscreenshots-previous", g_debug_dir);
    remove_dir_contents(b);
    sceIoRmdir(b);
    sceIoRename(a, b);
    sceIoMkdir(a, 0777);
    snprintf(g_shot_dir, sizeof(g_shot_dir), "%s/", a);

    // An older build wrote its log straight into the game folder; it's out of date now.
    snprintf(a, sizeof(a), "%slstcs-log.txt", data_path());
    sceIoRemove(a);

    lstcs_log_set_path(g_debug_dir);
}

static void put32(uint8_t * p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

int debug_screenshot(const char * tag) {
    if (!g_shot_dir[0]) return -1;

    SceDisplayFrameBuf fb;
    memset(&fb, 0, sizeof(fb));
    fb.size = sizeof(fb);
    if (sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_IMMEDIATE) < 0 || !fb.base || fb.width == 0 || fb.height == 0) {
        lstcs_log("screenshot \"%s\" failed: no frame on screen", tag);
        return -1;
    }
    if (fb.pixelformat != SCE_DISPLAY_PIXELFORMAT_A8B8G8R8) {
        lstcs_log("screenshot \"%s\" failed: unexpected pixel format %d", tag, (int) fb.pixelformat);
        return -1;
    }

    // File name: NN-tag.bmp, with anything unusual in the tag made safe.
    char safe[48];
    size_t n = 0;
    for (const char * c = tag; *c && n < sizeof(safe) - 1; c++)
        safe[n++] = ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-') ? *c : '_';
    safe[n] = 0;
    char path[192];
    snprintf(path, sizeof(path), "%s%02u-%s.bmp", g_shot_dir, ++g_shot_count, safe);

    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) {
        lstcs_log("screenshot \"%s\" failed: can't create %s (0x%08X)", tag, path, fd);
        return -1;
    }

    const uint32_t w = fb.width, h = fb.height;
    const uint32_t row = (w * 3 + 3) & ~3u;
    uint8_t hdr[54] = {'B', 'M'};
    put32(hdr + 2, 54 + row * h);
    put32(hdr + 10, 54);
    put32(hdr + 14, 40);
    put32(hdr + 18, w);
    put32(hdr + 22, h);               // positive height: rows stored bottom-up
    hdr[26] = 1; hdr[28] = 24;
    put32(hdr + 34, row * h);
    sceIoWrite(fd, hdr, sizeof(hdr));

    static uint8_t chunk[32 * 1024];
    size_t used = 0;
    const uint32_t * pixels = (const uint32_t *) fb.base;
    for (int y = (int) h - 1; y >= 0; y--) {
        if (used + row > sizeof(chunk)) {
            sceIoWrite(fd, chunk, used);
            used = 0;
        }
        const uint32_t * src = pixels + (size_t) y * fb.pitch;
        uint8_t * dst = chunk + used;
        for (uint32_t x = 0; x < w; x++) {
            const uint32_t p = src[x];   // memory order R, G, B, A
            dst[x * 3 + 0] = (uint8_t) (p >> 16);
            dst[x * 3 + 1] = (uint8_t) (p >> 8);
            dst[x * 3 + 2] = (uint8_t) p;
        }
        memset(dst + w * 3, 0, row - w * 3);
        used += row;
    }
    if (used) sceIoWrite(fd, chunk, used);
    sceIoClose(fd);

    lstcs_log("screenshot saved: %s", path + strlen(g_debug_dir));
    return 0;
}

// ------------------------------------------------------------ level detection --
// Level files are opened as "levels\<area>\<name>\...". The newest level's name is
// kept here; once no file at all has been opened for a few seconds it's on screen.
static char g_level_name[64];
static volatile unsigned int g_level_seq = 0;
static volatile uint64_t g_level_last_open = 0;

void debug_note_file_open(const char * path) {
    // Any file opened (characters, sounds...) means the level is still loading.
    if (g_level_last_open) g_level_last_open = sceKernelGetProcessTimeWide();
    if (!path || strncasecmp(path, "levels", 6) != 0 || (path[6] != '\\' && path[6] != '/')) return;
    // Take "<area>\<name>" (the first two folders after "levels").
    const char * p = path + 7;
    char name[64];
    size_t n = 0;
    int seps = 0;
    for (; *p && n < sizeof(name) - 1; p++) {
        if (*p == '\\' || *p == '/') {
            if (++seps == 2) break;
            name[n++] = '-';
        } else
            name[n++] = *p;
    }
    if (seps < 1) return;   // a file directly in levels\<area>
    name[n] = 0;

    g_level_last_open = sceKernelGetProcessTimeWide();
    if (strcmp(name, g_level_name) != 0) {
        strcpy(g_level_name, name);
        g_level_seq++;
        lstcs_log("level loading: %s", name);
    }
}

void debug_tick(unsigned int buttons, unsigned int frames) {
    static int startup_done = 0;
    static unsigned int level_seq_shot = 0;
    static uint64_t next_periodic = 0;
    static unsigned int old_buttons = 0;
    const uint64_t now = sceKernelGetProcessTimeWide();

    // Manual: L + R + SELECT.
    const unsigned int combo = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_SELECT;
    if ((buttons & combo) == combo && (old_buttons & combo) != combo && g_manual_shots < MAX_MANUAL_SHOTS) {
        g_manual_shots++;
        debug_screenshot("manual");
    }
    old_buttons = buttons;

    if (g_auto_shots >= MAX_AUTO_SHOTS) return;

    if (!startup_done && frames >= 90) {
        startup_done = 1;
        g_auto_shots++;
        debug_screenshot("startup");
        next_periodic = now + PERIODIC_SHOT_US;
    }

    const unsigned int seq = g_level_seq;
    if (seq != level_seq_shot && g_level_last_open && now - g_level_last_open > LEVEL_SETTLE_US) {
        level_seq_shot = seq;
        char tag[80];
        snprintf(tag, sizeof(tag), "level-%s", g_level_name);
        g_auto_shots++;
        debug_screenshot(tag);
    }

    if (next_periodic && now >= next_periodic) {
        next_periodic = now + PERIODIC_SHOT_US;
        char tag[32];
        snprintf(tag, sizeof(tag), "after-%us", (unsigned) (now / 1000000));
        g_auto_shots++;
        debug_screenshot(tag);
    }
}
