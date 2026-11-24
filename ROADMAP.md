# Roadmap

Ordered roughly by dependency. Nothing here is a promise.

## Now - UI only, no vehicle

- [x] Panel, capacitive touch and backlight running on the board
- [x] Six pages, swipe left/right between them
- [x] Two large gauges on the main page, speed and engine speed
- [x] Navigation collapses to a handle and reappears on swipe up
- [x] Tile grid placeholder on a second swipe up (3 x 6, empty)
- [x] Settings: units, display blanking, WiFi
- [x] WiFi station mode plus OTA, so further updates need no cable
- [x] Values animated at 50 Hz from a 20 Hz source so gauges flow
- [ ] Back gesture from the left edge - implemented, **not yet confirmed on hardware**
- [ ] Font with Czech diacritics (built-in Montserrat has none)
- [ ] Font larger than 48 px for true driver-glance sizing

## Next - talk to the car

- [ ] TWAI at 500 kbit/s, 11-bit identifiers
- [ ] Sniffer page: raw frame identifiers on screen. **Do this first** - it
      proves wiring and transceiver before anything depends on it
- [ ] ISO-TP: single, first and consecutive frames, flow control
- [ ] UDS client: `0x22` read, `0x19` fault memory, `0x10` session
- [ ] Legacy Renault transport (`0x21` read, `0x3B` write, session `10 C0`) -
      needed because the body, energy and airbag modules do not speak UDS
- [ ] Vehicle profile loaded from a table, not hard-coded
- [ ] Replace simulated values with real ones
- [ ] Measure the achievable sample rate and put it on screen

## Then - logging and remote

- [ ] Log to the TF card with timestamps from the on-board RTC (PCF85063)
- [ ] **WiFi + web server** - live values in a browser on a phone
  - [ ] Access point mode, so it works with no infrastructure
  - [ ] Station mode, already done for OTA
  - [ ] Mode switch in settings
  - [ ] Server-sent events for live data
  - [ ] CSV download of the current session
- [ ] Fault memory page with status bit decoding
- [ ] Freeze frame page (`19 04`)
- [ ] Extended records page (`19 06`): occurrence counter, odometer stamps

## Later - careful territory

Everything in this block writes to the vehicle. Each needs a confirmation
dialog, a precondition check and a backup of what it overwrites.

- [ ] Forced particulate filter regeneration (`31 01 02 0D`)
- [ ] Service interval reset
- [ ] Adaptation resets

## Hardware

- [ ] Ignition sensing on `DI0` so the device sleeps with the vehicle.
      At about 3 W it will flatten a healthy battery in under a week
- [ ] Optional: cut `EXIO2` and drive the backlight from `GPIO6` for real PWM
      dimming. See the closed experiment in CLAUDE.md
- [ ] Support a second board with a different pinout, to prove the abstraction
- [ ] Enclosure and a dashboard mount

## Deliberately not doing

- **Software PWM on the backlight.** Measured and closed, see CLAUDE.md.
- **Driving the ECU through an ELM327.** The 7-byte limit and the serial
  bottleneck are the reason this project exists.
- **Clearing fault memory as a convenience button.** It destroys the extended
  records, which are often the only evidence of what happened.

## Open questions

- Operating range of the board is 0 to 65 C. A vehicle cabin leaves that in both
  directions. Permanent installation needs a different board or a different
  place to put it.
- Only one CAN interface. Enough for a platform where everything sits on one
  500 kbit/s bus, not enough in general.
