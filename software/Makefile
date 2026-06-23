# PIC32CM MC Modbus client — dev helpers
VENV ?= .venv
PY   := $(VENV)/bin/python
PORT ?= /dev/ttyUSB0

.PHONY: install gui clean

$(VENV)/bin/python:
	python3 -m venv $(VENV)
	$(PY) -m pip install --upgrade pip

install: $(VENV)/bin/python
	$(PY) -m pip install -r requirements.txt

gui: install
	$(PY) main.py --port $(PORT)

clean:
	rm -rf $(VENV) **/__pycache__ __pycache__
