# Conformance of the build against docs/requirements.md

Every bullet in `docs/requirements.md`, checked against the tree. The tag in
the requirements file is the claim; the verdict column says whether the claim
holds.

| verdict | means |
|---|---|
| `ok` | the tag matches what is built |
| `over` | tagged better than it is - the tag claims more than exists |
| `under` | tagged worse than it is - already built |
| `n/a` | cannot be established from this repository |

**How it was checked.** Strongest evidence available, in this order: a named
runtime check in `sim/lv_shot.c` or `test/*`, then a rendered screenshot that
was looked at and measured, then source.

Evidence base, all re-run against the tree as of `src/ui.c` 00:29:

- `./sim/build/lv_shot` - 181 lines, 46 screenshots, **0 FAIL**
- `./test/build/isotp_test` - 34 tests, 187 checks, 0 errors
- `./test/build/uds_test` - 318 checks, 0 errors
- `lv_shot` re-run with `cs` and `de` - 0 FAIL each
- a one-off instrumented `lv_shot` (temporary copy in `/tmp`, nothing in the
  repo was touched) that walks the LVGL tree at every screenshot and prints
  every visible `LV_OBJ_FLAG_CLICKABLE` object smaller than 120 px. Referred
  to below as **click audit**.

---

## The thing itself

| # | Requirement | Tag | Verdict | Evidence |
|---|---|---|---|---|
| 1 | Touchscreen recorder, read-only by default, live values, fault memory, drawn like a chart recorder | done | `ok` | shots `05-MAIN`, `04-chyby-detail`, `07-BOOST`. BOOST is a real time-axis chart: `lv_chart_set_next_value` runs every `DIAG_FAST_MS` regardless of whether data arrived, and the shot shows the resulting **gap** where the ECU went quiet. Read-only: see #47, #51. |
| 2 | Two halves - transport plus vehicle knowledge in one place, interface in another | part | `ok` (with a correction) | The board half does separate cleanly (`ui_host_t`, `config/board/*.env`). The note "the vehicle profile is a document, not yet a data file" is not quite right: `config/vehicle/x82-trafic3-talento.env` exists and carries name, bitrate, `0x7E0`/`0x7E8`. What is **not** in a data file is the knowledge that matters - the DID map and scaling live in a C table in `src/diag/poller.c` (e.g. `[DIAG_CH_RGN_DUR] = { 0x2487u, ... }`). |
| 3 | Values must never be invented | done | `ok` - **hardest-checked item, see the audit below** | |
| 4 | Public under GPLv3 | done | `ok` | `LICENSE` is GPL-3.0 verbatim; `README.md` states it. |

### Audit of requirement 3 - every path a value takes to the screen

Not just the one function that does it right. All of them:

- `val_fmt()` (`src/ui.c:376`) is the single formatter, and it evaluates the
  caller's format string **only** in the `LIVE` and `STALE` branches. `NONE`
  prints `VAL_NONE` ("---"), `REFUSED` prints `7F xx`. A caller cannot leak a
  number by accident.
- 18 of the 20 numeric labels go through it (`grep val_fmt src/ui.c`).
- The two that do not are both gated first:
  - `lbl_gap` (`ui.c:3844`) is inside `if (ch_has(BOOST) && ch_has(BOOST_SP))`,
    else branch writes `VAL_NONE`.
  - `lbl_regen` (`ui.c:3764`) is inside `if (!ch_has(RGN_DUR)) ... VAL_NONE`.
- **Non-text paths**, which a text-only audit would miss:
  - arcs: `lv_arc_set_value(arc_speed, ch_has(SPEED) ? v : 0)` and the same for
    rpm and boost. Value at the arc minimum means LVGL draws no indicator at
    all - shot `00-motor-bez-vozu` shows an **empty track**, not a needle at
    zero. This reads as "nothing", not as "zero".
  - DPF rail bar: `lv_bar_set_value` is always called, but the indicator
    opacity is set to `LV_OPA_TRANSP` when `!has` (`ui.c:3596`), so the fill is
    invisible. Shot `00-motor-bez-vozu` - empty outline with dashes.
  - chart: `LV_CHART_POINT_NONE` is pushed when the pair is not both `LIVE`.
- Four states are visually distinct. Pixel diff of `02-motor-ziva-data` against
  `03-motor-stara-data`: the stale numbers change from `(246,222,189)` bone to
  `(98,121,139)` grey, and nothing else in those boxes moves. Refused is red
  `7F 31`. Missing is grey `---`.
