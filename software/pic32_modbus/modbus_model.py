"""Modbus model — thin pymodbus RTU wrapper for the PIC32CM MC server.

Same design philosophy as the Sirius desktop's ``ModbusModel`` (the
reference project): the pymodbus client type never leaks to callers,
every read returns a small :class:`ReadResult` dataclass, and the model
is thread-safe so a polling loop and a one-shot CLI write never collide.

NO INTERNAL RETRIES — ON PURPOSE
--------------------------------
A failed read returns immediately with ``ok=False``. The GUI poll worker's
next tick re-issues it ~1 s later anyway; retrying here would only hide link
problems (bad wiring, slave busy-window) that the operator wants to see.
"""

from __future__ import annotations

import logging
import threading
from dataclasses import dataclass, field

from pymodbus.client import ModbusSerialClient
from pymodbus.exceptions import ConnectionException, ModbusException

log = logging.getLogger("pic32.modbus")


@dataclass
class ReadResult:
    ok: bool
    values: list[int] = field(default_factory=list)
    error: str = ""


class ModbusModel:
    """RTU master for the PIC32CM MC Modbus server (unit id 1, 115200 8N1)."""

    def __init__(self, port: str, baud: int = 115200, unit: int = 1,
                 parity: str = "N", stopbits: int = 1, bytesize: int = 8,
                 timeout: float = 1.0):
        self._port = port
        self._unit = unit
        self._lock = threading.Lock()
        self._client = ModbusSerialClient(
            port=port, baudrate=baud, parity=parity, stopbits=stopbits,
            bytesize=bytesize, timeout=timeout)

    # -- lifecycle ----------------------------------------------------

    def connect(self) -> bool:
        with self._lock:
            ok = bool(self._client.connect())
            if ok:
                log.info("connected to %s (unit %d)", self._port, self._unit)
            else:
                log.error("failed to open %s", self._port)
            return ok

    def close(self) -> None:
        with self._lock:
            self._client.close()

    # -- reads --------------------------------------------------------

    def read_input(self, addr: int, qty: int) -> ReadResult:
        return self._read("read_input_registers", addr, qty, bits=False)

    def read_holding(self, addr: int, qty: int) -> ReadResult:
        return self._read("read_holding_registers", addr, qty, bits=False)

    def read_coils(self, addr: int, qty: int) -> ReadResult:
        return self._read("read_coils", addr, qty, bits=True)

    def read_discrete(self, addr: int, qty: int) -> ReadResult:
        return self._read("read_discrete_inputs", addr, qty, bits=True)

    def _read(self, method_name: str, addr: int, qty: int, bits: bool) -> ReadResult:
        method = getattr(self._client, method_name)
        try:
            with self._lock:
                rsp = method(address=addr, count=qty, slave=self._unit)
        except (ConnectionException, ModbusException, OSError) as exc:
            return ReadResult(False, error=str(exc))

        if rsp is None or rsp.isError():
            return ReadResult(False, error=str(rsp))

        raw = rsp.bits if bits else rsp.registers
        return ReadResult(True, [int(v) for v in raw[:qty]])

    # -- writes -------------------------------------------------------

    def write_coil(self, addr: int, value: bool) -> ReadResult:
        try:
            with self._lock:
                rsp = self._client.write_coil(
                    address=addr, value=bool(value), slave=self._unit)
        except (ConnectionException, ModbusException, OSError) as exc:
            return ReadResult(False, error=str(exc))
        if rsp is None or rsp.isError():
            return ReadResult(False, error=str(rsp))
        return ReadResult(True)

    def write_register(self, addr: int, value: int) -> ReadResult:
        try:
            with self._lock:
                rsp = self._client.write_register(
                    address=addr, value=int(value), slave=self._unit)
        except (ConnectionException, ModbusException, OSError) as exc:
            return ReadResult(False, error=str(exc))
        if rsp is None or rsp.isError():
            return ReadResult(False, error=str(rsp))
        return ReadResult(True)

    def write_registers(self, addr: int, values: list[int]) -> ReadResult:
        """FC16 — write several holding regs in one frame (e.g. a 32-bit time)."""
        try:
            with self._lock:
                rsp = self._client.write_registers(
                    address=addr, values=[int(v) for v in values],
                    slave=self._unit)
        except (ConnectionException, ModbusException, OSError) as exc:
            return ReadResult(False, error=str(exc))
        if rsp is None or rsp.isError():
            return ReadResult(False, error=str(rsp))
        return ReadResult(True)
