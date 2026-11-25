# Build configuration

Everything that can be switched without touching code lives here: write
protection, which vehicle, which board, which optional modules.

**Configuration is never done by editing `platformio.ini`.** The versions
pinned there are load bearing - see [CLAUDE.md](../CLAUDE.md) - and a build
option is not a reason to go anywhere near them.

`scripts/build_config.py` runs before every build, merges the layers below,
hands the result to the compiler as `-D` macros and prints a table of what it
decided and where each value came from.

## Layers

Later overrides earlier.

| | file | in the repository | what it is for |
|---|---|---|---|
| 1 | `config/default.env` | yes | defaults, and the list of every known key |
| 2 | `config/board/<board>.env` | yes | one board |
| 3 | `config/vehicle/<vehicle>.env` | yes | one vehicle platform |
| 4 | `local.env` | **no**, gitignored | this machine, this bench, this van |

Which profile is loaded in layers 2 and 3 is itself a key - `CANOGRAF_BOARD`
and `CANOGRAF_VEHICLE` - so the files are read twice: first `default.env` plus
`local.env` to settle the selectors, then the whole chain.

To change something only for yourself:

```sh
cp local.env.example local.env
```

`local.env` never reaches the repository, so a bench setting cannot be pushed
by accident and a colleague's bench setting cannot arrive by pulling.

## File format

```sh
# comment
CANOGRAF_ALLOW_WRITE=off           # trailing comment
CANOGRAF_DEFAULT_LANG="cs"
CANOGRAF_ECU_TX_ID=0x7E0
```

- `true` `false` `on` `off` `yes` `no` become `1` / `0`
- integers, `0x` hex and decimals go into the macro unchanged
- anything in quotes is always a string, even `"1"`, and reaches the compiler
  as a real C string literal - spaces included
- ASCII only, like the rest of the repository

## Two things deliberately refuse to be quiet

**An unknown key stops the build.** Not a warning - a hard error, with a guess
at what you meant. `config/default.env` is the list of valid keys; a key that
is not there is a typo. The alternative is worse than a failed build: a
misspelled `CANOGRAF_ALLOW_WRITE` in `local.env` would silently do nothing, and
you would be holding firmware whose write protection is not in the state you
just read on the screen. Adding a genuinely new key is one line in
`config/default.env`, with a comment and a default.

**A profile that does not exist stops the build.** A missing `local.env` is
normal and is skipped in silence, but `CANOGRAF_VEHICLE=x82-trafick3` is a typo,
and skipping it would quietly build firmware for no vehicle at all. The error
lists the profiles that do exist.

## Keys

### Layer selectors

| key | default | meaning |
|---|---|---|
| `CANOGRAF_BOARD` | `waveshare-esp32s3-touch-lcd-4.3b` | loads `config/board/<value>.env`; empty = no board profile |
| `CANOGRAF_VEHICLE` | `x82-trafic3-talento` | loads `config/vehicle/<value>.env`; empty = no vehicle profile |

### Write protection

Owned by [`src/safety.h`](../src/safety.h); the reasoning is in
[`docs/safety.md`](../docs/safety.md). Default is **protected**.

| key | default | meaning |
|---|---|---|
| `CANOGRAF_ALLOW_WRITE` | `off` | `off`: write code is not in the image at all, the firmware can only read. `on`: write code is in the image **behind the gate** |
| `CANOGRAF_WRITE_UNLOCKED` | `off` | `on`: gate compiled open, no unlock sequence. Development only. Requires `CANOGRAF_ALLOW_WRITE=on`, otherwise `safety.h` raises `#error` |

`CANOGRAF_ALLOW_WRITE=on` does **not** mean "writing is allowed". It means
"writing exists and is guarded". The user still has to unlock it at runtime.

With `CANOGRAF_ALLOW_WRITE=off` the script also appends `-<write/>` to
`build_src_filter`, so "not compiled in" is literally true and can be checked
on the `.elf` with `nm`.

The unlock sequence length, the attempt count and the timeouts are **not**
configurable and will not become configurable. They are `SAFETY_SEQ_*`
constants in `src/safety.h`, so that no configuration file can weaken the
protection.

### Interface

| key | default | meaning |
|---|---|---|
| `CANOGRAF_DEFAULT_LANG` | `"en"` | ISO 639-1 code, string. Currently `"en"`, `"cs"`, `"de"` - see [`lang/README.md`](../lang/README.md). Only the value used on first start; the user's choice is kept in NVS |

It is a code and not an index on purpose: the index is just a row number in
`LANGS[]` in `src/i18n.c` and it shifts the moment somebody inserts a language
in the middle. `i18n_find()` does the lookup; an unknown code falls back to the
first language.

### Board profile

Declared in `config/default.env` with deliberately invalid values so that a
missing board profile is obvious in the build table rather than subtle at
runtime.

| key | default | set by the profile to | meaning |
|---|---|---|---|
| `CANOGRAF_CAN_TX_PIN` | `-1` | `15` | CAN transceiver TX |
| `CANOGRAF_CAN_RX_PIN` | `-1` | `16` | CAN transceiver RX |
| `CANOGRAF_SPARE_PIN` | `-1` | `-1` | a pin left over for anything else; this board's only free pin went to the backlight |
| `CANOGRAF_BL_PWM_PIN` | `-1` | `6` | PWM out for backlight dimming; `-1` = the board cannot dim and the interface does not offer it |
| `CANOGRAF_BL_PWM_HZ` | `5000` | `5000` | dimming PWM frequency, well above the `R11`+`C11` filter |

### Vehicle profile

