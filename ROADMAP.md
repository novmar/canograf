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
- [ ] **Reset buttons** in settings. Each behind a confirmation:
  - [ ] Restart the device
  - [ ] Settings back to defaults
  - [ ] Forget stored WiFi networks
  - [ ] Clear the display blanking and unit preferences only

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
- [ ] **Three settings profiles** plus "reset to default"
  - [ ] Stored in NVS. The partition here is 20 kB and a profile is a few
        hundred bytes, so **no SD card is needed** - keep the card for trip logs
  - [ ] Name each profile, so they are not just 1 / 2 / 3
  - [ ] Profile covers units, blanking, language, debug flags - **except the
        write confirmation switch**, which must never be stored or exported,
        or a profile would carry a disabled safety gate between devices

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
- [ ] **Ambient light sensor** for adaptive backlight. Pointless on this board
      as it stands, because the backlight is a switch and not a dimmer, but
      worth having ready for a board where it is not, or together with the
      GPIO6 modification below
- [ ] Optional: cut `EXIO2` and drive the backlight from `GPIO6` for real PWM
      dimming. See the closed experiment in CLAUDE.md
- [ ] Support a second board with a different pinout, to prove the abstraction
- [ ] Enclosure and a dashboard mount

## Deliberately not doing

- **Software PWM on the backlight.** Measured and closed, see CLAUDE.md.
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
