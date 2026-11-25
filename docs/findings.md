# Findings

Everything established about this board and this van, in one place, so it can
be checked later instead of rediscovered. Terse on purpose.

**Every line carries how it was established.** That is the point of the file:
a claim without a method is a guess, and guesses cost us half a day on the
backlight already.

| tag | means |
|---|---|
| `[meas]` | measured on the real board or the real vehicle |
| `[sch]` | read out of the Waveshare schematic |
| `[ds]` | from a component datasheet |
| `[calc]` | computed from `[meas]` or `[sch]` numbers, arithmetic shown where it matters |
| `[code]` | read out of a library's source, version pinned |
| `[open]` | not established - do not rely on it |

---

## Backlight

- Driver is `U2` = `AP3032KTR-G1`, SOT-23-6, marking `GJL`, bottom side under
  the LCD FPC. `[sch]` `[ds]`
- Feedback reference 200 mV typ, 188/212 min/max. FB absolute max 20 V.
  Soft-start 300 us, standby 0.7 ms, 1 MHz switching. `[ds]`
- Path from the pad: `BL` -> `R11` 10 k -> node X -> `R7` 68 k -> `FB` ->
  `R6` 5.1 k -> `VLED-` -> `R8` 3.3 R -> GND. `C11` 1 uF from node X to GND.
  `[sch]`
- `R11` + `C11` is a 10 ms low-pass, so PWM above roughly 500 Hz arrives at
  the driver as a DC level. `[calc]`
- Current gain from node X to the LED string is `R6`/`R8` = 1545. That is why
  tens of microamps move tens of milliamps. `[calc]`
- Transfer, computed and then confirmed by prediction: 0 V -> 64.6 mA,
  1.0 V -> 44.8 mA, 1.66 V -> 31.7 mA, 2.5 V -> 15.0 mA, 3.3 V -> 0 mA.
  Nearly linear in current. `[calc]` `[meas]`
- `BL` to GND unpowered measured 82 k, computed `R11+R7+R6+R8` = 83.10 k,
  -1.3 %, inside the 1 % parts plus meter error. `[meas]` `[calc]`
- `BL` sits at 0.19 V with the backlight on, 0.00 V with it off. The 0.19 V is
  a direct reading of the chip's internal reference. `[meas]`
- **Dimming works and is installed**: one wire `GPIO6` -> `BL`, no cut, nothing
  removed, ground not needed because the module and the driver share board
  ground. Confirmed on hardware, full range. `[meas]`
- 3.3 V on the pad extinguishes the backlight completely. `[meas]`

### Why software PWM through the expander could never work

- The `DISP` net drives **both** `AP3032` `CTRL` **and** LCD FPC pin 31, so
  toggling it blanks the panel, not just the backlight. `[sch]`
- Panel resync after a blank costs about one frame, 16.7 ms. `[calc]`
- One expander pin toggle over I2C measured **174 us**, which caps usable
  software PWM at roughly 25 Hz - visibly flickering. `[meas]`
- Both of these were measured only after hours of guessing at the converter
  and the I2C bus. The measurement settled it in a minute.

---

## Pins

- `GPIO6` is the only ESP32-S3 pin not claimed by panel, touch, I2C, USB, SD,
  RS485 or CAN. `[sch]`
- `GPIO6` goes to exactly one place: `PCF85063` `INT`, pin 4. Not to any
  connector or user pad. The via next to the module edge is that trace heading
  to the bottom layer. `[sch]`
- That output is open-drain and the board has **no pull-up on it**, so with the
  RTC alarm disabled - which it is out of reset - the line is high impedance
  and a push-pull output is safe. `[sch]` `[ds]`
- `CAN RX` is `GPIO16`, which is inside the RTC GPIO range, so it can wake the
  chip from deep sleep. `[sch]` `[ds]`
- `TJA1051T/3` has its `S` pin tied to GND through `R96` 0 R, so it is
  permanently in normal mode. It cannot be put in standby, and it will
  reproduce bus traffic on `RX` even while the processor sleeps. The Waveshare
  wiki claim that CAN is enabled through the expander is **wrong for this
  board**. `[sch]`
- ESP32-S3 has its own deep-sleep timer, so periodic wake needs no external
  pin at all. The external RTC is only needed for absolute-time alarms.
  `[ds]`
- `EXIO6`, `EXIO7`, `OC2`, `OC3` are not routed anywhere. They are **not** a
  spare-pin reserve: the expander is on I2C, I2C is dead in deep sleep, so
  they cannot wake anything, and at 174 us per toggle they cannot dim either.
  `[sch]` `[meas]`
- `EXIO0` and `EXIO5` are isolated **inputs** `DI0`/`DI1`. Driving them as
  outputs pushes against the optocoupler transistors. `[sch]`
- `EXIO3` = `LCD_RST` goes to LCD FPC pin 35, which the panel marks NC, so it
  does nothing. `[sch]`
