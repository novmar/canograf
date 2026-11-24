# Writing to a vehicle

Reading is free. Writing is not, and this device can already do both: `2E 2110`
resets the service data in the engine control unit, `3B 16` rewrites the battery
configuration in the energy module, `31 01 02 0D` drives the exhaust past
600 C. All three were done on a real van. A wrong value in any of them can leave
the vehicle unable to start, and some of it cannot be put back the way it was.

The question is not whether to allow writing - the whole point of the project is
to do work a generic scan tool cannot. The question is what has to happen
between a finger and a frame on the bus.

This document decides that. The interface is in
[`src/safety.h`](../src/safety.h); there is no implementation yet and that is
deliberate - the policy is settled first, the transport layer picks it up when it
lands.

---

## What we are actually defending against

Not an attacker. Someone holding the device can flash their own firmware over
USB and do as they please; nothing here changes that, and pretending otherwise
would be theatre.

The realistic failures, in the order they will happen:

1. **A wrong tap.** The device is mounted in a van, the road is bad, a finger
   lands on the DPF page and the vehicle starts a regeneration on a filling
   station forecourt.
2. **Somebody who just downloaded this.** They flash it, plug it into their own
   van, and go looking at what the buttons do. They must not be able to find out
   by accident.
3. **A tool left connected.** The remote gateway is on, a laptop runs DDT4All or
   pyren against the ELM327 emulation, a script sends a configuration write while
   the van is being driven somewhere else.
4. **A write at the wrong moment.** Right block, right value, battery at 11.6 V,
   half the block lands.

Everything below exists for one of those four.

---

## The shape of it

| Gate | Lives in | Default | How it opens |
|---|---|---|---|
| 1 - the build | `platformio.ini` / `config`, `#if` | writing not compiled in | rebuild with a flag, deliberately |
| 2 - the switch | Settings, RAM only | off | one tap, which starts gate 3 |
| 3 - the sequence | Full-screen dialog | - | read the warning, repeat five steps |

Three gates in series, and one rule that holds them together:

> **The gate is in the transport, and it applies to every channel equally.** The
> touchscreen, the web server and the ELM327 emulation all reach the bus through
> `safety_send_guarded()`. The raw transmit is `static` inside the transport unit
> and takes a `safety_token_t` that only `safety.c` can mint. While the gate is
> shut, a write is refused where the frame would have been built - not by hiding
> a button. A remote client cannot open the gate at all: only a person standing
> at the panel can.

---

## Gate 1 - the build

Two flags. Both have the safe value even when nothing sets them, so a fresh
checkout is protected without reading any documentation:

```c
#ifndef CANOGRAF_ALLOW_WRITE
#define CANOGRAF_ALLOW_WRITE 0          /* write code is not in the image */
#endif

#ifndef CANOGRAF_WRITE_UNLOCKED
#define CANOGRAF_WRITE_UNLOCKED 0       /* the sequence is required */
#endif

#if CANOGRAF_WRITE_UNLOCKED && !CANOGRAF_ALLOW_WRITE
#error "CANOGRAF_WRITE_UNLOCKED=1 nedava smysl bez CANOGRAF_ALLOW_WRITE=1"
#endif
```

`CANOGRAF_ALLOW_WRITE=1` does **not** mean "writing is permitted". It means "the
write code is in the image and it is behind the gate". Permission is given by a
person, at run time, with the sequence. The contradictory flag combination is a
build error rather than a silent promotion: quietly repairing a nonsensical
configuration is how you get a write-capable image nobody asked for.

The flags come from `config` through `build_config.py` (owned by the build
configuration work) and arrive as `-D`. The `#ifndef` pair above is the backstop
for the case where that script does not run.

### Not compiled in means not compiled in

In a read-only build the write code is absent from the image, not disabled in it:

- **File level.** Everything that composes a write - payload builders for `2E`,
  `3B` and `31 01`, the oil reset screen, the battery configuration screen, the
  regeneration trigger, the remote write path - lives in `src/write/`, and
  `build_src_filter` drops that directory when `CANOGRAF_ALLOW_WRITE=0`. Absence
  is provable on the artifact: `nm firmware.elf | grep -c dpf_regen` prints `0`.
- **Call site level.** The few places in `src/ui.c` that offer a write sit in
  `#if CANOGRAF_ALLOW_WRITE` blocks. In a read-only build the regeneration
  button is still drawn, greyed, and says so when tapped
  (`STR_SAFETY_NO_WRITE_BUILD`). Showing a capability the image does not have is
  better than silently omitting it - the user learns what the device could do
  and which build would do it, rather than concluding the feature is missing.

