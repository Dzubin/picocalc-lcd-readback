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

/* ---- Screenshot names and loading a BMP back (main.c, bmp.c) ---------- */
/* The test program names its screenshots SCRNxxxx.BMP, xxxx four random hex
 * digits: "SCRN" + 4 + ".BMP" + the null is 13 bytes. screenshot.c itself
 * takes any name the caller gives it. */
#define SHOT_NAME_FORMAT         "SCRN%04X.BMP"
#define SHOT_NAME_MASK           0xFFFFu
#define BMP_FILENAME_LEN         16
/* Random names tried before giving up when each one is already taken. */
#define BMP_NAME_MAX_RETRIES     20

/* Rows read from the card per fat32_read() call when loading a BMP. */
#define BMP_IO_BATCH_ROWS        4

/* The part of the BMP header that bmp.c reads: both headers together, the
 * bits per pixel it accepts, and the "no compression" value. */
#define BMP_HEADER_SIZE          54
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
