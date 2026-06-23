"""PIC32CM MC Modbus client — read/monitor/write the firmware's registers."""

from .modbus_model import ModbusModel, ReadResult
from .registers import REGISTERS, Area, Kind, Register, decode

__all__ = [
    "ModbusModel", "ReadResult",
    "REGISTERS", "Area", "Kind", "Register", "decode",
]
