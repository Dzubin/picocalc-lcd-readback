/*
 * picocalc-screenshot - a small console tool to check that lcd_unblit()
 * (see lcd_readback.c) really reads back what lcd_blit() wrote: write a
 * rectangle of pixels to the ST7365P's frame memory, read the same
 * rectangle back over SPI, and compare, pixel for pixel.
 *
 * Runs as a REPL over the picocalc-text-starter console (picocalc_init(),
 * same as picocalc-text-starter-main/main.c by Blair Leduc), so prompts
 * and results are just printf()/getchar(); the pixel test rectangle is
 * drawn well below the console's text rows so the two never overlap.
 *
 * Author: Thomas Dzubin
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"      /* reset_usb_boot() - BOOTSEL reboot */
#include "hardware/watchdog.h" /* watchdog_hw, watchdog_reboot() - exit to the UF2 Loader */

#include "picocalc.h"
#include "lcd.h"
#include "lcd_readback.h"
#include "sdcard.h"   /* sd_card_present() */
#include "fat32.h"    /* fat32_get_status()/_volume_name()/_free_space()/... */
#include "bmp.h"
#include "screenshot.h"
#include "readback_config.h"
#include "screenshot_const.h"   /* SHOT_BATCH_ROWS, for the progress text */

/* keyboard.c and audio.c (picocalc-text-starter-main/drivers/) both refer
 * to this as `extern volatile bool user_interrupt`; a Ctrl+C-style flag
 * normally defined by the starter's own commands.c, which this project
 * doesn't build (see CMakeLists.txt). This tool has no long-running
 * command to interrupt, so it's just provided and never set. */
volatile bool user_interrupt = false;

/* What main() calls. */
static void    readline(char *buffer, size_t size);
static void    cmd_dispatch(char *line);
static void    cmd_help(void);
static void    cmd_test(const char *args);
static void    cmd_pattern(void);
static void    cmd_raw(const char *args);
static void    cmd_solid(const char *args);
static void    cmd_rawbytes(const char *args);
static void    cmd_screenshot(const char *args);
static void    cmd_loadbmp(const char *args);
static void    cmd_screentest(const char *args);
static void    cmd_fullscreenshot(void);
static void    cmd_sdstatus(void);
static void    cmd_exit(void);

int main(void)
{
    stdio_init_all();
    picocalc_init();

    printf("\033c\033[1mpicocalc-screenshot\033[0m\n");
    printf("Checks lcd_unblit() (RAMRD readback) against lcd_blit() (RAMWR).\n");
    printf("Type \033[4mhelp\033[0m for a list of commands.\n\n");

    char line[64];
    for (;;)
    {
        printf("readback> ");
        readline(line, sizeof(line));
        if (line[0] != '\0')
            cmd_dispatch(line);
    }
}

/* ===================================================================== */
/*  Console input (same shape as picocalc-text-starter-main/main.c's own  */
/*  readline(), trimmed to what this tool needs)                          */
/* ===================================================================== */

static void readline(char *buffer, size_t size)
{
    size_t index = 0;
    for (;;)
    {
        char ch = getchar();
        if (ch == '\n' || ch == '\r')
        {
            printf("\n");
            break;
        }
        else if ((ch == 0x08 || ch == 0x7F) && index > 0)
        {
            index--;
            printf("\b \b");
        }
        else if (ch >= 0x20 && ch < 0x7F && index < size - 1)
        {
            buffer[index++] = ch;
            putchar(ch);
        }
    }
    buffer[index] = '\0';
}

/* ===================================================================== */
/*  Tiny xorshift32 PRNG for the random test pixels (a small generator    */
/*  with no float and no hardware-RNG dependency).                        */
/* ===================================================================== */

static uint32_t rng_state;