| key | default | set by the profile to | meaning |
|---|---|---|---|
| `CANOGRAF_VEHICLE_NAME` | `"(zadny profil vozidla)"` | `"Renault X82"` | name shown in the interface |
| `CANOGRAF_CAN_BITRATE` | `500000` | `500000` | diagnostic bus speed in bit/s |
| `CANOGRAF_ECU_TX_ID` | `0x000` | `0x7E0` | engine control unit request id, 11 bit |
| `CANOGRAF_ECU_RX_ID` | `0x000` | `0x7E8` | engine control unit response id, 11 bit |

### Web interface

Live values in a browser on a phone. The code is in [`src/net/`](../src/net);
the reasoning behind every decision it makes is in
[`src/net/server.h`](../src/net/server.h).

| key | default | meaning |
|---|---|---|
| `CANOGRAF_MOD_WEB` | `on` | `off`: `src/net/` is **not compiled into the image at all**, so the board cannot open a port. `on`: the server is in the image but **stopped** - it is switched on in the settings and the choice is kept in NVS |
| `CANOGRAF_WEB_LOG_ROWS` | `7200` | session log capacity, in rows |
| `CANOGRAF_WEB_LOG_MS` | `500` | one row every this many milliseconds |

`CANOGRAF_MOD_WEB=on` does **not** mean "the port is open". The port stays
closed until somebody opens it on the panel, and turning it on asks for a
three second hold, with what it means written on the dialog.

The server **only reads**. It has no way to write to the vehicle: every
handler is `HTTP_GET` and `net_server_start()` refuses to come up if anything
else ever appears in the handler table, and neither `net/server.c` nor
`net/report.c` is allowed to see `can/twai.h` - an `#error` stops the build if
it ever does. The network password never reaches a response either, because
`report_net_t` has no field to put it in.

With `CANOGRAF_MOD_WEB=off` the script appends `-<net/>` to `build_src_filter`,
exactly like write protection does, so "not compiled in" is literally true and
can be checked on the `.elf` with `nm`.

`7200` rows at `500` ms is **one hour** of driving for about 1 MB of the 8 MB
of PSRAM. A longer log costs more PSRAM, which is shared with the panel
buffers; a shorter period costs the same. 500 ms is also how often the page
refreshes, so the board has one cadence and not two that can drift apart.
The log lives in PSRAM and not on the microSD card because this board's card
select hangs off the CH422G expander, so every transaction costs a round trip
over the same I2C bus that carries the touch controller.

### Optional modules

Reserved, off, neither exists yet. They are here so the names are settled
before the code arrives.

| key | default | meaning |
|---|---|---|
| `CANOGRAF_MOD_ELM327` | `off` | ELM327 emulation for existing diagnostic apps |
| `CANOGRAF_MOD_SDLOG` | `off` | logging to the microSD card |

### Idle current measurement

| key | default | meaning |
|---|---|---|
| `CANOGRAF_SLEEP_TEST` | `off` | `on` replaces the normal start with a stepped idle current measurement |

This one does not change how the firmware behaves - it changes what the board
**is**. With it on, `setup()` never reaches the panel, LVGL, CAN or WiFi.
Instead it walks a list of states, holds each one for 20 seconds and announces
over the serial line (115200) what it just turned off, so that the difference
between two readings on a USB meter is the price of that one thing. The last
step is deep sleep; the board wakes on a timer, boots again and the
measurement repeats.

```sh
echo 'CANOGRAF_SLEEP_TEST=on' >> local.env
.pio-venv/bin/pio run -t upload
# measure, then take the line back out and flash the real firmware again
```

Keep it in `local.env`. Turning it on in a file that is in the repository
means somebody ends up with a current meter bolted into their van instead of
a diagnostic tool.

**Read the header of `src/sleep_test.cpp` before writing any number down.**
The measurement has three limits that make the raw figure mean something
other than what it looks like: it measures the 5 V rail fed from USB, not the
current drawn from the vehicle battery (roughly 0.49x); sleep over USB is
shallower than sleep from the terminals, so the floor is an upper bound; and
the CAN transceiver cannot be switched off at all.

It used to be a separate PlatformIO project in `tools/sleep-test`. It was
deleted when this key arrived: it carried its own copy of the CH422G expander
addresses, the EXIO bit map and the GT911 wake register - exactly the values
that change with the next board revision, and exactly the kind of duplicate
where one copy gets fixed and the other quietly lies.

## Adding a board

1. Measure it and write it down in `docs/hardware/<board>.md`.
2. `cp config/board/EXAMPLE.env.template config/board/<board>.env` and fill in
   the values. Lower case, hyphens, no spaces.
3. Put only genuinely board dependent keys in it. Everything else belongs in
   `config/default.env`.
4. Select it: `CANOGRAF_BOARD=<board>` in `local.env`, or in
   `config/default.env` when it becomes the default board.
5. A board with a different panel also needs its own definition in `boards/`
   and a different `board =` in `platformio.ini` - that part is PlatformIO's,
   not this system's.

## Adding a vehicle

1. Write `docs/vehicles/<platform>.md` first. The data identifier map is the
   expensive part; the profile without it is useless.
2. `cp config/vehicle/EXAMPLE.env.template config/vehicle/<platform>.env` and
   fill in the values.
3. Name it after the **platform**, not one brand - `x82-trafic3-talento` covers
   Trafic III, Talento, Vivaro B and NV300.
4. Select it: `CANOGRAF_VEHICLE=<platform>`.

Files ending in `.env.template` are never loaded as profiles.

## Adding a key

One entry in `config/default.env`: the key, a sensible default and a comment
saying what it does. Nothing else - the script picks it up, the table shows it,
profiles and `local.env` may override it. Then document it in the tables above.
