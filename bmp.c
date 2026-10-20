/*
 * bmp.c - see bmp.h. Reads a plain 24-bit uncompressed BMP a few rows at a
 * time (BMP_IO_BATCH_ROWS rows per fat32_read() call, then one lcd_blit() per
 * row), so no whole picture is held in RAM.
 *
 * Author: Thomas Dzubin
 */
#include "lcd.h"
#include "fat32.h"

#include "bmp.h"
#include "readback_config.h"

static uint16_t      pixel_row[WIDTH];
static uint8_t       io_batch[BMP_IO_BATCH_ROWS * (WIDTH * BMP_BYTES_PER_PIXEL + 3)];
static fat32_error_t last_fs_error = FAT32_OK;

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)get_le16(p) | ((uint32_t)get_le16(p + 2) << 16);
}

static uint16_t bgr888_to_rgb565(const uint8_t *in)
{
    return (uint16_t)(((in[2] >> 3) << 11) | ((in[1] >> 2) << 5) | (in[0] >> 3));
}

const char *bmp_error_string(bmp_result_t err)
{
    switch (err)
    {
    case BMP_OK:                       return "OK";
    case BMP_ERROR_FS:                 return "file system error";
    case BMP_ERROR_TOO_BIG:            return "picture larger than the screen";
    case BMP_ERROR_NOT_A_BMP:          return "not a BMP file";
    case BMP_ERROR_UNSUPPORTED_FORMAT: return "not a 24-bit uncompressed BMP";
    }
    return "unknown error";
}

fat32_error_t bmp_last_fs_error(void)
{
    return last_fs_error;
}

bmp_result_t bmp_load_and_blit(const char *filename, uint16_t x, uint16_t y,
                               uint16_t *width_out, uint16_t *height_out)
{
    fat32_file_t file;
    uint8_t hdr[BMP_HEADER_SIZE];
    size_t got;
    fat32_error_t fr = fat32_open(&file, filename);

    if (fr != FAT32_OK)
    {
        last_fs_error = fr;
        return BMP_ERROR_FS;
    }

    fr = fat32_read(&file, hdr, sizeof(hdr), &got);
    if (fr != FAT32_OK || got != sizeof(hdr) || hdr[0] != 'B' || hdr[1] != 'M')
    {
        fat32_close(&file);
        return BMP_ERROR_NOT_A_BMP;
    }

    uint32_t data_offset = get_le32(hdr + 10);
    int32_t  raw_width   = (int32_t)get_le32(hdr + 18);
    int32_t  raw_height  = (int32_t)get_le32(hdr + 22);

    if (get_le16(hdr + 28) != BMP_BITS_PER_PIXEL || get_le32(hdr + 30) != BMP_COMPRESSION_BI_RGB ||
        raw_width <= 0)
    {
        fat32_close(&file);
        return BMP_ERROR_UNSUPPORTED_FORMAT;
    }

    int bottom_up = raw_height > 0;
    uint16_t width = (uint16_t)raw_width;
    uint16_t height = (uint16_t)(bottom_up ? raw_height : -raw_height);

    if (height == 0 || (uint32_t)x + raw_width > WIDTH || (uint32_t)y + height > HEIGHT)
    {
        fat32_close(&file);
        return BMP_ERROR_TOO_BIG;
    }

    uint32_t row_bytes = (uint32_t)width * BMP_BYTES_PER_PIXEL;
    uint32_t rec_bytes = row_bytes + (4 - (row_bytes % 4)) % 4;   /* a row as stored */

    fr = fat32_seek(&file, data_offset);
    int ok = (fr == FAT32_OK);

    for (uint16_t i = 0; ok && i < height; )
    {
        int batch_rows = (int)(height - i);
        if (batch_rows > BMP_IO_BATCH_ROWS)
            batch_rows = BMP_IO_BATCH_ROWS;

        size_t want = (size_t)batch_rows * rec_bytes;
        fr = fat32_read(&file, io_batch, want, &got);
        ok = (fr == FAT32_OK && got == want);

        const uint8_t *src = io_batch;
        for (int r = 0; ok && r < batch_rows; r++, src += rec_bytes)
        {
            for (uint16_t col = 0; col < width; col++)
                pixel_row[col] = bgr888_to_rgb565(src + (size_t)col * BMP_BYTES_PER_PIXEL);

            uint16_t file_row = (uint16_t)(i + r);
            uint16_t dest_row = bottom_up ? (uint16_t)(height - 1 - file_row) : file_row;
            lcd_blit(pixel_row, x, (uint16_t)(y + dest_row), width, 1);
        }
        i = (uint16_t)(i + batch_rows);
    }

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
