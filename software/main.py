#!/usr/bin/env python3
"""PIC32CM MC — Modbus RTU client (GUI).

Reads, monitors and writes the firmware registers (Modbus RTU, unit id 1,
115200 8N1) through a USB-RS485 adapter (e.g. /dev/ttyUSB0).

    $ ./main.py                         # launch the GUI
    $ ./main.py --port /dev/ttyUSB1     # other serial port
"""

from __future__ import annotations

import argparse
import importlib.util
import logging
import os
import subprocess
import sys

log = logging.getLogger("pic32")

# Packages required to run. Keep in sync with requirements.txt (used as a
# fallback when that file is missing). Mapping: pip name -> import name.
_REQUIREMENTS = (
    ("pymodbus>=3.6,<3.10", "pymodbus"),
    ("pyserial>=3.5", "serial"),
    ("PySide6>=6.6,<6.9", "PySide6"),
)

_HERE = os.path.dirname(os.path.abspath(__file__))
_VENV_DIR = os.path.join(_HERE, ".venv")
_VENV_PY = os.path.join(_VENV_DIR, "bin", "python")


def _deps_present() -> bool:
    return all(importlib.util.find_spec(mod) is not None
               for _, mod in _REQUIREMENTS)


def _bootstrap() -> None:
    """Make `./main.py` self-sufficient: ensure dependencies are installed.

    If the required packages are already importable (e.g. the venv is active),
    do nothing. Otherwise create/use the project ``.venv``, install the
    requirements there and re-exec this script with the venv interpreter so we
    never install into the system Python.
    """
    if _deps_present():
        return
    if os.environ.get("PIC32_BOOTSTRAPPED") == "1":
        return  # already re-exec'd once; let the import raise a clear error

    if not os.path.exists(_VENV_PY):
        log.info("creating virtualenv at %s", _VENV_DIR)
        subprocess.check_call([sys.executable, "-m", "venv", _VENV_DIR])
        subprocess.check_call([_VENV_PY, "-m", "pip", "install", "--upgrade", "pip"])

    req_file = os.path.join(_HERE, "requirements.txt")
    if os.path.exists(req_file):
        pip_args = ["-r", req_file]
    else:
        pip_args = [spec for spec, _ in _REQUIREMENTS]
    log.info("installing dependencies into %s", _VENV_DIR)
    subprocess.check_call([_VENV_PY, "-m", "pip", "install", *pip_args])

    os.environ["PIC32_BOOTSTRAPPED"] = "1"
    os.execv(_VENV_PY, [_VENV_PY, os.path.abspath(__file__), *sys.argv[1:]])


def _setup_logging() -> None:
    level = logging.DEBUG if os.environ.get("PIC32_DEBUG") else logging.INFO
    logging.basicConfig(
        level=level,
        format="%(asctime)s %(levelname)-7s %(name)-12s %(message)s",
        handlers=[logging.StreamHandler(sys.stderr)],
        force=True)
    # pymodbus/pyserial are noisy — warnings only.
    for noisy in ("pymodbus", "pyserial"):
        logging.getLogger(noisy).setLevel(logging.WARNING)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="PIC32CM MC Modbus client (GUI)")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="RS-485 serial port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--unit", type=int, default=1, help="Modbus unit id")
    parser.add_argument("--interval", type=float, default=1.0,
                        help="poll interval (s)")
    args = parser.parse_args(argv)
    _setup_logging()

    # The GUI manages its own connection, so we don't pre-connect here.
    from pic32_modbus.gui import run as run_gui
    return run_gui(args.port, args.baud, args.unit, args.interval)


if __name__ == "__main__":
    _bootstrap()
    raise SystemExit(main())
