/*
 * screenshot_const.h - the constants of screenshot.c.
 *
 * Author: Thomas Dzubin
 */
#ifndef SCREENSHOT_CONST_H
#define SCREENSHOT_CONST_H

#include "lcd.h"                /* WIDTH */

/* How many LCD rows go to the SD card in one fat32_write() call. fat32_write()
 * walks the file's cluster chain from the start on every call, so fewer, larger
 * calls are faster. Each row costs (WIDTH * 3 + 3) bytes of RAM here, about 1 KB
 * on the PicoCalc, so 4 rows is about 4 KB. */
#define SHOT_BATCH_ROWS         4

#define SHOT_BYTES_PER_PIXEL    3                       /* BGR, one byte each */
#define SHOT_ROW_PAD_MAX        3                       /* rows end on a 4 byte boundary */
#define SHOT_BATCH_BYTES        (SHOT_BATCH_ROWS * (WIDTH * SHOT_BYTES_PER_PIXEL + SHOT_ROW_PAD_MAX))

/* BITMAPFILEHEADER + BITMAPINFOHEADER of an uncompressed 24-bit BMP. */
#define SHOT_FILE_HEADER_SIZE   14
#define SHOT_INFO_HEADER_SIZE   40
#define SHOT_HEADER_SIZE        (SHOT_FILE_HEADER_SIZE + SHOT_INFO_HEADER_SIZE)
#define SHOT_BITS_PER_PIXEL     24

/* Byte offsets of the header fields that are filled in. */
#define SHOT_OFF_FILE_SIZE      2
#define SHOT_OFF_PIXEL_OFFSET   10
#define SHOT_OFF_INFO_SIZE      14
#define SHOT_OFF_WIDTH          18
#define SHOT_OFF_HEIGHT         22
#define SHOT_OFF_PLANES         26
#define SHOT_OFF_BIT_COUNT      28
#define SHOT_OFF_IMAGE_SIZE     34

#endif /* SCREENSHOT_CONST_H */