- Runtime checks that defend it: `hodnota, kterou jednotka nevydala, se
  nekresli jako cislo`, `neprecteny udaj vypada presne jako pred dotazem`,
  `odmitnuty udaj vypada jinak nez chybejici`, `po utichnuti jednotky je cislo
  videt jinak nez zive`, `dokud se nic necetlo, vypadaji vsechny hodnoty
  stejne`. All PASS.

**No hole found.** One cosmetic inconsistency: stale grey `C_LABEL` is the same
colour as the missing dashes, so the two states are told apart by content
(digits vs dashes) rather than colour. That is still unambiguous.

---

## Interface - Layout

| # | Requirement | Tag | Verdict | Evidence |
|---|---|---|---|---|
| 5 | Menu overlays the screen, never resizes it | done | `ok` | check `vysunuti navigace obsah stranky nezmensi` and `obsah stranky saha i pod vysunutou listu`. Shot `15-nav-tazeni` shows the gauges at full size behind a half-pulled panel. |
| 6 | Page buttons 129 x 140 px | done | `ok` - **exact** | `lv_shot` prints `tlacitka stranek: 6, nejmensi 129 x 140 px`. The 110 px predecessor is guarded by check `tlacitko stranky ma aspon 120 x 120 px`. |
| 7 | Second menu level keeps page buttons at the top, one panel that grows | done | `ok` | check `lista stranek je videt i ve stavu mrizky` compares a pixel hash of the bar strip in state 1 against state 2 and requires all six buttons in it. Shot `13-nav-mrizka`. |
| 8 | `DEBUG` tile second from the right in the bottom row | done | `ok` | shot `13-nav-mrizka`. `NAV_TILE_DEBUG = (ROWS-1)*COLS + COLS-2` (`ui.c:854`), and `NAV_TILE_ROWS` is 2, so the bottom row really is the bottom. |
| 9 | Status bar: WiFi name or `none`, 0-5 bars, transmit/receive icon, vehicle link state | done | `ok`, **plus one thing you did not ask for** | shot `12-nav-lista`. Bars: `WIFI_BARS 5` with a 5-threshold table and `rssi == 0 -> 0 bars` (`ui.c:3237`), so 0 to 5 inclusive. Icon: two chevrons, up stays dark in listen-only. Link state: `NO BUS` / `LISTEN` / `ECU` plus age - shots `00` vs `02`. **Extra:** the bar also carries `COOLANT 88` and the profile switcher. The profile switcher is accounted for under #31; the coolant readout is in no requirement (it is in `docs/ui.md` "Layout"). |
| 10 | Touch targets at least 120 px | done | **`over`** | see the table below |
| 11 | Pages splittable like i3/sway | idea | `ok` | `docs/tiles.md` (33 kB) exists, nothing in `src/`. |
| 12 | Interface that changes with road speed | idea | `ok` | nothing in `src/`, correctly not promised. |

### Requirement 10 - every clickable element, measured

Click audit output, deduplicated. These are visible, clickable LVGL objects
under 120 px in at least one dimension. There are no extended click areas
anywhere (`grep ext_click_area src/ui.c` - no matches), so the drawn size is
the touch size.

| Element | Size | Where |
|---|---|---|
| Settings page buttons (speed, temp, dim, language, wifi, CAN, profile, brightness) | **160 x 68** | shot `10-SETUP` |
| `DISPLAY OFF`, `SERVICE ...` | **170 x 68** | shot `10-SETUP` |
| Dialog `BACK` button | **124 x 70** | measured off shot `16-potvrzeni`: x 660-783, y 14-83 |
| Brightness level buttons | **104 x 120** | shot `28-jas-dialog` - 104 px wide |
| Fault list rows | **350 x 48** | shot `04-chyby-detail` |
| WiFi network rows | **598 x 58** | shot `23-wifi-seznam` |
| Debug `SAVE TO ...` / `DEFAULTS` | **128 x 100** | shot `14-diagnostika` |
| Debug FPS toggle | **260 x 100** | shot `14-diagnostika` |
| Top-bar profile switcher | **164 x 64** | present on every page |
| Navigation handle | **800 x 44** | present on every page |
| DPF rail | **64 x 416** | present on every page |
| Password keyboard keys | roughly **60 x 60** | shot `24-wifi-heslo` (an `lv_keyboard` button matrix, so it does not show up as objects) |

What **is** at least 120 px: the six page buttons (129 x 140), the navigation
tiles (`_Static_assert(NAV_TILE_H >= 120)` at `ui.c:856`), the two confirmation
buttons (360 x 120), and the no-dimming note (check `poznamka ma porad dotykovy
cil 120 px`).

