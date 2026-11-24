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
- [x] UI split from board code, so it builds on a host and CI renders previews
- [ ] Back gesture from the left edge - implemented, **not yet confirmed on hardware**
- [x] Font with Czech and German diacritics, generated from Montserrat
- [x] Interface translated: English (default), Czech, German, switchable on
      the settings page and remembered in NVS. Adding a language is one file
      in `lang/`, see `lang/README.md`
- [ ] Font larger than 48 px for true driver-glance sizing
- [x] **Reset buttons** in settings, behind a SERVICE button and then a
      hold-to-confirm. Each one names what it erases with real numbers -
      how many networks and their names, what the profiles hold now, what
      the bus counters read - because "reset settings" alone is a blind
      promise:
  - [x] Restart the device
  - [x] Settings back to defaults: all three profiles and the units. The
        language stays, because it is the one setting whose reset can make
        the screen unreadable for the person who pressed the button
  - [x] Forget every stored WiFi network at once, passwords included
  - [x] Zero the bus counters

## Debug panel and profiles

- [ ] **Debug tile** in the pull-up grid, bottom row, second from the right
  - [ ] FPS and CPU overlay toggle. Note: LVGL's built-in `LV_USE_PERF_MONITOR`
        is a compile-time switch, so a runtime toggle needs our own label with
        its own frame timing. The built-in one cannot be turned on from a menu
  - [ ] Show raw values next to scaled ones, for checking a vehicle profile
  - [ ] Bus statistics: frames per second, error counters, timeouts
  - [ ] Log level
  - [ ] **Skip the write confirmation** - turns the unlock sequence off for this
        run only, with the explanation beside it. Never persisted, see
        [`docs/safety.md`](docs/safety.md)
  - [ ] **Try captcha** - runs the unlock sequence as a rehearsal, unlocking
        nothing and sending nothing, so it can be tested without a vehicle
- [x] **Three settings profiles** plus "reset to default"
  - [x] Stored in NVS. Six entries of 32 B for the profiles, four for the
        shared part, so 320 B of the 20 kB partition - **no SD card is
        needed**, keep the card for trip logs
  - [x] Named BENCH / DAY / NIGHT, not 1 / 2 / 3: a number has to be
        remembered, a name is read at a glance in the dark
  - [x] A profile covers **brightness and display blanking** - the two
        things that are genuinely different on the bench, in daylight and
        at night. Units and language are **shared**, because they belong to
        the person and not to the situation, and switching a profile must
        never make the interface unreadable. Debug flags stay out as well,
        together with the write confirmation switch, which must never be
        stored or exported - a profile would otherwise carry a disabled
        safety gate between devices
  - [x] Profiles replaced the separate day/night brightness pair. Two
        mechanisms for one thing meant six stored levels and two switches
        that could disagree
  - [x] Switched from the top strip, where it is one tap from every page,
        and from the settings page

## Known bugs

- [x] **Closing the WiFi scan dialog sometimes crashes.** `wifi_list` was left
      pointing at a deleted object and a scan finishing afterwards made
      `wifi_poll()` write into freed memory. Every dialog now clears its own
      state from an `LV_EVENT_DELETE` handler, so no exit path can forget it.
      Reproduced and confirmed fixed with AddressSanitizer in the host build
- [x] Left edge swipe does not go back from a dialog. The real cause was not
      the keyboard: every LVGL object is created with `LV_OBJ_FLAG_GESTURE_BUBBLE`,
      so the gesture walked past the dialog up to the screen, which switched
      page underneath instead. Dialogs now clear that flag and handle the
      gesture themselves
- [x] One swipe could act twice. LVGL re-arms the gesture when the object under
      the finger changes, so the rest of the same swipe fired again - pages
      flew past two at a time. Acting on a gesture now swallows the rest of
      the press
- [x] A drag starting on a navigation button also counted as a tap and switched
      page. Clicks are now discarded when finger travel exceeds 30 px
- [x] **Remembered networks.** Six networks are kept in NVS, most recent first.
      A known network connects without asking, is marked green with a tick in
      the list, and a long press offers to forget it
- [x] **WiFi scan ends on "scanning" and finds nothing.** `scanComplete()`
      returns -2 for a failed scan and the UI treated anything below zero as
      "still running". The scan now reports failure, an empty result and a
      15 s timeout separately, with a retry button; the board puts the radio
      into station mode and drops an unfinished connection attempt first,
      because `esp_wifi_scan_start()` refuses to run in that state

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

## Vehicle presence and actuator tests