static uint32_t rand_u32(void)
{
    if (rng_state == 0)
        rng_state = (uint32_t)to_ms_since_boot(get_absolute_time()) | 1u;
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static uint16_t rand_rgb565(void)
{
    return (uint16_t)rand_u32();
}

/* ===================================================================== */
/*  Commands                                                               */
/* ===================================================================== */

static void cmd_dispatch(char *line)
{
    char *args = line;
    while (*args && *args != ' ')
        args++;
    if (*args == ' ')
        *args++ = '\0';
    while (*args == ' ')
        args++;

    if (strcmp(line, "help") == 0)
        cmd_help();
    else if (strcmp(line, "test") == 0)
        cmd_test(args);
    else if (strcmp(line, "pattern") == 0)
        cmd_pattern();
    else if (strcmp(line, "raw") == 0)
        cmd_raw(args);
    else if (strcmp(line, "solid") == 0)
        cmd_solid(args);
    else if (strcmp(line, "rawbytes") == 0)
        cmd_rawbytes(args);
    else if (strcmp(line, "screenshot") == 0)
        cmd_screenshot(args);
    else if (strcmp(line, "loadbmp") == 0)
        cmd_loadbmp(args);
    else if (strcmp(line, "screentest") == 0)
        cmd_screentest(args);
    else if (strcmp(line, "fullscreenshot") == 0)
        cmd_fullscreenshot();
    else if (strcmp(line, "sdstatus") == 0)
        cmd_sdstatus();
    else if (strcmp(line, "bootsel") == 0)
        reset_usb_boot(0, 0);      /* does not return */
    else if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0)
        cmd_exit();                /* does not return */
    else
        printf("Unknown command \"%s\". Type help.\n", line);
}

static void cmd_help(void)
{
    printf("Commands:\n");
    printf("  test [w h count]  write random pixels, read them back, compare\n");
    printf("                    (default %dx%d, %d time(s); max %dx%d, %d times)\n",
           TEST_RECT_W_DEFAULT, TEST_RECT_H_DEFAULT, TEST_COUNT_DEFAULT,
           TEST_RECT_MAX_W, TEST_RECT_MAX_H, TEST_COUNT_MAX);
    printf("  pattern           draw a visible checkerboard, verify it, leave it on\n");
    printf("                    screen for a photo\n");
    printf("  raw x y w h       read back a rectangle and dump it as hex (no write)\n");
    printf("  solid x y w h rrggbb   fill a rectangle with one exact RGB565 colour\n");
    printf("                         (rrggbb: 5-6-5 bit fields packed into 4 hex digits)\n");
    printf("  rawbytes x y w h n     write nothing; dump n raw RAMRD bytes as hex, with\n");
    printf("                         NO dummy-byte skip or RGB565 repacking; for\n");
    printf("                         figuring out the real on-wire byte layout\n");
    printf("  bootsel                reboot into BOOTSEL (USB drive) mode\n");
    printf("  exit (or quit)         leave for the PicoCalc UF2 Loader menu\n");
    printf("  screenshot x y w h     save a rectangle to SD as SCRNxxxx.BMP (24-bit,\n");
    printf("                         xxxx a random hex name); prints the filename\n");
    printf("  loadbmp file x y       load a BMP from SD and display it at (x,y)\n");
    printf("  screentest x y w h destx desty\n");
    printf("                         round-trip test: screenshot (x,y,w,h) to SD,\n");
    printf("                         close it, reopen it, and display it at\n");
    printf("                         (destx,desty)\n");
    printf("  fullscreenshot         the WHOLE %dx%d screen to SD, a few LCD\n", WIDTH, HEIGHT);
    printf("                         rows at a time (no big RAM buffer)\n");
    printf("  sdstatus               SD card presence, mount status, free space\n");
    printf("  help              this text\n");
}

/* Prints a failed result with the fat32 detail when the file system was to blame. */
static void print_shot_error(const char *what, shot_result_t r)
{
    if (r == SHOT_ERROR_FS)
        printf("%s FAILED: %s (%s)\n", what, screenshot_error_string(r),
               fat32_error_string(screenshot_fs_error()));
    else
        printf("%s FAILED: %s\n", what, screenshot_error_string(r));
}