So the rule holds for the controls someone deliberately sized and for nothing
else. `docs/ui.md` is honest about this - its table claims only "navigation
buttons 128 px" and "dialog buttons 120 px tall". The blanket claim is only in
`requirements.md`. **Retag to `[part]`** and name the exceptions.

---

## Interface - Gestures

| # | Requirement | Tag | Verdict | Evidence |
|---|---|---|---|---|
| 13 | Swipe left/right moves between pages | done | `ok` | checks `tuknuti na tlacitko navigace stranku prepne`, `tah od praveho okraje pres sloupec DPF prepne stranku`. Mapping in `gest_release_cb`: `LEFT` -> next, `RIGHT` -> previous (`ui.c:1127`). Whether this is the second swap or the third is a fact about the conversation, not about the tree - `n/a` on the history, `ok` on the behaviour. |
| 14 | A swipe can start anywhere, including over the DPF rail | done | `ok` | check `tah od praveho okraje pres sloupec DPF prepne stranku` (swipe begins at x=770, inside the rail). |
| 15 | Swipe from the left edge means back | done | `ok` | `press_x < GEST_EDGE (70)` branch at `ui.c:1129`; checks `gesto zpet se provede po pusteni`, `gesto zprava zavre potvrzovaci dialog`. |
| 16 | Every gesture acts on release | done | `ok` - **well defended** | checks `gesto zpet se nespusti, dokud je prst dole` and `vraceny tah gesto zrusi`. The second one is the real test: finger goes out, comes back, releases, nothing happens. |
| 17 | A drag starting on a button must not click it | done | `ok` | `drag_guard_cb` registered before every handler in `mk_button`; check `tah z tlacitka navigace neprepne stranku`, with the counter-check `tuknuti na tlacitko navigace stranku prepne` so the guard cannot be passing by being too strict. |
| 18 | Menu dragged like an Android bottom sheet, without lifting | done | `ok` | shot `15-nav-tazeni` is taken mid-drag with the finger still down, and the panel sits at an intermediate height. `lv_shot` drags down 140 px while held, then releases. No assertion on the reverse direction specifically - the evidence here is the picture. |
| 19 | Choosing a page from the menu closes the menu | done | `ok` | check `vyber stranky z listy menu zasune` - counts visible page buttons in the bar strip afterwards, must be zero. |

---

## Interface - The DPF rail

| # | Requirement | Tag | Verdict | Evidence |
|---|---|---|---|---|
| 20 | Tapping marks it and does nothing else | done | `ok` | checks `tuknuti na sloupec DPF neprepne stranku` and `tuknuti na sloupec DPF neotevre DPF`. |
| 21 | Holding opens DPF; 1 to 1.5 s; countdown cancelled by movement | done | `ok` - **exact** | `RAIL_HOLD_MS 1200` (`ui.c:2804`), inside the asked range. Checks `podrzeni na sloupci otevre DPF` and `pohyb odpocet zrusi a z tahu je prepnuti stranky`. |
| 22 | Dragging from it still switches pages | done | `ok` | same check as #14. |
| 23 | Tap feedback must not look like a change in the measured value | done | `ok` | the feedback is a separate 3 px rule in `C_DEMAND` blue growing from the bottom (`rail_rule`, `ui.c:3606-3624`); the soot fill is a different object in sage/bone/vermilion whose opacity is driven only by `ch_has`. Two different objects, two different palettes. |

---

## Interface - Confirmations

