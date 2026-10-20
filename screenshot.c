/*
 * screenshot.c - see screenshot.h.
 *
 * The screen is read one row at a time (lcd_unblit()) and written to the card
 * SHOT_BATCH_ROWS rows at a time, so the RAM used is a few KB however big the
 * picture is. The BMP is built by hand, byte by byte, so nothing depends on how
 * a compiler lays out a struct.
 *
 * Author: Thomas Dzubin
 */
#include <string.h>

#include "sdcard.h"             /* sd_card_present() */
#include "lcd.h"                /* lcd_unblit(), WIDTH, HEIGHT */

#include "screenshot.h"
#include "screenshot_const.h"

static uint16_t      pixel_row[WIDTH];          /* one LCD row, RGB565 */
static uint8_t       batch[SHOT_BATCH_BYTES];   /* rows as they go to the file */
static fat32_error_t last_fs_error = FAT32_OK;

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    put_le16(p, (uint16_t)(v & 0xFFFF));
    put_le16(p + 2, (uint16_t)(v >> 16));
}

/* The header of a top-down BMP: the height is stored negative. */
static void build_header(uint8_t *hdr, uint16_t width, uint16_t height, uint32_t row_bytes)
{
    uint32_t image_bytes = row_bytes * height;

    memset(hdr, 0, SHOT_HEADER_SIZE);
    hdr[0] = 'B';
    hdr[1] = 'M';
    put_le32(hdr + SHOT_OFF_FILE_SIZE, SHOT_HEADER_SIZE + image_bytes);
    put_le32(hdr + SHOT_OFF_PIXEL_OFFSET, SHOT_HEADER_SIZE);
    put_le32(hdr + SHOT_OFF_INFO_SIZE, SHOT_INFO_HEADER_SIZE);
    put_le32(hdr + SHOT_OFF_WIDTH, width);
    put_le32(hdr + SHOT_OFF_HEIGHT, (uint32_t)(-(int32_t)height));
    put_le16(hdr + SHOT_OFF_PLANES, 1);
    put_le16(hdr + SHOT_OFF_BIT_COUNT, SHOT_BITS_PER_PIXEL);
    put_le32(hdr + SHOT_OFF_IMAGE_SIZE, image_bytes);
}

/* RGB565 to the file's blue, green, red bytes; the top bits of each colour are
 * repeated into the new low bits so white stays white. */
static void put_pixel(uint8_t *out, uint16_t px)
{
    uint8_t r = (uint8_t)((px >> 11) & 0x1F);
    uint8_t g = (uint8_t)((px >> 5) & 0x3F);
    uint8_t b = (uint8_t)(px & 0x1F);

    out[0] = (uint8_t)((b << 3) | (b >> 2));
    out[1] = (uint8_t)((g << 2) | (g >> 4));
    out[2] = (uint8_t)((r << 3) | (r >> 2));
}

shot_result_t screenshot_save(const char *filename, uint16_t x, uint16_t y,
                              uint16_t width, uint16_t height,
                              shot_progress_fn progress)
{
    fat32_file_t file;
    fat32_error_t fr;
    size_t written;
    uint8_t header[SHOT_HEADER_SIZE];
    uint32_t row_bytes = (uint32_t)width * SHOT_BYTES_PER_PIXEL;
    uint32_t pad = (4 - (row_bytes % 4)) % 4;
    uint16_t row = 0;
    int ok;

    if (width == 0 || height == 0 || (uint32_t)x + width > WIDTH || (uint32_t)y + height > HEIGHT)
        return SHOT_ERROR_RANGE;
    if (!sd_card_present())
        return SHOT_ERROR_NO_CARD;

    fr = fat32_create(&file, filename);
    if (fr == FAT32_ERROR_FILE_EXISTS)
        return SHOT_ERROR_EXISTS;
    if (fr != FAT32_OK)
    {
        last_fs_error = fr;
        return SHOT_ERROR_FS;
    }

    build_header(header, width, height, row_bytes + pad);
    fr = fat32_write(&file, header, sizeof(header), &written);
    ok = (fr == FAT32_OK && written == sizeof(header));

    while (ok && row < height)
    {
        size_t used = 0;

        for (int n = 0; n < SHOT_BATCH_ROWS && row < height; n++, row++)
        {
            lcd_unblit(pixel_row, x, (uint16_t)(y + row), width, 1);
            for (uint16_t col = 0; col < width; col++)
                put_pixel(batch + used + (size_t)col * SHOT_BYTES_PER_PIXEL, pixel_row[col]);
            memset(batch + used + row_bytes, 0, pad);
            used += row_bytes + pad;
        }
        fr = fat32_write(&file, batch, used, &written);
        ok = (fr == FAT32_OK && written == used);
        if (progress)
            progress();
    }

    fat32_close(&file);
    if (!ok)
    {
        last_fs_error = fr;
        fat32_delete(filename);
        return SHOT_ERROR_FS;
    }
    return SHOT_OK;
}

shot_result_t screenshot_save_screen(const char *filename, shot_progress_fn progress)
{
    return screenshot_save(filename, 0, 0, WIDTH, HEIGHT, progress);
}

const char *screenshot_error_string(shot_result_t result)
{
    switch (result)
    {
    case SHOT_OK:            return "OK";
    case SHOT_ERROR_NO_CARD: return "no SD card present";
    case SHOT_ERROR_RANGE:   return "rectangle is empty or off the screen";
    case SHOT_ERROR_EXISTS:  return "that file already exists";
    case SHOT_ERROR_FS:      return "file system error";
    }
    return "unknown error";
}

fat32_error_t screenshot_fs_error(void)
{
    return last_fs_error;
}