static void print_bmp_error(const char *what, bmp_result_t r)
{
    if (r == BMP_ERROR_FS)
        printf("%s FAILED: %s (%s)\n", what, bmp_error_string(r),
               fat32_error_string(bmp_last_fs_error()));
    else
        printf("%s FAILED: %s\n", what, bmp_error_string(r));
}

static void show_progress(void)
{
    putchar('.');
}

/* Saves a rectangle under a random SCRNxxxx.BMP name, trying another name when
 * one is taken. screenshot_save() leaves the choice of name to the caller. */
static shot_result_t take_screenshot(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                     char *name, size_t name_size)
{
    shot_result_t r = SHOT_ERROR_EXISTS;

    for (int tries = 0; tries < BMP_NAME_MAX_RETRIES && r == SHOT_ERROR_EXISTS; tries++)
    {
        snprintf(name, name_size, SHOT_NAME_FORMAT, (unsigned)(rand_u32() & SHOT_NAME_MASK));
        r = screenshot_save(name, x, y, w, h, show_progress);
    }
    printf("\n");
    return r;
}

/* Writes a width x height block of random RGB565 pixels at (x, y), reads
 * the same block back, and reports whether every pixel matches. Returns
 * true on a full match. */
static bool run_one_test(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    size_t n = (size_t)width * height;
    uint16_t *written = malloc(n * sizeof(uint16_t));
    uint16_t *readback = malloc(n * sizeof(uint16_t));
    if (!written || !readback)
    {
        printf("Out of memory for a %ux%u rectangle; try something smaller.\n", width, height);
        free(written);
        free(readback);
        return false;
    }

    for (size_t i = 0; i < n; i++)
        written[i] = rand_rgb565();

    lcd_blit(written, x, y, width, height);

    absolute_time_t t0 = get_absolute_time();
    lcd_unblit(readback, x, y, width, height);
    int64_t read_us = absolute_time_diff_us(t0, get_absolute_time());

    uint32_t mismatches = 0;
    uint16_t first_col = 0, first_row = 0;
    uint16_t first_written = 0, first_readback = 0;
    for (uint16_t row = 0; row < height; row++)
    {
        for (uint16_t col = 0; col < width; col++)
        {
            size_t i = (size_t)row * width + col;
            if (written[i] != readback[i])
            {
                if (mismatches == 0)
                {
                    first_col = col;
                    first_row = row;
                    first_written = written[i];
                    first_readback = readback[i];
                }
                mismatches++;
            }
        }
    }

    if (mismatches == 0)
        printf("PASS  %ux%u (%u pixels) matched, read in %lld us\n",
               width, height, (unsigned)n, (long long)read_us);
    else
        printf("FAIL  %u of %u pixels differ (first at column %u, row %u: "
               "wrote 0x%04X, read 0x%04X)\n",
               (unsigned)mismatches, (unsigned)n, first_col, first_row, first_written, first_readback);

    free(written);
    free(readback);
    return mismatches == 0;
}

static void cmd_test(const char *args)
{
    int w = TEST_RECT_W_DEFAULT, h = TEST_RECT_H_DEFAULT, count = TEST_COUNT_DEFAULT;
    if (*args)
    {
        int got = sscanf(args, "%d %d %d", &w, &h, &count);
        if (got < 2)
        {
            printf("Usage: test [width height [count]]\n");
            return;
        }
        if (got < 3)
            count = TEST_COUNT_DEFAULT;
    }

    if (w <= 0 || h <= 0 || w > TEST_RECT_MAX_W || h > TEST_RECT_MAX_H)
    {
        printf("Width/height must be 1..%d x 1..%d.\n", TEST_RECT_MAX_W, TEST_RECT_MAX_H);
        return;
    }
    if (count <= 0 || count > TEST_COUNT_MAX)
    {
        printf("Count must be 1..%d.\n", TEST_COUNT_MAX);
        return;
    }
    if (TEST_RECT_X + w > WIDTH || TEST_RECT_Y + h > HEIGHT)
    {
        printf("That rectangle wouldn't fit on screen at (%d,%d).\n", TEST_RECT_X, TEST_RECT_Y);
        return;
    }

    int passed = 0;
    for (int i = 0; i < count; i++)
    {
        if (count > 1)
            printf("[%d/%d] ", i + 1, count);
        if (run_one_test((uint16_t)TEST_RECT_X, (uint16_t)TEST_RECT_Y, (uint16_t)w, (uint16_t)h))
            passed++;
    }
    if (count > 1)
        printf("%d of %d passed.\n", passed, count);
}