`safety_send_guarded()` is always compiled, because reads need it, and it refuses
`SAFETY_CLASS_WRITE` with `SAFETY_DENY_BUILD` whatever the flags say. That check
is not the protection - the missing object file is. It is the belt to those
braces and costs one comparison.

### Compiling the protection off

This is the escape hatch, and it is the point of the flag rather than an
oversight:

```sh
# writing compiled in, still behind the sequence
PLATFORMIO_BUILD_FLAGS=-DCANOGRAF_ALLOW_WRITE=1 .pio-venv/bin/pio run

# writing compiled in, gate compiled open - no dialog, no sequence, ever
PLATFORMIO_BUILD_FLAGS="-DCANOGRAF_ALLOW_WRITE=1 -DCANOGRAF_WRITE_UNLOCKED=1" \
  .pio-venv/bin/pio run
```

Somebody who writes to control units all day should not have to repeat a
five-step sequence forty times an afternoon. A protection its own author routes
around is already dead, so the way around it is official, documented and one
command long.

What it costs, and the price is deliberate:

- The top strip carries a permanent `STR_SAFETY_DEV_BADGE` in vermillion. Not a
  toast, not a log line - a badge that never goes away, so a development image
  cannot be mistaken for a normal one after it has been left in a vehicle.
- CI refuses to publish an artifact built with it: the release job greps the
  compiler command line and fails on `CANOGRAF_WRITE_UNLOCKED`.
- The flag never appears in a committed configuration file. It is passed on the
  command line, so it cannot be committed by accident.

### What the public build gets

**Recommendation: `CANOGRAF_ALLOW_WRITE=0`, `CANOGRAF_WRITE_UNLOCKED=0` for the
default build.** The reasoning, including the side that argues the other way:

- The released artifact reaches people who did not read this document. For them
  the device should be a recorder: it reads, it logs, it draws, and nothing it
  does can change the vehicle. That is a property of the image, not of a setting
  they might find.
- A capability absent from the image cannot be reached by a bug. The remote
  gateway is the most interesting part of the roadmap and the most likely to
  contain a parsing mistake; an overflow in an ELM327 command parser cannot turn
  into `3B 16` if `3B 16` was never linked.
- Against: it splits the artifact, and an unbuilt write path rots unnoticed. The
  mitigation is explicit - **CI builds both variants, and `sim/` builds with
  `CANOGRAF_ALLOW_WRITE=1`** so the dialog that guards everything is in the
  rendered screenshots instead of being the one screen nobody looks at.
- Against: someone needing a single oil reset has to rebuild. Acceptable - they
  are already in the vehicle with a laptop, which is how the device got there.

**Honest limitation.** OTA is in the firmware, so a read-only device can be
updated to a write-capable one over WiFi. Gate 1 is a build-time decision, not a
hardware fuse. That makes the OTA password part of the safety story.

---

## Gate 2 - the switch in settings

A row in Settings, `STR_SET_WRITE`, "ENABLE WRITE FUNCTIONS", default off.
Tapping it opens the dialog of gate 3; the switch follows the result and never
grants anything by itself.

**There is no time limit and nothing is stored.** The unlocked state lives in
RAM. A restart, a power cut or an ignition-off sleep leaves it locked again. It
is a captcha, not a session cookie: anyone who downloads this project, builds it
and switches it on goes through the sequence, every time the device starts.

Nothing about the gate is written to NVS - not a flag, not a timestamp, not "the
user agreed once". A persisted permission is a permanently armed device, which
is the state this whole document exists to prevent; and a flash cell half-written
during a brown-out must never be readable as "unlocked".

What *is* worth persisting is the opposite: an **audit line per write** to the TF
card (`safety_host_t.audit`), timestamped from the on-board RTC, with module,
service, identifier and the bytes before and after. CLAUDE.md already demands a
backup of every block touched; that is where it goes, and the write path refuses
with `SAFETY_DENY_PRECOND` when the original block was not read and saved first.

### One sequence per session, not one per write

This was the open question, and it is the only thing about gate 2 left to decide.

**Decision: an unlock lasts until the device restarts.**

- **Per-write would be tapped through.** A ceremony repeated eight times in ten
  minutes becomes motor memory; by the fifth time nobody reads anything. The
  value of the captcha is the single deliberate "I am now in writing mode"
  decision. Repeating it turns a decision into a toll, and tolls get paid
  automatically.
- **Real jobs are several writes.** The oil reset is `2E 2110` then `2E 2112 01`.
  The battery configuration is `3B 16` and `3B 46`, each preceded by a read and
  followed by a verifying read. Per-write that is four or more sequences for one
  logical job - a minute of tapping that teaches the user to resent the
  protection.
