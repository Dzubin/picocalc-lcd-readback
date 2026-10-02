# Changelog

All notable changes to PicoCalc LCD Readback.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added
- Build outputs are named without the `picocalc-` prefix, since the chip at the
  end of the name already says what they are for: `lcd-readback-RP2040.uf2` and
  `lcd-readback-RP2350.uf2`.
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
