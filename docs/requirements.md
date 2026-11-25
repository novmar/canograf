# What was asked for

Every requirement that came from the person who owns the van, collected from
the conversation rather than invented. One file so the next session does not
have to reconstruct it.

**Where two requests conflict, the later one wins** and the earlier one is
kept with a note, because the reason it changed is usually the useful part.

| tag | means |
|---|---|
| `[done]` | built and verified |
| `[part]` | partly built, the rest is named |
| `[open]` | agreed but not built |
| `[idea]` | wanted later, not a promise |
| `[ask]` | waiting on a decision from the owner |

---

## The thing itself

- A touchscreen diagnostic recorder for the van, read-only by default, that
  shows live values and fault memory and draws them like a chart recorder.
  `[done]`
- Two halves: transport plus vehicle knowledge in one place, interface in
  another, so a different board or a different vehicle swaps one half.
  `[part]` - the vehicle profile is a document, not yet a data file
- Values must never be invented. Anything that was not read from the ECU is
  shown as missing, not as a number, not as zero, not as the last reading
  without saying so. `[done]`
- The repository is public under GPLv3 so that a derivative stays open.
  `[done]`

---

## Interface

### Layout

- Screens stay full size. Pulling the menu up must **overlay** them, never
  resize them - the first version shrank the gauges and the content jumped.
  `[done]`
- Page buttons were too small. They are 129 x 140 px now, 15.1 x 16.4 mm; the
  earlier 110 px was 12.9 mm, below the 14 mm an in-vehicle control needs.
  `[done]`
- The second level of the menu keeps the page buttons **at the top**, with
  tiles below. It is one panel that grows, not two panels. `[done]`
- `DEBUG` tile sits second from the right in the bottom row. `[done]`
- Top row is a **status bar**: WiFi name or `none`, signal as 0-5 bars like a
  phone, an icon for transmitting / receiving / not connected, and the vehicle
  link state. `[done]`
- Touch targets at least 120 px. `[part]` - **the blanket claim was wrong**,
  and the fix was not to grow everything. Since the interface now has two
  modes (below), the rule reads: **above 10 km/h there is no touch target
  under 120 px anywhere on the screen** - checked by walking the whole LVGL
  tree and measuring the click area, not the drawing, on every page, on the
  tile grid and in the one dialog that opens while moving.
  Below the threshold the small controls stay, because at a standstill a
  person can aim with their eye. Two controls keep their drawing and grow only
  the touch area - the dialog back button to 174 x 120 and the navigation
  handle to 800 x 120 - and both are at a screen edge with nothing beside
  them. What is still open: the 20-25 mm that thin gloves want, and the
  keyboard, which is simply unreachable while moving rather than fixed.
- Pages should be splittable like windows in i3 or sway: halves or thirds,
  each field either split again or holding one widget. Granularity by size -
  percent plus caption, or a value up to 10000, possibly 1000 with a `x10`
  multiplier the way a tachometer does it. `[idea]` - designed in
  `docs/tiles.md`, not built
- Interface that changes with road speed: below about 10 km/h sliders and
  denser controls are fine, moving it should be large buttons only. `[done]` -
  two modes from `2003`. Enters driving at 10 km/h immediately, leaves it
  below 5 km/h sustained for five seconds; the double hysteresis is what keeps
  a queue from flipping the layout every few seconds.
  **A missing speed is not a zero**: with no live reading the interface assumes
  driving, because assuming standing is dangerous exactly when the cable comes
  out at speed, while assuming driving is only annoying on a bench. That state
  also occurs after every power-up and permanently with no vehicle attached,
  so the person may state that the vehicle is standing behind a three-second
  hold; it is offered only when the speed is missing, dropped by the first
  reading above 10 km/h, and never stored.
  Settings lock while moving except brightness and bus mode, and the lock says
  why rather than greying out. Full table in `docs/ui.md`.

### Gestures

- Swipe left and right moves between pages. Direction was changed twice; the
  current setting is the second swap. `[done]`
- A swipe can start **anywhere**, including over the DPF rail at the right
  edge. `[done]`
- Swipe from the left edge means back. `[done]`
- **Every gesture acts on release, not while the finger travels**, so a swipe
  that is pulled back does nothing. This replaced the earlier behaviour that
  fired as soon as a threshold was crossed and caused accidental backs.
  `[done]`
