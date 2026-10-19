/*
 * lcd_readback.h - lcd_unblit_raw(): the raw RAMRD probe of the LCD readback
 * test program. lcd_unblit(), the read-side mirror of lcd_blit(), began here
 * and is in the starter's drivers/lcd.c now (declared in lcd.h). This is built
 * purely on lcd.h's already-public pins, command macros and
 * lcd_write_cmd()/lcd_write_data(), plus hardware/spi.h directly for the one
 * piece lcd.h doesn't expose: driving a read cycle.
 *
 * Author: Thomas Dzubin
 */
#pragma once

#include <stddef.h>   /* size_t */

#include "lcd.h"   /* WIDTH, HEIGHT, LCD_*, lcd_write_cmd/lcd_write_data */

/* Debug/bring-up helper: issues the same CASET/RASET/RAMRD sequence as
 * lcd_unblit() (lcd.h) against a (width x height) window, then reads exactly `n`
 * raw bytes into `raw` with NO dummy-byte discard and NO RGB565
 * repacking; so the real on-wire byte layout can be inspected directly
 * instead of guessed at. See readback_config.h's comment for why this
 * exists: the datasheet excerpt available for RAMRD's SPI byte format
 * was inconclusive. */
void lcd_unblit_raw(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                     uint8_t *raw, size_t n);