/* A checkerboard alternating a handful of named colours, so a readback
 * bug (wrong channel order, a stuck bit, an off-by-one in the unpacking)
 * would show up as a visibly wrong photo, not just a console PASS/FAIL. */
static void cmd_pattern(void)
{
    int w = TEST_RECT_W_DEFAULT, h = TEST_RECT_H_DEFAULT;
    if (TEST_RECT_X + w > WIDTH || TEST_RECT_Y + h > HEIGHT)
    {
        printf("Pattern rectangle wouldn't fit on screen.\n");
        return;
    }

    static const uint16_t palette[] = {
        0xF800, /* red   */
        0x07E0, /* green */
        0x001F, /* blue  */
        0xFFFF, /* white */
    };

    size_t n = (size_t)w * h;
    uint16_t *written = malloc(n * sizeof(uint16_t));
    uint16_t *readback = malloc(n * sizeof(uint16_t));
    if (!written || !readback)
    {
        printf("Out of memory.\n");
        free(written);
        free(readback);
        return;
    }

    for (int row = 0; row < h; row++)
    {
        for (int col = 0; col < w; col++)
        {
            int cell = (row / PATTERN_CELL_PX) + (col / PATTERN_CELL_PX);
            written[(size_t)row * w + col] = palette[cell % (int)(sizeof(palette) / sizeof(palette[0]))];
        }
    }

    lcd_blit(written, (uint16_t)TEST_RECT_X, (uint16_t)TEST_RECT_Y, (uint16_t)w, (uint16_t)h);
    lcd_unblit(readback, (uint16_t)TEST_RECT_X, (uint16_t)TEST_RECT_Y, (uint16_t)w, (uint16_t)h);

    uint32_t mismatches = 0;
    for (size_t i = 0; i < n; i++)
        if (written[i] != readback[i])
            mismatches++;

    if (mismatches == 0)
        printf("PASS  pattern matched (%u pixels). Left on screen; go take your photo.\n", (unsigned)n);
    else
        printf("FAIL  %u of %u pixels differ. Pattern is still on screen either way.\n",
               (unsigned)mismatches, (unsigned)n);

    free(written);
    free(readback);
}

/* Reads an arbitrary rectangle (no write first) and dumps it as hex, so a
 * region that was never touched by this tool; e.g. console text, or
 * whatever was on screen already; can be inspected too. */
static void cmd_raw(const char *args)
{
    int x, y, w, h;
    if (sscanf(args, "%d %d %d %d", &x, &y, &w, &h) != 4)
    {
        printf("Usage: raw x y width height\n");
        return;
    }
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > WIDTH || y + h > HEIGHT ||
        w > TEST_RECT_MAX_W || h > TEST_RECT_MAX_H)
    {
        printf("Rectangle out of range (screen is %dx%d, max %dx%d per read).\n",
               WIDTH, HEIGHT, TEST_RECT_MAX_W, TEST_RECT_MAX_H);
        return;
    }

    size_t n = (size_t)w * h;
    uint16_t *readback = malloc(n * sizeof(uint16_t));
    if (!readback)
    {
        printf("Out of memory.\n");
        return;
    }

    lcd_unblit(readback, (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h);

    for (int row = 0; row < h; row++)
    {
        printf("row %3d:", row);
        for (int col = 0; col < w; col++)
            printf(" %04X", readback[(size_t)row * w + col]);
        printf("\n");
    }

    free(readback);
}

/* Fills a rectangle with one exact RGB565 colour, no randomness; a
 * controlled reference pixel for `rawbytes` to read back over. */
