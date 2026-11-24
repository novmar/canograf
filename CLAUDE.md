# CLAUDE.md

Notes for anyone (human or model) working on this repository. Everything here
was measured on real hardware, not assumed. Where something is an assumption it
says so.

---

## What this is

A touchscreen CAN-bus diagnostic recorder. Two halves:

1. **Transport + vehicle profile** - talk to ECUs over CAN, decode data
   identifiers into physical values. Vehicle specific, lives in `docs/vehicles/`.
2. **UI** - LVGL 8.4 on 800x480. Hardware specific only in the pin mapping.

Keep the two apart. The UI must not know which car it is looking at.

## Repository layout

```
src/main.cpp        board bring-up, WiFi, OTA, NVS, backlight  (board specific)
src/ui.*            the whole interface                        (portable)
src/i18n.*          string table, one entry per visible text   (portable)
src/font_ui.h       UI_FONT_xx aliases for the generated fonts
src/font_ui_*.c     generated fonts, do not edit by hand
src/lv_conf.h       LVGL config, 800x480, LV_COLOR_DEPTH 16
src/lvgl_v8_port.*  LVGL <-> panel glue, from Espressif         (board specific)
lang/               one file per language, plus how to add one
tools/gen_fonts.sh  regenerates src/font_ui_*.c
sim/                host build that renders screens to PNG
boards/             PlatformIO board definition
docs/vehicles/      per-vehicle data identifier maps
docs/hardware/      per-board pinouts and measurements
```

## Build

```sh
.pio-venv/bin/pio run
.pio-venv/bin/pio run -t upload
```

**Do not unpin the versions in `platformio.ini`.** They are not arbitrary:

- The official `espressif32` platform does not support Arduino core 3.1.x, which
  `ESP32_Display_Panel` 1.x needs. Hence the `pioarduino` fork.
- `esp32-3.1.1-h.zip` is a performance build of the Arduino libs. Without it the
  RGB panel on this board **drifts** - the image walks sideways. This is a known
  ESP32-S3 RGB issue, not a wiring fault.
- LVGL is **8.4.0**, not 9.x. The reference port from Espressif targets 8.x.

Reference for all of the above:
`esp-arduino-libs/ESP32_Display_Panel`, `examples/platformio/lvgl_v8_port`.

## Conventions

**No hard-coded visible strings.** Everything the user can read goes through
`T(STR_...)` from `src/i18n.h`; the translations live in `lang/`. A new label
means a new enum entry and a line in every language file - a missing line is a
compile error (`_Static_assert` in `src/i18n.c`), never a silent fallback.

**Diacritics are fine now, but only in `lang/`.** `src/font_ui_*.c` are
generated from the same Montserrat the built-ins use, extended with Czech and
German accented letters, the degree sign, the bullet and the eight FontAwesome
icons that LVGL widgets draw themselves. So `lang/cs.h` is written with proper
accents. The `.c` and `.h` files outside `lang/` stay pure ASCII.

**Never use `lv_font_montserrat_*`.** They are all switched off in `lv_conf.h`
precisely because they have no accented glyphs, and a single missing glyph
makes LVGL log `glyph dsc. not found for U+XXXX` thirty times a second. Use the
`UI_FONT_xx` aliases from `src/font_ui.h`. Sizes available: 14, 16, 20, 24, 28,
32, 36, 40, 48. A language that needs more characters adds them to `SYMBOLS` in
`tools/gen_fonts.sh` and reruns it.

**Changing the language rebuilds the whole screen.** `ui_rebuild()` in `ui.c`
loads a fresh screen and calls `ui_build()` again instead of walking a list of
labels. It is less code and it cannot forget a label; measured values survive
because they sit in static variables, not in LVGL objects.

**Comments in Czech without diacritics**, matching the existing code.

**Nothing secret in the repository.** WiFi credentials live in NVS
(`Preferences`, namespace `talento`), never in source. No VIN, no MAC, no IP.

## UI rules

Derived from the physical panel, not from taste. Active area is
**93.7 x 56.2 mm**, so **8.54 px/mm** (217 DPI).