- **The per-write risk is already covered, and covered better.** Every individual
  write keeps its own plain confirmation showing the module, the identifier and
  the bytes about to be sent. That dialog has *content*; the sequence does not.
  Against a wrong value, a confirmation that shows the value is worth more than a
  colour game, and the colour game would crowd it out.
- **A restart is the natural scope.** A visit to a vehicle is one power-up. With
  ignition sensing on the roadmap the device will sleep with the vehicle, so
  "until restart" becomes "until you get out of the van".

The known weakness, stated plainly: a device left permanently powered stays
unlocked for as long as it stays powered. Three things bound it - the Settings
switch locks it instantly with one tap, a `STOP` control appears whenever a
remote client is attached, and every individual write still confirms. If
operating experience shows the gate drifting open for days at a time, the fix is
ignition-based sleep, not a timer that expires mid-job and makes people rush the
second attempt.

---

## Gate 3 - the sequence

Two screens. First the warning, which is not a one-liner. Then the sequence: the
device plays five steps in a strip and the user repeats them on four large
tokens. Simon says, with the showing and the answering in separate places so
there is never a doubt about whose turn it is.

### Screen one - the warning

Because an unlock lasts the whole session, this screen is shown roughly once per
visit to the vehicle. That is what buys it the right to be long: it is not a
dialog anyone will see forty times in an afternoon, so it can afford to explain
instead of nag.

`STR_SAFETY_DISCLAIMER`, English original:

> This device can change data inside the vehicle's control units.
>
> Reading is safe and changes nothing in the vehicle.
>
> Writing is not. A wrong value can leave the vehicle unable to start or heat
> the exhaust above 600 C. Some blocks cannot be put back, and a module does not
> always report that it refused.
>
> Read the block first, save it, and know what each byte means. Engine off,
> ignition on, never while driving.
>
> Repeat the sequence on the next screen to enable write functions until the
> device restarts.

Five short paragraphs: what this is, what is safe, what is not, what to do, what
happens next. No exclamation mark, no skull, no "are you sure?" - and equally no
softening. "Can leave the vehicle unable to start" is simply true: it is what
happens when the energy module gets the wrong battery class, or the service
strategies are re-initialised against a wrong odometer.

What it deliberately avoids is "only proceed if you know what you are doing" -
the sentence everybody agrees with and nobody is slowed by.

**It has to fit without scrolling.** The column is 740 px wide and runs from
y 84 down to the CONTINUE button at y 358 - 274 px, ten lines of Montserrat 20
at 26 px plus the gaps, about 70 characters each. That is the budget, and the
text above spends about 500 characters of it. The five paragraphs are five
labels drawn 10 px apart rather than one label with `\n\n` between them: a
blank line of font 20 costs 26 px, and four of them would eat 104 px of the
column. `show_info()` is not usable here either - its body starts at y 100
with a button at y 330, which is six lines.

Translations run longer than the English original - the German strings in
`lang/de.h` are already about 20 % longer - so the column has to be measured
against the longest translation rather than the original, and any buffer used
for formatted text is sized in **UTF-8 bytes**, not characters.

```
+============================================================+ 6 px accent
| ENABLE WRITING                                     [ BACK ]|
|                                                            |
|  This device can change data inside the vehicle's control  |  x 30, y 84
|  units.                                                    |  w 740, font 20
|                                                            |  one label per
|  Reading is safe and changes nothing in the vehicle.       |  paragraph,
|  ...                                                       |  10 px apart
|                            +-----------------------------+ |  340 x 104
|                            |         CONTINUE            | |  x 430, y 358
+============================================================+
```

### Screen two - the sequence

```
+============================================================+
| ENABLE WRITING                                     [ BACK ]|
|                                                            |
| A wrong value can disable       +---------+ +---------+    |
| the vehicle. Repeat the         |   (1)   | |  [ 2 ]  |    |  180 x 180
| sequence to continue.           |  solid  | |  solid  |    |  = 21 mm
|                                 +---------+ +---------+    |
| [#][#][ ][ ][ ]                                            |
|                                 +---------+ +---------+    |
| WATCH                           |   (3)   | |  [ 4 ]  |    |
| +----------------------------+  | outline | | outline |    |
| |       SHOW IT AGAIN        |  +---------+ +---------+    |
| +----------------------------+                             |
+============================================================+
```

| Element | Position | Size |
|---|---|---|
| token grid, 2 x 2 | x 400 / 600, y 90 / 290 | 180 x 180, 20 px gaps |
| sequence strip, 5 slots | x 30, y 264 | 60 x 60, 10 px gaps |
| short warning line | x 30, y 90, width 340 | font 20, wrapped |
| state caption | x 30, y 340 | font 20 |
| SHOW IT AGAIN | x 30, y 366 | 340 x 100 |

