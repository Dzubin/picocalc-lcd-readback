/*
 * bmp.h - the test program's way to show a screenshot again: load a 24-bit
 * uncompressed BMP file from the SD card (via picocalc-text-starter-main's
 * fat32.c, by Blair Leduc) and lcd_blit() it onto the screen. Saving is in
 * screenshot.h. See readback_config.h for the constants.
 *
 * Author: Thomas Dzubin
 */
#pragma once

#include <stdint.h>

#include "fat32.h"

typedef enum
{
    BMP_OK = 0,
    BMP_ERROR_FS,                /* a fat32_* call failed; see bmp_last_fs_error() */
    BMP_ERROR_TOO_BIG,           /* does not fit on the screen at that position */
    BMP_ERROR_NOT_A_BMP,         /* missing "BM" signature */
    BMP_ERROR_UNSUPPORTED_FORMAT /* not 24-bit uncompressed BI_RGB */
} bmp_result_t;

const char *bmp_error_string(bmp_result_t err);

/* The fat32 error behind the last BMP_ERROR_FS. */
fat32_error_t bmp_last_fs_error(void);

/* Opens filename on the SD card, checks it is a BMP this reader understands,
 * and lcd_blit()s it onto the screen with its top left corner at (x, y). Both
 * bottom-up and top-down files load. width_out and height_out (either may be
 * NULL) receive the image's size. */
bmp_result_t bmp_load_and_blit(const char *filename, uint16_t x, uint16_t y,
                               uint16_t *width_out, uint16_t *height_out);
