# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Out-of-tree board directory lives at <project>/boards (project root = app/..).
if(NOT DEFINED BOARD_ROOT)
  get_filename_component(BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
  set(BOARD_ROOT ${BOARD_ROOT} CACHE PATH "Board root" FORCE)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ZPHR-PIC32-0001)

# --- Local fix for a Zephyr SAM0 UART driver bug (no SDK edit) ---
# uart_sam0.c only caches the TXC flag (used by uart_irq_tx_complete(), which
# RS-485 / Modbus needs to drop DE after TX) when SERCOM_REV500 is defined,
# i.e. REV_SERCOM == 0x500 (SAMD51). The PIC32CM/SAMC21 SERCOM is REV 0x311 but
# DOES have TXC, so without this the Modbus TX never completes -> DRE interrupt
# storm -> lockup/watchdog reset. Force the define for this build only.
zephyr_compile_definitions(SERCOM_REV500)

target_include_directories(app PRIVATE inc)

target_sources(app PRIVATE
    src/main.c
    src/app.c
    src/button.c
    src/sensor.c
    src/modbus.c
    src/shell_nvs.c
    src/setup/setup_hw.c
    src/drivers/rtc.c
    src/drivers/watchdog.c
    src/tasks/task_ui.c
)
