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
| `CANOGRAF_SPARE_PIN` | `-1` | `6` | the only free pin on this board; `-1` when a board has none |

### Vehicle profile

| key | default | set by the profile to | meaning |
|---|---|---|---|
| `CANOGRAF_VEHICLE_NAME` | `"(zadny profil vozidla)"` | `"Renault X82"` | name shown in the interface |
| `CANOGRAF_CAN_BITRATE` | `500000` | `500000` | diagnostic bus speed in bit/s |
| `CANOGRAF_ECU_TX_ID` | `0x000` | `0x7E0` | engine control unit request id, 11 bit |
| `CANOGRAF_ECU_RX_ID` | `0x000` | `0x7E8` | engine control unit response id, 11 bit |

### Optional modules

Reserved, all off, none of them exists yet. They are here so the names are
settled before the code arrives.

| key | default | meaning |
|---|---|---|
| `CANOGRAF_MOD_WEB` | `off` | web interface over WiFi |
| `CANOGRAF_MOD_ELM327` | `off` | ELM327 emulation for existing diagnostic apps |
| `CANOGRAF_MOD_SDLOG` | `off` | logging to the microSD card |

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
