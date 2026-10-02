/*
 * bmp.c - see bmp.h. Reads/writes plain, uncompressed 24-bit BMP files
 * (BITMAPFILEHEADER + BITMAPINFOHEADER, BI_RGB, bottom-up row order --
 * the ordinary, most widely compatible BMP shape) by hand, a byte at a
 * time via little-endian put/get helpers, rather than an
 * __attribute__((packed)) struct overlaid on the file bytes: on-disk
 * layout and in-memory struct layout can still disagree across compilers
 * even with packing attributes, and this way there's nothing to get
 * subtly wrong.
 *
 * The LCD side works ONE ROW AT A TIME (Thomas, 2026-09-28): never a
 * whole WIDTH x HEIGHT pixel buffer in RAM, which for the PicoCalc's full
 * 320x320 screen would be 320*320*2 = 200KB, more than half the RP2040's
 * total 264KB of SRAM. The SD card side batches BMP_IO_BATCH_ROWS rows
 * into each fat32_read()/fat32_write() call instead of one row at a time
 * -- see readback_config.h's comment on BMP_IO_BATCH_ROWS for why a
 * batch of rows, not the single row the LCD side uses, turned out to be
 * necessary there too: fat32.c's own read/write calls get slower as a
 * file grows, badly enough that doing them one small row at a time made
 * a full screenshot take minutes with no visible progress in the
 * meantime. Both buffers below stay small and fixed-size regardless of
 * screenshot size either way.
 *
 * Author: Thomas Dzubin
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "pico/time.h"

#include "sdcard.h"   /* sd_card_present() */
#include "fat32.h"

#include "bmp.h"
#include "lcd.h"
#include "lcd_readback.h"
#include "readback_config.h"

/* One LCD row's worth of pixels, RGB565 -- the unit lcd_unblit()/
 * lcd_blit() work in. */
static uint16_t pixel_row[BMP_RECT_MAX_W];

/* Up to BMP_IO_BATCH_ROWS rows' worth of on-disk bytes (BGR888, each row
 * followed immediately by its own padding, exactly as the file itself
 * lays them out) -- the unit fat32_write()/fat32_read() work in. Both
 * static for the same reason lcd_readback.c's own row buffer is: bounded,
 * small, and not worth a fresh allocation per call. */
static uint8_t io_batch[BMP_IO_BATCH_ROWS * (BMP_RECT_MAX_W * BMP_BYTES_PER_PIXEL + 3)];

/* The most recent fat32_error_t behind a BMP_ERROR_FS result, so a
 * caller that wants more detail than the generic bmp_error_string() can
 * ask for it, without bmp.c needing an error-message-formatting API of
 * its own. (bmp.c does print its own "." progress dots per I/O batch --
 * see BMP_IO_BATCH_ROWS's comment in readback_config.h for why a
 * multi-second-plus operation with literally no feedback turned out to
 * be worth avoiding -- but never an error message; that's still the
 * caller's job.) */
static fat32_error_t last_fs_error = FAT32_OK;

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

/* ---- little-endian byte <-> value helpers ----------------------------- */

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- pixel format conversion ------------------------------------------ */

static inline void rgb565_to_bgr888(uint16_t px, uint8_t *out)
{
    uint8_t r5 = (uint8_t)((px >> 11) & 0x1F);
    uint8_t g6 = (uint8_t)((px >> 5) & 0x3F);
    uint8_t b5 = (uint8_t)(px & 0x1F);
    /* Replicate the top bits into the newly widened low bits -- the
     * ordinary "expand N bits to 8 evenly" trick. */
    out[0] = (uint8_t)((b5 << 3) | (b5 >> 2));   /* B */
    out[1] = (uint8_t)((g6 << 2) | (g6 >> 4));   /* G */
    out[2] = (uint8_t)((r5 << 3) | (r5 >> 2));   /* R */
}

