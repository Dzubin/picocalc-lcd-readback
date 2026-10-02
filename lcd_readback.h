/*
 * lcd_readback.h - lcd_unblit(): the read-side mirror of lcd_blit()
 * (drivers/lcd.c, by Blair Leduc). Implemented as a separate file so the
 * vendored picocalc-text-starter-main/ tree stays untouched.  Everything
 * here is built purely on lcd.h's already-public pins, command macros and
 * lcd_write_cmd()/lcd_write_data(), plus hardware/spi.h directly for the
 * one piece lcd.h doesn't expose: driving a read cycle.
 *
 * Author: Thomas Dzubin
 */
#pragma once

#include <stddef.h>   /* size_t */

#include "lcd.h"   /* WIDTH, HEIGHT, LCD_*, lcd_write_cmd/lcd_write_data */

/* Reads back a width x height block of RGB565 pixels starting at (x, y),
 * in the same row-major order lcd_blit() writes them in, into `pixels`
 * (caller-owned, width * height uint16_t entries). See readback_config.h
 * for the RAMRD protocol details this is built from, confirmed on real
 * hardware: a dummy byte, then two bytes per pixel, big-endian RGB565;
 * the same format lcd_blit() writes, byte for byte.
 *
 * Slower than lcd_blit(): the SPI bus is dropped to LCD_READ_BAUDRATE for
 * the read (see readback_config.h for why) and restored to LCD_BAUDRATE
 * before returning. Call with interrupts already however the caller wants
 * them; unlike lcd_blit(), this does not touch them itself, since it is
 * meant to be called from straight-line test-program code, not from
 * inside the display driver's own critical sections.
 */
void lcd_unblit(uint16_t *pixels, uint16_t x, uint16_t y, uint16_t width, uint16_t height);

/* Debug/bring-up helper: issues the same CASET/RASET/RAMRD sequence as
 * lcd_unblit() against a (width x height) window, then reads exactly `n`
 * raw bytes into `raw` with NO dummy-byte discard and NO RGB565
 * repacking; so the real on-wire byte layout can be inspected directly
 * instead of guessed at. See readback_config.h's comment for why this
 * exists: the datasheet excerpt available for RAMRD's SPI byte format
 * was inconclusive. */
void lcd_unblit_raw(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                     uint8_t *raw, size_t n);
