/*
 * lcd_readback.c - lcd_unblit_raw(): see lcd_readback.h for what this is and
 * readback_config.h for where the RAMRD numbers come from; confirmed on real
 * hardware, see its header comment. lcd_unblit() itself began in this file and
 * is in the starter's drivers/lcd.c now.
 *
 * Author: Thomas Dzubin
 */
#include "hardware/spi.h"
#include "hardware/sync.h"   /* save_and_disable_interrupts()/restore_interrupts() */

#include "lcd_readback.h"
#include "readback_config.h"

/* Sets the read window (CASET/RASET, same as lcd_blit()'s own private
 * lcd_set_window()) and sends RAMRD, leaving CSX low and D/CX high so the
 * caller can read data bytes immediately (used by lcd_unblit_raw()). Also
 * drops the SPI bus to LCD_READ_BAUDRATE; restoring it and raising CSX again
 * is the caller's job once the read is done. RAMRD can't go through the
 * existing public lcd_write_cmd() the way CASET/RASET do; lcd_unblit() in the
 * starter's drivers/lcd.c does the same thing.                              */
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

void lcd_unblit_raw(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                     uint8_t *raw, size_t n)
{
    /* Interrupts off for the whole read, as lcd_blit() does: the cursor's timer
     * interrupt drives the same SPI peripheral and would corrupt a read. */
    uint32_t irq_state = save_and_disable_interrupts();

    start_ramrd(x, y, width, height);

    spi_read_blocking(LCD_SPI, 0x00, raw, n);

    gpio_put(LCD_CSX, 1);
    spi_set_baudrate(LCD_SPI, LCD_BAUDRATE);

    restore_interrupts(irq_state);
}
