#!/usr/bin/env bash
#
# Vygeneruje LVGL fonty pro UI. Vysledek jsou soubory
# src/font_ui_<velikost>.c, ktere se verzuji - tenhle skript je potreba
# jen kdyz se meni rozsah znaku nebo velikosti.
#
# Zdroje jsou oba z LVGL (scripts/built_in_font), takze vysledek vypada
# presne jako vestaveny Montserrat, jen navic umi diakritiku:
#   Montserrat Medium                      SIL Open Font License 1.1
#   FontAwesome5-Solid+Brands+Regular      SIL Open Font License 1.1
#
# Pozadavky: node + npx (balicek lv_font_conv se stahne pri prvnim spusteni).
#
#   ./tools/gen_fonts.sh
#
set -euo pipefail

cd "$(dirname "$0")/.."

TTF="${TTF:-.pio/libdeps/waveshare43b/lvgl/scripts/built_in_font/Montserrat-Medium.ttf}"
FA="${FA:-.pio/libdeps/waveshare43b/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff}"
for f in "$TTF" "$FA"; do
    if [ ! -f "$f" ]; then
        echo "Zdrojovy font nenalezen: $f" >&2
        echo "Nastavte TTF=<Montserrat-Medium.ttf> a FA=<FontAwesome...woff>" >&2
        exit 1
    fi
done

# Velikosti, ktere UI pouziva (viz src/font_ui.h)
SIZES="14 16 20 24 28 32 36 40 48"

# Nad ASCII jen to, co je potreba pro cestinu a nemcinu. Kazdy znak navic
# stoji flash, takze novy jazyk = pridat jeho znaky sem a prelozit znovu.
SYMBOLS="áčďéěíňóřšťúůýžÁČĎÉĚÍŇÓŘŠŤÚŮÝŽäöüßÄÖÜ"

# 0xB0 stupen, 0x2022 odrazka - oboji ma i vestaveny Montserrat a LVGL
# odrazku pouziva (LV_SYMBOL_BULLET).
EXTRA="0xB0,0x2022"

# Ikony, ktere si kresli samotne widgety LVGL. Klavesnice bez nich mista
# kláves jen spamuje "glyph dsc. not found". Vestavene fonty jich maji 61,
# tady jsou jen ty, ktere LVGL 8.4 opravdu pouziva:
#   F00C OK   F00D CLOSE  F053 LEFT      F054 RIGHT
#   F078 DOWN F11C KEYBOARD  F55A BACKSPACE  F8A2 NEW_LINE
ICONS="0xF00C,0xF00D,0xF053,0xF054,0xF078,0xF11C,0xF55A,0xF8A2"

HEADER='/*
 * Vygenerovany LVGL font - NEUPRAVOVAT RUCNE.
 *
 * Zdroj:   Montserrat Medium + FontAwesome 5, oboji SIL OFL 1.1
 *          (LVGL scripts/built_in_font/)
 * Rozsah:  ASCII 0x20-0x7F, stupen, odrazka, ceska a nemecka diakritika
 *          a osm ikon, ktere pouzivaji widgety LVGL
 * Nastroj: lv_font_conv 1.5.3, bpp 4, bez komprese
 *
 * Znovu vygenerovat: tools/gen_fonts.sh
 */'

for s in $SIZES; do
    out="src/font_ui_${s}.c"
    npx --yes lv_font_conv@1.5.3 \
        --font "$TTF" \
        -r "0x20-0x7F,$EXTRA" \
        --symbols "$SYMBOLS" \
        --font "$FA" \
        -r "$ICONS" \
        --size "$s" \
        --bpp 4 \
        --format lvgl \
        --no-compress \
        --force-fast-kern-format \
        --lv-font-name "font_ui_${s}" \
        -o "$out"
    printf '%s\n%s' "$HEADER" "$(cat "$out")" > "$out.tmp"
    mv "$out.tmp" "$out"
    echo "  $out"
done