- A drag that starts on a button must not click it. LVGL sends the click to
  wherever the press started regardless of where it ended, so this needed an
  explicit guard. `[done]`
- The menu panel is dragged like an Android bottom sheet - up, hold, back
  down, without lifting. `[done]`
- Choosing a page from the menu closes the menu. `[done]`

### The DPF rail

- Tapping it **marks** it and does nothing else, so a mistake costs nothing.
  `[done]`
- Holding it opens the DPF page. 3 s was too long; 1 to 1.5 s, and the
  countdown **must be cancelled by movement** - past the drag threshold it is
  a swipe, not a hold. `[done]`
- Dragging from it still switches pages. `[done]`
- The tap feedback must not look like a change in the measured value. `[done]`

### Confirmations

- Destructive actions are confirmed by **holding for 3 s**. `[done]`
- The countdown sits in the **middle of the screen**, because a finger on the
  button covers the button. It blinks yellow once a second. `[done]`
- Two equally sized buttons in the dialog - a smaller cancel would be a trap.
  `[done]`
- Every dialog has a back button at the top right. `[part]` - 19 of 20 do.
  The WiFi password dialog has none and can only be left by swiping right,
  which is the one dialog where a stuck user is most likely.
- When the hold finishes, the result must read as a **statement, not a
  button**. `[done]` - the ring is replaced by a flat notice, every button
  disappears, and `OK` became `DONE`. Fixing this also uncovered a real
  fault: during the 700 ms notice the dialog could still be closed, which
  cancelled the timer and silently threw the action away after a full
  three-second hold.

### Brightness and blanking

- Levels as buttons, never a slider - a slider cannot be hit while driving.
  Five levels, each roughly halving the LED current. `[done]`
- Day and night as two stored levels, switchable in one tap, values visible
  side by side. `[done]` - then **superseded**: three profiles
  (`BENCH`/`DAY`/`NIGHT`) hold brightness and dim delay; units and language
  are shared. The agent that did it argued day/night is a subset of profiles
  and two mechanisms would drift apart.
- A small sun/moon toggle in the top bar, optional, enabled from the
  brightness dialog. `[ask]` - replaced by a profile switcher showing the
  profile name. Open points: it cycles three profiles so going dark can take
  two taps instead of one, and it is always present rather than optional.
- Wake-up ramps the brightness up over about a second, so it does not shine
  in the face at night. `[done]`
- **Dim instead of blank.** After the timeout the screen drops to a chosen
  level rather than going dark. Which of the two happens is decided at compile
  time by whether the `GPIO6` wire is fitted, not by a setting. `[done]`
- When the wire is absent, a small button opens a page explaining that a light
  hardware modification can add dimming - what it costs, what it buys, and a
  pointer to the measurements. No step-by-step instructions, no urging.
  `[done]`
- The brightness button in settings shows its current value, which makes it
  look like a value toggle rather than a way into a dialog. Controls that open
  something must look different from controls that change something. `[part]`
- The WiFi button shows `none` or the connected network name. `[done]`

### Settings

- Three profiles, switchable quickly, persistent across restarts. `[done]`
- Reset buttons, each behind a hold confirmation, each showing **what exactly
  will be lost** with real numbers rather than a generic warning. Language is
  never reset, because that is the one setting whose loss can make the device
  unreadable. `[done]`
- Debug tile holds the FPS overlay, raw values, bus statistics, save to
  profile and reset to default. `[done]` - I had this as unfinished; all five
  are wired, and the save button marks itself unsaved with an accent border.
- Language in Czech, English and German, each in its own file. `[done]`
- Czech and German diacritics need a generated font; the built-in one has
  neither. `[done]`

---

## Vehicle

- Read live values and log them to CSV. `[done]` on the laptop tool, `[open]`
  on the board
- Fault memory with the status bits decoded, freeze frame and extended
  records. `[done]`
- **Never clear fault memory as a convenience.** It destroys the extended
  records, which are often the only evidence of what happened. `[done]` - it
  is on the explicit not-doing list
- DPF page: soot, temperatures before and after, regeneration state and
  progress. `[done]`
