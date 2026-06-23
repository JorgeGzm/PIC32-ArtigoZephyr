# Copyright (c) 2026
# SPDX-License-Identifier: Apache-2.0

# Flash/debug via pyOCD using the Microchip part identity.
# Requires the DFP:  pyocd pack install pic32cm1216mc00032
# (The die is an Atmel SAMC21E17A; --target=atsamc21e17a + the Keil.SAM-C_DFP
#  also works if you prefer the Atmel identity.)
board_runner_args(pyocd "--target=pic32cm1216mc00032")
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
