/*
 * bmp.h - screenshot-to-SD-card: read a rectangle off the LCD
 * (lcd_unblit(), lcd_readback.h) and save it as an uncompressed 24-bit
 * BMP file on the SD card (via picocalc-text-starter-main's fat32.c, by
 * Blair Leduc), and the reverse: load a BMP file back and lcd_blit() it
 * onto the screen. See readback_config.h for the file-naming and BMP
 * format constants.
 *
 * Author: Thomas Dzubin
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/* Mirrors fat32_error_t's own naming/shape (fat32.h) for consistency,
 * plus a couple of cases specific to the BMP/screenshot layer itself. */
typedef enum
{
    BMP_OK = 0,
    BMP_ERROR_NO_CARD,          /* no SD card present                      */
    BMP_ERROR_FS,                /* an underlying fat32_* call failed; see
                                   * bmp_error_string()'s caller for detail
                                   * via fat32_error_string() separately   */
    BMP_ERROR_MEMORY,            /* reserved; both directions now stream one
                                   * row at a time and never allocate       */
    BMP_ERROR_TOO_BIG,           /* rectangle exceeds BMP_RECT_MAX_W/H      */
    BMP_ERROR_NAME_TAKEN,        /* every random SCRNxxxx.BMP name tried
                                   * (BMP_NAME_MAX_RETRIES of them) already
                                   * existed                                 */
    BMP_ERROR_NOT_A_BMP,         /* missing "BM" signature                  */
    BMP_ERROR_UNSUPPORTED_FORMAT,/* not 24-bit uncompressed BI_RGB          */
} bmp_result_t;

const char *bmp_error_string(bmp_result_t err);

/* More detail behind a BMP_ERROR_FS result: the fat32_error_t (fat32.h,
 * by Blair Leduc) from whichever fat32_* call actually failed. */
const char *bmp_last_fs_error_string(void);

/* Reads a width x height rectangle off the LCD at (x, y) and saves it as
 * a new 24-bit uncompressed BMP file on the SD card, named SCRNxxxx.BMP
 * with xxxx a random 4-digit uppercase hex number (retried with a fresh
 * number if that exact name is already taken). On BMP_OK, the filename
 * actually used is copied into filename_out (a caller-owned buffer of at
 * least BMP_FILENAME_LEN bytes, readback_config.h). */
bmp_result_t bmp_save_screenshot(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                  char *filename_out, size_t filename_out_size);

/* Screenshots the WHOLE PicoCalc screen (WIDTH x HEIGHT, lcd.h; 320x320
 * on real hardware) to a new SCRNxxxx.BMP file. A thin wrapper around
 * bmp_save_screenshot(0, 0, WIDTH, HEIGHT, ...); safe to call because
 * that function (like bmp_load_and_blit() below) reads/writes one LCD
 * row at a time and never holds the whole screen in RAM at once (320x320
 * pixels would be 200KB, more than half the RP2040's total SRAM). */
bmp_result_t picocalc_screenshot(char *filename_out, size_t filename_out_size);

/* Opens filename on the SD card, checks it's a BMP this reader
 * understands (24-bit, uncompressed BI_RGB), and lcd_blit()s it onto the
 * screen with its top-left corner at (x, y). width_out/height_out (either
 * may be NULL) receive the image's dimensions from its own header. */
bmp_result_t bmp_load_and_blit(const char *filename, uint16_t x, uint16_t y,
                                uint16_t *width_out, uint16_t *height_out);