static void cmd_solid(const char *args)
{
    int x, y, w, h;
    unsigned colour;
    if (sscanf(args, "%d %d %d %d %x", &x, &y, &w, &h, &colour) != 5)
    {
        printf("Usage: solid x y width height rrggbb\n");
        return;
    }
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > WIDTH || y + h > HEIGHT || colour > 0xFFFF)
    {
        printf("Rectangle out of range, or colour not 0000-FFFF.\n");
        return;
    }

    size_t n = (size_t)w * h;
    uint16_t *fill = malloc(n * sizeof(uint16_t));
    if (!fill)
    {
        printf("Out of memory.\n");
        return;
    }
    for (size_t i = 0; i < n; i++)
        fill[i] = (uint16_t)colour;

    lcd_blit(fill, (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h);
    printf("Filled (%d,%d) %dx%d with 0x%04X.\n", x, y, w, h, colour);

    free(fill);
}

/* Reads n raw RAMRD bytes back from a window, completely unprocessed;
 * no dummy-byte skip, no RGB565 repacking; so the real on-wire layout
 * can be read directly off the dump instead of guessed at. Pair with
 * `solid` first: a known, unchanging colour over several pixels makes it
 * obvious where any dummy byte ends and where the per-pixel byte pattern
 * starts repeating. */
static void cmd_rawbytes(const char *args)
{
    int x, y, w, h, n;
    if (sscanf(args, "%d %d %d %d %d", &x, &y, &w, &h, &n) != 5)
    {
        printf("Usage: rawbytes x y width height n\n");
        return;
    }
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > WIDTH || y + h > HEIGHT || n <= 0 || n > 512)
    {
        printf("Rectangle out of range, or n not 1-512.\n");
        return;
    }

    uint8_t *raw = malloc((size_t)n);
    if (!raw)
    {
        printf("Out of memory.\n");
        return;
    }

    lcd_unblit_raw((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, raw, (size_t)n);

    for (int i = 0; i < n; i++)
    {
        printf("%02X ", raw[i]);
        if (i % 16 == 15)
            printf("\n");
    }
    if (n % 16 != 0)
        printf("\n");

    free(raw);
}

/* Saves a rectangle to a new SCRNxxxx.BMP on the SD card; see bmp.c. */
static void cmd_screenshot(const char *args)
{
    int x, y, w, h;
    if (sscanf(args, "%d %d %d %d", &x, &y, &w, &h) != 4)
    {
        printf("Usage: screenshot x y width height\n");
        return;
    }
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > WIDTH || y + h > HEIGHT)
    {
        printf("Rectangle out of range (screen is %dx%d).\n", WIDTH, HEIGHT);
        return;
    }

    char name[BMP_FILENAME_LEN];
    shot_result_t r = take_screenshot((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h,
                                      name, sizeof(name));
    if (r == SHOT_OK)
        printf("Saved (%d,%d) %dx%d as %s\n", x, y, w, h, name);
    else
        print_shot_error("Screenshot", r);
}

/* Loads a BMP from the SD card and displays it at (x, y); see bmp.c. */
static void cmd_loadbmp(const char *args)
{
    char name[64];
    int x, y;
    if (sscanf(args, "%63s %d %d", name, &x, &y) != 3)
    {
        printf("Usage: loadbmp filename x y\n");
        return;
    }
    if (x < 0 || y < 0)
    {
        printf("x/y must be 0 or more.\n");
        return;
    }

    uint16_t w, h;
    bmp_result_t r = bmp_load_and_blit(name, (uint16_t)x, (uint16_t)y, &w, &h);
    if (r == BMP_OK)
        printf("Loaded %s (%ux%u) at (%d,%d)\n", name, w, h, x, y);
    else
        print_bmp_error("Load", r);
}

/* The round-trip test Thomas asked for: screenshot a rectangle to SD,
 * close the file, reopen it, and display it back at a different
 * rectangle; proving screenshot_save()/bmp_load_and_blit() (and so
 * the SD card path end to end) work, not just lcd_unblit() on its own. */
