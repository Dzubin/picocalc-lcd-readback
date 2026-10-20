/*
 * screenshot.h - save part or all of the PicoCalc's screen as a BMP file on
 * the SD card. To use it in another program, add screenshot.c, screenshot.h and
 * screenshot_const.h, and build them with picocalc-text-starter by Blair Leduc
 * (its lcd.c for lcd_unblit(), fat32.c and sdcard.c). Nothing is printed, no
 * clock or random numbers are used, and the caller chooses the file name.
 *
 * The file is an uncompressed 24-bit BMP with its rows stored top to bottom
 * (a negative height in the header), which every common viewer reads.
 *
 * Author: Thomas Dzubin
 */
#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <stdint.h>

#include "fat32.h"              /* fat32_error_t */

typedef enum
{
    SHOT_OK = 0,
    SHOT_ERROR_NO_CARD,         /* no SD card present */
    SHOT_ERROR_RANGE,           /* empty rectangle, or it reaches off the screen */
    SHOT_ERROR_EXISTS,          /* a file of that name is already on the card */
    SHOT_ERROR_FS               /* a file system call failed; see screenshot_fs_error() */
} shot_result_t;

/* Called after each group of rows reaches the card, so a caller can show
 * progress. Pass NULL for none. */
typedef void (*shot_progress_fn)(void);

/* Saves the width x height part of the screen whose top left corner is at
 * (x, y) to a new file called filename (8.3 or long name) in the SD card's root
 * folder. An existing file is never overwritten (SHOT_ERROR_EXISTS). A file
 * that could not be finished is deleted again. */
shot_result_t screenshot_save(const char *filename, uint16_t x, uint16_t y,
                              uint16_t width, uint16_t height,
                              shot_progress_fn progress);

/* The whole screen, WIDTH x HEIGHT as in lcd.h. */
shot_result_t screenshot_save_screen(const char *filename, shot_progress_fn progress);

const char *screenshot_error_string(shot_result_t result);

/* The fat32 error behind the last SHOT_ERROR_FS. */
fat32_error_t screenshot_fs_error(void);

#endif /* SCREENSHOT_H */