The tokens are 180 px - 21 mm, half as much again as the 120 px floor in
[`docs/ui.md`](ui.md), because this is the one dialog where a mis-hit has a cost.

`SHOW IT AGAIN` is 100 px tall, which is 11.7 mm and **below** the floor. That is
the only deliberate exception: it is 340 px wide, it is the only control in that
column, and the worst outcome of hitting it by accident is that the sequence
plays again - which is the thing the user wanted. A control whose failure mode is
"you got what you asked for" does not need 14 mm.

### Length: five steps, not four

Four tokens over four steps is 256 combinations, over five it is 1024. The
combination count is not really the point - nobody brute-forces a dashboard - but
the length is:

- Four steps fit inside everyone's immediate span and can be repeated without
  encoding them, which is exactly the "I clicked through it" failure we are
  trying to prevent. Five forces a deliberate pass.
- Five steps at 1 s each is a 5 s playback plus 3-4 s of input: about ten seconds
  of attention, once per session. That is the right price for switching a vehicle
  tool into writing mode.
- On brute force anyway: three attempts then a 60 s cooldown is about 1.5
  attempts a minute, so random tapping needs on the order of **eleven hours** on
  average. A child in the passenger seat or a bag resting on the panel will not
  get there.

### Timing

| Phase | Time | Why |
|---|---|---|
| lead-in after WATCH appears | 500 ms | the eye has to arrive before the first token lights |
| token lit | 700 ms | long enough to read a shape and a digit, short enough not to drift |
| gap between tokens | 300 ms | **not optional**, see below |
| tap feedback | 150 ms full-background swap | the feedback rule from `docs/ui.md` |
| longest pause between taps | 10 s | a half-entered sequence does not sit waiting |
| dialog idle timeout | 60 s | it closes itself |

The 300 ms dark gap is load-bearing. Repeated tokens are allowed, so when steps 3
and 4 are both the filled circle, the gap is the only thing that says "two" and
not "one, held". Forbidding repeats instead would shrink the space from 1024 to
324 and make the sequence easier - the wrong trade.

Taps during playback are **ignored**, not counted as errors, and the tokens are
drawn dimmed while the strip plays. Eagerness is not inattention.

### Getting it wrong

**A wrong tap ends the attempt at once, the strip flashes vermillion, and a
completely new sequence is generated. Three attempts, then the dialog closes and
refuses to reopen for 60 seconds.**

The alternatives and why they lost:

- *Close the dialog on the first mistake.* Punishes a fat finger in a moving van
  with a round trip through Settings, and teaches people to rush rather than to
  attend. A mistake should cost ten seconds, not a navigation task.
- *Retry with the same sequence.* This one is actually unsafe. Immediate feedback
  on the first wrong tap plus a fixed sequence is Mastermind: you learn one
  position per attempt, so the whole thing falls in at most 20 taps instead of
  1024 possibilities. **Regenerating on every attempt is a correctness
  requirement, not a preference.**
- *Unlimited attempts.* Turns an inattention barrier into a toy that eventually
  opens.

Replays do not count as attempts and are unlimited. The barrier is
deliberateness, not memory; failing a legitimate repair because somebody has a
poor working memory is a bad outcome with no safety gain.

### The four tokens, and colour blindness

Shape and digit carry the identity. Colour is redundancy.

| # | Shape | Colour | Hex | L* | contrast vs `ink` |
|---|---|---|---|---|---|
| 1 | filled circle | yellow | `#F0E442` | 89.1 | 14.4 |
| 2 | filled square | sky blue | `#56B4E9` | 69.8 | 8.3 |
| 3 | outline circle, 14 px ring | bluish green | `#009E73` | 57.7 | 5.6 |
| 4 | outline square, 14 px border | orange | `#E69F00` | 70.6 | 8.5 |

Each token also carries its digit, centred, in Montserrat 48.

This is not a guess. The hues are the Okabe-Ito colour-blind safe set, and the
pairs were checked by running the actual hex values through dichromat
simulations:

```
deuteranopia   1 yellow vs 4 orange   dL* 10.2   contrast 1.32
protanopia     1 yellow vs 4 orange   dL* 12.9   contrast 1.43
tritanopia     3 green  vs 4 orange   dL*  2.4   contrast 1.09
```

**No set of four colours survives all three dichromacies.** Yellow and orange
collapse together for the red-green cases, green and orange for the blue case.
Prettier hex values do not fix it. The conclusion is that colour cannot be a
channel we rely on, which is why the shape system is systematic - filled/outline
crossed with circle/square, four combinations with no accidental similarity - and
why every token carries a digit.