static void cmd_screentest(const char *args)
{
    int x, y, w, h, dx, dy;
    if (sscanf(args, "%d %d %d %d %d %d", &x, &y, &w, &h, &dx, &dy) != 6)
    {
        printf("Usage: screentest x y width height destx desty\n");
        return;
    }
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > WIDTH || y + h > HEIGHT)
    {
        printf("Source rectangle out of range.\n");
        return;
    }
    if (dx < 0 || dy < 0 || dx + w > WIDTH || dy + h > HEIGHT)
    {
        printf("Destination rectangle wouldn't fit on screen at (%d,%d).\n", dx, dy);
        return;
    }

    char name[BMP_FILENAME_LEN];
    shot_result_t sr = take_screenshot((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h,
                                       name, sizeof(name));
    if (sr != SHOT_OK)
    {
        print_shot_error("Screenshot", sr);
        return;
    }
    printf("Saved (%d,%d) %dx%d to %s. Reopening and displaying at (%d,%d)...\n",
           x, y, w, h, name, dx, dy);

    uint16_t rw, rh;
    bmp_result_t r = bmp_load_and_blit(name, (uint16_t)dx, (uint16_t)dy, &rw, &rh);
    if (r == BMP_OK)
        printf("PASS  round trip through %s: %ux%u now showing at (%d,%d).\n",
               name, rw, rh, dx, dy);
    else
        print_bmp_error("Load-back", r);
}

/* A full WIDTH x HEIGHT capture, timed. */
static void cmd_fullscreenshot(void)
{
    char name[BMP_FILENAME_LEN];
    printf("Capturing (each . is %d rows written to SD)...\n", SHOT_BATCH_ROWS);
    absolute_time_t t0 = get_absolute_time();
    shot_result_t r = take_screenshot(0, 0, WIDTH, HEIGHT, name, sizeof(name));
    int64_t elapsed_ms = absolute_time_diff_us(t0, get_absolute_time()) / 1000;

    if (r == SHOT_OK)
        printf("Saved full %dx%d screen as %s in %lld ms\n", WIDTH, HEIGHT, name, (long long)elapsed_ms);
    else
        print_shot_error("Full screenshot", r);
}

/* SD card presence, mount status, volume name and free space; so a
 * screenshot failure can be told apart from "no card inserted" without
 * guessing. */
static void cmd_sdstatus(void)
{
    if (!sd_card_present())
    {
        printf("No SD card inserted.\n");
        return;
    }

    fat32_error_t status = fat32_get_status();
    if (status != FAT32_OK)
    {
        printf("SD card inserted, but unreadable: %s\n", fat32_error_string(status));
        return;
    }

    char volume[32] = "";
    fat32_get_volume_name(volume, sizeof(volume));
    uint64_t free_space = 0, total_space = 0;
    fat32_get_free_space(&free_space);
    fat32_get_total_space(&total_space);
    printf("SD card mounted. Volume: \"%s\"  Free: %llu KB / %llu KB  Cluster: %lu bytes\n",
           volume, (unsigned long long)(free_space / 1024), (unsigned long long)(total_space / 1024),
           (unsigned long)fat32_get_cluster_size());
}

/* Leaves the program for the PicoCalc UF2 Loader's menu: prints a short
 * message, asks the loader for its menu (see LOADER_COMMAND_MAGIC in
 * readback_config.h) and reboots with the watchdog. If the program was flashed
 * straight to the chip with no loader, nothing reads the request and it simply
 * restarts. Never returns. */
static void cmd_exit(void)
{
    printf("Back to the loader...\n");
    sleep_ms(LOADER_EXIT_MSG_MS);

    watchdog_hw->scratch[LOADER_SCRATCH_MODE] = LOADER_BOOT_MODE_SD;
    watchdog_hw->scratch[LOADER_SCRATCH_ARGUMENT] = 0;
    watchdog_hw->scratch[LOADER_SCRATCH_MAGIC] = LOADER_COMMAND_MAGIC;
    watchdog_reboot(0, 0, LOADER_REBOOT_DELAY_MS);

    for (;;)
        tight_loop_contents();      /* the reboot comes in a few milliseconds */
}