| # | Requirement | Tag | Verdict | Evidence |
|---|---|---|---|---|
| 24 | Destructive actions confirmed by holding 3 s | done | `ok` - **exact** | `HOLD_MS 3000`. Each destructive action has the pair tap-does-nothing / hold-does-it: `tuknuti ulozene site nesmaze` + `podrzeni vyprazdni seznam ulozenych siti`, `tuknuti citace nevynuluje` + `podrzeni vynuluje citace sbernice`, `tuknuti desku nerestartuje` + `podrzeni desku restartuje`, `tuknuti nastaveni neresetuje` + `podrzeni vrati profily do vychoziho stavu`. |
| 25 | Countdown in the middle of the screen, blinks yellow once a second | done | `ok`, one quibble on the colour | measured off shot `18-potvrzeni-odpocet`: ring 200 px tall, y 109-308, horizontal centre x=400 - exactly the screen centre; vertical centre y=208 against a screen centre of 240, so 32 px high, inside the middle third. Blink: `hold_flash_ms` is re-armed whenever the whole-second digit changes and the flash lasts `HOLD_FLASH_MS 200` - once a second. Shot `17-potvrzeni-blik`. The flash colour is `C_ACTUAL`, a warm bone `(246,222,189)` rather than a yellow. Call it right in behaviour, loose in the word. |
| 26 | Two equally sized buttons | done | `ok` - **measured equal** | from shot `16-potvrzeni`, scanning the pixel rows: left run x 30-389 (360 px), right run x 410-769 (360 px), both y 330-449 (120 px). Identical. Source agrees: `mk_button(m, 30, 330, 360, 120, ...)` and `mk_button(m, 410, 330, 360, 120, ...)`. |
| 27 | Every dialog has a back button at the top right | done | **`over`** | true for 19 of 20 dialog screenshots. The **WiFi password dialog has no back button** - click audit for scene `wifi-heslo` lists only the textarea and the objects of the page behind the modal; shot `24-wifi-heslo` confirms the top strip is the SSID caption and the text field, nothing else. It can only be left by the right-swipe gesture (check `gesto zprava zavre dialog s heslem`). Either add the button or amend the requirement. |
| 28 | The finished hold must read as a statement, not a button | part | **`under` - done while this was being written** | at the start of this review `STR_HOLD_DONE` was `"OK"` on a `\n` line under a tick. The current tree has `"DONE"` (`lang/en.h`), laid out as `LV_SYMBOL_OK "  %s"`, drawn in sage on the panel with no button chrome - re-rendered shot `19-potvrzeni-hotovo`. `STR_OK` elsewhere became `"GOT IT"`. **Retag to `[done]`.** |

---

## Interface - Brightness and blanking

| # | Requirement | Tag | Verdict | Evidence |
|---|---|---|---|---|
| 29 | Levels as buttons never a slider; five levels, each roughly halving the LED current | done | `ok` - **arithmetic checks out** | `grep lv_slider src/ui.c` - zero matches. `BL_LEVEL = {100, 75, 50, 35, 25}` - five. Those are perceived-brightness percentages; the PWM table converts with `(p/100)^2.2`, so the currents are 1000, 531, 218, 99, 47 per mille. Step ratios 1.9, 2.4, 2.2, 2.1 - each step roughly halves the current, as asked. Shot `28-jas-dialog`. Monotonicity is guarded by check `strida klesa po celem rozsahu jasu`. |
| 30 | Day and night as two stored levels, side by side - superseded by three profiles | done | `ok` | shot `28-jas-dialog` shows BENCH/DAY/NIGHT each on its own row with brightness and dim delay visible at once. Checks `nastaveni urovne ciziho profilu nehne jasem`, `prepnuti na NOC posle nocni uroven`, `navrat na DEN vrati denni uroven`. The supersession is recorded correctly. |
| 31 | Sun/moon toggle - replaced by a profile switcher, cycles three, always present | ask | `ok` | the switcher appears in the top right of every single screenshot (click audit: `164x64 at 629,1` in every scene), and tapping cycles DAY -> NIGHT -> BENCH (checks `prepnuti profilu zmeni jas`, `treti profil ma vlastni jas`). Both open points in the tag are real and both are visible here. |
| 32 | Wake ramps brightness up over about a second | done | `ok` - **exact** | `BL_WAKE_MS 1000` (`ui.c:622`). Checks `probuzeni jas rozsviti postupne` (mid-ramp value strictly between dim and target) and `nabeh po probuzeni dojede na ulozenou uroven`. Note the dim-*down* fade is a separate, shorter `BL_FADE_MS 500` - that is not what the requirement is about, so it is fine. |
| 33 | Dim instead of blank; which one is a compile-time matter of the `GPIO6` wire | done | `ok` | `#if CANOGRAF_BL_PWM_PIN >= 0` in `main.cpp` decides whether `UI_HOST.bl_set_percent` is ever populated; with it null, `bl_can_dim()` is false and `dim_begin()` falls back to `backlight(false)`. No setting exposes the choice. Checks `ztlumeny displej sviti na zvolene urovni` and `ztlumeni displej NEZHASNE`, plus the whole `phase_bez_dratku` which rebuilds the UI over a host with no dimming. |
| 34 | Without the wire, a small button opens a page explaining the modification | done | `ok` - **matches the brief line by line** | shot `45-stmivani-jde-dodelat`: what it is (one wire, nothing cut), what it costs (the PCF85063 interrupt), what it buys (full range, measured), where the measurements are (`docs/hardware/backlight.md`). No steps, no urging. Checks `bez dratku zustane misto urovni jedina poznamka`, `poznamka otevre stranku o dodelani stmivani`. |
| 35 | The brightness button shows its value and so looks like a toggle; openers must look different from changers | part | `ok` | a convention now exists and is applied: `mk_dlg_button` appends `" ..."` (`ui.c:552`) and is used for DIM AFTER, WIFI, BRIGHTNESS, SERVICE, while SPEED, TEMPERATURE, LANGUAGE, PROFILE, DISPLAY OFF are plain. Shot `10-SETUP`. The original complaint still stands though - BRIGHTNESS still reads `100 % ...`, value and all. `[part]` is the right tag. |
| 36 | The WiFi button shows `none` or the network name | done | `ok` | shot `10-SETUP` (`none ...`), checks `pripojena sit stoji ve stavovem radku` and `bez pripojeni se v liste vrati 'none'`. |