static inline uint16_t bgr888_to_rgb565(const uint8_t *in)
{
    uint8_t b = in[0], g = in[1], r = in[2];
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* ---- error strings ------------------------------------------------------ */

const char *bmp_error_string(bmp_result_t err)
{
    switch (err)
    {
    case BMP_OK:                       return "OK";
    case BMP_ERROR_NO_CARD:            return "no SD card present";
    case BMP_ERROR_FS:                 return "file system error";
    case BMP_ERROR_MEMORY:             return "out of memory";
    case BMP_ERROR_TOO_BIG:            return "rectangle too large";
    case BMP_ERROR_NAME_TAKEN:         return "could not find a free SCRNxxxx.BMP name";
    case BMP_ERROR_NOT_A_BMP:          return "not a BMP file";
    case BMP_ERROR_UNSUPPORTED_FORMAT: return "not a 24-bit uncompressed BMP";
    }
    return "unknown error";
}

const char *bmp_last_fs_error_string(void)
{
    return fat32_error_string(last_fs_error);
}

/* ---- saving ------------------------------------------------------------ */

static void build_headers(uint8_t hdr[BMP_HEADER_SIZE], uint16_t width, uint16_t height,
                           uint32_t row_bytes_padded)
{
    memset(hdr, 0, BMP_HEADER_SIZE);
    uint32_t pixel_bytes = row_bytes_padded * height;

    hdr[0] = 'B';
    hdr[1] = 'M';
    put_le32(hdr + 2, BMP_HEADER_SIZE + pixel_bytes);   /* bfSize     */
    put_le32(hdr + 10, BMP_HEADER_SIZE);                /* bfOffBits  */

    put_le32(hdr + 14, BMP_INFO_HEADER_SIZE);           /* biSize     */
    put_le32(hdr + 18, width);                          /* biWidth    */
    put_le32(hdr + 22, height);                         /* biHeight, positive = bottom-up */
    put_le16(hdr + 26, 1);                               /* biPlanes   */
    put_le16(hdr + 28, BMP_BITS_PER_PIXEL);             /* biBitCount */
    put_le32(hdr + 30, BMP_COMPRESSION_BI_RGB);         /* biCompression */
    put_le32(hdr + 34, pixel_bytes);                    /* biSizeImage */
}

bmp_result_t bmp_save_screenshot(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                  char *filename_out, size_t filename_out_size)
{
    if (width == 0 || height == 0 || width > BMP_RECT_MAX_W || height > BMP_RECT_MAX_H)
        return BMP_ERROR_TOO_BIG;
    if (!sd_card_present())
        return BMP_ERROR_NO_CARD;

    char name[BMP_FILENAME_LEN];
    fat32_file_t file;
    fat32_error_t fr = FAT32_ERROR_FILE_EXISTS;
    for (int tries = 0; tries < BMP_NAME_MAX_RETRIES && fr == FAT32_ERROR_FILE_EXISTS; tries++)
    {
        snprintf(name, sizeof(name), "SCRN%04X.BMP", (unsigned)(rand_u32() & 0xFFFFu));
        fr = fat32_create(&file, name);
    }
    if (fr == FAT32_ERROR_FILE_EXISTS)
        return BMP_ERROR_NAME_TAKEN;
    if (fr != FAT32_OK)
    {
        last_fs_error = fr;
        return BMP_ERROR_FS;
    }

    uint32_t row_bytes = (uint32_t)width * BMP_BYTES_PER_PIXEL;
    uint32_t pad = (uint32_t)(4 - (row_bytes % 4)) % 4;

    uint8_t hdr[BMP_HEADER_SIZE];
    build_headers(hdr, width, height, row_bytes + pad);

    size_t written;
    fr = fat32_write(&file, hdr, sizeof(hdr), &written);
    bool ok = (fr == FAT32_OK && written == sizeof(hdr));

    uint32_t rec_bytes = row_bytes + pad;   /* one row's bytes on disk */

    /* The LCD is read one row at a time (see this file's header comment);
     * the SD card is written BMP_IO_BATCH_ROWS rows at a time
     * (readback_config.h explains why one row at a time there was too
     * slow). Bottom-up BMP order, so the screen's LAST row is written
     * first. */
    int16_t row = (int16_t)height - 1;
    while (ok && row >= 0)
    {
        size_t offset = 0;
        int batch_rows = 0;
        while (row >= 0 && batch_rows < BMP_IO_BATCH_ROWS)
        {
            lcd_unblit(pixel_row, x, (uint16_t)(y + row), width, 1);

            uint8_t *dst = io_batch + offset;
            for (uint16_t col = 0; col < width; col++)
                rgb565_to_bgr888(pixel_row[col], dst + (size_t)col * BMP_BYTES_PER_PIXEL);
            if (pad)
                memset(dst + row_bytes, 0, pad);

            offset += rec_bytes;
            row--;
            batch_rows++;
        }

        fr = fat32_write(&file, io_batch, offset, &written);
        ok = (fr == FAT32_OK && written == offset);

        putchar('.');
        fflush(stdout);
    }
    printf("\n");   /* height was already checked > 0 above */

    fat32_close(&file);

    if (!ok)
    {
        last_fs_error = fr;
        fat32_delete(name);   /* best-effort cleanup of a partial file */
        return BMP_ERROR_FS;
    }

    if (filename_out)
        snprintf(filename_out, filename_out_size, "%s", name);
    return BMP_OK;
}

bmp_result_t picocalc_screenshot(char *filename_out, size_t filename_out_size)
{
    return bmp_save_screenshot(0, 0, WIDTH, HEIGHT, filename_out, filename_out_size);
}

/* ---- loading ------------------------------------------------------------ */

bmp_result_t bmp_load_and_blit(const char *filename, uint16_t x, uint16_t y,
                                uint16_t *width_out, uint16_t *height_out)
{
    fat32_file_t file;
    fat32_error_t fr = fat32_open(&file, filename);
    if (fr != FAT32_OK)
    {
        last_fs_error = fr;
        return BMP_ERROR_FS;
    }

    uint8_t hdr[BMP_HEADER_SIZE];
    size_t got;
    fr = fat32_read(&file, hdr, sizeof(hdr), &got);
    if (fr != FAT32_OK || got != sizeof(hdr) || hdr[0] != 'B' || hdr[1] != 'M')
    {
        fat32_close(&file);
        return BMP_ERROR_NOT_A_BMP;
    }

    uint32_t data_offset = get_le32(hdr + 10);
    int32_t  raw_width   = (int32_t)get_le32(hdr + 18);
    int32_t  raw_height  = (int32_t)get_le32(hdr + 22);
    uint16_t bit_count   = get_le16(hdr + 28);
    uint32_t compression = get_le32(hdr + 30);

    if (bit_count != BMP_BITS_PER_PIXEL || compression != BMP_COMPRESSION_BI_RGB || raw_width <= 0)
    {
        fat32_close(&file);
        return BMP_ERROR_UNSUPPORTED_FORMAT;
    }

    bool bottom_up = raw_height > 0;
    uint16_t width = (uint16_t)raw_width;
    uint16_t height = (uint16_t)(bottom_up ? raw_height : -raw_height);
    if (width == 0 || height == 0 || width > BMP_RECT_MAX_W || height > BMP_RECT_MAX_H)
    {
        fat32_close(&file);
        return BMP_ERROR_TOO_BIG;
    }

    uint32_t row_bytes = (uint32_t)width * BMP_BYTES_PER_PIXEL;
    uint32_t pad = (uint32_t)(4 - (row_bytes % 4)) % 4;

    fr = fat32_seek(&file, data_offset);
    bool ok = (fr == FAT32_OK);

    uint32_t rec_bytes = row_bytes + pad;   /* one row's bytes on disk */

    /* SD card read BMP_IO_BATCH_ROWS rows at a time, same reasoning as
     * the save side; the LCD is still drawn one row at a time (each file
     * row is decoded and lcd_blit()'d to its own destination row
     * immediately once its batch is in memory), so no whole-image pixel
     * buffer is needed on this side either. File row i is image row
     * (height-1-i) from the top when bottom-up, or image row i directly
     * when top-down; either way, since each row is drawn independently,
     * the file's own row order doesn't have to match the screen's
     * top-to-bottom order. */
    for (uint16_t i = 0; ok && i < height; )
    {
        int batch_rows = (int)(height - i);
        if (batch_rows > BMP_IO_BATCH_ROWS)
            batch_rows = BMP_IO_BATCH_ROWS;

        size_t want = (size_t)batch_rows * rec_bytes;
        fr = fat32_read(&file, io_batch, want, &got);
        ok = (fr == FAT32_OK && got == want);

        if (ok)
        {
            const uint8_t *src = io_batch;
            for (int r = 0; r < batch_rows; r++, src += rec_bytes)
            {
                for (uint16_t col = 0; col < width; col++)
                    pixel_row[col] = bgr888_to_rgb565(src + (size_t)col * BMP_BYTES_PER_PIXEL);

                uint16_t file_row = (uint16_t)(i + r);
                uint16_t dest_row = bottom_up ? (uint16_t)(height - 1 - file_row) : file_row;
                lcd_blit(pixel_row, x, (uint16_t)(y + dest_row), width, 1);
            }
        }

        i = (uint16_t)(i + batch_rows);
        putchar('.');
        fflush(stdout);
    }
    printf("\n");

    fat32_close(&file);

    if (!ok)
    {
        last_fs_error = fr;
        return BMP_ERROR_FS;
    }

    if (width_out)
        *width_out = width;
    if (height_out)
        *height_out = height;
    return BMP_OK;
}
