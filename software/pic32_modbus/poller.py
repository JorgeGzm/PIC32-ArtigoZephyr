"""Read-all-and-decode helper used by the GUI polling worker.

One Modbus frame per area (no per-point round-trips), then decode every
:class:`Register` into a display string. Transport/UI agnostic — returns
plain dataclasses.
"""

from __future__ import annotations

import calendar
import time
from dataclasses import dataclass

from .modbus_model import ModbusModel, ReadResult
from .registers import REGISTERS, Area, Register, decode

# words to fetch per area (covers the whole map)
AREA_BLOCKS = {
    Area.COIL: (0, 2),
    Area.DISCRETE: (0, 1),
    Area.INPUT: (0, 10),    # 0..9 (incl. RTC time at 8/9)
    Area.HOLDING: (0, 2),
}

# Holding regs that hold the "set time" 32-bit epoch (hi, lo).
TIME_SET_ADDR = 1


def local_epoch_now() -> int:
    """Current LOCAL wall-clock encoded as a UTC epoch.

    ``calendar.timegm`` treats the local broken-down time as if it were UTC,
    so the device (which formats the stored epoch with gmtime) shows the
    operator's local wall-clock — no timezone handling on the MCU.
    """
    return calendar.timegm(time.localtime())


def sync_time(model: ModbusModel) -> ReadResult:
    """Write the PC's current local time to the device RTC (FC16)."""
    epoch = local_epoch_now()
    hi = (epoch >> 16) & 0xFFFF
    lo = epoch & 0xFFFF
    return model.write_registers(TIME_SET_ADDR, [hi, lo])


@dataclass
class Cell:
    reg: Register
    text: str
    ok: bool


def _read_area(model: ModbusModel, area: Area):
    start, qty = AREA_BLOCKS[area]
    if area is Area.COIL:
        return model.read_coils(start, qty)
    if area is Area.DISCRETE:
        return model.read_discrete(start, qty)
    if area is Area.INPUT:
        return model.read_input(start, qty)
    return model.read_holding(start, qty)


def snapshot(model: ModbusModel) -> tuple[list[Cell], dict[Area, str]]:
    """Return (cells, errors): one Cell per register, plus per-area errors."""
    blocks: dict[Area, list[int]] = {}
    errors: dict[Area, str] = {}
    for area in Area:
        res = _read_area(model, area)
        if res.ok:
            blocks[area] = res.values
        else:
            errors[area] = res.error

    cells: list[Cell] = []
    for reg in REGISTERS:
        if reg.area in errors:
            cells.append(Cell(reg, "-- error --", False))
            continue
        base = AREA_BLOCKS[reg.area][0]
        off = reg.addr - base
        words = blocks[reg.area][off:off + reg.words]
        cells.append(Cell(reg, decode(reg, words), True))
    return cells, errors
