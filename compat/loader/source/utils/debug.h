#ifndef LSTCS_DEBUG_H
#define LSTCS_DEBUG_H

/*
 * The debug folder (<game folder>/debug/) collects what's needed to track down a
 * problem without a PC attached:
 *   lstcs-log.txt           this session's log
 *   lstcs-log-previous.txt  the session before (kept so a crash's log isn't lost)
 *   screenshots/            this session's screenshots, as .bmp
 *   screenshots-previous/   the session before's screenshots
 *
 * Screenshots are taken shortly after start-up, a few seconds after each level
 * finishes loading, once a minute (up to a limit), and whenever L + R + SELECT is
 * pressed together.
 */

// Creates the folder and moves the last session's files aside. Call once the game
// folder is known (after data_path_init).
void debug_init(void);

// "<game folder>/debug/"
const char * debug_dir(void);

// Saves what's on screen now as debug/screenshots/NN-<tag>.bmp. Returns 0 on success.
int debug_screenshot(const char * tag);

// Called for every file the game opens; spots level loads.
void debug_note_file_open(const char * path);

// Called regularly (~120 times a second) from the input thread.
void debug_tick(unsigned int buttons, unsigned int frames);

#endif