Two consequences worth stating:

- **Shapes use LVGL style properties only** - radius, border width, background
  opacity. No canvas, no images, no triangle needing a bitmap. The whole set is
  four style variants on one object type.
- **No token is red.** Vermillion `#C8432B` is reserved for the error flash, so
  "something red happened" is never ambiguous. For the same reason the tokens
  avoid `demand` blue and `actual` bone: in `docs/ui.md` that pair always means
  setpoint and measurement and is never decorative. The unlock dialog shows no
  vehicle data, so nothing could actually be misread - but the palette rule is a
  rule, and borrowing those two for a game would be the first crack in it.

### Where the randomness comes from

`safety_host_t.random32()`, which on the board is `esp_random()`. Two bits per
step from a fresh word; four divides 2^32, so there is no modulo bias and no
rejection loop.

One caveat that matters: `esp_random()` is a true hardware generator only while
the RF subsystem is running. With WiFi off it degrades to a PRNG seeded at boot,
and this device will often sit in a vehicle with WiFi disabled - so in principle
the sequence could be predictable from boot time. The implementation therefore
mixes in `uptime_ms()` at the moment the dialog was opened. Human touch timing at
millisecond resolution is not much entropy, but it is unpredictable and free.

Neither `ui.c` nor `safety.c` may call `esp_random()` directly - they cannot,
since neither may include ESP-IDF headers. In `sim/` the hook is NULL, and **a
NULL `random32` means the dialog refuses to open.** A fixed sequence for the
screenshot build would be worse than no dialog: a working unlock with a known
answer is precisely what must never ship by accident.

---

## What is behind the gate

Taken from [`docs/vehicles/x82-trafic3-talento.md`](vehicles/x82-trafic3-talento.md),
which is the only description of what this device can currently emit.

Classification is by service and target address, on the **assembled request**,
before the first ISO-TP frame goes out - never per CAN frame. The token minted
for a multi-frame request covers the whole payload, so a write cannot be smuggled
into a consecutive frame behind a read-looking first frame.

### Read - never gated, in any build, in any state

| Request | What it is |
|---|---|
| `22 xxxx` on any module | every live value, the DPF block, oil data `2110`/`2111`/`2299`/`211B`, turbo end stops `2903`-`290F`, air mass adaptation `2508`/`2509`, every counter |
| `19 02 FF`, `19 02 01`, `19 04`, `19 06`, `19 0A` | fault memory, freeze frames, extended records |
| `21 xx` legacy reads | `21 16` alternator management, `21 46` cranking current, `21 6B` stop-start |
| `31 03 02 0D`, `31 03 02 09` | routine **status** - a query, it starts nothing |
| `3E 00` | tester present, needed to hold a read session open |
| `10 01` / `10 81` | returning to the default session is always allowed |
| passive sniffing | no frame leaves the device at all |

Nothing a user can do in a read-only build should ever produce a refusal, because
every read is in this table.

### Write - gated

| Request | What it does |
|---|---|
| `2E 2110` | odometer at last oil change |
| `2E 2111` | oil change type |
| `2E 2112 01` | re-initialise strategies - zeroes every derived counter |
| `2E xxxx` any other | including `2E 2299`, which the ECU accepts with `6E 2299` and then silently recomputes. Accepted-and-ignored still goes through the gate: a write that appears to work and does not is worse than a refusal |
| `3B 16` | battery type, class and capacity in the energy module, first 36 bytes |
| `3B 46` | cold cranking current block, first 53 bytes |
| `3B xx` any other | body module configuration, stop-start inhibit |
| `31 01 02 0D` | forced regeneration - exhaust past 600 C, stationary, outdoors |
| `31 02 02 0D` | stop regeneration - see the exception below |
| `31 01 02 09`, `31 02 02 09` | rail pressure test |
| `31 00 ...` | generic routine control: unknown effect by definition |
| `2F xxxx` | input/output control - the roadmap's lamp, window and door-lock tests |
| `11 01/02/03` | ECU reset |
| `14 ...` | clear fault memory - destroys the extended records, see CLAUDE.md |
| `27 ...` | security access |
| `85 ...` | control DTC setting |
| any unknown service | the list is an allowlist; an unrecognised service is a write |

### Forbidden - refused even when unlocked

| Request | Why there is no case for it |
|---|---|
| `10 02` programming session | this device does not program modules |
| `34` / `36` / `37` | request download, transfer data, exit transfer. If these bytes ever appear it is a bug or an attack, never a feature |
| `28 ...` communication control | can silence a module's bus traffic on a moving vehicle |
| any write to `752` | airbag module: pyrotechnic actuators, no diagnostic benefit to this project, and a legal exposure nobody wants. Reads stay available |
| any write to `7C7` | the gateway never answered on this vehicle, and writing blind into a gateway is how you lose the bus you are standing on |

