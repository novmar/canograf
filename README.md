# canograf

Open CAN-bus diagnostic recorder with a touchscreen. Reads live values and
fault memory from vehicle ECUs and draws them like a chart recorder.

Built first for a **Waveshare ESP32-S3-Touch-LCD-4.3B (rev 1.1)** and a
**Fiat Talento 2020 / Renault Trafic III (platform X82)**, but both the
hardware and the vehicle side are meant to be swappable:

- **Other boards** - anything with an ESP32 and a CAN transceiver. The pinout
  lives in one place, see [`docs/hardware/`](docs/hardware/).
- **Other vehicles** - a vehicle profile is just a table of data identifiers
  with scaling. See [`docs/vehicles/`](docs/vehicles/). Pull requests with new
  cars are very welcome.

> Status: **work in progress.** The UI runs on the board, the CAN layer is not
> written yet, values on screen are simulated. See [ROADMAP.md](ROADMAP.md).

---

## Why

A laptop plus an ELM327 dongle works, but it is slow and clumsy in a vehicle.
An ELM327 clone cannot send a CAN message longer than 7 data bytes and tops out
at roughly 3-4 samples per second over its serial link. Talking to the ESP32
TWAI controller directly removes both limits and fits in a device you can leave
on the dashboard.

The part that actually takes time is not the code, it is knowing **which data
identifier means what and how to scale it**. That knowledge is in
[`docs/vehicles/`](docs/vehicles/) and it is verified against a real car, not
copied from a forum.

## Hardware

| | |
|---|---|
| Board | Waveshare ESP32-S3-Touch-LCD-4.3B rev 1.1 (`ESP32-S3-WROOM-1-N16R8`) |
| Display | 4.3" IPS, 800x480, 16 MB flash, 8 MB PSRAM |
| Touch | GT911, capacitive, 5 point |
| CAN | TJA1051T/3 on `GPIO15` (TX) and `GPIO16` (RX) |
| Power | 7-36 V DC or USB-C, about 3 W |

Wiring to an OBD-II (SAE J1962) socket:

```
OBD pin 6   ->  CAN H
OBD pin 14  ->  CAN L
OBD pin 4   ->  GND
OBD pin 16  ->  VIN     (fused, 1 A)
```

Leave the on-board 120 ohm CAN termination switch **off** - the vehicle bus is
already terminated at both ends.

## Build and flash

```sh
python3 -m venv .pio-venv
.pio-venv/bin/pip install platformio
.pio-venv/bin/pio run              # build
.pio-venv/bin/pio run -t upload    # flash over USB-C
```

Versions in `platformio.ini` are pinned on purpose - see
[CLAUDE.md](CLAUDE.md) for why.

## Screen previews

CI builds the UI for the host and renders every screen to PNG, so changes are
visible in a pull request without owning the board. See [`sim/`](sim/).

## Documentation

| | |
|---|---|
| [CLAUDE.md](CLAUDE.md) | How the project is put together, conventions, traps |
| [ROADMAP.md](ROADMAP.md) | What is planned and what is deliberately not |
| [`docs/vehicles/`](docs/vehicles/) | Per-vehicle data identifier maps |
| [`docs/hardware/`](docs/hardware/) | Per-board pinouts and measurements |
| [`docs/ui.md`](docs/ui.md) | Why the interface looks the way it does |

## Licence

MIT. See [LICENSE](LICENSE).

**No warranty.** Writing to a control unit can disable your vehicle. Reading is
safe, writing is not. Everything in this project that writes is marked as such.