- **No divider from `VIN`, `VBUS`, the 5 V rail or `VBAT` to any ADC-capable
  pin.** The board cannot measure any voltage in software, including its own
  supply. `[sch]`

---

## Board, other

- Isolated I/O is **not galvanically isolated**. `DO0`/`DO1` are open-collector
  with emitters on board ground; `DI_COM` is tied to the board 5 V through
  `D4` 1N5822 and `R79` 220 R. `[sch]`
- Realistic input ceiling is about **30 V, not 36 V**: `R78`/`R73` 3.6 k in
  1206 exceed 0.25 W above roughly 31 V. `[sch]` `[calc]`
- microSD is 1-bit only, and CS is on the expander, so every transaction costs
  an I2C round-trip. `[sch]`
- RS485 has no direction pin; `DE`/`RE` are driven by a transistor off TXD.
  `[sch]`
- CAN termination is on `SW2` and is currently **off**, which is correct for
  connecting to a vehicle that already has its two terminators. Measured about
  28 k across CANH/CANL, which is the transceiver's own differential input
  resistance. `[meas]`
- Board draws about 550-600 mA at 5 V, roughly 3 W, with the backlight on.
  `[meas]`
- **Sleep floor has never been measured.** Expect milliamps, not microamps,
  because the CAN transceiver cannot be switched off. Measure before building
  anything that depends on sleeping. `[open]`

---

## Vehicle - Fiat Talento 2020, X82

- ECM is Bosch `MD1CS006`, UDS on `7E0`/`7E8`, session `10 03`. `[meas]`
- Everything else speaks old Renault: session `10 C0`, read `21 xx`, write
  `3B xx`. BCM `745`/`765`, EMM `74D`/`76D`, ABS `740`/`760`, EPS `742`/`762`,
  airbag `752`/`772`. `[meas]`
- Flow control from the ECU is `30 01 05`: **block size 1**, STmin 5 ms. Every
  single frame has to be acknowledged. `[meas]`
- **At most 3 DIDs per `22` request.** Four or more are refused. `[meas]`
- The `5P00` software refuses `247B`, `24C3`, `2437`, `247C`, `247D` with
  `7F 22 31`, although all of them exist in the ECU description file. Being in
  the file is not the same as being supported. `[meas]`
- `19 02` with a full fault list and the DPF regeneration routine both return
  `7F xx 78` repeatedly. Without extended P2* handling every such read fails.
  `[meas]`

### What the van actually told us

- Only active fault: **P2263-22**, turbocharger boost system performance,
  status `0xE5` = testFailed + pending + failedSinceLastClear + MIL requested.
  `[meas]`
- Freeze frame puts it at **92208 km**; odometer read **92213 km**, so it arose
  about 5 km earlier, during the first regeneration after a factory reset.
  `[meas]`
- Regeneration history: last successful at **87250 km**, next due **92283 km**.
  Interval is adaptive, not fixed - this engine has no fixed service interval,
  the model decides. `[meas]`
- DPF soot 5.72 g at the time of reading. `[meas]`
- Air mass adaptation is **8 separate flags**, bytes 4 to 11 of `22 2508`, one
  per breakpoint, and `22 2509` returns 8 learned corrections of 2 bytes each.
  All zero - the engine is untrained after the reset. Reading only byte 4 was
  our own earlier mistake. `[meas]`
- Battery fitted is an Exide EK820, AGM, 82 Ah, 800 A. EMM was configured for
  70 Ah / 720 A flooded; rewritten to VRLA bit 1, type 4, 80 Ah / 800 A.
  `[meas]`
- The EEPROM write for the oil counter is refused with the engine running -
  `conditionsNotCorrect`. Ignition on, engine off works. `[meas]`
- EMM `21 34` returns cranking voltage, voltage at last sleep, **battery
  current**, state of charge and alternator load in one frame. The van has a
  current sensor on the battery, so it can measure its own parasitic drain
  better than any meter we would buy. `[sch]` `[open]` - the frame is in the
  ECU file, not yet read from the vehicle.

### From the 24 November drive, 975 samples

- Rail tracking is near perfect: about 10 bar of deviation at 2400 bar, 0.4 %.
  `[meas]`
- Air mass runs a median 58 mg below setpoint, p5 -541, p95 +114. `[meas]`
- Boost deviation falls from 708 mbar at 1500-2000 rpm to 22 mbar at
  2500-3000 rpm, so any tolerance band for a warning has to depend on engine
  speed. `[meas]`
- DPF downstream sits a median **22.8 C below** upstream when upstream is above
  250 C and the engine is turning above 500 rpm. That is the normal passive
  case - the filter shedding heat. A real regeneration drives it strongly
  positive, and a catalyst that lost its coating cannot. `[meas]` `[calc]`

---

## Libraries and tooling, learned the hard way

- LVGL gives **every** object `LV_OBJ_FLAG_GESTURE_BUBBLE` in the constructor,
  but `PRESSED`/`PRESSING`/`RELEASED` only bubble with
  `LV_OBJ_FLAG_EVENT_BUBBLE`, which has to be added explicitly. A gesture can
  therefore arm on a child and never be evaluated. `[code]` LVGL 8.4
