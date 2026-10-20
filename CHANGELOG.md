# Changelog

All notable changes to PicoCalc Screenshot.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

- The project is renamed from picocalc-lcd-readback to picocalc-screenshot: the folder, the
  CMake project, the program banner and the output files (`screenshot-RP2040.uf2` and
  `screenshot-RP2350.uf2`).

- Screenshot saving is its own small module for other programs to take:
  `screenshot.c`, `screenshot.h` and `screenshot_const.h`, replacing
  `bmp_save_screenshot()` and `picocalc_screenshot()`. The caller chooses the file
  name, nothing is printed, no clock or random numbers are used, and the file is a
  top-down BMP. The work buffers shrank from about 16 KB to about 4 KB. The README
  lists exactly which starter functions it uses and gives step by step instructions
  for adding it to another program.
- `bmp.c` keeps only loading a BMP back (the test program), and checks that the
  picture fits on the screen at the chosen position.

- Updated the vendored `picocalc-text-starter` to Blair Leduc's code of 2026-10-18 (commit
  `85a002e`) and moved to Pico SDK 2.3.1. The search-hint, deleted-entry and busy-card fixes this
  copy carried are in his code now; four small local fixes remain (README, Third-party). The audio
  driver in it now sounds 20 to 20000 Hz, where it used to be silent outside 100 to 2000 Hz.
- `lcd_unblit()` is in the starter's `drivers/lcd.c` now, so `lcd_readback.c` no longer defines it (both
  copies at once was a link error) and keeps only `lcd_unblit_raw()`. `readback_config.h` uses the
  starter's `LCD_READ_BAUDRATE` instead of its own.
- Vendored starter: `fat32.c` `fat32_write()` no longer leaks a cluster each time a write starts on a
  cluster boundary. The program itself is unchanged; not rebuilt.

- Vendored starter: `fat32.c` skips deleted directory entries before long-name parts (an
  out-of-bounds stack write) and `sdcard.c` waits for a busy card by the clock and before
  every command (failed reads and writes on a card that was still programming). The
  program itself is unchanged; not rebuilt.

### Added
- Build outputs are named without the `picocalc-` prefix, since the chip at the
  end of the name already says what they are for: `screenshot-RP2040.uf2` and
  `screenshot-RP2350.uf2`.
- Leaving: a new `exit` (or `quit`) console command leaves for the PicoCalc UF2
  Loader menu. It asks the loader for its menu through the watchdog scratch
  registers (constants in `readback_config.h`); with no loader installed the
  program just restarts. `bootsel` still reboots into BOOTSEL.

## Earlier work

### Added
- `lcd_unblit()`, the read-side mirror of `lcd_blit()`, built on the LCD
  controller's RAMRD command. Confirmed on real RP2040 hardware: with the
  controller set to 16-bit pixels, pixels read back as big-endian RGB565, byte
  for byte what was written, after one dummy byte.
- A console test tool: write random pixels, read them back and compare; a
  visible test pattern; raw and byte-level dumps for working out the on-wire
  format.
- Screenshots to the SD card as 24-bit BMP files (a rectangle or the whole
  screen) and loading a BMP back onto the screen, streamed one LCD row at a time
  so a full 320x320 image never has to fit in RAM.
- A local fix in the vendored `drivers/fat32.c`: `get_next_free_cluster()`
  advances its search hint past the cluster it hands out, so a long sequential
  write to the SD card no longer re-scans every cluster already given out and
  looks hung.