Unlocking does not relax these. `safety_classify()` returns
`SAFETY_CLASS_FORBIDDEN` and `safety_authorize()` answers `SAFETY_DENY_FORBIDDEN`
in every build and every state.

### The borderline cases

**`10 03` extended session and `10 C0` legacy session - not gated.** Entering a
session changes nothing by itself, and gating it would gate reading: several
identifiers only answer in the extended session, and the whole body / energy /
airbag half of the vehicle needs `10 C0` before `21 xx` answers at all. The
protection sits elsewhere:

- the gate never treats the session level as evidence of permission - being in
  `10 03` grants nothing;
- when the gate locks, the client drops back to `10 01` and stops tester-present
  on legacy modules, so no armed session is left behind;
- the current session is shown on screen, so "why is this module in extended
  session" is answerable.

**`27` security access - gated**, although it writes nothing. There is no reason
to send it while the gate is shut, so allowing it would widen the surface for
free.

**`31 02 02 0D`, stopping a regeneration - gated, with one exception.** If this
device started the regeneration in the current session, the stop control stays
live for as long as that regeneration runs. Aborting what you started is a safety
function, and it must not depend on the gate still being open. It keeps its own
single confirmation, because interrupting a regeneration is not free either - it
leaves a partly burnt filter and fuel in the oil.

**`3E 00` tester present - read**, but only sent when a session actually needs
holding open.

### Preconditions, which are not permission

Even with the gate open, a write is refused with `SAFETY_DENY_PRECOND` when:

| Condition | Source |
|---|---|
| engine running (`2002` > 0) | the ECU answers `7F 2E 22` anyway; we say it in words first |
| vehicle moving (`2003` > 0) | - |
| battery below 12.0 V (`2005`) | ignition on with the engine off sits near 12.4 V; below 12.0 a multi-frame block write risks landing half-written |
| the original block was not read and saved this session | CLAUDE.md: "Save a backup of every block you touch." The gate enforces it instead of trusting a habit |

---

## Remote interfaces

The roadmap has a web server and an ELM327 emulation on a TCP port, so that the
device can stay in the vehicle while the person is at a desk. **There is nobody
in front of a socket**, so the rule is short:

> A remote client is subject to exactly the same gate as the panel, and it can
> never open it. While the device is locked, a write arriving over HTTP or from
> the ELM327 emulation is refused in the transport, before the frame is built.
> The only way to unlock is five taps on the touchscreen of the device itself.

Consequences that follow from that and are worth writing down:

- **Remote writing is possible, but only while somebody in the vehicle has
  unlocked the panel.** That is the condition, and it is the whole condition. A
  laptop at a desk cannot arrange it; a person in the van can.
- **The panel shows what the remote end is doing.** While a client is attached
  (`safety_channel_attach()`), the panel lists it and logs every write as it
  happens - module, service, identifier, length - with a full-width `STOP`
  control that locks the gate in one tap. Someone sitting in the van must be able
  to end a remote session without understanding anything about it.
- **Refusals are protocol-correct, not silent.** A denied remote write gets a
  synthesised `7F <sid> 33` (securityAccessDenied) as if it came from the module.
  Dropping it silently is worse: DDT4All and pyren retry, then time out, and the
  user concludes the module is dead or the wiring is bad, and hours get spent
  chasing our own refusal. `33` rather than `22 conditionsNotCorrect` because the
  vehicle is fine - it is this device saying no. Reads are unaffected.
- **The ELM327 emulation gets no raw path.** `AT` commands that only configure
  the emulated dongle (`AT H1`, `AT CAF0`, `AT ST`, protocol selection) never
  reach the bus and are free. When the client builds its own ISO-TP frames, the
  classifier runs on the reassembled payload, and a frame whose ISO-TP context is
  unknown is classified as a write. Being unparseable is not a free pass.
- **Reads are not rate limited.** A tool polling thirty identifiers a second is
  normal and must not trip anything.

Authentication is a separate matter from authorisation, and the roadmap already
states the rule: not an open port. The gate assumes it. The port stays closed
unless switched on in Settings, binds in AP mode by default rather than onto
whatever hotspot the van is near, and requires a key or password before any frame
- read or write - is accepted. Read access alone leaks the odometer and the VIN.

---

## Telling the user why

A refused write never fails silently and never fails vaguely. Every refusal says
what is in the way and where the switch is:

