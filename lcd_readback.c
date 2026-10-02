/*
 * lcd_readback.c - lcd_unblit(): see lcd_readback.h for what this is and
 * readback_config.h for where the numbers below (dummy byte count,
 * bytes/pixel, read baud rate) come from; confirmed on real hardware,
 * see its header comment.
 *
 * Author: Thomas Dzubin
 */
#include "hardware/spi.h"
#include "hardware/sync.h"   /* save_and_disable_interrupts()/restore_interrupts() */

#include "lcd_readback.h"
#include "readback_config.h"

/* One row's worth of raw pixel bytes read off the bus before they are
 * repacked into RGB565. Static (not a local array) for the same reason
 * lcd_solid_rectangle() in lcd.c keeps its own row buffer static: WIDTH
 * (320) * 2 bytes is small, but a repeated large stack array per call
 * still isn't worth it, and the RP2040/RP2350 have plenty of RAM to spare
 * a fixed one. */
static uint8_t read_row[WIDTH * LCD_READ_BYTES_PER_PIXEL];

/* Sets the read window (CASET/RASET, same as lcd_blit()'s own private
 * lcd_set_window()) and sends RAMRD, leaving CSX low and D/CX high so the
 * caller can read data bytes immediately; shared by lcd_unblit() and
 * lcd_unblit_raw(). Also drops the SPI bus to LCD_READ_BAUDRATE; restoring
 * it and raising CSX again is the caller's job once the read is done. See
 * lcd_unblit()'s own comment for why RAMRD can't go through the existing
 * public lcd_write_cmd() the way CASET/RASET do.                         */
static void start_ramrd(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    lcd_write_cmd(LCD_CMD_CASET);
    lcd_write_data(4,
                   UPPER8(x), LOWER8(x),
                   UPPER8((uint16_t)(x + width - 1)), LOWER8((uint16_t)(x + width - 1)));

    lcd_write_cmd(LCD_CMD_RASET);
    lcd_write_data(4,
                   UPPER8(y), LOWER8(y),
                   UPPER8((uint16_t)(y + height - 1)), LOWER8((uint16_t)(y + height - 1)));

    spi_set_baudrate(LCD_SPI, LCD_READ_BAUDRATE);

    gpio_put(LCD_DCX, 0);      /* command   */
    gpio_put(LCD_CSX, 0);      /* CSX low for the whole read that follows */
    uint8_t cmd = LCD_CMD_RAMRD;
    spi_write_blocking(LCD_SPI, &cmd, 1);

    gpio_put(LCD_DCX, 1);      /* data phase starts here                  */
}

void lcd_unblit(uint16_t *pixels, uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    /* lcd_blit() (lcd.c) disables interrupts for its whole body, and for
     * good reason: the PicoCalc's cursor blinks via a 500ms hardware
     * timer interrupt that itself calls lcd_blit() to draw/erase it. If
     * that fires while this hand-rolled RAMRD read has CSX held low
     * mid-transaction, the interrupt drives the SAME SPI1 peripheral and
     * pins out from under it; confirmed on real hardware
     * to leave a later spi_read_blocking() spinning forever
     * waiting for a hardware "done" flag it will never see, an
     * intermittent full hang depending only on 500ms timing luck against
     * however long a call happens to take. lcd_blit()'s own
     * lcd_disable_interrupts()/lcd_enable_interrupts() are file-private
     * to lcd.c, so this uses the same underlying pico-sdk primitives
     * directly instead.                                                 */
    uint32_t irq_state = save_and_disable_interrupts();

    start_ramrd(x, y, width, height);

    /* Discard the dummy byte(s); see readback_config.h.                  */
    uint8_t dummy[LCD_READ_DUMMY_BYTES];
    spi_read_blocking(LCD_SPI, 0x00, dummy, LCD_READ_DUMMY_BYTES);

    for (uint16_t row = 0; row < height; row++)
    {
        spi_read_blocking(LCD_SPI, 0x00, read_row, (size_t)width * LCD_READ_BYTES_PER_PIXEL);

        /* Big-endian RGB565, same as lcd_blit() writes; see
         * readback_config.h. */
        uint16_t *out = pixels + (size_t)row * width;
        const uint8_t *in = read_row;
        for (uint16_t col = 0; col < width; col++, in += LCD_READ_BYTES_PER_PIXEL)
            out[col] = (uint16_t)((in[0] << 8) | in[1]);
    }

    gpio_put(LCD_CSX, 1);      /* only now is it safe to send a new command */

    spi_set_baudrate(LCD_SPI, LCD_BAUDRATE);

    restore_interrupts(irq_state);
}

void lcd_unblit_raw(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                     uint8_t *raw, size_t n)
{
    /* See lcd_unblit()'s comment just above; same reasoning applies. */
    uint32_t irq_state = save_and_disable_interrupts();

    start_ramrd(x, y, width, height);

    spi_read_blocking(LCD_SPI, 0x00, raw, n);

    gpio_put(LCD_CSX, 1);
    spi_set_baudrate(LCD_SPI, LCD_BAUDRATE);

    restore_interrupts(irq_state);
}
