/*
 * readback_config.h - tunable constants for the LCD readback test program.
 * Author: Thomas Dzubin
 *
 * Everything here is either a test-program setting (region size, test
 * counts) or a hardware constant for the RAMRD read path, most of them
 * now confirmed on real RP2040 hardware (via the `solid` +
 * `rawbytes` commands: fill a rectangle with one exact colour, then dump
 * the raw RAMRD bytes with no interpretation and read the pattern off by
 * eye) rather than taken from the ST7365P datasheet, which turned out to
 * describe the read format ambiguously enough to get the byte layout
 * wrong on the first attempt (see CLAUDE.md's "Build gotchas" section for
 * that whole story):
 *
 *   - With COLMOD set to 16-bit/pixel (LCD_CMD_COLMOD, 0x55, same as
 *     lcd_init() always uses), RAMRD reads pixels back in that same
 *     16-bit RGB565 format lcd_blit() writes them in; two bytes per
 *     pixel, big-endian, byte-for-byte identical to what was written. NOT
 *     the 18-bit/three-bytes-per-pixel frame-memory format an earlier,
 *     wrong reading of the datasheet assumed (that version read every
 *     pixel wrong, confirmed on hardware: filling a rectangle with random
 *     pixels and reading it back failed on all of them). Whatever 16-to-
 *     18-bit expansion the write path applies internally (see EMS/0xC6 in
 *     lcd_init()) is apparently undone again on the way back out for a
 *     16-bit COLMOD read.
 *   - The very first byte clocked out after the command really is a
 *     dummy byte with no pixel data in it (confirmed by the same
 *     `rawbytes` dump: one odd byte, then the real, repeating pixel
 *     pattern starts immediately after).
 *   - LCD_READ_BAUDRATE is still an UNCONFIRMED conservative guess; the
 *     `solid`/`rawbytes` round above happened to work at this speed, but
 *     that only proves 1MHz is safe, not that it's necessary. Try raising
 *     it and re-running `test` to see how far it actually goes.
 */
#pragma once

/* ---- Read-path SPI speed --------------------------------------------- */
/* LCD_READ_BAUDRATE (1 MHz: works; untested how much higher can go) is
 * defined in the starter's lcd.h, and the bus goes back to LCD_BAUDRATE after
 * every read. */

/* ---- RAMRD protocol shape --------------------------------------------- */
#define LCD_READ_DUMMY_BYTES      1   /* discarded before the first real pixel */
#define LCD_READ_BYTES_PER_PIXEL  2   /* big-endian RGB565, same as lcd_blit() writes */

/* ---- Test program: default rectangle under test ----------------------- */
/* Placed below the console text rows so printf() output and the test
 * rectangle never overlap on the 320x320 panel. */
#define TEST_RECT_X            8
#define TEST_RECT_Y           160
#define TEST_RECT_W_DEFAULT    64
#define TEST_RECT_H_DEFAULT    64
#define TEST_RECT_MAX_W       304   /* WIDTH  - 2*TEST_RECT_X, see lcd.h */
#define TEST_RECT_MAX_H       152   /* HEIGHT - TEST_RECT_Y - margin     */
#define TEST_COUNT_DEFAULT      1
#define TEST_COUNT_MAX         100

/* A visible checkerboard/gradient pattern for the "pattern" command,
 * left on screen afterward so it can be photographed. */
#define PATTERN_CELL_PX          8

/* ---- Screenshot-to-SD-card (bmp.c) ------------------------------------ */
/* SCRNxxxx.BMP: 4 random uppercase hex digits. "SCRN" + 4 hex + ".BMP" +
 * a null terminator = 13 bytes; a little headroom in case a future
 * naming scheme needs more. */
#define BMP_FILENAME_LEN         16
/* How many random names bmp_save_screenshot() will try before giving up
 * if it keeps landing on a file that already exists (astronomically
 * unlikely to ever matter with a 16-bit random suffix, but it's a loop,
 * so it needs a bound). */
#define BMP_NAME_MAX_RETRIES     20
/* bmp.c streams a screenshot one LCD row at a time; never a whole
 * WIDTH x HEIGHT pixel buffer in RAM (320x320x2 = 200KB, more than half
 * the RP2040's total SRAM); so these just need to cover the real
 * screen, not stay small the way TEST_RECT_MAX_W/H (used by the other
 * commands, which DO hold a full rectangle in RAM) have to. 320 matches
 * the PicoCalc's WIDTH/HEIGHT (lcd.h); written as a literal, not derived
 * from WIDTH/HEIGHT, so this header doesn't have to assume lcd.h was
 * already included first. */
#define BMP_RECT_MAX_W  320
#define BMP_RECT_MAX_H  320

/* fat32_write()/fat32_read() (fat32.c, by Blair Leduc) walk the file's
 * ENTIRE cluster chain from the start on EVERY call; confirmed by
 * reading their source: both start from file->start_cluster and step
 * forward file->position / bytes_per_cluster times, discarding whatever
 * cluster they found last call. One fat32_write()/fat32_read() call per
 * LCD row is fine for a small rectangle, but for a full 320x320
 * screenshot (~300KB) each row's I/O call gets slower than the last, and
 * the whole thing took several minutes with no on-screen feedback in the
 * meantime, so it is easily mistaken for a hang. Batching
 * BMP_IO_BATCH_ROWS rows into one call cuts the NUMBER of calls (and so
 * the total chain-walk cost) by roughly that factor, while still never
 * holding anywhere near a full image in RAM: at most
 * BMP_IO_BATCH_ROWS * (BMP_RECT_MAX_W*BMP_BYTES_PER_PIXEL + 3) bytes, a
 * small fixed buffer regardless of how tall the screenshot actually is.
 * Raise it for more speed at some more RAM; lower it if RAM gets tight
 * elsewhere. */
#define BMP_IO_BATCH_ROWS  16

/* ---- BMP file format constants (BITMAPFILEHEADER/BITMAPINFOHEADER,     */
/* uncompressed 24-bit BI_RGB; see bmp.c) ------------------------------ */
#define BMP_FILE_HEADER_SIZE     14
#define BMP_INFO_HEADER_SIZE     40
#define BMP_HEADER_SIZE          (BMP_FILE_HEADER_SIZE + BMP_INFO_HEADER_SIZE)
#define BMP_BITS_PER_PIXEL       24
#define BMP_BYTES_PER_PIXEL      3
#define BMP_COMPRESSION_BI_RGB   0

/* ---- Leaving for the PicoCalc UF2 Loader (pelrun/uf2loader) -------------
 * The loader has no call for an app to use, but its own menu hands commands to
 * its start-up code through the chip's watchdog scratch registers, which
 * survive a watchdog reboot: scratch 0 holds a magic number, 1 the boot mode,
 * 2 an argument. Asking for boot mode "SD" and then rebooting makes the loader
 * show its menu again. With no loader installed the program just restarts. */
#define LOADER_COMMAND_MAGIC      0xE98CC638u /* PICOCALC_BL_MAGIC in the loader's proginfo.h */
#define LOADER_BOOT_MODE_SD       1           /* BOOT_SD: load the menu from the SD card */
#define LOADER_SCRATCH_MAGIC      0
#define LOADER_SCRATCH_MODE       1
#define LOADER_SCRATCH_ARGUMENT   2
#define LOADER_EXIT_MSG_MS        300         /* how long "Back to the loader..." shows */
#define LOADER_REBOOT_DELAY_MS    10          /* the reboot happens this long after it is asked for */