---

## Interface - Settings

| # | Requirement | Tag | Verdict | Evidence |
|---|---|---|---|---|
| 37 | Three profiles, switchable quickly, persistent across restarts | done | `ok` | `UI_PROFILES` 3, two switch paths both checked (`tlacitko PROFIL v nastaveni strida stejne jako lista`). Persistence: `host_save_cfg` writes every profile field to NVS `Preferences` (`main.cpp:150`). |
| 38 | Reset buttons behind a hold, each showing what exactly is lost, language never reset | done | `ok` | shot `36-reset-nabidka` shows live numbers on each button (`3 profiles and units`, `2 saved`, `rx 2640, tx 265`, `up 0:01:52`); shot `37-reset-site` names the two networks that will go. Language: `reset_cfg_ok_cb` (`ui.c:2251`) resets profiles, profile index, mph and fahrenheit and **never touches `cfg.lang`**. |
| 39 | Debug tile holds FPS overlay, raw values, bus statistics, save to profile, reset to default | part - "save and reset to profile are not wired" | **`under`** | all five are there and all five work. Shot `14-diagnostika` shows RAW VALUES, CAN BUS, POLLING, the FPS+MEMORY toggle, `SAVE TO DAY` and `DEFAULTS`. `dbg_save_cb` (`ui.c:3037`) sets `PROF.perf` and calls `save_cfg()`; `dbg_default_cb` (`ui.c:3045`) restores from `ui_profile_default()` and saves. The save button even marks itself unsaved with a 3 px accent border. **Retag to `[done]`.** |
| 40 | Czech, English, German, each in its own file | done | `ok` | `lang/cs.h`, `lang/en.h`, `lang/de.h`, included by `i18n.c` with a compile-time count assertion. |
| 41 | Czech and German diacritics need a generated font | done | `ok` | `lv_shot <dir> cs` and `lv_shot <dir> de` both run with 0 FAIL; shot `cs/36-reset-nabidka` renders `udrzba, VYCHOZI, CITACE, SITE, bezi` with correct carons and acutes, shot `de/16-potvrzeni` renders `hort, lasst, HORE ZU, ZURUCK` with correct umlauts. Fonts generated by `tools/gen_fonts.sh` into `src/font_ui_*.c`. |

---

## Vehicle