| Verdict | What the user is told |
|---|---|
| `SAFETY_DENY_LOCKED` | `STR_SAFETY_LOCKED_HINT` - "Write functions are switched off. Enable them in Settings; you will be asked to repeat a short sequence first. If you build the firmware yourself, this check can also be disabled at compile time." |
| `SAFETY_DENY_BUILD` | `STR_SAFETY_NO_WRITE_BUILD` - "This build can only read. Write functions are not compiled in - see docs/safety.md." |
| `SAFETY_DENY_FORBIDDEN` | this device does not send that request, in any mode |
| `SAFETY_DENY_PRECOND` | the specific condition: engine running, vehicle moving, voltage too low, no saved backup |

The locked message names the compile-time escape on purpose. Someone who is
competent enough to be annoyed by the gate is competent enough to be told how to
remove it, and a protection that pretends to be a limitation breeds exactly the
kind of workaround nobody can audit.

---

## Strings

All new text goes through `T(STR_...)`. English originals below; they are added
to `src/i18n.h` and `lang/*.h` **with the implementation, not before** -
`src/i18n.c` has a `_Static_assert` on the count, so adding keys early would only
make three language files go stale.

| Key | English |
|---|---|
| `STR_SET_WRITE` | `ENABLE WRITE FUNCTIONS` |
| `STR_SAFETY_TITLE` | `ENABLE WRITING` |
| `STR_SAFETY_DISCLAIMER` | the five paragraphs above |
| `STR_SAFETY_CONTINUE` | `CONTINUE` |
| `STR_SAFETY_WARN` | `A wrong value can disable the vehicle. Repeat the sequence to continue.` |
| `STR_SAFETY_WATCH` | `WATCH` |
| `STR_SAFETY_REPEAT` | `NOW REPEAT IT` |
| `STR_SAFETY_REPLAY` | `SHOW IT AGAIN` |
| `STR_SAFETY_ATTEMPT` | `attempt %d of %d` (two `%d`, order fixed) |
| `STR_SAFETY_WRONG` | `Wrong order. New sequence.` |
| `STR_SAFETY_BLOCKED` | `Too many attempts. Try again in %u s.` (one `%u`) |
| `STR_SAFETY_GRANTED` | `Write functions enabled until the device restarts.` |
| `STR_SAFETY_BADGE` | `WRITE` (top strip, while unlocked) |
| `STR_SAFETY_DEV_BADGE` | `WRITE UNLOCKED` (development build only) |
| `STR_SAFETY_LOCKED_HINT` | `Write functions are switched off. Enable them in Settings; you will be asked to repeat a short sequence first. If you build the firmware yourself, this check can also be disabled at compile time.` |
| `STR_SAFETY_NO_WRITE_BUILD` | `This build can only read. Write functions are not compiled in - see docs/safety.md.` |
| `STR_SAFETY_REMOTE_STOP` | `STOP REMOTE ACCESS` |

Two conventions from `lang/README.md` that these strings have to follow: every
key carrying a `%` marker needs a comment at its entry in `src/i18n.h` saying
what the arguments are and in what order - the translated string goes into
`snprintf`, so a wrong count fails at run time, not at build time - and long
texts are written as adjacent literals of about 75 characters per line, with
paragraphs split on `\n\n` where a single label is used. All three languages
have to be added in the same commit, or `_Static_assert` in `src/i18n.c` fails
the build, which is the intended behaviour.

---

## The interface

[`src/safety.h`](../src/safety.h) in full; the shape of it here.

```c
bool safety_write_allowed(void);          /* same answer for every channel */
void safety_request_unlock(void);         /* warning page, then the sequence */
void safety_unlock_grant(void);           /* only the dialog calls this      */
void safety_lock(void);                   /* idempotent                      */

safety_class_t   safety_classify(uint32_t tx_id, const uint8_t *req, uint16_t len);
safety_verdict_t safety_authorize(safety_channel_t, uint32_t, const uint8_t *,
                                  uint16_t, safety_token_t *out);
safety_verdict_t safety_send_guarded(safety_channel_t, uint32_t,
                                     const uint8_t *, uint16_t);
#define SAFETY_SEND(ch, id, buf, len) safety_send_guarded(...)
```

### Why a token and not a boolean

`safety_send_guarded()` is the public door. Inside it, `safety_authorize()` mints
a `safety_token_t` that is

- **single use** - a serial invalidated by `safety_token_consume()`;
- **bound to the frame** - a fingerprint over channel, address, length and data,
  so a token minted for one request cannot carry another;
- **short lived** - 200 ms, so it cannot be kept for later.

The transport keeps its raw transmit `static` and exports only a function taking
`const safety_token_t *`. That is what makes the protection structural rather
than procedural: there is no second path to the bus to forget about when the web
server gets written, and a patch that wants one has to visibly defeat the token.

