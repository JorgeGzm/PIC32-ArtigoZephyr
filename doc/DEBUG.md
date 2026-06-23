# Debug & flash — common environment (PIC32CM MC00 Curiosity Nano)

This kit's on-board debugger is the Microchip **nEDBG (PKOB nano)**. It is a
standard composite USB device — **no kernel driver is required** on Linux:

| Interface | Use |
|-----------|-----|
| CMSIS-DAP v1 (HID) | SWD program/debug (pyOCD, OpenOCD) |
| CDC virtual COM | serial console / shell (`/dev/ttyACM*`) |
| Mass Storage (MSC) | drag-and-drop `.hex`/`.uf2` programming |
| DGI | data gateway (logic/timestamps) — not used here |

VID:PID = **`03eb:2175`**.

## 1. One-time host setup

Non-root access needs only **udev rules** — and the Zephyr SDK's
`60-openocd.rules` already covers the nEDBG via its generic
`ATTRS{product}=="*CMSIS-DAP*"` catch-all (no custom rule needed). Full steps
are in **[SETUP.md](SETUP.md) §5**. Quick verify:

```bash
lsusb | grep 03eb:2175
pyocd list        # should show the nEDBG with no "Access denied"
```

## 2. Toolchain

- **Zephyr** is the shared install at `~/zephyrproject` (v4.4.1) — this app
  builds against it (see the VS Code tasks; no west manifest here).
- **Zephyr SDK** (`arm-zephyr-eabi-gcc`/`-gdb`) for the compiler + GDB.
- **pyOCD** (in `~/zephyrproject/.venv`) with the Microchip pack:
  ```bash
  pyocd pack install pic32cm1216mc00032
  ```
- **OpenOCD** ≥ 0.12 (optional alternative): `at91samdXX.cfg` covers the SAMC21.

### Official Microchip Python tools (optional)

Not required for GDB debugging, but handy and officially supported (they use
the same probe access as above):

```bash
pip install pymcuprog pykitinfo pydebuggerupgrade
pykitinfo                       # identify the connected kit
pydebuggerupgrade latest        # update the nEDBG firmware
```

## 3. Build / flash / debug

From VS Code: tasks **app_build**, **app_flash**, and the **Debug** launch
configs. From the CLI (the app builds against the external workspace):

```bash
export PATH=$HOME/zephyrproject/.venv/bin:$PATH
APP=$PWD/app
cd $HOME/zephyrproject

west build -b pic32cm1216mc -p always -s "$APP" -d "$APP/build"
west flash -d "$APP/build"       # pyOCD runner
```

### Debug server — use pyOCD

```bash
pyocd gdbserver --target pic32cm1216mc00032
```

pyOCD is the recommended (and tested) server here: it works out of the box with
the SDK udev rule and the Microchip device pack.

> **Why not OpenOCD on this kit?** The nEDBG is CMSIS-DAP **v1 (HID only)**.
> OpenOCD's CMSIS-DAP driver then needs the HID interface bound to the kernel
> `usbhid`/`hidraw` driver — but pyOCD (libusb) detaches it, leaving interface
> `:1.0` with no driver and no `/dev/hidraw*`. So OpenOCD reports "unable to
> find a matching CMSIS-DAP device" until the board is replugged with no pyOCD
> session active. It's a permissions-independent binding conflict, not a setup
> bug — hence pyOCD is preferred on this probe.

> **Watchdog while debugging:** the SAM0 WDT keeps counting while the core is
> halted, so a paused session resets the board after ~8 s. For a smooth debug
> session, build with the watchdog off — e.g. an `app/debug.conf` containing
> `CONFIG_WATCHDOG=n` and `west build ... -- -DEXTRA_CONF_FILE=debug.conf`
> (this also needs `watchdog.c` guarded for `CONFIG_WATCHDOG=n`). Not set up in
> the repo yet.

### No-tools fallback: drag-and-drop

The nEDBG exposes a USB drive; copying a UF2/hex programs the target with zero
host tooling or permissions:

```bash
cp app/build/zephyr/zephyr.uf2 /media/$USER/CURIOSITY/
```
