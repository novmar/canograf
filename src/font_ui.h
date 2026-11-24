/*
 * font_ui.h - fonty UI s ceskou a nemeckou diakritikou
 *
 * Vestaveny lv_font_montserrat_* ma jen ASCII. Jediny znak s hackem nebo
 * carkou v popisku rozjede do logu "glyph dsc. not found for U+XXXX"
 * desitky krat za sekundu a znak se nevykresli. Proto UI nesmi sahat na
 * vestavene fonty - vsude se pouzivaji jen aliasy UI_FONT_XX odsud.
 *
 * Zdroj je stejny Montserrat Medium (SIL OFL 1.1), takze se vzhled nemeni,
 * jen pribyly znaky. Generuje tools/gen_fonts.sh.
 *
 * Novy jazyk s jinymi znaky = pridat je do SYMBOLS v tools/gen_fonts.sh
 * a fonty prelozit znovu.
 */
#ifndef FONT_UI_H
#define FONT_UI_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(font_ui_14)
LV_FONT_DECLARE(font_ui_16)
LV_FONT_DECLARE(font_ui_20)
LV_FONT_DECLARE(font_ui_24)
LV_FONT_DECLARE(font_ui_28)
LV_FONT_DECLARE(font_ui_32)
LV_FONT_DECLARE(font_ui_36)
LV_FONT_DECLARE(font_ui_40)
LV_FONT_DECLARE(font_ui_48)

#define UI_FONT_14 (&font_ui_14)
#define UI_FONT_16 (&font_ui_16)
#define UI_FONT_20 (&font_ui_20)
#define UI_FONT_24 (&font_ui_24)
#define UI_FONT_28 (&font_ui_28)
#define UI_FONT_32 (&font_ui_32)
#define UI_FONT_36 (&font_ui_36)
#define UI_FONT_40 (&font_ui_40)
#define UI_FONT_48 (&font_ui_48)

#ifdef __cplusplus
}
#endif

#endif /* FONT_UI_H */