| rule | source | applied as |
|---|---|---|
| touch target >= 14 mm | driver workload research | 120 px minimum |
| >= 15 mm for industrial controls | ISA-101, IEC 61131 | dialog buttons 120 px tall |
| 76 dp minimum | Android Auto | comfortably exceeded |
| 5-10 mm between targets | capacitive panels | 4 px gap plus border |
| readable in one glance, under 2 s | automotive HMI | three key values always on top |
| avoid text under 6.5 mm | visual load research | main numbers `UI_FONT_48` |

Known shortfall: the largest generated size is 48 px, cap height about 4.1 mm,
below the 6.5 mm guidance. The font generator takes any size, so this is now a
matter of adding one line to `tools/gen_fonts.sh` and finding the flash for it,
not of missing tooling.

**Shape language.** Panels are square with a hairline border - an instrument
panel, not floating cards. Buttons get a 6 px radius, just enough to read as
pressable. Pressing swaps the whole background to the accent colour; subtle
feedback was tested and was not noticed.

**Palette** (shared with the laptop-side tooling so both feel like one tool):

```
ink      #0B1016   ground
panel    #0E151D   surfaces
rule     #1B2A38   hairlines
demand   #6FA8D0   what the ECU asks for
actual   #F0DFB8   what the engine delivers
deficit  #C8432B   fault
sage     #68937A   in tolerance
label    #65798B   captions
bright   #DCE7F0   primary text
```

The cool/warm split is not decoration: cool blue is always the setpoint, warm
bone is always the measured value.

## Traps that cost real time

**ELM327 cannot send more than 7 data bytes.** Any longer UDS message returns
`?` and is never transmitted. Writing a 36 byte configuration block needs
hand-rolled ISO-TP: `AT CAF0`, first frame `1LLL` + 6 bytes, then consecutive
frames `2N` + 7 bytes. This project talks to TWAI directly and avoids all of it,
but the knowledge matters when cross-checking with a dongle.

**Flow control is real.** The ECU answers a first frame with `30 01 05`:
block size 1, separation 5 ms. Parse it from offset 3 of the line when headers
are on (`AT H1`) - the three header characters come first. Getting this offset
wrong reads block size 0 and the transfer silently dies.

**Consecutive frames must be fast.** With `AT ST FF` the dongle waits about a
second after each frame; the ECU gives up after roughly 150 ms (N_Cs). Disable
replies with `AT R0` while streaming consecutive frames.

**`0x78` responsePending** on `19 02` needs `AT AT0` plus a long `AT ST`,
otherwise the real answer never arrives.

**Serial port hijacking.** A monitor process that falls back to "any serial
port" will happily grab a connected ESP32 and make flashing fail with
`Packet content transfer stopped`. Match on `FTDI`/`CH340`/`CP210` or on
`/dev/ttyUSB*`, never on `/dev/ttyACM*`.

## Closed experiments

Do not repeat these.

**Software PWM backlight - does not work.** The backlight is a switch on the
CH422G expander (`EXIO2`), not a PWM pin. Toggling it over I2C takes **174 us**,
so the bus is not the limit. The limit is the AP3032 boost driver, which needs
on the order of tens of milliseconds to light up. Measured: at 25 Hz and 70 %
duty (28 ms on) it barely flickers into life; anything shorter stays black.
Usable dimming would need below 30 Hz, which flickers visibly.
The only real fix is hardware: cut `EXIO2` from the driver control pin and
drive it from **GPIO6**, the single free pin on this board.

**Turbo actuator adaptation routine - does not exist** on Bosch MD1CS006. The
ECU implements exactly seven routines (`0x31`) and none of them is turbo
related. No tool can run one, regardless of what forums claim. What does exist
is the learned end stop values in EEPROM - see the vehicle profile.

## Working with a vehicle

Read freely. **Think before writing.**

- Writes (`0x2E`, `0x3B`) are refused with `conditionsNotCorrect` (`7F .. 22`)
  while the engine runs. Ignition on, engine off.
- Always read the current block first and write it back modified. Write request
  layouts are often **shorter** than the matching read response - verify that the
  leading bytes line up before assuming a 1:1 mapping.
- Save a backup of every block you touch.
- Clearing fault memory destroys the extended records: occurrence counter,
  odometer at first occurrence, distance driven with the fault present. Read
  `19 06` **before** anyone clears anything. A self-healed fault wipes them too.