| # | Requirement | Tag | Verdict | Evidence |
|---|---|---|---|---|
| 42 | Read live values and log to CSV - done on the laptop tool, open on the board | done / open | `ok` on the board, `n/a` on the laptop | no CSV code anywhere in `src/` or `tools/` (only two scons build caches match the string). The laptop tool is not in this repository, so its half cannot be checked here. |
| 43 | Fault memory with status bits decoded, freeze frame and extended records | done | `ok` | shot `04-chyby-detail`: `2 faults`, `P2263-22 CONFIRMED`, `P0401-64 CONFIRMED`, EXTENDED RECORDS (odometer at occurrence, aging counter, occurrences, odometer at first) and FREEZE FRAME (rpm, speed, coolant). Check `vybrana chyba ma docteny detail` requires both record sets non-empty. |
| 44 | Never clear fault memory | done | `ok` - **strongest check in the repo** | no clear service exists: service `0x14` is not implemented anywhere in `src/diag`. `test/uds_test.c:test_no_write_service` walks **every frame the whole 318-check suite ever emitted** and asserts zero occurrences of SIDs `0x14, 0x27, 0x2E, 0x31, 0x34, 0x3B`, while requiring reads to be non-zero so the test cannot pass by emitting nothing. |
| 45 | DPF page: soot, temperatures before and after, regeneration state and progress | done | `ok` on the first three, see #46 on progress | shot `09-DPF`: TEMP BEFORE 300, TEMP AFTER 273, TARGET 550, SOOT 8.3, LOAD 11.2, SINCE LAST REGEN 20, REGEN off. |
| 46 | Manual regeneration behind a hold confirmation, rail state, ECU progress where reported, fifths where not | part - "the UI exists, the command is not sent" | **`over`, and the direction is wrong** | there is no manual regeneration UI at all. The source says so outright (`ui.c:1445`): the button was removed because `31 01 02 0D` is a write, writes belong behind the gate, and a button that sends nothing is worse than no button. Shot `09-DPF` shows the explanation in its place. The fifths estimate was removed for the same reason it should never have been asked for - see the contradiction below. What survives is the running-state tint driven by DID `2487` (`regen_on()`, `RGN_BLINK_MS`), which is measured. The red wash "for the attempt" cannot exist, because no attempt can be made. **Retag to `[open]`** and record that the fifths clause was dropped on purpose. |
| 47 | First connection is listen-only; the gate is in the poller; not a single frame leaves, not even a keep-alive | done | `ok` - **hardest-checked item, see below** | |
| 48 | Switching to normal mode is behind a confirmation that says the device will start transmitting | done | `ok` | shot `16-potvrzeni` - the body says "this board STARTS TRANSMITTING ... diagnostic requests and the acknowledge bit of every frame it receives". Checks `zavreny dialog rezim sbernice nezmeni`, `podrzeni prepne radic do normalniho rezimu`, and the deliberate asymmetry `tuknuti vrati radic do poslechu` - going quiet takes one tap, going loud takes a 3 s hold. |
| 49 | Worth reading next - back pressure, differential pressure, battery current and SoC, injector balance | open | `ok` | `docs/ideas-monitoring.md` (34 kB) exists, nothing in `src/`. |
| 50 | Auxiliary heater, on the bus at `0x1A` | idea | `ok` on the tag, `n/a` on the fact | nothing built. The `0x1A` claim itself appears nowhere in the repository other than in `requirements.md` - **not establishable without the vehicle**, and it is not in `docs/findings.md` either, which is where a claim like that belongs. |

### Audit of requirement 47 - can a single frame get out?

Three independent gates, checked separately:

1. **Poller.** `diag_poller_poll()` is the only function that can start a
   transfer, and its second statement is
   `if (!p->armed) { diag_link_poll(&p->dl, now_ms); return; }`
   (`poller.c:579`). `uds_poll()` - the function that emits `3E 00` on the
   `UDS_TESTER_MS` idle timer (`uds.c:411-427`) - is below that return, so
   **the keep-alive cannot fire while disarmed**. The public API has no other
   way in: `dtc_refresh` and `dtc_select` only set flags that `start_next()`
   consumes, and `start_next()` is only reachable from `poll()`.
2. **Frame intake.** `diag_poller_on_frame()` deliberately lets the link-state
   detector see traffic before the gate and feeds `uds_on_frame()` only when
   armed, which is why the bar can say LISTEN without anything being sent.
3. **Controller.** `can_send()` returns false and bumps `tx_fail` unless
   `s_cfg.mode == CAN_MODE_NORMAL` (`twai.c:244`), and the controller itself is
   started in `TWAI_MODE_LISTEN_ONLY`, which withholds even the acknowledge
   bit. Default is `CAN_MODE_LISTEN_ONLY` (`twai.c:85`); `main.cpp:481` derives
   the arm state from the controller mode, so the two can never disagree.

Runtime proof: check `v listen-only neodejde zadny dotaz` asserts
`bus_tx_frames == 0 && ecu_requests == 0` after 3 s of a live, chattering fake
bus, while check `sbernice zije, i kdyz se nikdo neptal` asserts the link layer
reached `DIAG_LINK_BUS` in the same window - so the silence is real silence and
not a dead test.

One loose end, not a hole: `src/diag/legacy.c` is a complete pre-UDS Renault
client with its own `3E 00` logic, and it is built into the firmware by
PlatformIO's source glob. It has **no caller anywhere in the tree**
(`grep -rl legacy_ src` returns only `legacy.{c,h}`), so it cannot transmit.
It is also not in any requirement - see the list at the end.

---

## Writing to the vehicle

This section is the problem. `src/safety.h` is 331 lines of interface and its
own header says so: *"Tahle hlavicka je jen rozhrani - implementace prijde
spolu s transportni vrstvou."* There is **no `safety.c`**, no `src/write/`, and
no file in the tree includes `safety.h` or calls any `safety_*` function
(`grep -rn "safety" src/*.c src/*.cpp src/*.h sim/` - nothing outside the
header itself). Four of these seven items are tagged `[done]` for something
that has been designed and not built.