- LVGL sends `LV_EVENT_CLICKED` to the object where the press **started**,
  regardless of where the finger is released, unless the object scrolled. A
  swipe that begins on a button clicks that button. `[code]` `[meas]`
- `LV_ASSERT_HANDLER` defaults to `while(1)`. Correct on the board, where a
  watchdog restarts it; in a host build it hangs CI until the runner's ceiling
  instead of failing in 30 seconds. `[code]`
- The built-in Montserrat font has **no Czech or German diacritics** and no sun
  or moon symbols. Anything beyond ASCII needs a generated font. `[meas]`
- Arduino's `WiFi.disconnect()` does **not** reset the controller when no
  connection stands - it returns quietly and the radio stays in "connecting".
  `esp_wifi_scan_start()` then returns `ESP_ERR_WIFI_STATE` and
  `scanComplete()` returns -2 forever. The fix is `esp_wifi_disconnect()` plus
  disabling auto-reconnect. `[meas]`
- `actions/setup-python` with `cache: pip` fails the job outright if there is
  no `requirements.txt` or `pyproject.toml` in the repository. `[meas]`
- ELM327 v1.5 clones do not assemble multi-frame ISO-TP themselves. `AT CAF0`
  plus manual framing is required, and the 7-byte payload limit is why this
  project exists. `[meas]`

---

## Process

- **Tests that cannot fail are worse than no tests.** Eight were found in one
  day: two by hand, six by review. The check is a mutation - break the thing
  the test defends, and the test must go red. If it stays green it defends
  nothing.
- Mutations that passed silently included shortening P2* from 5000 to 1500 ms,
  which is the exact value the whole `0x78` branch exists for, and painting
  every screenshot magenta.
- Claiming something works without running it cost time twice today: CI was
  asserted working while both runs were failing, and a WiFi fix was reported
  as root-caused when it had only changed the error message.
- A report that separates "I wrote" from "I ran" is worth more than a confident
  one. One agent handed back incomplete work and said exactly which parts had
  never been compiled; that was more useful than a claim of done.

---

## Open - do not rely on these

- Sleep current of the board with the panel off. `[open]`
- Which OBD pins on this van are live with the ignition off. Pin 16 is
  permanent; whether any discretionary pin is switched is unknown. `[open]`
- Whether the X82 body module answers a diagnostic request on a sleeping bus,
  and whether it logs the wake-up. `[open]`
- Absolute LED current of the backlight. FB bias current across `R6` shifts the
  operating point by up to 179 mV, so the real figure is somewhere between 58
  and 110 mA. Does not affect the dimming slope. One measurement of `VLED-`
  settles it, bottom side. `[open]`
- Eye-to-panel distance in the intended mounting position, which sets the
  smallest legible tile. `[open]`

---

## Added after the first full day

- **Deep sleep does not bring USB back.** After the measurement firmware ended
  in `esp_deep_sleep_start()`, the board stayed enumerated as
  `303a:1001 Espressif USB JTAG/serial debug unit` but never created a serial
  node again - 48 s of polling found nothing. A hard reset fixed it. In the
  vehicle this does not matter; it does mean no USB debugging after a wake.
  `[meas]`
- **The serial node is not always `ttyACM0`.** It came up as `ttyACM1` and a
  flash aimed at `ttyACM0` failed with "device not connected" while the board
  was sitting there connected. Never assume the number - the same mistake was
  already fixed once in `talento_monitor.py` and then repeated by hand. `[meas]`
- **A clean `pio run` says nothing about files PlatformIO never saw.** The
  whole `src/net/` layer compiled to nothing for two rounds because the
  directory was in no build - not the firmware, not the host build. The check
  that actually answers the question is `ls .pio/build/*/src/<dir>/` for the
  object files. `[meas]`
- **A guard belongs in the `.c`, not the header.** An `#error` meant to stop a
  layer from reaching the CAN driver sat in `net/report.h` and `net/server.h`,
  where it fired on every consumer including `main.cpp`, which is supposed to
  drive the bus. Right idea, wrong file: in the header it is an assertion about
  everyone, in the `.c` it is an assertion about that one layer. `[meas]`
- **A limit belongs with the code that enforces it.** `NET_AP_CLIENTS_MAX`
  lived in `net/server.h`, which is not compiled when the web module is off -
  but the access point still works in that build, so the limit disappeared
  with the module. `[meas]`
- **A local build without `-DCMAKE_BUILD_TYPE=Release` cannot see what CI
  sees.** `-Wstringop-overflow` and `-Wformat-truncation` need optimised code
  to analyse, so an unoptimised build silently turns them off. CI builds the
  host target with `Release`; the command written down for humans did not. Five
  runs failed in a row while every local check was green. The fix is not "look
  at CI more often" - it is that the documented command has to be the same
  command. `[meas]`
