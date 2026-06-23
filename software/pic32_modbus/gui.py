"""PySide6 GUI — live view + controls for the PIC32CM MC Modbus server.

Same toolkit as the reference project (sirius-python uses PySide6). The
GUI never touches pymodbus directly: it drives the shared
:class:`~pic32_modbus.modbus_model.ModbusModel`, and a background
:class:`PollWorker` (QThread) emits decoded snapshots so the UI thread
never blocks on serial I/O.
"""

from __future__ import annotations

from PySide6.QtCore import Qt, QThread, Signal
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QGroupBox, QHBoxLayout, QHeaderView,
    QLabel, QMainWindow, QPushButton, QTableWidget,
    QTableWidgetItem, QVBoxLayout, QWidget,
)
from serial.tools import list_ports

from .modbus_model import ModbusModel
from .poller import snapshot, sync_time
from .registers import REGISTERS, Area

_AREA_LABEL = {
    Area.COIL: "Coil",
    Area.DISCRETE: "Discrete In",
    Area.INPUT: "Input Reg",
    Area.HOLDING: "Holding Reg",
}


def _available_ports() -> list[str]:
    """Serial port device paths currently present on the PC (e.g. /dev/ttyUSB0)."""
    return sorted(p.device for p in list_ports.comports())


class PollWorker(QThread):
    """Polls the model on an interval, emits (cells, errors)."""

    snapshot_ready = Signal(object, object)

    def __init__(self, model: ModbusModel, interval: float):
        super().__init__()
        self._model = model
        self._interval_ms = int(interval * 1000)
        self._running = True

    def run(self) -> None:
        while self._running:
            cells, errors = snapshot(self._model)
            if not self._running:
                break
            self.snapshot_ready.emit(cells, errors)
            # sleep in small slices so stop() is responsive
            waited = 0
            while self._running and waited < self._interval_ms:
                self.msleep(50)
                waited += 50

    def stop(self) -> None:
        self._running = False