| # | Requirement | Tag | Verdict | Evidence |
|---|---|---|---|---|
| 51 | Everything that writes is marked as such | done | `ok`, vacuously | nothing writes. Where a write would have been, it is named: the DPF page says "Starting a regeneration writes to the ECU (31 01 02 0D)" (shot `09-DPF`). |
| 52 | Write access off by default, unlocked deliberately | done - "designed in docs/safety.md" | `ok` | the tag already says designed, and that is exactly what exists. |
| 53 | Simon-says over four coloured icons, one sequence per session | done | **`over`** | no implementation and no UI. No `captcha`/`simon`/`sequence` string in `src/ui.c`, `src/i18n.h` or any `lang/*.h`. The behaviour is specified in `safety.h` comments and `docs/safety.md` only. **Retag to `[open]`.** |
| 54 | No time limit on the unlock | done | **`over`** | same - the rule is stated in `safety.h:25`, there is nothing to time. **Retag to `[open]`.** |
| 55 | Debug menu has a "try the captcha" entry | done | **`over`** | the debug dialog is fully enumerated at `ui.c:3161-3192` and in shot `14-diagnostika`: raw values, CAN bus, polling, FPS toggle, memory, save, defaults. **No captcha entry.** Retag to `[open]`. |
| 56 | A compile-time option builds the firmware with writing removed entirely | done | `ok`, vacuously | the machinery works: `scripts/build_config.py:292-299` appends `-<write/>` to the source filter when `CANOGRAF_ALLOW_WRITE=0`, and `safety.h` raises `#error` on the nonsensical combination. The script's own comment admits `src/write/ zatim neexistuje`. Every current build is a read-only build, so the option removes nothing. |
| 57 | Colour alone must not carry meaning - tokens differ in shape too | done | **`over` as written, `ok` in spirit** | the four tokens do not exist, so the sentence has no subject. The principle is genuinely applied where there is something to apply it to: saved WiFi networks carry a tick as well as the green (`ui.c:1482` says why - colour alone fails in workshop sunlight), a refused value is the literal text `7F 31` as well as red, and the four value states differ by glyph (`---` vs digits vs `7F xx`) and not only by colour. Reword the item to match what it now covers. |

---

## Hardware

| # | Requirement | Tag | Verdict | Evidence |
|---|---|---|---|---|
| 58 | Backlight dimming through one wire, `GPIO6` to `BL`, measured, installed, full range, reversible | done | `ok` | `docs/hardware/backlight.md` plus the `[meas]`/`[sch]`/`[ds]` lines in `docs/findings.md` (driver part number, feedback path, R11/C11 time constant, 0 V = 64.6 mA / 3.3 V = 0 mA). The firmware side is `CANOGRAF_BL_PWM_PIN` + `ui_bl_duty()`. Cannot be re-measured here - **not establishable without the board** - but it is recorded with its method, which is what the project asks for. |
| 59 | The cost is the RTC interrupt line, the only wake path | done | `ok` | recorded in `main.cpp:70`, in `config/board/waveshare-esp32s3-touch-lcd-4.3b.env:33`, and shown to the user on the modification page (shot `45-stmivani-jde-dodelat`). |
| 60 | Ignition-switched supply or permanent power with sleep; needs the sleep current measured first | open | `ok` | nothing built. |
| 61 | A measurement firmware exists; becoming a compile-time option | part | `ok` | `tools/sleep-test/` is a standalone PlatformIO project with its own 153-line `main.cpp` - so it exists, and it is still the second copy the note wants to eliminate. No sleep-related key in `config/default.env` or `scripts/build_config.py`, so the "becoming" half has not started. The tag is fair. |

---

## How the work is run

| # | Requirement | Tag | Verdict | Evidence |
|---|---|---|---|---|
| 62 | Findings carry how they were established | done | `ok` | `docs/findings.md` defines six method tags and every line I sampled carries one. |
| 63 | A test that cannot fail is worse than no test; the check is a mutation | done | `ok`, unevenly applied | `sim/lv_shot.c` carries **39** named mutation recipes, several of them concrete enough to paste (`sed -i 's/#define DRAG_PX  30/#define DRAG_PX  9000/'`). `test/isotp_test.c` has 1. `test/uds_test.c`, 318 checks, has **0**. The "eight found and fixed in one day" count is a fact about a session, not about the tree - `n/a` on the number. |
| 64 | Claims about what works have to be run | done | `ok` | `CLAUDE.md:230` - "Verify, do not claim. Run the build. Render the screens and *look* at them." This report is itself an instance. |
| 65 | Agents own files exclusively | done | `ok` | `CLAUDE.md:211-224` - a lane table with Owns / Must not touch, and "Only one agent may own `src/ui.c` at any moment." |
| 66 | Reports separate what was written from what was run | done | `n/a` | a property of session output, not of the repository. |