- [ ] **Detect whether the device is connected to a vehicle.** Bus traffic is
      the honest signal - an active terminated bus looks nothing like an open
      pair of wires. Replace the DEMO badge in the status strip with it
- [ ] **Test page with actuators.** Each one behind a confirmation, each a
      single shot:
  - [ ] Flash the hazard lights once
  - [ ] Turn a single lamp on and off
  - [ ] Lock and unlock the doors
  - [ ] Lower and raise a window

  These are body module functions, so they need the legacy Renault transport,
  not UDS. Useful as a self-test and as a way to confirm that a parameter
  change did what it was supposed to.
- [ ] The same actuator tests reachable remotely

## Remote access - work on the car from a desk

The feature that changes how the device is used: leave it in the vehicle and
develop without sitting in the driver's seat.

- [ ] **Remote diagnostic gateway over WiFi**
  - [ ] Tunnel CAN and ISO-TP requests from a PC to the vehicle
  - [ ] **Emulate an ELM327 on a TCP port.** DDT4All, pyren and most other
        tools already know how to talk to a WiFi ELM327. Expose that interface
        and every existing tool works remotely with no changes on their side.
        Our implementation can handle multi-frame properly, which the real
        dongle cannot
  - [ ] Authentication with a key or password. **Not an open port** - a device
        that can unlock the doors must not be reachable by anyone on the network
  - [ ] Choose between a plain socket, SSH or TLS. SSH brings key auth for free
- [ ] **Web server** - live values in a browser on a phone
  - [ ] Access point mode, so it works with no infrastructure around
  - [ ] Station mode, already done for OTA
  - [ ] Mode switch in settings
  - [ ] Server-sent events for live values
  - [ ] CSV download of the current session

## Logging and fault memory

- [ ] Log to the TF card with timestamps from the on-board RTC (PCF85063)
- [ ] Fault memory page with status bit decoding
- [ ] Freeze frame page (`19 04`)
- [ ] Extended records page (`19 06`): occurrence counter, odometer stamps

## Later - careful territory

Everything here writes to the vehicle. Each needs a confirmation dialog, a
precondition check and a backup of whatever it overwrites.

- [ ] Forced particulate filter regeneration (`31 01 02 0D`)
- [ ] Service interval reset
- [ ] Adaptation resets

## Hardware

- [ ] Ignition sensing on `DI0` so the device sleeps with the vehicle.
      At about 3 W it will flatten a healthy battery in under a week
- [x] **Backlight dimming through `GPIO6`** - done and measured on hardware.
      One wire from `GPIO6` to the `BL` pad, no cut, nothing removed. The pad
      feeds the driver's feedback node through a 10 k / 1 uF filter, so a
      filtered PWM voltage moves the LED current over the full range while the
      panel itself is never touched. See `docs/hardware/backlight.md`
- [ ] **Ambient light sensor** for adaptive backlight. Now worth doing, since
      the backlight is a real dimmer. Needs a free pin or an I2C part, and
      `GPIO6` is already spent
- [ ] **Switch the profile from the vehicle** instead of from a sensor. The
      car already knows - lighting state and instrument dimming are on the
      bus. Per vehicle, so the mapping belongs in the vehicle profile, not
      in the firmware. The brightness dialog already says why it is not
      offered yet
- [ ] Support a second board with a different pinout, to prove the abstraction
- [ ] Enclosure and a dashboard mount

## Ideas worth keeping

- [ ] **Interface that changes with road speed.** Below about 10 km/h the
      driver can look at the screen, so sliders and dense controls are fine.
      Moving, everything should be large buttons with no precision required.
      The speed is already on the bus and already read. Nice to have, not
      a promise

## Deliberately not doing

- **Software PWM through the `EXIO2` expander line.** Measured and closed. That
  line carries `DISP`, which blanks the panel as well as the backlight, so it
  can never dim anything. Real dimming goes through `GPIO6` and is done - see
  `docs/hardware/backlight.md`.
- **Driving the ECU through an ELM327 dongle.** The 7-byte limit and the serial
  bottleneck are the reason this project exists. Emulating one so that other
  tools can connect to us is a different thing and is on the list above.
- **Clearing fault memory as a convenience button.** It destroys the extended
  records, which are often the only evidence of what happened.

## Open questions

- Operating range of the board is 0 to 65 C. A vehicle cabin leaves that range
  in both directions. Permanent installation needs a different board or a
  sheltered place to put it.
- Only one CAN interface. Enough for a platform where everything sits on one
  500 kbit/s bus, not enough in general.
- How much of the body module command set is safe to expose. Lowering a window
  remotely is useful for testing and unpleasant when triggered by accident.
