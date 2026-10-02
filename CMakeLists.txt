# PicoCalc LCD Readback Test (by Thomas Dzubin)
#
# A small console tool that checks lcd_unblit() (this project's own
# addition, lcd_readback.c) against lcd_blit(), both from
# picocalc-text-starter-main/drivers/lcd.c by Blair Leduc. Runs the
# starter's own console stack (picocalc_init()), so it needs the same
# driver set the starter's own main.c does, minus the starter's demo REPL
# (main.c/commands.c/songs.c/tests.c) and serial.c (unused by anything
# here).

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in the Pico SDK (must be before project())
include(pico_sdk_import.cmake)

project(picocalc-lcd-readback C CXX ASM)

pico_sdk_init()

# Location of the picocalc-text-starter drivers we build against
set(STARTER ${CMAKE_CURRENT_LIST_DIR}/picocalc-text-starter-main)

add_executable(picocalc-lcd-readback
    main.c
    lcd_readback.c
    lcd_readback.h
    bmp.c
    bmp.h
    readback_config.h
    ${STARTER}/drivers/lcd.c
    ${STARTER}/drivers/southbridge.c
    ${STARTER}/drivers/keyboard.c
    ${STARTER}/drivers/display.c
    ${STARTER}/drivers/picocalc.c
    ${STARTER}/drivers/audio.c
    ${STARTER}/drivers/fat32.c
    ${STARTER}/drivers/sdcard.c
    ${STARTER}/drivers/clib.c
    ${STARTER}/drivers/font-8x10.c
    ${STARTER}/drivers/font-5x10.c
)

pico_set_program_name(picocalc-lcd-readback "picocalc-lcd-readback")
pico_set_program_version(picocalc-lcd-readback "0.1")

# audio.c needs its PIO program's generated header (audio.pio.h), pulled
# in transitively through picocalc_init().
pico_generate_pio_header(picocalc-lcd-readback ${STARTER}/drivers/audio.pio)

# No serial console needed; keep USB free so ~ -> BOOTSEL is clean, and so
# stdio really does go over the PicoCalc's own screen/keyboard, not a USB
# serial port.
pico_enable_stdio_uart(picocalc-lcd-readback 0)
pico_enable_stdio_usb(picocalc-lcd-readback 0)

target_compile_options(picocalc-lcd-readback PRIVATE -Wall)

target_include_directories(picocalc-lcd-readback PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${STARTER}/drivers
)

target_link_libraries(picocalc-lcd-readback
    pico_stdlib
    hardware_gpio
    hardware_i2c
    hardware_spi
    hardware_pio
    hardware_clocks
    hardware_watchdog   # watchdog_reboot(): leaving for the UF2 Loader
)

# The chip is part of the output file name (lcd-readback-RP2040.uf2 /
# -RP2350.uf2, and the matching .elf/.bin/.hex), so a downloaded file
# always says which board it was built for.
if(PICO_PLATFORM MATCHES "^rp2040")
    set(CHIP_NAME RP2040)
elseif(PICO_PLATFORM MATCHES "^rp2350")
    set(CHIP_NAME RP2350)
else()
    set(CHIP_NAME ${PICO_PLATFORM})
endif()
set_target_properties(picocalc-lcd-readback PROPERTIES OUTPUT_NAME lcd-readback-${CHIP_NAME})

pico_add_extra_outputs(picocalc-lcd-readback)

# Copy the finished .uf2 (same name) to the top-level project folder, so
# the RP2040 and RP2350 builds can sit side by side.
add_custom_command(TARGET picocalc-lcd-readback POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE_DIR:picocalc-lcd-readback>/$<TARGET_FILE_BASE_NAME:picocalc-lcd-readback>.uf2
            ${CMAKE_CURRENT_LIST_DIR}/
)
