# Translations

Every string the interface shows lives here. One file per language, nothing
else to touch. **English (`en.h`) is the source language** - translate from it,
not from Czech.

Current languages: `en` (default), `cs`, `de`.

## Add a language

1. Copy `en.h` to `<code>.h`, where `<code>` is the ISO 639-1 code
   (`fr.h`, `pl.h`, ...).
2. Rename the include guard, the `LANG_EN_NAME` macro and the `LANG_EN[]`
   array to match, then translate the strings. **Keep the order** - it is the
   order of the enum in `src/i18n.h` and nothing else defines it.
3. `LANG_<CODE>_NAME` is the name of the language written in that language
   (`Čeština`, `Deutsch`). It is what the language button shows.
4. In `src/i18n.c`: add the `#include`, one `LANG_CHECK(LANG_<CODE>);` line
   and one row in `LANGS[]`. Append it at the end - the position is the number
   stored in NVS, so inserting in the middle changes everyone's saved setting.
5. Build. If you dropped or added a string the build stops with
   `pocet retezcu neodpovida vyctu` and names the array. A missing translation
   can never slip through silently.
6. Send a pull request. A language file on its own is a perfectly good PR.

## Rules that matter

**Format specifiers.** A few strings carry `%s`, `%u` or `%d`. They must stay,
in the same order and with the same types - they are passed to `snprintf` at
run time and a mismatch crashes the device, not the compiler. `src/i18n.h`
says which strings these are.

**What is deliberately not here.** Unit symbols (`km/h`, `mph`, `mbar`, `C`,
`F`), the diagnostic request bytes, the product name, the `DEMO` badge, `WIFI`,
`DPF`, `VNT` and the heap/PSRAM line are hard-coded in `src/ui.c`. They are the
same in every language, or they are hexadecimal. If one of them really does
differ in yours, say so in the pull request and it gets an enum entry.

**Characters outside ASCII.** The fonts in `src/font_ui_*.c` are generated with
a fixed character set: ASCII plus Czech and German diacritics. A character that
is not in that set does not get drawn and LVGL logs
`glyph dsc. not found for U+XXXX` many times a second.

If your language needs characters that are not covered yet, add them to
`SYMBOLS` in `tools/gen_fonts.sh` and regenerate:

```sh
./tools/gen_fonts.sh
```

Mention in the pull request that the fonts were regenerated - it is a large
diff and it costs flash, so it should not be a surprise.

**Length.** The layout uses absolute positions, so a translation that is much
longer than the English one will run into its neighbour. The places to watch:

| string | fits in |
|---|---|
| page names (`MAIN` ... `SETUP`) | 128 px at font 20, roughly 8 capitals |
| settings captions | 145 px at font 14 |
| settings button labels | 130 px at font 24 |
| `FORCE REGEN` | 340 px at font 24 |
| `FULL BRIGHTNESS` | 380 px at font 24 |
| `CLOSE` | 120 px at font 20 |

Render the screens and look before sending the PR:

```sh
cmake -S sim -B sim/build
cmake --build sim/build
./sim/build/lv_shot /tmp/shots <code>
```

The last argument is the language code; without it you get English.

**Encoding.** UTF-8, no BOM, Unix line endings.