A boolean would have been simpler, and would have been checked in the wrong place
by the third caller.

### Why the sequence logic is not in `ui.c`

`safety_seq_begin()`, `safety_seq_input()` and the attempt counting live in
`safety.c` because they are rules, not pixels. Generation, comparison, the
regenerate-on-failure requirement and the lockout are all testable on the host
with no LVGL. `ui.c` draws four styled objects and reports taps. If the two ever
disagree, the rule wins.

### Board dependencies

`safety.c` may not include Arduino or ESP-IDF headers, exactly like `ui.c`.
Everything board-specific arrives through `safety_host_t`: `random32`,
`uptime_ms`, `engine_running`, `vehicle_moving`, `batt_mv`, `open_unlock_ui`,
`audit`. The simulator leaves them NULL and the implementation must survive that,
always degrading towards shut - no `random32` means the dialog does not open.

---

## How to prove it works

Not a test suite - the checks that would actually catch a regression.

| Claim | Check |
|---|---|
| writing is not in the public image | `nm firmware.elf \| grep -c 'dpf_regen\|oil_reset\|emm_batt'` is `0` by default and non-zero with `CANOGRAF_ALLOW_WRITE=1` |
| a locked gate stops a frame, not a button | call `safety_send_guarded(SAFETY_CH_PANEL, 0x7E0, "\x2E\x21\x10...", n)` from a host test, assert `SAFETY_DENY_LOCKED` and that the transport saw nothing |
| remote cannot open the gate | unlock nothing, drive `safety_send_guarded(SAFETY_CH_ELM, ...)` with a `2E` payload, assert `SAFETY_DENY_LOCKED` |
| reads never trip the gate | run every request in the read table through `safety_classify()`, assert `SAFETY_CLASS_READ` |
| forbidden stays forbidden when unlocked | unlock, then assert `SAFETY_DENY_FORBIDDEN` for `34`, `10 02`, `28`, and any `3B` to `752` |
| a failed attempt changes the sequence | two `safety_seq_begin()` calls around a wrong input differ over many runs |
| a token is single use | consume twice, the second must fail |
| a token cannot carry another frame | mint for request A, consume with request B, must fail |
| a restart locks again | the state is in RAM; assert no NVS key is ever written by `safety.c` |

All of these run on the host, which is the point of keeping `safety.c` free of
board headers.

---

## Still open

- **Ignition sensing** (`DI0`, on the roadmap) is a better auto-lock trigger than
  anything time based: when the vehicle sleeps, the device sleeps, and the gate
  closes with it. That is the intended answer to "a permanently powered device
  stays unlocked".
- **A per-client confirmation for remote writes** - a one-time panel prompt
  naming the connecting client - was considered and left out. The unlock already
  requires a person in the vehicle, and the attached-client list plus `STOP` give
  them the same control with less ceremony. Worth revisiting if the gateway ever
  accepts more than one client at a time.
- **Audit log format.** Fields are decided above, the encoding is not. CSV beside
  the session log is the obvious answer unless something better appears.
- **A second vehicle profile.** The classification tables name X82 addresses
  directly. When another platform arrives, the forbidden-module list moves into
  the vehicle profile; the three classes stay generic.

---

## Recommendation, in one place

**Public build: `CANOGRAF_ALLOW_WRITE=0`, `CANOGRAF_WRITE_UNLOCKED=0`.**

- The published artifact **reads and cannot write**. The person who flashes it
  did not read this document, and a capability absent from the image cannot be
  reached by a wrong tap, a parser bug or a remote client.
- A second artifact with `CANOGRAF_ALLOW_WRITE=1` is built by the same CI run and
  published beside it, with the difference stated in one line on the release
  page. In that build writing exists but is **off until somebody unlocks it**.
- The unlock is a **captcha, not a setting**: the switch defaults to off, the
  state lives in RAM, nothing is written to NVS, and a restart locks it again.
  **No timer** - it lasts until the device restarts, because a per-write ceremony
  would be tapped through and a timer expires mid-job.
- It costs reading one honest screen and repeating a **five step sequence**,
  regenerated on every attempt, three attempts, then a minute of cooldown. Tokens
  are told apart by **shape and digit**; colour is redundancy, because the
  measurements above show that no four colours survive all three dichromacies.
- **The same gate applies to the web server and the ELM327 emulation.** They are
  refused in the transport while locked and they can never unlock themselves -
  only a person at the panel can.
- Anyone who finds this in the way can build with
  `-DCANOGRAF_ALLOW_WRITE=1 -DCANOGRAF_WRITE_UNLOCKED=1` and have no gate at all.
  That is documented in the README, it is the honest escape hatch, and the price
  is a permanent badge on screen saying the protection is off.