class MainWindow(QMainWindow):
    def __init__(self, port: str, baud: int, unit: int, interval: float):
        super().__init__()
        self.setWindowTitle("PIC32CM MC — Modbus Monitor")
        self.resize(720, 560)

        self._baud = baud
        self._unit = unit
        self._interval = interval
        self._model: ModbusModel | None = None
        self._worker: PollWorker | None = None

        # Default text colour from the active palette (adapts to dark/light
        # themes — forcing black made values invisible on a dark theme).
        self._fg_default = self.palette().text().color()

        root = QWidget()
        self.setCentralWidget(root)
        layout = QVBoxLayout(root)

        layout.addLayout(self._build_conn_bar(port))
        layout.addWidget(self._build_table())
        layout.addWidget(self._build_controls())

        self.statusBar().showMessage("Disconnected")
        self._set_connected(False)

    # -- UI builders --------------------------------------------------

    def _build_conn_bar(self, port: str) -> QHBoxLayout:
        bar = QHBoxLayout()
        bar.addWidget(QLabel("Port:"))
        # Editable so the user can also type a path that isn't auto-detected.
        self.port_combo = QComboBox()
        self.port_combo.setEditable(True)
        self.port_combo.setFixedWidth(180)
        bar.addWidget(self.port_combo)
        self._refresh_ports(select=port)

        self.refresh_btn = QPushButton("↻")
        self.refresh_btn.setToolTip("Refresh the serial port list")
        self.refresh_btn.setFixedWidth(36)
        self.refresh_btn.clicked.connect(lambda: self._refresh_ports())
        bar.addWidget(self.refresh_btn)

        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self._toggle_connection)
        bar.addWidget(self.connect_btn)

        self.led_dot = QLabel("●")
        self.led_dot.setStyleSheet("color: #c0392b; font-size: 18px;")
        bar.addWidget(self.led_dot)
        bar.addStretch(1)
        return bar

    def _refresh_ports(self, select: str | None = None) -> None:
        """Repopulate the port combo with the currently available ports,
        keeping the current/selected port if it's still around."""
        keep = select if select is not None else self.port_combo.currentText().strip()
        ports = _available_ports()
        self.port_combo.clear()
        self.port_combo.addItems(ports)
        if keep:
            idx = self.port_combo.findText(keep)
            if idx >= 0:
                self.port_combo.setCurrentIndex(idx)
            else:
                self.port_combo.setEditText(keep)

    def _build_table(self) -> QTableWidget:
        self.table = QTableWidget(len(REGISTERS), 4)
        self.table.setHorizontalHeaderLabels(["Area", "Point", "Addr", "Value"])
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionMode(QTableWidget.NoSelection)
        hdr = self.table.horizontalHeader()
        hdr.setSectionResizeMode(0, QHeaderView.ResizeToContents)
        hdr.setSectionResizeMode(1, QHeaderView.Stretch)
        hdr.setSectionResizeMode(2, QHeaderView.ResizeToContents)
        hdr.setSectionResizeMode(3, QHeaderView.ResizeToContents)

        for row, reg in enumerate(REGISTERS):
            self.table.setItem(row, 0, QTableWidgetItem(_AREA_LABEL[reg.area]))
            name = reg.name + (" *" if reg.writable else "")
            self.table.setItem(row, 1, QTableWidgetItem(name))
            self.table.setItem(row, 2, QTableWidgetItem(str(reg.addr)))
            val = QTableWidgetItem("—")
            val.setTextAlignment(Qt.AlignRight | Qt.AlignVCenter)
            self.table.setItem(row, 3, val)
        return self.table

    def _build_controls(self) -> QGroupBox:
        box = QGroupBox("Controls (write)")
        h = QHBoxLayout(box)

        self.led_on_btn = QPushButton("LED on")
        self.led_on_btn.clicked.connect(lambda: self._write_coil(0, True))
        self.led_off_btn = QPushButton("LED off")
        self.led_off_btn.clicked.connect(lambda: self._write_coil(0, False))
        self.reboot_btn = QPushButton("Reboot")
        self.reboot_btn.clicked.connect(lambda: self._write_coil(1, True))
        h.addWidget(self.led_on_btn)
        h.addWidget(self.led_off_btn)
        h.addWidget(self.reboot_btn)

        h.addSpacing(16)
        self.led_default_chk = QCheckBox("LED at boot")
        h.addWidget(self.led_default_chk)
        self.led_default_btn = QPushButton("Apply")
        self.led_default_btn.clicked.connect(
            lambda: self._write_reg(0, 1 if self.led_default_chk.isChecked() else 0))
        h.addWidget(self.led_default_btn)

        h.addSpacing(16)
        self.synctime_btn = QPushButton("Sync time")
        self.synctime_btn.clicked.connect(self._sync_time)
        h.addWidget(self.synctime_btn)
        h.addStretch(1)

        self._ctrl_buttons = [
            self.led_on_btn, self.led_off_btn, self.reboot_btn,
            self.led_default_btn, self.synctime_btn,
        ]
        return box

    # -- connection ---------------------------------------------------

    def _toggle_connection(self) -> None:
        if self._model is None:
            self._connect()
        else:
            self._disconnect()

    def _connect(self) -> None:
        port = self.port_combo.currentText().strip()
        model = ModbusModel(port, baud=self._baud, unit=self._unit)
        if not model.connect():
            self.statusBar().showMessage(f"Failed to open {port}")
            return
        self._model = model
        self._worker = PollWorker(model, self._interval)
        self._worker.snapshot_ready.connect(self._on_snapshot)
        self._worker.start()
        self._set_connected(True)
        self.statusBar().showMessage(f"Connected to {port} (unit {self._unit})")

    def _disconnect(self) -> None:
        if self._worker is not None:
            self._worker.stop()
            self._worker.wait()
            self._worker = None
        if self._model is not None:
            self._model.close()
            self._model = None
        self._set_connected(False)
        self.statusBar().showMessage("Disconnected")

    def _set_connected(self, on: bool) -> None:
        self.connect_btn.setText("Disconnect" if on else "Connect")
        self.port_combo.setEnabled(not on)
        self.refresh_btn.setEnabled(not on)
        self.led_dot.setStyleSheet(
            f"color: {'#27ae60' if on else '#c0392b'}; font-size: 18px;")
        for b in self._ctrl_buttons:
            b.setEnabled(on)

    # -- writes -------------------------------------------------------

    def _write_coil(self, addr: int, value: bool) -> None:
        if self._model is None:
            return
        res = self._model.write_coil(addr, value)
        self.statusBar().showMessage(
            f"coil {addr} <- {int(value)}: {'OK' if res.ok else res.error}")

    def _write_reg(self, addr: int, value: int) -> None:
        if self._model is None:
            return
        res = self._model.write_register(addr, value)
        self.statusBar().showMessage(
            f"holding {addr} <- {value}: {'OK' if res.ok else res.error}")

    def _sync_time(self) -> None:
        if self._model is None:
            return
        res = sync_time(self._model)
        self.statusBar().showMessage(
            "Time synced with the PC" if res.ok
            else f"Failed to sync time: {res.error}")

    # -- snapshot update ---------------------------------------------

    def _on_snapshot(self, cells, errors) -> None:
        for row, cell in enumerate(cells):
            item = self.table.item(row, 3)
            item.setText(cell.text)
            item.setForeground(Qt.red if not cell.ok else self._fg_default)
        if errors:
            self.statusBar().showMessage(
                "Read error: " + "; ".join(str(e) for e in errors.values()))

    def closeEvent(self, event) -> None:
        self._disconnect()
        event.accept()


def run(port: str, baud: int, unit: int, interval: float) -> int:
    app = QApplication.instance() or QApplication([])
    win = MainWindow(port, baud, unit, interval)
    win.show()
    # auto-connect on launch (silent if it fails — user can retry)
    win._connect()
    return app.exec()