---

## Waiting on a decision

| # | Requirement | Tag | Verdict | Evidence |
|---|---|---|---|---|
| 67 | Top-bar profile switcher: three-cycle, two-state toggle, or three plus a long press | ask | `ok` | the built behaviour is the three-cycle, with no long press (checks `prepnuti profilu zmeni jas`, `treti profil ma vlastni jas`). The question is genuinely still open. |

---

## Two requirements that contradict each other

**A. The fifths.** Requirement 46 asks for regeneration progress to "step in
fifths when [the ECU] does not [report one], because smooth movement would read
as a measured value". Requirement 3 says "Anything that was not read from the
ECU is shown as missing, not as a number". A fifth is a number nobody measured.
Stepping it coarsely makes it a *less convincing* invented value, not a
truthful one. The code chose requirement 3 and deleted the estimate
(`ui.c:1441` - "vymyslet ho znamena kreslit cislo, ktere nikdo nezmeril"), and
that was the right call. `requirements.md` still carries both clauses, and
`docs/ui.md` line 63 still describes the five-step sweep. Both need the clause
struck.

**B. The 120 px rule.** Requirement 10 asks for every touch target to be at
least 120 px. Requirement 14 requires a DPF rail at the right edge that a swipe
can cross, and it is 64 px wide; the WiFi flow requires a password entry, and an
on-screen keyboard cannot have 120 px keys on an 800 px screen. The rule as
written cannot hold alongside those two features. The code already knows this
and is inconsistent about it: the comment at `ui.c:1463` justifies putting
"forget network" on a long press because a second button in the row "would be
under the 120 px touch target" - on a row that is itself 58 px tall. Decide
which controls the rule binds (the ones used while driving) and say so.

---

## Things in the product that are in no requirement

- **Coolant temperature in the status bar.** Permanently occupies the centre of
  the top strip (shot `12-nav-lista`). Documented in `docs/ui.md` "Layout", not
  in `requirements.md`. Requirement 9 enumerates four things for that bar and
  this is a fifth.
- **`DISPLAY OFF`** on the settings page - manual blanking, deliberately
  distinct from the dim-after-timeout behaviour of requirement 33. Reasonable,
  unasked.
- **A fourth brightness mode, `BY VEHICLE`**, shown greyed with "requires a
  vehicle connection" (shot `28-jas-dialog`). Requirement 29 asks for five
  levels as buttons and says nothing about following the vehicle's own
  lighting.
- **`src/diag/legacy.c` + `legacy.h`**, a complete pre-UDS Renault protocol
  client (7.6 kB) with no caller anywhere. Compiled into the firmware by the
  source glob. No requirement mentions a second protocol.

---

## Count

| | |
|---|---|
| Items checked | **67** |
| Tag matches reality | **57** |
| Tagged better than it is (`over`) | **7** - #10, #27, #46, #53, #54, #55, #57 |
| Tagged worse than it is (`under`) | **2** - #28, #39 |
| Cannot be established here (`n/a`) | **1** - #66 |

Six of the seven `over` items cluster in two places: the write-safety section
(four of them) and the two features that were deliberately deleted rather than
built (#46, and #27 as a side effect of the keyboard screen).

## Three things to do first

1. **Retag the "Writing to the vehicle" section.** Four `[done]` items (#53,
   #54, #55, #57) describe a mechanism that exists as a header and a design
   document and nothing else. This is the section where a wrong tag is most
   expensive: the whole point of the gate is that nobody believes it is there
   when it is not. `safety.h` is honest; `requirements.md` is not.

2. **Fix requirement 10, not the code.** Measured, the 120 px rule holds for
   the page buttons, the navigation tiles and the confirmation buttons, and
   fails everywhere else - settings 160 x 68, dialog BACK 124 x 70, brightness
   104 x 120, fault rows 350 x 48, keyboard keys about 60 x 60. Enlarging all of
   them is not possible on an 800 x 480 panel. Say which controls the rule
   binds, and then the settings page and the dialog BACK button are the two
   worth arguing about, because both get used with the van moving.

3. **Settle the fifths contradiction in writing.** The code already did the
   right thing; the requirement still asks for the wrong thing, and so does
   `docs/ui.md`. Strike the clause, retag #46 to `[open]`, and record *why* it
   was dropped - it is the cleanest example in the project of the "never invent
   a value" rule beating a feature request, which is exactly the kind of
   reasoning the next session will need.