- Manual regeneration behind a hold confirmation, with the state visible on
  the DPF rail - a red wash for the attempt, a draining fill while it runs.
  `[open]` - **not partly done, not done at all.** I had this as "the UI
  exists, the command is not sent"; in fact the whole control was removed,
  because `31 01 02 0D` writes to the ECU and writing is gated. The DPF page
  carries the explanation where the button used to be.
- When it is built, the preconditions must be checked first and a refusal has
  to say **which** one failed. This needs no guessed thresholds: the ECU
  reports its own reason in `246D`, a 17-value list that includes coolant
  temperature not reached, differential pressure not reached, engine speed
  deviation and regeneration forbidden. See `docs/regeneration.md`. `[open]`
- First connection to a real vehicle is **listen-only**. The gate is in the
  poller: until it is armed, not a single frame leaves, not even a keep-alive.
  `[done]`
- Switching to normal mode is a deliberate act behind a confirmation that says
  the device will start transmitting. `[done]`
- Worth reading next: exhaust back pressure and the filter differential
  pressure instead of the modelled soot estimate; battery current and state of
  charge from the energy module; per-cylinder injector balance at warm idle.
  `[open]` - catalogued with verified identifiers in
  `docs/ideas-monitoring.md`
- Turning on the auxiliary heater remotely is wanted. It is on the bus at
  `0x1A`. `[idea]` - it writes to a control unit, which is a different class
  of risk

---

## Writing to the vehicle

**I marked this whole section done. It is designed, not built.** `src/safety.h`
is an interface and says so in its own header; there is no `safety.c`, nothing
includes it, and no token, sequence or captcha string exists anywhere in the
interface or the translations. The design in `docs/safety.md` is worth keeping
- it is the reasoning, and the reasoning survived a round of review - but none
of it runs.

The one part that is real: **nothing writes to the vehicle today**, so the
protection has nothing to guard yet. Manual regeneration was removed for
exactly this reason. That is a safe state, not a finished feature.

- Reading is safe, writing is not. Everything that writes is marked as such.
  `[done]` - trivially, because nothing writes
- Write access is **off by default** and has to be unlocked deliberately.
  `[open]` - designed in `docs/safety.md`
- The unlock is a Simon-says sequence over four coloured icons, not a timed
  challenge. **One sequence per session, not per write** - a per-write
  sequence gets clicked through without reading. `[open]`
- **No time limit**, because a countdown makes people rush exactly when they
  should be reading. `[open]`
- The debug menu has a "try the captcha" entry so it can be practised without
  arming anything. `[open]`
- A compile-time option builds the firmware with writing removed entirely, for
  anyone who wants a read-only device. `[open]`
- Colour alone must not carry meaning - the tokens differ in shape as well,
  because a red/green pair is useless to a lot of people. `[open]` for the
  tokens, which do not exist. The principle itself **is** applied elsewhere:
  a saved network carries a tick as well as green, a refused value prints the
  literal `7F 31` as well as red, and the four value states differ by glyph.

---

## Hardware

- Backlight dimming through one wire from `GPIO6` to the `BL` pad. Measured,
  installed, full range, reversible. `[done]`
- The cost is the RTC interrupt line, which is the only wake path. `[done]` -
  recorded
- Power and sleep: either an ignition-switched supply, or permanent power with
  the device sleeping and waking on bus activity. The second needs the sleep
  current measured first. `[open]`
- A measurement firmware exists that walks through the states and prints what
  each one costs. `[part]` - becoming a compile-time option so it does not
  live as a second copy of the same code

---

## How the work is run

- Findings go into `docs/findings.md` as they are made, each with **how it was
  established**, so the end of a session can check them instead of trusting
  them. `[done]`
- A test that cannot fail is worse than no test. The check is a mutation:
  break what the test defends and the test must go red. `[done]` - eight such
  tests were found and fixed in one day
- Claims about what works have to be run, not asserted. `[done]`
- Agents own files exclusively; two of them editing one file already cost a
  wasted verification cycle. `[done]`
- Reports separate what was written from what was run. `[done]`

---

## Waiting on a decision

- The top-bar profile switcher: keep it cycling three profiles, make it a
  two-state day/night toggle with `BENCH` reachable only from settings, or
  keep three and add a long press that jumps straight to `NIGHT`. `[ask]`
