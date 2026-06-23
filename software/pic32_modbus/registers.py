"""PIC32CM MC Modbus register map + decoders.

The map mirrors, 1:1, the server in the firmware
(``app/src/main.c``). Each :class:`Register` is a frozen dataclass so the
table stays declarative — adding a point is one line, no logic change.

Areas (Modbus function codes):
    COIL      FC01 read / FC05 write   (RW bits)
    DISCRETE  FC02                     (RO bits)
    INPUT     FC04                     (RO 16-bit)
    HOLDING   FC03 read / FC06 write   (RW 16-bit, persisted in NVS)

All addresses are 0-based (the same convention the firmware uses).
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
from enum import Enum, auto
from typing import Callable, Optional


class Area(Enum):
    COIL = "coil"
    DISCRETE = "discrete"
    INPUT = "input"
    HOLDING = "holding"


class Kind(Enum):
    BOOL = auto()   # single bit (coil / discrete input)
    U16 = auto()    # unsigned 16-bit
    S16 = auto()    # signed 16-bit (two's complement)
    U32 = auto()    # unsigned 32-bit spanning two input regs (hi, lo)


@dataclass(frozen=True)
class Register:
    name: str
    area: Area
    addr: int
    kind: Kind
    desc: str
    scale: float = 1.0
    unit: str = ""
    words: int = 1          # how many 16-bit words this point occupies
    writable: bool = False
    fmt: Optional[Callable[[int], str]] = None   # custom formatter (raw -> text)


# --------------------------------------------------------------------
# Custom formatters
# --------------------------------------------------------------------

def _fw(raw: int) -> str:
    """0x0100 -> 'v1.0'."""
    return f"v{raw >> 8}.{raw & 0xFF}"


def _status(raw: int) -> str:
    return "OK" if raw == 0 else "ERROR"


def _uptime(raw: int) -> str:
    """Seconds -> 'Hh Mm Ss' (plus the raw seconds)."""
    h, rem = divmod(raw, 3600)
    m, s = divmod(rem, 60)
    return f"{h:02d}:{m:02d}:{s:02d} ({raw}s)"


def _onoff(raw: int) -> str:
    return "on" if raw else "off"


def _clock(raw: int) -> str:
    """Epoch seconds -> 'DD/MM/YYYY HH:MM:SS'. The firmware keeps the clock
    as local wall-time stored in UTC fields, so we decode as UTC to read it
    back exactly as it was synced (see poller.sync_time)."""
    if raw == 0:
        return "not set"
    return datetime.fromtimestamp(raw, tz=timezone.utc).strftime("%d/%m/%Y %H:%M:%S")


# --------------------------------------------------------------------
# The map (matches app/src/main.c)
# --------------------------------------------------------------------

REGISTERS: list[Register] = [
    # Coils (RW) ------------------------------------------------------
    Register("LED",          Area.COIL, 0, Kind.BOOL, "LED on/off (manual)",
             writable=True, fmt=_onoff),
    Register("Reboot",       Area.COIL, 1, Kind.BOOL, "reboot command (write 1)",
             writable=True, fmt=_onoff),

    # Discrete inputs (RO) -------------------------------------------
    Register("Button SW0",   Area.DISCRETE, 0, Kind.BOOL, "board button state",
             fmt=lambda r: "pressed" if r else "released"),

    # Input registers (RO) -------------------------------------------
    Register("Uptime",       Area.INPUT, 0, Kind.U32, "time powered on", words=2,
             fmt=_uptime),
    Register("Button clicks", Area.INPUT, 2, Kind.U16, "click counter"),
    Register("Firmware",     Area.INPUT, 3, Kind.U16, "firmware version", fmt=_fw),
    Register("Free heap",    Area.INPUT, 4, Kind.U16, "free memory", unit="B"),
    Register("Temperature",  Area.INPUT, 5, Kind.S16, "SHT3x", scale=0.01, unit="C"),
    Register("Humidity",     Area.INPUT, 6, Kind.U16, "SHT3x", scale=0.01, unit="%"),
    Register("Distance",     Area.INPUT, 7, Kind.U16, "VL53L0X", unit="mm"),
    Register("Clock",        Area.INPUT, 8, Kind.U32, "RTC (date/time)", words=2,
             fmt=_clock),

    # Holding registers (RW) -----------------------------------------
    # 0 = LED default (shown/editable). 1,2 = set-time hi/lo (write-only via the
    # "sync time" action, see poller.TIME_SET_ADDR) — not displayed as rows.
    Register("LED default",  Area.HOLDING, 0, Kind.U16, "LED state at boot (0/1) [NVS]",
             writable=True),
]


def decode(reg: Register, words: list[int]) -> str:
    """Turn the raw register word(s) into a human-readable string."""
    if reg.kind is Kind.BOOL:
        raw = 1 if words[0] else 0
    elif reg.kind is Kind.U32:
        raw = (words[0] << 16) | words[1]
    elif reg.kind is Kind.S16:
        raw = words[0] - 0x10000 if words[0] >= 0x8000 else words[0]
    else:  # U16
        raw = words[0]

    if reg.fmt is not None:
        return reg.fmt(raw)

    if reg.scale != 1.0:
        text = f"{raw * reg.scale:.2f}"
    else:
        text = str(raw)
    return f"{text} {reg.unit}".strip()
