/*
 * ui.c - cele uzivatelske rozhrani Talento HMI (LVGL 8.4, 800x480)
 *
 * Vytazeno z main.cpp, aby slo postavit i mimo desku. Zadny Arduino.h,
 * zadne WiFi.h, zadny ESP32_Display_Panel - jen LVGL a libc.
 * Cas se bere z lv_tick_get(), takze na desce je to porad millis()
 * a v simulatoru virtualni cas.
 *
 * Rozvrzeni vychazi z fyzickych rozmeru: 93,7 x 56,2 mm, 8,54 px/mm.
 * Dotykove cile >= 120 px = 14 mm.
 *
 * Texty: zadny viditelny retezec tu nesmi byt natvrdo - vsechny jdou
 * pres T(STR_...) z i18n.h, preklady jsou v lang/. Fonty maji diakritiku
 * (src/font_ui.h), takze se pouzivaji aliasy UI_FONT_XX a nikdy
 * lv_font_montserrat_XX.
 */
#include "ui.h"

#include "font_ui.h"
#include "i18n.h"

#include <lvgl.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ---------------- paleta ---------------- */
#define C_INK       0x0B1016
#define C_PANEL     0x0E151D
#define C_RULE      0x1B2A38
#define C_DEMAND    0x6FA8D0
#define C_ACTUAL    0xF0DFB8
#define C_DEFICIT   0xC8432B
#define C_SAGE      0x68937A
#define C_LABEL     0x65798B
#define C_BRIGHT    0xDCE7F0
/* Podkres sloupce DPF pri regeneraci - C_DEFICIT primichana do pozadi.
 * Tlumena pri pokusu o spusteni, vyrazna pri blikani bezici regenerace. */
#define C_REGEN_DIM 0x3A1D1B
#define C_REGEN_HOT 0x7C2F23

/* ---------------- rozvrzeni ---------------- */
#define TOP_H   64
#define NAV_H   124             /* 14,5 mm */
#define RAIL_W  64
#define CONT_W  (800 - RAIL_W)

/* Stranky maji vzdy plnou vysku pod horni listou. Navigace je prekryv:
 * vysouva se pres obsah, nic nepostrkuje a nic nezmensuje. Diky tomu
 * obsah pri hrani s navigaci nikam neposkakuje a ukazatele maji porad
 * stejny prumer. */
#define PAGE_H  (480 - TOP_H)

/* Pravy sloupec s nasycenim DPF. Neni to panel - ma stejne pozadi jako
 * obsah stranky a od ukazatelu ho deli jen vlasova linka. Samotny
 * ukazatel je oramovany svisly bar, ktery se plni odspodu. */
#define RAIL_BAR_W    24
#define RAIL_BAR_X    ((RAIL_W - RAIL_BAR_W) / 2)
#define RAIL_PAD      2         /* odstup vyplne od obrysu */
#define RAIL_TOP      26        /* prvni radek pod popiskem DPF */
#define RAIL_BOT      6         /* mezera mezi barem a hodnotou */
#define RAIL_VAL_H    30        /* radek s hodnotou uplne dole */
#define RAIL_MAX_G    24        /* rozsah baru v gramech */
#define RAIL_TICK_H   2
#define RAIL_TICK_OUT 4         /* presah rysky ven z baru */
#define RAIL_TICK_MID 4         /* mezera mezi levou a pravou ryskou */
#define RAIL_TICK_W   (RAIL_TICK_OUT + RAIL_BAR_W / 2 - RAIL_TICK_MID / 2)

#define N_PAGES 6
enum { PG_HLAVNI = 0, PG_MOTOR, PG_TLAK, PG_CHYBY, PG_DPF, PG_NASTAV };
static const int PAGE_STR[N_PAGES] = {
    STR_PAGE_MAIN, STR_PAGE_ENGINE, STR_PAGE_BOOST,
    STR_PAGE_FAULTS, STR_PAGE_DPF, STR_PAGE_SETUP,
};

static lv_obj_t *page[N_PAGES], *navbtn[N_PAGES];
static lv_obj_t *navbar, *rail;
static int cur_page = PG_HLAVNI;

/* ---------------- trvale prvky ---------------- */
static lv_obj_t *lbl_temp, *lbl_state, *rail_bar, *rail_val;
static lv_obj_t *rail_rule, *rail_tick[4];
/* naposled nastavene pozadi sloupce; prekresluje se jen pri zmene */
static uint32_t rail_bg_shown = C_INK;
/* hlavni */
static lv_obj_t *arc_speed, *arc_rpm, *lbl_speed_v, *lbl_rpm_v, *lbl_speed_u, *lbl_rpm_u;
/* motor */
static lv_obj_t *arc_boost, *lbl_boost, *lbl_boost_sp, *lbl_vnt, *lbl_railp;
/* tlak */
static lv_obj_t *chart, *lbl_gap;
static lv_chart_series_t *ser_sp, *ser_act;
/* nastaveni */
static lv_obj_t *btn_units_v, *btn_units_t, *btn_blank, *lbl_wifi, *lbl_heap;
static lv_obj_t *btn_bright, *btn_daynight;
/* DPF */
static lv_obj_t *btn_regen_state;

/* ---------------- deska ---------------- */
static const ui_host_t *host = NULL;

void ui_set_host(const ui_host_t *h) { host = h; }

static bool host_wifi_connected(void)
{
    return (host && host->wifi_connected) ? host->wifi_connected() : false;
}

static const char *host_wifi_ssid(void)
{
    const char *s = (host && host->wifi_ssid) ? host->wifi_ssid() : NULL;
    return s ? s : "";
}

static const char *host_wifi_ip(void)
{
    const char *s = (host && host->wifi_ip) ? host->wifi_ip() : NULL;
    return s ? s : "";
}

/* Zapamatovane site. Hesla zustavaji na desce, UI se jen pta. */
static bool host_wifi_known(const char *ssid)
{
    if (!host || !host->wifi_known || !ssid || !ssid[0]) return false;
    return host->wifi_known(ssid);
}

/* ---------------------------------------------------------------------
 * Nastaveni
 *
 * Nabidnute urovne jasu. Pet je kompromis mezi "netrefim se" a "nenajdu
 * tu spravnou": kazdy krok ubere zhruba polovinu proudu LEDkami
 * (64,6 / 34,5 / 14,1 / 6,6 / 3,2 mA), takze rozdil je videt na prvni
 * pohled a dva sousedni stupne se nepletou. Sest a vic tlacitek uz by
 * muselo byt uzsich nez 120 px, coz je minimalni dotykovy cil
 * (docs/ui.md), a za jizdy by se do nich nikdo netrefil.
 *
 * Nejnizsi uroven je 25 %, ne 0 %. Pod ni klesa proud LEDkami pod 3 mA,
 * kde AP3032 nikdo nepromeril a boost menic muze zacit preskakovat
 * pulzy. Uplne zhasnout stejne umi jen EXIO2, a to je jine tlacitko.
 * ------------------------------------------------------------------- */
#define BL_LEVELS 5
static const int BL_LEVEL[BL_LEVELS] = {100, 75, 50, 35, 25};
#define BL_MIN  (BL_LEVEL[BL_LEVELS - 1])

static ui_cfg_t cfg;

static int bl_clamp(int p)
{
    if (p > 100)    return 100;
    if (p < BL_MIN) return BL_MIN;      /* nesmysl z NVS nesmi zhasnout displej */
    return p;
}

void ui_set_cfg(const ui_cfg_t *c)
{
    if (!c) return;
    cfg = *c;
    cfg.bright_day   = bl_clamp(cfg.bright_day);
    cfg.bright_night = bl_clamp(cfg.bright_night);
    /* i18n_set() necha neplatny index byt, takze cfg.lang musi projit
     * stejnou kontrolou - jinak by ui_rebuild() cyklovalo od nesmyslu. */
    i18n_set(cfg.lang);
    cfg.lang = i18n_get();
}

/* ---------------- hodnoty ---------------- */
typedef struct { float target, shown; } val_t;
static val_t v_speed, v_rpm, v_temp, v_boost, v_boost_sp, v_vnt, v_rail, v_soot;
static float sim_t = 0;
static uint32_t last_sample = 0;
static inline void approach(val_t *v, float k) { v->shown += (v->target - v->shown) * k; }

/* ---------------------------------------------------------------------
 * Dotek: tuknuti versus tah
 *
 * LV_EVENT_CLICKED posila LVGL pri uvolneni tomu objektu, na kterem tah
 * ZACAL, at uz prst skonci kdekoliv. Jedina vestavena vyjimka je
 * rolovani - a nase panely rolovatelne nejsou, takze tah nahoru zacaty
 * na tlacitku navigace poslal i klik a prepnul stranku.
 *
 * Rozhoduje vzdalenost mezi mistem stisku a mistem uvolneni. Prah 30 px
 * je 3,5 mm: nad chvenim prstu pri tuknuti (LVGL sam bere posun do
 * 10 px jako stani) a pod 50 px, ktere potrebuje gesto, takze jeden tah
 * nikdy neudela obe veci najednou.
 * ------------------------------------------------------------------- */
#define DRAG_PX  30

static lv_coord_t press_x, press_y;

static void press_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *in = lv_indev_get_act();
    if (!in) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    press_x = p.x;
    press_y = p.y;
}

static bool was_drag(void)
{
    lv_indev_t *in = lv_indev_get_act();
    if (!in) return false;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    int dx = p.x - press_x, dy = p.y - press_y;
    return dx * dx + dy * dy > DRAG_PX * DRAG_PX;
}

/* Visi na kazdem tlacitku pred jeho vlastni obsluhou kliku: kdyz slo o
 * tah, zarazi zpracovani udalosti a obsluha se vubec nezavola. Jedno
 * misto pro vsechna tlacitka - na nove uz se neda zapomenout. */
static void drag_guard_cb(lv_event_t *e)
{
    if (was_drag()) lv_event_stop_processing(e);
}

/* ---------------- stavebni prvky ---------------- */
static lv_obj_t *mk_panel(lv_obj_t *p, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(C_RULE), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *mk_lbl(lv_obj_t *p, const char *t, const lv_font_t *f,
                        uint32_t c, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static lv_obj_t *mk_button(lv_obj_t *p, lv_coord_t x, lv_coord_t y,
                           lv_coord_t w, lv_coord_t h, const char *text,
                           const lv_font_t *font, uint32_t accent,
                           lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_btn_create(p);
    lv_obj_set_pos(b, x, y); lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(C_RULE), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(accent), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, lv_color_hex(accent), LV_STATE_PRESSED);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(C_BRIGHT), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(C_INK), LV_STATE_PRESSED);
    lv_obj_center(l);
    /* Poradi je dulezite: hlidac tahu musi bezet pred vlastni obsluhou,
     * aby ji stihl zarazit. LVGL vola obsluhy v poradi registrace. */
    lv_obj_add_event_cb(b, press_cb, LV_EVENT_PRESSED, NULL);
    if (cb) {
        lv_obj_add_event_cb(b, drag_guard_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    }
    return b;
}

static lv_obj_t *mk_arc(lv_obj_t *p, lv_coord_t x, lv_coord_t y, lv_coord_t d,
                        int32_t lo, int32_t hi, uint32_t color, lv_coord_t w)
{
    lv_obj_t *a = lv_arc_create(p);
    lv_obj_set_pos(a, x, y); lv_obj_set_size(a, d, d);
    lv_arc_set_rotation(a, 135);
    lv_arc_set_bg_angles(a, 0, 270);
    lv_arc_set_range(a, lo, hi);
    lv_arc_set_value(a, lo);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, w, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(C_RULE), LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_INDICATOR);
    return a;
}

/* ---------------------------------------------------------------------
 * Podsviceni
 *
 * Dve nezavisle veci:
 *
 *   backlight()  tvrdy vypinac pres EXIO2 na expanderu. Vypina i samotny
 *                panel, takze je to jedina cesta k uplne tme a jedina
 *                uspora, ktera stoji za rec.
 *   bl_set()     strida PWM na plosce BL, tedy jas. Panelu se nedotkne.
 *
 * Kdyz deska stmivat neumi (host->bl_set_percent je NULL, protoze na
 * plosce BL neni dratek), zustane jen vypinac. UI pak stmivani vubec
 * nenabidne - viz build_nastav(). Predstirat ovladani, ktere nic
 * nedela, je horsi nez ho nemit.
 *
 * Pred zhasnutim po necinnosti se jas za pul vteriny stahne dolu a
 * teprve pak se vypne panel. Tvrdy skok z plneho jasu do tmy je v noci
 * rana do oka a clovek nepozna, jestli deska jeste zije. Probuzeni
 * zustava okamzite - dotek vraci obrazovku bez jakehokoli prejezdu.
 * ------------------------------------------------------------------- */
#define BL_FADE_MS 500

static bool backlight_on = true;
static bool bl_fading = false;
static uint32_t bl_fade_start = 0;
static int bl_shown = -1;           /* naposled poslany jas; -1 = nic */

/* dialog jasu - zije jen po dobu otevreneho dialogu, uklizi modal_forget() */
static lv_obj_t *btn_bl_level[BL_LEVELS], *btn_bl_mode;

static bool bl_can_dim(void) { return host && host->bl_set_percent; }

static int bl_level(void) { return cfg.night ? cfg.bright_night : cfg.bright_day; }

static void bl_set(int percent)
{
    if (!bl_can_dim() || percent == bl_shown) return;
    bl_shown = percent;
    host->bl_set_percent(percent);
}

static void bl_apply(void) { bl_set(bl_level()); }

static void backlight(bool on)
{
    bl_fading = false;
    /* Jas se nastavuje jeste pred zapnutim panelu, aby se displej nikdy
     * neukazal se stridou, ktera zbyla po stmivani. */
    if (on) bl_apply();
    if (on == backlight_on) return;
    backlight_on = on;
    if (host && host->backlight) host->backlight(on);
}

static void blank_begin(void)
{
    if (bl_fading) return;
    if (!bl_can_dim()) { backlight(false); return; }
    bl_fading = true;
    bl_fade_start = lv_tick_get();
}

static void blank_step(void)
{
    if (!bl_fading) return;
    uint32_t el = lv_tick_get() - bl_fade_start;
    if (el >= BL_FADE_MS) {
        bl_set(0);              /* strida na doraz, LEDky uz nesviti */
        backlight(false);       /* az ted vypnout panel */
        return;
    }
    bl_set((int)((uint32_t)bl_level() * (BL_FADE_MS - el) / BL_FADE_MS));
}

/* ---------------------------------------------------------------------
 * Prevod jasu na stridu PWM.
 *
 * Prevodni charakteristika desky je v PROUDU temer linearni (zmereno,
 * docs/hardware/backlight.md): napeti na plosce BL ubira proud LEDkam
 * linearne, 0 V = 64,6 mA, 3,3 V = 0 mA. PWM pres filtr R11+C11 je to
 * napeti, takze strida se chova jako napeti - a POLARITA JE OBRACENA:
 * vetsi jas znamena MENSI stridu.
 *
 * Lidske oko ale linearni neni. Vnimany jas odpovida zhruba druhe
 * odmocnine az 2,2. mocnine svetelneho toku (CIE L*, stejna uvaha jako
 * za gamou 2,2 v sRGB), takze linearni prevod procent na proud by dal
 * horni polovinu stupnice skoro k nerozeznani a dolni po skocich.
 *
 * Tabulka je proto proud v promilich plneho proudu pro kazdych 10 %
 * vnimaneho jasu, spocitany jako (p/100)^2,2 * 1000:
 *
 *     p %     0    10    20    30    40    50    60    70    80    90   100
 *     promile 0     6    29    71   133   218   325   456   612   793  1000
 *     mA    0,0   0,4   1,9   4,6   8,6  14,1  21,0  29,5  39,5  51,2  64,6
 *
 * Mezi body se interpoluje primkou; chyba proti mocnine je pod jedno
 * procento, coz je hluboko pod rozlisovaci schopnosti oka i pod
 * tolerancemi R6/R8. Celociselne, zadne powf() v obsluze doteku.
 * ------------------------------------------------------------------- */
uint32_t ui_bl_duty(int percent, uint32_t duty_max)
{
    static const uint16_t CURVE[11] = {
        0, 6, 29, 71, 133, 218, 325, 456, 612, 793, 1000
    };
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    int i = percent / 10;
    uint32_t permille = CURVE[i];
    if (i < 10)
        permille += (uint32_t)(CURVE[i + 1] - CURVE[i]) * (percent % 10) / 10;

    /* Obracena polarita: plny jas = strida 0, tma = strida duty_max. */
    return duty_max - (uint32_t)((uint64_t)duty_max * permille / 1000);
}

/* ---------------------------------------------------------------------
 * Navigace ma tri stavy:
 *   0  jen uchyt  - tenky pruh s "=" uprostred, jako na Androidu
 *   1  lista      - sest tlacitek stranek; po 5 s necinnosti spadne na 0
 *   2  dlazdice   - mrizka 3x6 pro budouci ikony, zatim prazdna
 *
 * Navigace je PREKRYV nad strankami. Driv si stranky i sloupec DPF
 * prepocitavaly vysku podle toho, kolik navigace zabirala, takze se
 * obsah pri kazdem vysunuti scvrkl a ukazatele zmenily prumer. Ovladani
 * nema prekreslovat obsah, takze stranky maji vzdy PAGE_H a lista se
 * vysouva pres ne.
 *
 * Prepina se trojim zpusobem: tuknutim na uchyt, gestem a tazenim prstu
 * jako u bottom sheetu na Androidu - nahoru, drzet, dolu zpatky, bez
 * pusteni. Pri tazeni panel jen jezdi po ose y a nemeni velikost: neni
 * co preskladat a prekresli se jen pruh, o ktery se panel posunul.
 * ------------------------------------------------------------------- */
#define NAV_HANDLE_H  44
#define NAV_GRID_H    300
#define NAV_IDLE_MS   5000
#define NAV_ANIM_MS   180       /* zapadnuti po pusteni */
#define NAV_FLING     8         /* px mezi dvema ctenimi doteku (~20 ms),
                                   tedy asi 400 px/s = svihnuti */

static lv_obj_t *navhandle, *navgrid;
static int nav_state = 0;
static uint32_t nav_shown_ms = 0;

/* kolik navigace prave zabira zespodu; 0 = jeste neni postavena */
static lv_coord_t nav_h_shown = 0;

static lv_coord_t nav_state_h(int s)
{
    switch (s) {
        case 1:  return NAV_H;
        case 2:  return NAV_GRID_H;
        default: return NAV_HANDLE_H;
    }
}

static void nav_apply(lv_coord_t h)
{
    if (h < NAV_HANDLE_H) h = NAV_HANDLE_H;
    if (h > NAV_GRID_H)   h = NAV_GRID_H;
    nav_h_shown = h;

    /* Co je videt, urcuje vyska: do vysky uchytu uchyt, nad ni lista a od
     * jeji plne vysky vys mrizka. Panely si drzi svou velikost a jen
     * vyjizdeji zpod spodni hrany, takze se v nich nic nepreklada. */
    bool grid = h > NAV_H;
    bool bar  = !grid && h > NAV_HANDLE_H;

    if (grid) lv_obj_clear_flag(navgrid, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(navgrid, LV_OBJ_FLAG_HIDDEN);
    if (bar)  lv_obj_clear_flag(navbar, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(navbar, LV_OBJ_FLAG_HIDDEN);
    if (!grid && !bar) lv_obj_clear_flag(navhandle, LV_OBJ_FLAG_HIDDEN);
    else               lv_obj_add_flag(navhandle, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_y(navbar, 480 - h);
    lv_obj_set_y(navgrid, 480 - h);
}

static void nav_anim_cb(void *p, int32_t v) { (void)p; nav_apply((lv_coord_t)v); }

static void nav_animate_to(lv_coord_t h)
{
    lv_anim_del(&nav_h_shown, NULL);
    if (nav_h_shown == 0 || nav_h_shown == h) { nav_apply(h); return; }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &nav_h_shown);
    lv_anim_set_exec_cb(&a, nav_anim_cb);
    lv_anim_set_values(&a, nav_h_shown, h);
    lv_anim_set_time(&a, NAV_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void set_nav_state(int s)
{
    if (s < 0) s = 0;
    if (s > 2) s = 2;
    nav_state = s;
    nav_shown_ms = lv_tick_get();
    nav_animate_to(nav_state_h(s));
}

int ui_page_count(void) { return N_PAGES; }

const char *ui_page_name(int i)
{
    if (i < 0 || i >= N_PAGES) return "";
    return T(PAGE_STR[i]);
}

void ui_show_page(int idx)
{
    if (idx < 0) idx = N_PAGES - 1;
    if (idx >= N_PAGES) idx = 0;
    cur_page = idx;
    for (int i = 0; i < N_PAGES; i++) {
        if (i == idx) lv_obj_clear_flag(page[i], LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_add_flag(page[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(navbtn[i],
            lv_color_hex(i == idx ? C_RULE : C_PANEL), 0);
    }
}

static void nav_cb(lv_event_t *e)
{
    ui_show_page((int)(intptr_t)lv_event_get_user_data(e));
    nav_shown_ms = lv_tick_get();   /* prace s listou odklada jeji schovani */
}

static void handle_cb(lv_event_t *e) { (void)e; set_nav_state(1); }

/* ---- tazeni navigace prstem ---- */
static bool nav_drag = false;           /* tah uz prekrocil prah */
static lv_coord_t nav_drag_h0;          /* vyska ve chvili stisku */
static int nav_drag_state0;
static int nav_drag_vel;                /* vyhlazena rychlost, + = nahoru */
static lv_coord_t nav_drag_last_y;

static void nav_press_cb(lv_event_t *e)
{
    (void)e;
    lv_anim_del(&nav_h_shown, NULL);    /* prst prebira rizeni od animace */
    nav_drag = false;
    nav_drag_h0 = nav_h_shown;
    nav_drag_state0 = nav_state;
    nav_drag_vel = 0;
    nav_drag_last_y = press_y;
}

static void nav_pressing_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *in = lv_indev_get_act();
    if (!in) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_coord_t dy = press_y - p.y;              /* nahoru = kladne */
    if (!nav_drag) {
        if (dy > -DRAG_PX && dy < DRAG_PX) return;
        nav_drag = true;
    }
    /* Rychlost z poslednich cteni, at se pozna svihnuti. Vaha 3:1 ve
     * prospech noveho vzorku: kratke svihnuti se musi stihnout projevit,
     * jednotlive chveni prstu to jeste vyhladi. */
    nav_drag_vel = (nav_drag_vel + 3 * (nav_drag_last_y - p.y)) / 4;
    nav_drag_last_y = p.y;
    nav_apply(nav_drag_h0 + dy);
}

static void nav_release_cb(lv_event_t *e)
{
    (void)e;
    if (!nav_drag) return;
    nav_drag = false;

    /* Rychle svihnuti prepne o stupen, i kdyz prst neurazil ani pulku
     * cesty - tak se bottom sheet chova na telefonu a cekat na pulku by
     * bylo liknave. Pomale tazeni rozhoduje podle pixelu, ne po
     * tretinach: stavy nejsou stejne daleko (44 / 124 / 300 px). */
    int s;
    if (nav_drag_vel >= NAV_FLING)       s = nav_drag_state0 + 1;
    else if (nav_drag_vel <= -NAV_FLING) s = nav_drag_state0 - 1;
    else {
        s = 0;
        lv_coord_t best = LV_ABS(nav_h_shown - nav_state_h(0));
        for (int i = 1; i <= 2; i++) {
            lv_coord_t d = LV_ABS(nav_h_shown - nav_state_h(i));
            if (d < best) { best = d; s = i; }
        }
    }
    set_nav_state(s);       /* zapadnuti animaci a novy odpocet 5 s */
}

/* Panely navigace reaguji na tah prstem. Tlacitka stranek a dlazdice maji
 * EVENT_BUBBLE, takze tah zacaty na nich dojde sem taky. */
static void nav_attach_drag(lv_obj_t *o)
{
    lv_obj_add_event_cb(o, press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(o, nav_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(o, nav_pressing_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(o, nav_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(o, nav_release_cb, LV_EVENT_PRESS_LOST, NULL);
}

/* ---------------------------------------------------------------------
 * Gesto na obrazovce
 *
 * Jeden tah musi udelat nejvys jednu vec. LVGL si sice pamatuje, ze uz
 * gesto z tohoto stisku poslalo, ale jakmile se objekt pod prstem zmeni
 * - a prepnuti stranky ho zmeni - priznak zahodi a zbytek tehoz tahu
 * posle gesto znovu. Jedno svihnuti pak proletelo dve az tri stranky.
 * lv_indev_wait_release() zbytek stisku zahodi, takze dalsi gesto prijde
 * az z noveho doteku.
 * ------------------------------------------------------------------- */
static void gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *in = lv_indev_get_act();
    switch (lv_indev_get_gesture_dir(in)) {
        case LV_DIR_RIGHT:
            /* tah od leveho okraje = zpet na hlavni obrazovku */
            if (press_x < 70 && cur_page != PG_HLAVNI) ui_show_page(PG_HLAVNI);
            else ui_show_page(cur_page + 1);
            break;
        case LV_DIR_LEFT:  ui_show_page(cur_page - 1); break;
        /* Kdyz tah zacal na navigaci, uz si ho vyridilo tazeni prstem -
         * jinak by se stav prepnul dvakrat. Tah pokracuje, takze se stisk
         * nesmi zahodit. */
        case LV_DIR_TOP:    if (nav_drag) return; set_nav_state(nav_state + 1); break;
        case LV_DIR_BOTTOM: if (nav_drag) return; set_nav_state(nav_state - 1); break;
        default: return;
    }
    lv_indev_wait_release(in);
}

/* ---------------------------------------------------------------------
 * Modalni dialog
 *
 * Otevreny je vzdycky nanejvys jeden, takze staci jeden ukazatel. Co ale
 * uvnitr dialogu zije, se musi zapomenout ve chvili, kdy dialog zanikne,
 * a zaniknout muze ctyrmi cestami: tlacitkem, gestem zprava, otevrenim
 * jineho dialogu a prestavbou UI po zmene jazyka. Proto uklid visi na
 * LV_EVENT_DELETE samotneho dialogu - spusti se pri kazde z nich a neda
 * se na nej zapomenout. Driv se nulovalo jen v nekterych obsluhach,
 * takze ukazatel na seznam siti prezil smazany dialog a dobihajici sken
 * psal do uvolnene pameti.
 * ------------------------------------------------------------------- */
static lv_obj_t *modal = NULL;

/* stav, ktery zije jen po dobu otevreneho dialogu */
static lv_obj_t *wifi_list, *wifi_ta, *wifi_kb;
static int scan_state;                  /* 0 klid, 1 bezi sken, 2 dobehl */
static uint32_t scan_started_ms;
static lv_obj_t *hold_panel, *hold_ring, *hold_num;
static lv_timer_t *hold_timer;
static lv_event_cb_t hold_action;
static bool hold_done;

static void modal_forget(void)
{
    modal      = NULL;
    wifi_list  = NULL;
    wifi_ta    = NULL;
    wifi_kb    = NULL;
    scan_state = 0;
    hold_panel = NULL;
    hold_ring  = NULL;
    hold_num   = NULL;
    hold_action = NULL;
    hold_done   = false;
    btn_bl_mode = NULL;
    for (int i = 0; i < BL_LEVELS; i++) btn_bl_level[i] = NULL;
    /* Timer odpoctu kresli do popisku v dialogu, takze musi skoncit tady.
     * Ukazatele uz jsou NULL, takze i kdyby se jeste jednou spustil,
     * nema do ceho psat. */
    if (hold_timer) { lv_timer_del(hold_timer); hold_timer = NULL; }
}

/* Prestavba UI po zmene jazyka rusi starou obrazovku az pozdeji
 * (lv_obj_del_async), takze tahle obsluha muze dobehnout ve chvili, kdy
 * uz je otevreny jiny dialog. Uklizi se proto jen stav toho dialogu,
 * ktery se opravdu rusi. */
static void modal_deleted_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) == modal) modal_forget();
}

static void modal_close_cb(lv_event_t *e) { (void)e; if (modal) lv_obj_del(modal); }

static void modal_gesture_cb(lv_event_t *e)
{
    (void)e;
    /* Zpetne gesto dela totez co tlacitko ZPET, a to kdekoliv na dialogu -
     * tedy i nad klavesnici nebo seznamem siti. Zbytek tahu se musi
     * zahodit: dialog pod prstem zmizi, LVGL si vsimne zmeny objektu,
     * odemkne gesto a zbyvajicich par desitek pixelu tehoz tahu by uz
     * dostala obrazovka a prepnula stranku. */
    lv_indev_t *in = lv_indev_get_act();
    if (lv_indev_get_gesture_dir(in) != LV_DIR_RIGHT) return;
    lv_indev_wait_release(in);
    modal_close_cb(NULL);
}

static lv_obj_t *modal_open(const char *title, uint32_t accent)
{
    if (modal) lv_obj_del(modal);       /* uklid obstara obsluha DELETE */
    modal = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(modal, 0, 0); lv_obj_set_size(modal, 800, 480);
    lv_obj_set_style_bg_color(modal, lv_color_hex(C_INK), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_radius(modal, 0, 0);
    lv_obj_set_style_pad_all(modal, 0, 0);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

    /* Gesto posila LVGL prvnimu predkovi BEZ priznaku GESTURE_BUBBLE a ten
     * priznak dostane po vytvoreni kazdy objekt (lv_obj.c). Bez tohohle
     * radku tah propadne az na obrazovku: dialog zustane otevreny a jeste
     * se pod nim prepne stranka. Takhle gesto konci u dialogu, at uz tah
     * zacal kdekoliv - na klavesnici, v seznamu siti i na tlacitku - a na
     * obrazovku se nedostane, takze stranka se neprepne. */
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(modal, modal_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(modal, modal_deleted_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *s = lv_obj_create(modal);
    lv_obj_set_pos(s, 0, 0); lv_obj_set_size(s, 800, 6);
    lv_obj_set_style_bg_color(s, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(s, 0, 0); lv_obj_set_style_radius(s, 0, 0);
    if (title && title[0]) {
        mk_lbl(modal, title, UI_FONT_32, C_BRIGHT, 30, 26);
        /* Kazdy dialog ma vlastni "zpet" vpravo nahore. Bez nej se z nekterych
         * obrazovek nedalo vycouvat jinak nez tlacitkem s jinym vyznamem.
         * Dialog s klavesnici predava prazdny nadpis - tam by se tlacitko
         * prekryvalo se vstupnim polem a klavesnice ma vlastni zavreni. */
        mk_button(modal, 660, 14, 124, 70, T(STR_BACK), UI_FONT_20,
                  C_LABEL, modal_close_cb, NULL);
    }
    return modal;
}

static void show_info(const char *title, const char *body, uint32_t accent)
{
    lv_obj_t *m = modal_open(title, accent);
    lv_obj_t *b = mk_lbl(m, body, UI_FONT_20, C_LABEL, 30, 100);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, 740);
    mk_button(m, 30, 330, 360, 120, T(STR_OK), UI_FONT_32,
              accent, modal_close_cb, NULL);
}

/* ---------------------------------------------------------------------
 * Potvrzovaci dialog pro akce, ktere neco opravdu provedou.
 *
 * Proti show_info() ma ctyri veci navic:
 *   - dve STEJNE VELIKA tlacitka. Kdyby bylo zruseni mensi, cetlo by se jako
 *     druhorada volba, a u akce, ktera umi poskodit vuz, to je spatne.
 *   - potvrzeni se musi DRZET 3 vteriny
 *   - odpocet je UPROSTRED OBRAZOVKY, ne na tlacitku. Na tlacitku ho
 *     zakryval prst, takze uzivatel nevidel, jak daleko odpocet je.
 *     Prst lezi dole, stred zustava volny, a cislice je pres pul dialogu.
 *     Kazda ubehla vterina celym prekryvem blikne zlute: deska nema ani
 *     bzucak, ani motorek, takze zpetna vazba muze byt jedine videt a
 *     musi byt citelna i periferne.
 *   - po dokonceni se nejdriv kratce ukaze potvrzeni a teprve pak se
 *     akce provede - jinak by dialog zmizel driv, nez by si clovek stihl
 *     vsimnout, ze drzel dost dlouho.
 * ------------------------------------------------------------------- */
#define HOLD_MS        3000
#define HOLD_STEP_MS   30       /* krok odpoctu - prstenec se musi hybat plynule */
#define HOLD_FLASH_MS  200      /* jak dlouho blikne zluta na kazde vterine */
#define HOLD_DONE_MS   700      /* jak dlouho je videt potvrzeni pred akci */
#define HOLD_PANEL_Y   92
#define HOLD_PANEL_H   230
#define HOLD_RING_D    200

static uint32_t hold_start, hold_flash_ms, hold_done_ms, hold_accent;
static int hold_last_sec;

static void hold_paint(bool flash)
{
    if (!hold_panel) return;
    lv_obj_set_style_bg_color(hold_panel,
        flash ? lv_color_mix(lv_color_hex(C_ACTUAL), lv_color_hex(C_INK), 48)
              : lv_color_hex(C_INK), 0);
    lv_obj_set_style_border_color(hold_panel,
        lv_color_hex(flash ? C_ACTUAL : C_RULE), 0);
    lv_obj_set_style_arc_color(hold_ring,
        lv_color_hex(flash ? C_ACTUAL : hold_accent), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(hold_num,
        lv_color_hex(flash ? C_ACTUAL : C_BRIGHT), 0);
}

static void hold_reset(void)
{
    if (hold_done) return;      /* uz se jen ukazuje potvrzeni, to se nerusi */
    if (hold_timer) { lv_timer_del(hold_timer); hold_timer = NULL; }
    if (hold_panel) lv_obj_add_flag(hold_panel, LV_OBJ_FLAG_HIDDEN);
}

static void hold_tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t now = lv_tick_get();

    if (hold_done) {
        if (now - hold_done_ms < HOLD_DONE_MS) return;
        lv_event_cb_t a = hold_action;
        hold_done = false;
        hold_action = NULL;
        if (hold_timer) { lv_timer_del(hold_timer); hold_timer = NULL; }
        /* Akce az ted, kdyz uzivatel videl potvrzeni. Vetsinou dialog
         * zrusi, takze po tomhle radku uz se na nic v nem nesmi sahat. */
        if (a) a(NULL);
        else   modal_close_cb(NULL);
        return;
    }

    uint32_t el = now - hold_start;
    if (el >= HOLD_MS) {
        hold_done = true;
        hold_done_ms = now;
        lv_arc_set_value(hold_ring, 100);
        lv_obj_set_style_arc_color(hold_ring, lv_color_hex(C_SAGE), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(hold_panel, lv_color_hex(C_INK), 0);
        lv_obj_set_style_border_color(hold_panel, lv_color_hex(C_SAGE), 0);
        /* Fajfka nad slovem: slovo samo by pri fontu 48 z prstence vylezlo,
         * takze text je o cislo mensi a znacka nese hlavni sdeleni. */
        lv_obj_set_style_text_font(hold_num, UI_FONT_32, 0);
        lv_obj_set_style_text_color(hold_num, lv_color_hex(C_SAGE), 0);
        lv_obj_set_style_text_align(hold_num, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text_fmt(hold_num, LV_SYMBOL_OK "\n%s", T(STR_HOLD_DONE));
        return;
    }

    int left = (int)((HOLD_MS - el + 999) / 1000);
    if (left != hold_last_sec) { hold_last_sec = left; hold_flash_ms = now; }
    lv_arc_set_value(hold_ring, (int32_t)((HOLD_MS - el) * 100 / HOLD_MS));
    lv_label_set_text_fmt(hold_num, "%d", left);
    hold_paint(now - hold_flash_ms < HOLD_FLASH_MS);
}

static void hold_press_cb(lv_event_t *e)
{
    if (hold_done) return;      /* po dokonceni uz pusteni nic nemeni */
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        hold_start = lv_tick_get();
        hold_last_sec = 0;      /* prvni tik hned blikne */
        lv_obj_set_style_text_font(hold_num, UI_FONT_48, 0);
        lv_obj_clear_flag(hold_panel, LV_OBJ_FLAG_HIDDEN);
        if (!hold_timer) hold_timer = lv_timer_create(hold_tick_cb, HOLD_STEP_MS, NULL);
    } else {
        hold_reset();           /* pusteni driv = zacina se znovu */
    }
}

static void show_confirm(const char *title, const char *body,
                         uint32_t accent, lv_event_cb_t on_ok)
{
    lv_obj_t *m = modal_open(title, accent);

    lv_obj_t *b = mk_lbl(m, body, UI_FONT_20, C_LABEL, 30, 100);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, 740);

    hold_action = on_ok;
    hold_accent = accent;
    hold_done   = false;

    hold_panel = lv_obj_create(m);
    lv_obj_set_pos(hold_panel, 0, HOLD_PANEL_Y);
    lv_obj_set_size(hold_panel, 800, HOLD_PANEL_H);
    lv_obj_set_style_bg_color(hold_panel, lv_color_hex(C_INK), 0);
    lv_obj_set_style_bg_opa(hold_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(hold_panel, lv_color_hex(C_RULE), 0);
    lv_obj_set_style_border_width(hold_panel, 2, 0);
    lv_obj_set_style_radius(hold_panel, 0, 0);
    lv_obj_set_style_pad_all(hold_panel, 0, 0);
    lv_obj_clear_flag(hold_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hold_panel, LV_OBJ_FLAG_HIDDEN);

    hold_ring = mk_arc(hold_panel, (800 - HOLD_RING_D) / 2,
                       (HOLD_PANEL_H - HOLD_RING_D) / 2, HOLD_RING_D,
                       0, 100, accent, 14);
    /* Cely kruh od dvanactky, ne gauge jako ukazatele - odpocet se cte
     * jako ubyvajici kolac, ne jako merena hodnota. */
    lv_arc_set_rotation(hold_ring, 270);
    lv_arc_set_bg_angles(hold_ring, 0, 360);
    lv_arc_set_value(hold_ring, 100);

    hold_num = mk_lbl(hold_panel, "", UI_FONT_48, C_BRIGHT, 0, 0);
    lv_obj_center(hold_num);

    char cap[32];
    snprintf(cap, sizeof(cap), T(STR_HOLD_FMT), HOLD_MS / 1000);
    lv_obj_t *ok = mk_button(m, 30, 330, 360, 120, cap, UI_FONT_32,
                             accent, NULL, NULL);
    lv_obj_add_event_cb(ok, hold_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ok, hold_press_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(ok, hold_press_cb, LV_EVENT_PRESS_LOST, NULL);

    mk_button(m, 410, 330, 360, 120, T(STR_CANCEL), UI_FONT_32,
              C_LABEL, modal_close_cb, NULL);
}

/* ---------------------------------------------------------------------
 * Regenerace DPF
 *
 * Stav se kresli na pravy sloupec, protoze ten je videt na kazde strance -
 * jedine misto, kde si bezici regenerace vsimne i ten, kdo je zrovna
 * jinde. Pokus o spusteni podkresli sloupec cervene, bezici regenerace
 * k tomu blika a napln ubyva shora dolu.
 *
 * Procenta, nebo pet kroku? Kdyz jednotka postup hlasi (31 03 02 0D
 * status, delka regenerace v DID 2487), jede se podle nej a vypln klesa
 * plynule od nasyceni k nule. Kdyz ho nehlasi, ubyva po petinach podle
 * odhadovane delky. Hrubost je zamer: plynuly pohyb se cte jako merena
 * hodnota, skoky jako odhad, cimz to v tom pripade je. Posledni petina
 * zustane stat, dokud regeneraci nekdo neukonci - UI si nevymysli, ze
 * je hotovo.
 * ------------------------------------------------------------------- */
#define RGN_BLINK_MS  500
#define RGN_STEP_MS   180000    /* petina z odhadovanych 15 minut */
#define RGN_SIM_MS    20000     /* rucni ukazka: 0 az 100 % za 20 s */

static ui_regen_t regen_state = UI_REGEN_OFF;
static int regen_pct = -1;      /* <0 = jednotka postup nehlasi */
static uint32_t regen_since = 0;
static float regen_soot0 = 0;   /* nasyceni ve chvili spusteni */
static int regen_demo = 0;      /* 0 vyp, 1 odeslano, 2 bezi, 3 bezi s procenty */

void ui_set_regen_state(ui_regen_t state, int percent)
{
    if (state != regen_state) {
        regen_since = lv_tick_get();
        regen_soot0 = v_soot.shown;
    }
    regen_state = state;
    regen_pct = percent;
}

static void regen_demo_label(void)
{
    static const int S[4] = {
        STR_REGEN_OFF, STR_REGEN_REQ, STR_REGEN_RUN, STR_REGEN_RUN_PCT
    };
    if (btn_regen_state)
        lv_label_set_text(lv_obj_get_child(btn_regen_state, 0), T(S[regen_demo]));
}

static void regen_demo_set(int d)
{
    regen_demo = d & 3;
    ui_set_regen_state(UI_REGEN_OFF, -1);   /* vzdy od nuly, at jde cyklit */
    switch (regen_demo) {
        case 1: ui_set_regen_state(UI_REGEN_REQUESTED, -1); break;
        case 2: ui_set_regen_state(UI_REGEN_ACTIVE, -1); break;
        case 3: ui_set_regen_state(UI_REGEN_ACTIVE, 0); break;
        default: break;
    }
    regen_demo_label();
}

static void regen_state_cb(lv_event_t *e) { (void)e; regen_demo_set(regen_demo + 1); }

static void regen_ok_cb(lv_event_t *e)
{
    (void)e;
    /* Na CAN se zatim nic neposila - rozsviti se jen stav na sloupci, aby
     * slo rozhrani odladit. Az bude vrstva CAN, prijde sem 31 01 02 0D
     * a stav se bude prepinat podle odpovedi jednotky. */
    regen_demo_set(1);
    modal_close_cb(NULL);
}

static void regen_cb(lv_event_t *e)
{
    (void)e;
    show_confirm(T(STR_REGEN_TITLE), T(STR_REGEN_BODY), C_DEFICIT, regen_ok_cb);
}

/* ---------------------------------------------------------------------
 * WiFi
 *
 * Zapamatovane site drzi deska v NVS, UI se jen pta, jestli sit uz zna.
 * Znama sit se pripoji rovnou, neznama se zepta na heslo. Zapomenuti je
 * na dlouhem stisku radku - tuknuti uz je obsazene pripojenim a druhe
 * tlacitko v radku by bylo pod 120 px dotykovy cil.
 * ------------------------------------------------------------------- */
#define WIFI_SCAN_MS   15000    /* po teto dobe se sken vzdava */
#define WIFI_ROWS      12       /* kolik siti se vejde do seznamu */

static char wifi_ssid[33];      /* sit, ke ktere se prave zadava heslo */
static char wifi_target[33];    /* sit urcena k zapomenuti; musi prezit
                                   vymenu seznamu za potvrzovaci dialog */

static void wifi_scan_cb(lv_event_t *e);

static void wifi_save_connect(const char *ssid, const char *pass)
{
    if (host && host->wifi_connect) host->wifi_connect(ssid, pass);
}

/* Seznam je jedine misto, kde by prosvital vychozi svetly vzhled LVGL,
 * takze kazdy radek dostane paletu UI. Ulozene site jsou zelene a maji
 * fajfku - samotna barva by na snimku z dilny pri slunci nestacila. */
static void wifi_row_style(lv_obj_t *b, uint32_t color)
{
    lv_obj_set_style_text_font(b, UI_FONT_20, 0);
    lv_obj_set_style_text_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(C_RULE), 0);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(b, 1, 0);
}

/* Jeden radek misto seznamu siti: hledam / nic jsem nenasel / nepovedlo se. */
static void wifi_msg(int str)
{
    if (!wifi_list) return;
    lv_obj_clean(wifi_list);
    wifi_row_style(lv_list_add_text(wifi_list, T(str)), C_LABEL);
}

static void kb_ok_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_READY) return;
    wifi_save_connect(wifi_ssid, lv_textarea_get_text(wifi_ta));
    modal_close_cb(e);
}

static void ssid_pick_cb(lv_event_t *e)
{
    const char *txt = lv_list_get_btn_text(wifi_list, lv_event_get_target(e));
    if (!txt) return;
    /* Text zije v seznamu, ktery za chvili zanikne - nejdriv kopie. */
    char ssid[33];
    snprintf(ssid, sizeof(ssid), "%s", txt);

    /* Znamou sit uz se na heslo neptame, deska ho ma v NVS. */
    if (host_wifi_known(ssid)) {
        if (host->wifi_connect_known) host->wifi_connect_known(ssid);
        modal_close_cb(NULL);
        return;
    }
    snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", ssid);

    /* Vlastni rozvrzeni bez velkeho nadpisu - klavesnice potrebuje vysku.
     * Popisek 26 px + vstup 64 px + klavesnice 370 px = presne 480. */
    lv_obj_t *m = modal_open("", C_DEMAND);

    char cap[64];
    snprintf(cap, sizeof(cap), T(STR_WIFI_PASS_FOR), wifi_ssid);
    mk_lbl(m, cap, UI_FONT_16, C_LABEL, 20, 12);

    wifi_ta = lv_textarea_create(m);
    lv_obj_set_size(wifi_ta, 760, 64);
    lv_obj_align(wifi_ta, LV_ALIGN_TOP_MID, 0, 36);
    lv_textarea_set_one_line(wifi_ta, true);
    lv_textarea_set_placeholder_text(wifi_ta, T(STR_WIFI_PASS_PH));
    lv_obj_set_style_text_font(wifi_ta, UI_FONT_28, 0);
    lv_obj_set_style_bg_color(wifi_ta, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_text_color(wifi_ta, lv_color_hex(C_BRIGHT), 0);
    lv_obj_set_style_border_color(wifi_ta, lv_color_hex(C_DEMAND), 0);

    wifi_kb = lv_keyboard_create(m);
    lv_obj_set_size(wifi_kb, 800, 370);
    /* zarovnani je spolehlivejsi nez absolutni pozice - klavesnice si
     * pri zmene velikosti sama prepocita umisteni */
    lv_obj_align(wifi_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(wifi_kb, UI_FONT_20, 0);
    lv_obj_set_style_pad_all(wifi_kb, 4, 0);
    lv_keyboard_set_textarea(wifi_kb, wifi_ta);
    lv_obj_add_event_cb(wifi_kb, kb_ok_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(wifi_kb, modal_close_cb, LV_EVENT_CANCEL, NULL);
}

static void wifi_forget_ok_cb(lv_event_t *e)
{
    (void)e;
    if (host && host->wifi_forget) host->wifi_forget(wifi_target);
    wifi_scan_cb(NULL);         /* zpatky do seznamu a rovnou novy sken */
}

static void ssid_forget_cb(lv_event_t *e)
{
    const char *txt = lv_list_get_btn_text(wifi_list, lv_event_get_target(e));
    if (!txt || !host_wifi_known(txt)) return;
    snprintf(wifi_target, sizeof(wifi_target), "%s", txt);
    char body[192];
    snprintf(body, sizeof(body), T(STR_WIFI_FORGET_BODY), wifi_target);
    show_confirm(T(STR_WIFI_FORGET_TITLE), body, C_DEFICIT, wifi_forget_ok_cb);
}

static void wifi_scan_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *m = modal_open(T(STR_WIFI_TITLE), C_DEMAND);
    wifi_list = lv_list_create(m);
    lv_obj_set_pos(wifi_list, 30, 90);
    lv_obj_set_size(wifi_list, 600, 290);
    lv_obj_set_style_bg_color(wifi_list, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_border_color(wifi_list, lv_color_hex(C_RULE), 0);
    lv_obj_set_style_border_width(wifi_list, 1, 0);
    lv_obj_set_style_radius(wifi_list, 0, 0);
    lv_obj_set_style_pad_all(wifi_list, 0, 0);
    lv_obj_set_style_pad_row(wifi_list, 0, 0);
    /* Seznam roluje jen svisle. Vodorovny tah tim zustane gestem a zavre
     * dialog, misto aby se zaradil v seznamu. */
    lv_obj_set_scroll_dir(wifi_list, LV_DIR_VER);

    mk_button(m, 650, 90, 120, 120, T(STR_CLOSE), UI_FONT_20,
              C_DEMAND, modal_close_cb, NULL);
    mk_button(m, 650, 240, 120, 120, T(STR_WIFI_RETRY), UI_FONT_20,
              C_ACTUAL, wifi_scan_cb, NULL);
    mk_lbl(m, T(STR_WIFI_SAVED), UI_FONT_16, C_LABEL, 30, 392);

    wifi_msg(STR_WIFI_SEARCHING);
    if (host && host->wifi_scan_start) host->wifi_scan_start();
    scan_state = 1;
    scan_started_ms = lv_tick_get();
}

static void wifi_poll(void)
{
    if (scan_state != 1) return;
    if (!host || !host->wifi_scan_result || !host->wifi_scan_ssid) {
        scan_state = 2;                 /* sken nema kdo vyridit */
        wifi_msg(STR_WIFI_FAILED);
        return;
    }
    int n = host->wifi_scan_result();
    if (n == -1) {                      /* jeste bezi */
        if (lv_tick_get() - scan_started_ms > WIFI_SCAN_MS) {
            scan_state = 2;
            wifi_msg(STR_WIFI_FAILED);
        }
        return;
    }
    scan_state = 2;
    /* -2 je WIFI_SCAN_FAILED, tedy sken se vubec nerozjel. Driv spadlo
     * oboji do "n < 0" a dialog zustal navzdy na hlasce "hledam". */
    if (n < 0)  { wifi_msg(STR_WIFI_FAILED); return; }
    if (n == 0) { wifi_msg(STR_WIFI_NONE);   return; }
    if (!wifi_list) return;

    lv_obj_clean(wifi_list);
    for (int i = 0; i < n && i < WIFI_ROWS; i++) {
        const char *ssid = host->wifi_scan_ssid(i);
        if (!ssid) ssid = "";
        bool known = host_wifi_known(ssid);
        lv_obj_t *b = lv_list_add_btn(wifi_list, known ? LV_SYMBOL_OK : NULL, ssid);
        lv_obj_set_style_min_height(b, 58, 0);
        wifi_row_style(b, known ? C_SAGE : C_BRIGHT);
        lv_obj_add_event_cb(b, press_cb, LV_EVENT_PRESSED, NULL);
        /* SHORT_CLICKED, ne CLICKED: po dlouhem stisku posila LVGL oboji,
         * takze by se sit zaroven zapomnela i pripojila. */
        lv_obj_add_event_cb(b, drag_guard_cb, LV_EVENT_SHORT_CLICKED, NULL);
        lv_obj_add_event_cb(b, ssid_pick_cb, LV_EVENT_SHORT_CLICKED, NULL);
        if (known) lv_obj_add_event_cb(b, ssid_forget_cb, LV_EVENT_LONG_PRESSED, NULL);
    }
}

/* ---------------------------------------------------------------------
 * Prestavba UI po zmene jazyka.
 *
 * Druha moznost byla drzet si seznam vsech popisku a jeden po druhem je
 * prepsat. Tohle je min kodu a hlavne na to nejde zapomenout: novy
 * popisek pridany kdekoliv v UI je prelozeny automaticky. Namerene
 * hodnoty prezivaji, protoze sedi ve statickych promennych, ne v LVGL.
 * ------------------------------------------------------------------- */
static void ui_rebuild(void)
{
    lv_obj_t *old = lv_scr_act();
    lv_scr_load(lv_obj_create(NULL));
    /* Stara obrazovka se rusi az po navratu z obsluhy udalosti. Smazat ji
     * primo v callbacku tlacitka, ktere na ni lezi, LVGL nerozdycha. */
    lv_obj_del_async(old);

    /* Stare ukazatele jeste chvili plati, ale UI uz je nesmi pouzit -
     * layout_* funkce se podle nich rozhoduji, jestli uz je co pocitat. */
    for (int i = 0; i < N_PAGES; i++) { page[i] = NULL; navbtn[i] = NULL; }
    arc_speed = NULL;
    rail_bar  = NULL;
    modal_forget();     /* stejny uklid jako pri zavreni dialogu */

    /* Navigace se stavi znovu, takze animace vysouvani uz nema co hybat
     * a vyska se musi nastavit rovnou, bez prejezdu. */
    lv_anim_del(&nav_h_shown, NULL);
    nav_h_shown = 0;

    int keep_page = cur_page;
    int keep_nav  = nav_state;
    ui_build();
    ui_show_page(keep_page);
    set_nav_state(keep_nav);
}

/* ---------------- nastaveni: obsluha ---------------- */
static void save_cfg(void)
{
    if (host && host->save_cfg) host->save_cfg(&cfg);
}

/* Jazyky se cyklicky stridaji; samostatna obrazovka s vyberem by pro tri
 * polozky byla zbytecna a pri deseti stejne nestaci jedno tlacitko. */
static void lang_cb(lv_event_t *e)
{
    (void)e;
    cfg.lang = (cfg.lang + 1) % i18n_count();
    i18n_set(cfg.lang);
    save_cfg();
    ui_rebuild();
}

static void units_v_cb(lv_event_t *e)
{
    (void)e; cfg.mph = !cfg.mph; save_cfg();
    lv_label_set_text(lv_obj_get_child(btn_units_v, 0), cfg.mph ? "mph" : "km/h");
    lv_label_set_text(lbl_speed_u, cfg.mph ? "mph" : "km/h");
}
static void units_t_cb(lv_event_t *e)
{
    (void)e; cfg.fahrenheit = !cfg.fahrenheit; save_cfg();
    lv_label_set_text(lv_obj_get_child(btn_units_t, 0), cfg.fahrenheit ? "F" : "C");
}

/* Popisek tlacitka "ZHASNOUT PO". Potrebuje ho i build_nastav(), aby
 * tlacitko uz pri stavbe ukazovalo hodnotu nactenou z NVS a ne natvrdo
 * zadane "nikdy". */
static void blank_label(char *buf, size_t n, uint32_t s)
{
    if (s == 0)      snprintf(buf, n, "%s", T(STR_BLANK_NEVER));
    else if (s < 60) snprintf(buf, n, "%u s", (unsigned)s);
    else             snprintf(buf, n, "%u min", (unsigned)(s / 60));
}

/* Zhasne podsviceni okamzite, bez cekani na timeout. Hlavni duvod existence:
 * merit na ploskach BL/GND se zhasnutym podsvicenim.
 * Probuzeni resi kontrola necinnosti v ui_tick(). Aby se displej nerozsvitil
 * hned zpatky od doteku, ktery tohle tlacitko prave zmackl, dame kratkou
 * ochrannou lhutu - behem ni se probuzeni ignoruje. */
static uint32_t blank_guard_ms = 0;
static void display_off_cb(lv_event_t *e)
{
    (void)e;
    backlight(false);
    blank_guard_ms = lv_tick_get();
}

static void blank_cb(lv_event_t *e)
{
    (void)e;
    static const uint32_t steps[] = {0, 30, 120, 600};
    int i = 0;
    for (int k = 0; k < 4; k++) if (cfg.blank_s == steps[k]) i = k;
    cfg.blank_s = steps[(i + 1) % 4];
    save_cfg();
    char b[24];
    blank_label(b, sizeof(b), cfg.blank_s);
    lv_label_set_text(lv_obj_get_child(btn_blank, 0), b);
}

/* ---------------------------------------------------------------------
 * Jas podsviceni
 *
 * Tlacitka, ne posuvnik. Posuvnik vyzaduje trefit se a pak jeste
 * udrzet prst na jednom miste; za jizdy se obe veci nepovedou. Tlacitko
 * ma pevnou polohu, takze se da zmacknout naslepo.
 *
 * Urovne plati pro prave zapnuty rezim. Nocni se tedy nastavi tak, ze
 * se prepne na noc a vybere uroven - stejna tlacitka, zadna druha
 * obrazovka a zadne "upravit nocni jas".
 * ------------------------------------------------------------------- */

/* Popisek tlacitka JAS v nastaveni. Je i v build_nastav(), aby tlacitko
 * uz pri stavbe ukazovalo uroven z NVS a ne natvrdo zadanou. */
static void bright_btn_paint(void)
{
    if (!btn_bright) return;
    char b[16];
    snprintf(b, sizeof(b), "%d %%", bl_level());
    lv_label_set_text(lv_obj_get_child(btn_bright, 0), b);
}

/* Prepinac den/noc je na dvou mistech najednou - v nastaveni a v
 * dialogu - a oba musi ukazovat totez. */
static void daynight_btn_paint(void)
{
    const char *t = T(cfg.night ? STR_BRIGHT_NIGHT : STR_BRIGHT_DAY);
    if (btn_daynight) lv_label_set_text(lv_obj_get_child(btn_daynight, 0), t);
    if (btn_bl_mode)  lv_label_set_text(lv_obj_get_child(btn_bl_mode, 0), t);
}

/* Vybrana uroven ma silnejsi obrubu. Barva se nemeni: modra a kostena
 * maji v tehle aplikaci pevny vyznam (zadano / skutecnost) a vyber
 * tlacitka mezi ne nepatri. */
static void bl_level_paint(void)
{
    for (int i = 0; i < BL_LEVELS; i++) {
        if (!btn_bl_level[i]) continue;
        bool sel = BL_LEVEL[i] == bl_level();
        lv_obj_set_style_border_color(btn_bl_level[i],
            lv_color_hex(sel ? C_BRIGHT : C_RULE), 0);
        lv_obj_set_style_border_width(btn_bl_level[i], sel ? 3 : 1, 0);
    }
}

static void bl_level_cb(lv_event_t *e)
{
    int p = (int)(intptr_t)lv_event_get_user_data(e);
    if (cfg.night) cfg.bright_night = p;
    else           cfg.bright_day = p;
    bl_apply();
    save_cfg();
    bl_level_paint();
    bright_btn_paint();
}

static void daynight_cb(lv_event_t *e)
{
    (void)e;
    cfg.night = !cfg.night;
    bl_apply();
    save_cfg();
    daynight_btn_paint();
    bl_level_paint();
    bright_btn_paint();
}

/* Pet tlacitek po 140 px s mezerou 8 px: 30 + 4*148 + 140 = 762, takze
 * se vejdou na sirku i s okraji. Vyska 120 px je minimalni dotykovy cil
 * z docs/ui.md, stejny jako u ostatnich dialogovych tlacitek. */
#define BL_BTN_W    140
#define BL_BTN_H    120
#define BL_BTN_X(i) (30 + (i) * (BL_BTN_W + 8))

static void bright_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *m = modal_open(T(STR_BRIGHT_TITLE), C_ACTUAL);

    lv_obj_t *info = mk_lbl(m, T(STR_BRIGHT_INFO), UI_FONT_16, C_LABEL, 30, 96);
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info, 740);

    for (int i = 0; i < BL_LEVELS; i++) {
        char cap[8];
        snprintf(cap, sizeof(cap), "%d %%", BL_LEVEL[i]);
        btn_bl_level[i] = mk_button(m, BL_BTN_X(i), 200, BL_BTN_W, BL_BTN_H,
                                    cap, UI_FONT_24, C_ACTUAL, bl_level_cb,
                                    (void *)(intptr_t)BL_LEVEL[i]);
    }
    bl_level_paint();

    mk_lbl(m, T(STR_SET_DAYNIGHT), UI_FONT_14, C_LABEL, 30, 338);
    btn_bl_mode = mk_button(m, 30, 358, 300, BL_BTN_H - 10, "", UI_FONT_32,
                            C_DEMAND, daynight_cb, NULL);
    daynight_btn_paint();
}

/* ---------------- stranky ---------------- */
static lv_obj_t *mk_page(lv_obj_t *scr)
{
    lv_obj_t *p = lv_obj_create(scr);
    lv_obj_set_pos(p, 0, TOP_H);
    lv_obj_set_size(p, CONT_W, PAGE_H);
    lv_obj_set_style_bg_color(p, lv_color_hex(C_INK), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

/* Prepocet velikosti dvou hlavnich ukazatelu. Prumer je omezeny jak vyskou
 * stranky, tak sirkou - dva oblouky vedle sebe se musi vejit do CONT_W.
 * Driv byl prumer napevno 360 px a pravy oblouk tim prelezal pod pravy
 * sloupec s DPF; odhalil to az vyrenderovany snimek.
 *
 * Stranka ma vzdy plnou vysku, takze se prumer uz nemeni podle navigace.
 * Lista prekryje spodnich 124 px, ale tam maji oba oblouky stejne mezeru
 * (kresli se jen 270 stupnu od 135) a cisla uprostred zustanou videt. */
static void layout_hlavni(void)
{
    if (!arc_speed) return;
    const lv_coord_t margin = 16;
    lv_coord_t h = PAGE_H;
    lv_coord_t d_h = h - 16;                          /* limit vyskou */
    lv_coord_t d_w = (CONT_W - 3 * margin) / 2;       /* limit sirkou */
    lv_coord_t d = d_h < d_w ? d_h : d_w;
    if (d < 150) d = 150;
    lv_coord_t y = (h - d) / 2;
    lv_coord_t gap = (CONT_W - 2 * d) / 3;            /* rovnomerne rozlozeni */
    lv_obj_set_size(arc_speed, d, d);
    lv_obj_set_size(arc_rpm, d, d);
    lv_obj_set_pos(arc_speed, gap, y);
    lv_obj_set_pos(arc_rpm, gap * 2 + d, y);
    lv_obj_align_to(lbl_speed_v, arc_speed, LV_ALIGN_CENTER, 0, -12);
    lv_obj_align_to(lbl_speed_u, arc_speed, LV_ALIGN_CENTER, 0, 34);
    lv_obj_align_to(lbl_rpm_v, arc_rpm, LV_ALIGN_CENTER, 0, -12);
    lv_obj_align_to(lbl_rpm_u, arc_rpm, LV_ALIGN_CENTER, 0, 34);
}

static void build_hlavni(lv_obj_t *scr)
{
    lv_obj_t *p = page[PG_HLAVNI] = mk_page(scr);
    arc_speed = mk_arc(p, 30, 10, 350, 0, 160, C_ACTUAL, 22);
    arc_rpm   = mk_arc(p, 390, 10, 350, 0, 5000, C_DEMAND, 22);
    lbl_speed_v = mk_lbl(p, "0", UI_FONT_48, C_ACTUAL, 0, 0);
    /* stejne jako tlacitka v nastaveni: popisek patri z cfg, ne natvrdo -
     * jinak po startu hlasi km/h, i kdyz se rychlost uz prepocitava na mph */
    lbl_speed_u = mk_lbl(p, cfg.mph ? "mph" : "km/h", UI_FONT_20,
                         C_LABEL, 0, 0);
    lbl_rpm_v   = mk_lbl(p, "0", UI_FONT_48, C_DEMAND, 0, 0);
    lbl_rpm_u = mk_lbl(p, T(STR_UNIT_RPM), UI_FONT_20, C_LABEL, 0, 0);
    layout_hlavni();
}

static void build_motor(lv_obj_t *scr)
{
    lv_obj_t *p = page[PG_MOTOR] = mk_page(scr);
    arc_boost = mk_arc(p, 14, 6, 230, 800, 2900, C_ACTUAL, 18);
    lbl_boost = mk_lbl(p, "----", UI_FONT_40, C_ACTUAL, 62, 96);
    mk_lbl(p, "mbar", UI_FONT_16, C_LABEL, 106, 142);
    mk_lbl(p, T(STR_BOOST_PRESSURE), UI_FONT_14, C_LABEL, 76, 240);

    mk_lbl(p, T(STR_DEMAND), UI_FONT_14, C_LABEL, 290, 10);
    lbl_boost_sp = mk_lbl(p, "----", UI_FONT_36, C_DEMAND, 290, 30);
    mk_lbl(p, "VNT", UI_FONT_14, C_LABEL, 290, 90);
    lbl_vnt = mk_lbl(p, "--", UI_FONT_36, C_BRIGHT, 290, 110);
    mk_lbl(p, "RAIL bar", UI_FONT_14, C_LABEL, 290, 170);
    lbl_railp = mk_lbl(p, "---", UI_FONT_36, C_BRIGHT, 290, 190);
    mk_lbl(p, T(STR_SIM_DATA), UI_FONT_14, C_DEFICIT, 540, 10);
}

static void build_tlak(lv_obj_t *scr)
{
    lv_obj_t *p = page[PG_TLAK] = mk_page(scr);
    mk_lbl(p, T(STR_GAP_MBAR), UI_FONT_14, C_LABEL, 560, 8);
    lbl_gap = mk_lbl(p, "--", UI_FONT_48, C_SAGE, 560, 28);
    chart = lv_chart_create(p);
    lv_obj_set_pos(chart, 12, 10);
    lv_obj_set_size(chart, 530, 230);
    lv_obj_set_style_bg_color(chart, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_border_color(chart, lv_color_hex(C_RULE), 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_radius(chart, 0, 0);
    lv_obj_set_style_line_color(chart, lv_color_hex(C_RULE), LV_PART_MAIN);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 100);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 800, 2900);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart, 5, 8);
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);
    ser_sp = lv_chart_add_series(chart, lv_color_hex(C_DEMAND), LV_CHART_AXIS_PRIMARY_Y);
    ser_act = lv_chart_add_series(chart, lv_color_hex(C_ACTUAL), LV_CHART_AXIS_PRIMARY_Y);
    mk_lbl(p, T(STR_LEGEND_DEMAND), UI_FONT_14, C_DEMAND, 560, 100);
    mk_lbl(p, T(STR_LEGEND_ACTUAL), UI_FONT_14, C_ACTUAL, 560, 124);
}

static void build_chyby(lv_obj_t *scr)
{
    lv_obj_t *p = page[PG_CHYBY] = mk_page(scr);
    mk_lbl(p, T(STR_NO_CAN), UI_FONT_28, C_BRIGHT, 20, 12);
    mk_lbl(p, T(STR_DTC_ALL), UI_FONT_20, C_DEMAND, 20, 70);
    mk_lbl(p, T(STR_DTC_CURRENT), UI_FONT_20, C_DEMAND, 20, 106);
    mk_lbl(p, T(STR_DTC_FREEZE), UI_FONT_20, C_DEMAND, 20, 142);
    mk_lbl(p, T(STR_DTC_STATS), UI_FONT_20, C_DEMAND, 20, 178);
}

static void build_dpf(lv_obj_t *scr)
{
    lv_obj_t *p = page[PG_DPF] = mk_page(scr);
    mk_lbl(p, T(STR_DPF_TEMP_IN), UI_FONT_14, C_LABEL, 20, 10);
    mk_lbl(p, "-- C", UI_FONT_36, C_BRIGHT, 20, 30);
    mk_lbl(p, T(STR_DPF_TEMP_OUT), UI_FONT_14, C_LABEL, 240, 10);
    mk_lbl(p, "-- C", UI_FONT_36, C_BRIGHT, 240, 30);
    mk_button(p, 20, 110, 340, 130, T(STR_DPF_REGEN_BTN),
              UI_FONT_24, C_DEFICIT, regen_cb, NULL);

    /* Rucni prepinac stavu regenerace, dokud neni CAN: jinak by nebylo
     * jak si stav na sloupci prohlednout a odladit. Az bude vrstva CAN,
     * prepne stav jednotka a tohle tlacitko zmizi. */
    mk_lbl(p, T(STR_REGEN_STATE), UI_FONT_14, C_LABEL, 390, 90);
    btn_regen_state = mk_button(p, 390, 110, 300, 130, "", UI_FONT_24,
                                C_ACTUAL, regen_state_cb, NULL);
    regen_demo_label();

    /* Nize uz nez 250 px to nejde: od 292 px zacina prekryv navigace. */
    mk_lbl(p, T(STR_DPF_NO_VEHICLE), UI_FONT_16, C_LABEL, 20, 250);
}

/* Nastaveni ma tri rady. Prvni ctyri tlacitka prepinaji jednu hodnotu
 * tuknutim, druha rada otevira dialogy a prepina den/noc, treti nese
 * vypinac displeje a diagnostiku. Nic se nesmi dostat pod 292 px: od tam
 * dolu to prekryva vysunuta lista stranek. */
#define SET_BW      160
#define SET_BH      76
#define SET_PITCH   176
#define SET_X(i)    (20 + (i) * SET_PITCH)
#define SET_ROW2    132         /* druha rada tlacitek */
#define SET_ROW3    216         /* treti rada, konci presne na 292 */

static void build_nastav(lv_obj_t *scr)
{
    lv_obj_t *p = page[PG_NASTAV] = mk_page(scr);
    const lv_coord_t BW = SET_BW, BH = SET_BH;
    char b[24];

    mk_lbl(p, T(STR_SET_SPEED), UI_FONT_14, C_LABEL, SET_X(0), 8);
    btn_units_v = mk_button(p, SET_X(0), 28, BW, BH, cfg.mph ? "mph" : "km/h",
                            UI_FONT_24, C_DEMAND, units_v_cb, NULL);
    mk_lbl(p, T(STR_SET_TEMP), UI_FONT_14, C_LABEL, SET_X(1), 8);
    btn_units_t = mk_button(p, SET_X(1), 28, BW, BH, cfg.fahrenheit ? "F" : "C",
                            UI_FONT_24, C_DEMAND, units_t_cb, NULL);
    mk_lbl(p, T(STR_SET_BLANK), UI_FONT_14, C_LABEL, SET_X(2), 8);
    blank_label(b, sizeof(b), cfg.blank_s);
    btn_blank = mk_button(p, SET_X(2), 28, BW, BH, b,
                          UI_FONT_24, C_DEMAND, blank_cb, NULL);
    mk_lbl(p, T(STR_SET_LANG), UI_FONT_14, C_LABEL, SET_X(3), 8);
    mk_button(p, SET_X(3), 28, BW, BH, i18n_name(cfg.lang), UI_FONT_24,
              C_SAGE, lang_cb, NULL);

    mk_lbl(p, "WIFI", UI_FONT_14, C_LABEL, 20, SET_ROW2 - 20);
    mk_button(p, 20, SET_ROW2, 320, BH, T(STR_WIFI_PICK), UI_FONT_24,
              C_DEMAND, wifi_scan_cb, NULL);

    /* Jas. Kdyz deska stmivat neumi, nevznikne ani jedno tlacitko a na
     * jejich miste je vysvetleni proc - predstirat ovladani, ktere nic
     * nedela, by bylo horsi nez ho nemit. */
    mk_lbl(p, T(STR_SET_BRIGHT), UI_FONT_14, C_LABEL, 360, SET_ROW2 - 20);
    btn_bright = NULL;
    btn_daynight = NULL;
    if (bl_can_dim()) {
        btn_bright = mk_button(p, 360, SET_ROW2, BW, BH, "", UI_FONT_24,
                               C_ACTUAL, bright_cb, NULL);
        bright_btn_paint();
        mk_lbl(p, T(STR_SET_DAYNIGHT), UI_FONT_14, C_LABEL, 540, SET_ROW2 - 20);
        btn_daynight = mk_button(p, 540, SET_ROW2, 170, BH, "", UI_FONT_24,
                                 C_DEMAND, daynight_cb, NULL);
        daynight_btn_paint();
    } else {
        lv_obj_t *na = mk_lbl(p, T(STR_BRIGHT_NA), UI_FONT_14, C_LABEL,
                              360, SET_ROW2);
        lv_label_set_long_mode(na, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(na, 350);
    }

    lbl_wifi = mk_lbl(p, T(STR_WIFI_NOT_CONN), UI_FONT_20, C_LABEL,
                      20, SET_ROW3);
    lbl_heap = mk_lbl(p, "", UI_FONT_16, C_LABEL, 20, SET_ROW3 + 36);

    /* Primy vypinac podsviceni. Probuzeni je dotekem kamkoliv, stejne jako
     * u automatickeho zhasnuti - resi to ui_tick() pres necinnost LVGL.
     * Zhasina natvrdo, bez stmivani: hlavni duvod existence je merit na
     * ploskach BL/GND a tam je potreba znamy stav, ne prejezd. */
    mk_button(p, 360, SET_ROW3, 320, BH, T(STR_SET_DISPLAY_OFF), UI_FONT_24,
              C_ACTUAL, display_off_cb, NULL);
}

/* Sloupec je vysoky jako stranka, ale jeho obsah musi zustat nad uchytem
 * navigace - ten je videt porad. Listu a mrizku UI naopak prekryt necha:
 * obe samy spadnou po peti vterinach a posouvat kvuli nim hodnotu vys by
 * znamenalo drzet volnych 300 px, ktere jsou jinak k nicemu. */
static const int RAIL_TICK_G[2] = {12, 18};

static void layout_rail(void)
{
    if (!rail_bar) return;
    lv_coord_t h = PAGE_H - NAV_HANDLE_H;
    lv_coord_t bh = h - RAIL_TOP - RAIL_BOT - RAIL_VAL_H;
    if (bh < 40) bh = 40;
    lv_obj_set_size(rail_rule, 1, PAGE_H);      /* vlasova linka po cele vysce */
    lv_obj_set_pos(rail_bar, RAIL_BAR_X, RAIL_TOP);
    lv_obj_set_size(rail_bar, RAIL_BAR_W, bh);
    lv_obj_set_pos(rail_val, 0, h - RAIL_VAL_H);

    /* Vypln je o RAIL_PAD uzsi a nizsi nez obrys, rysky musi sedet na ni.
     * Dve rysky na kazdou mez: jedna zleva, jedna zprava. Doprostred baru
     * nedosahnou, takze vypln nikdy neprerusi, a protoze presahuji ven na
     * pozadi, jsou citelne i kdyz je bar plny. */
    lv_coord_t iy = RAIL_TOP + RAIL_PAD;
    lv_coord_t ih = bh - 2 * RAIL_PAD;
    for (int i = 0; i < 4; i++) {
        lv_coord_t y = iy + ih - (lv_coord_t)((ih * RAIL_TICK_G[i / 2]) / RAIL_MAX_G)
                       - RAIL_TICK_H / 2;
        lv_coord_t x = (i & 1) ? RAIL_BAR_X + RAIL_BAR_W / 2 + RAIL_TICK_MID / 2
                               : RAIL_BAR_X - RAIL_TICK_OUT;
        lv_obj_set_pos(rail_tick[i], x, y);
    }
}

/* ---------------- chrome ---------------- */
static void build_chrome(lv_obj_t *scr)
{
    lv_obj_t *bar = mk_panel(scr, 0, 0, 800, TOP_H);
    mk_lbl(bar, "TALENTO", UI_FONT_20, C_BRIGHT, 16, 20);
    mk_lbl(bar, T(STR_TOP_TEMP), UI_FONT_14, C_LABEL, 320, 8);
    lbl_temp = mk_lbl(bar, "--", UI_FONT_32, C_BRIGHT, 320, 24);
    lbl_state = mk_lbl(bar, "DEMO", UI_FONT_16, C_DEFICIT, 640, 22);

    /* Zadny panel: stejne pozadi jako obsah, zadny ramecek. Oddeluje
     * jen vlasova linka po leve hrane. */
    rail = lv_obj_create(scr);
    lv_obj_set_pos(rail, CONT_W, TOP_H);
    lv_obj_set_size(rail, RAIL_W, PAGE_H);
    lv_obj_set_style_bg_color(rail, lv_color_hex(C_INK), 0);
    lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rail, 0, 0);
    lv_obj_set_style_radius(rail, 0, 0);
    lv_obj_set_style_pad_all(rail, 0, 0);
    lv_obj_clear_flag(rail, LV_OBJ_FLAG_SCROLLABLE);

    rail_rule = lv_obj_create(rail);
    lv_obj_set_pos(rail_rule, 0, 0);
    lv_obj_set_style_bg_color(rail_rule, lv_color_hex(C_RULE), 0);
    lv_obj_set_style_bg_opa(rail_rule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rail_rule, 0, 0);
    lv_obj_set_style_radius(rail_rule, 0, 0);

    lv_obj_t *rail_lbl = mk_lbl(rail, "DPF", UI_FONT_14, C_LABEL, 0, 4);
    lv_obj_set_width(rail_lbl, RAIL_W);
    lv_obj_set_style_text_align(rail_lbl, LV_TEXT_ALIGN_CENTER, 0);

    /* Oramovany bar: obrys 1 px, vnitrek prusvitny, vypln roste odspodu.
     * Vypln se ridi jen odsazenim (lv_bar ramecek neodecita), proto
     * RAIL_PAD - jinak by prekryla obrys. */
    rail_bar = lv_bar_create(rail);
    lv_bar_set_range(rail_bar, 0, 100 * RAIL_MAX_G);
    lv_obj_set_style_bg_opa(rail_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(rail_bar, lv_color_hex(C_RULE), LV_PART_MAIN);
    lv_obj_set_style_border_width(rail_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rail_bar, RAIL_PAD, LV_PART_MAIN);
    lv_obj_set_style_radius(rail_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rail_bar, lv_color_hex(C_SAGE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(rail_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(rail_bar, 0, LV_PART_INDICATOR);

    /* Rysky vznikaji az po baru, takze se kresli nad vypln. */
    for (int i = 0; i < 4; i++) {
        lv_obj_t *t = rail_tick[i] = lv_obj_create(rail);
        lv_obj_set_size(t, RAIL_TICK_W, RAIL_TICK_H);
        lv_obj_set_style_bg_color(t, lv_color_hex(C_LABEL), 0);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(t, 0, 0);
        lv_obj_set_style_radius(t, 0, 0);
    }

    rail_val = mk_lbl(rail, "--", UI_FONT_24, C_SAGE, 0, 0);
    lv_obj_set_width(rail_val, RAIL_W);
    lv_obj_set_style_text_align(rail_val, LV_TEXT_ALIGN_CENTER, 0);
    layout_rail();

    /* uchyt - to, co je videt vetsinu casu */
    navhandle = mk_panel(scr, 0, 480 - NAV_HANDLE_H, 800, NAV_HANDLE_H);
    lv_obj_add_flag(navhandle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(navhandle, lv_color_hex(C_RULE), LV_STATE_PRESSED);
    nav_attach_drag(navhandle);
    lv_obj_add_event_cb(navhandle, drag_guard_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(navhandle, handle_cb, LV_EVENT_CLICKED, NULL);
    /* dve kratke carky nad sebou = univerzalni znacka "da se vytahnout" */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *d = lv_obj_create(navhandle);
        lv_obj_set_size(d, 86, 4);
        lv_obj_set_pos(d, (800 - 86) / 2, 16 + i * 10);
        lv_obj_set_style_bg_color(d, lv_color_hex(C_LABEL), 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_radius(d, 2, 0);
        /* jen ozdoba - dotek patri uchytu pod ni, jinak by se z carek
         * nedalo tahnout */
        lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
    }

    /* lista se sesti strankami */
    navbar = mk_panel(scr, 0, 480 - NAV_H, 800, NAV_H);
    nav_attach_drag(navbar);
    for (int i = 0; i < N_PAGES; i++) {
        navbtn[i] = mk_button(navbar, 4 + i * 132, 6, 128, NAV_H - 14,
                              T(PAGE_STR[i]), UI_FONT_20,
                              C_DEMAND, nav_cb, (void *)(intptr_t)i);
        /* Aby slo tahat i z tlacitka, musi se jeho dotyk dostat na listu.
         * Klik si tlacitko vyridi samo a hlidac tahu v mk_button() ho pri
         * tazeni zahodi, takze stranka se neprepne. */
        lv_obj_add_flag(navbtn[i], LV_OBJ_FLAG_EVENT_BUBBLE);
    }

    /* mrizka 3 rady x 6 sloupcu pro budouci ikony - zatim prazdna */
    navgrid = mk_panel(scr, 0, 480 - NAV_GRID_H, 800, NAV_GRID_H);
    nav_attach_drag(navgrid);
    mk_lbl(navgrid, T(STR_NAV_PLACEHOLDER), UI_FONT_14,
           C_LABEL, 16, 6);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 6; c++) {
            lv_obj_t *t = mk_panel(navgrid, 6 + c * 132, 28 + r * 90, 126, 84);
            lv_obj_set_style_bg_color(t, lv_color_hex(C_INK), 0);
            lv_obj_set_style_border_color(t, lv_color_hex(C_RULE), 0);
            /* az dlazdice neco budou delat, pujdou z nich tahat stejne
             * jako z tlacitek stranek */
            lv_obj_add_flag(t, LV_OBJ_FLAG_EVENT_BUBBLE);
        }

    set_nav_state(0);
}

/* ---------------- obnovovani ---------------- */
static void sample(void)
{
    sim_t += 0.05f;
    v_speed.target    = 55 + 45 * sinf(sim_t * 0.35f);
    v_rpm.target      = 1500 + 1300 * (0.5f + 0.5f * sinf(sim_t * 0.6f));
    v_temp.target     = 86 + 2 * sinf(sim_t * 0.1f);
    v_boost_sp.target = 1000 + 1700 * (0.5f + 0.5f * sinf(sim_t * 0.6f));
    v_vnt.target      = 45 + 35 * (0.5f + 0.5f * sinf(sim_t * 0.6f));
    v_rail.target     = 400 + 1600 * (0.5f + 0.5f * sinf(sim_t * 0.6f));
    v_soot.target     = 876 + 120 * sinf(sim_t * 0.08f);
    v_boost.target   += (v_boost_sp.target - v_boost.target) * 0.10f;
}

void ui_tick(void)
{
    uint32_t now = lv_tick_get();
    if (now - last_sample >= 50) {
        last_sample = now;
        sample();
        lv_chart_set_next_value(chart, ser_sp, (lv_coord_t)v_boost_sp.target);
        lv_chart_set_next_value(chart, ser_act, (lv_coord_t)v_boost.target);
    }
    approach(&v_speed, .25f); approach(&v_rpm, .25f); approach(&v_temp, .10f);
    approach(&v_boost, .30f); approach(&v_boost_sp, .30f);
    approach(&v_vnt, .25f);   approach(&v_rail, .25f); approach(&v_soot, .05f);

    float sp = cfg.mph ? v_speed.shown * 0.621371f : v_speed.shown;
    float tp = cfg.fahrenheit ? v_temp.shown * 1.8f + 32.0f : v_temp.shown;
    lv_label_set_text_fmt(lbl_temp, "%d", (int)tp);

    /* ----- pravy sloupec: nasyceni DPF a stav regenerace ----- */
    int soot = (int)v_soot.shown;
    uint32_t sc = soot > 1800 ? C_DEFICIT : (soot > 1200 ? C_ACTUAL : C_SAGE);
    uint32_t bg = C_INK;

    if (regen_state != UI_REGEN_OFF) {
        uint32_t el = now - regen_since;
        bg = C_REGEN_DIM;               /* pokus o spusteni: jen podkres */
        if (regen_state == UI_REGEN_ACTIVE) {
            if (regen_demo == 3) {      /* rucni ukazka misto dat z CAN */
                regen_pct = (int)(el * 100 / RGN_SIM_MS);
                if (regen_pct > 100) regen_pct = 100;
            }
            int done = regen_pct;
            if (done < 0) {
                int step = (int)(el / RGN_STEP_MS);
                done = (step > 4 ? 4 : step) * 20;
            } else if (done > 100) done = 100;
            soot = (int)(regen_soot0 * (float)(100 - done) / 100.0f);
            sc = C_DEFICIT;
            if ((el / RGN_BLINK_MS) & 1) bg = C_REGEN_HOT;
        }
    }
    if (bg != rail_bg_shown) {          /* prekreslovat jen pri zmene */
        rail_bg_shown = bg;
        lv_obj_set_style_bg_color(rail, lv_color_hex(bg), 0);
    }
    lv_bar_set_value(rail_bar, soot, LV_ANIM_OFF);
    lv_label_set_text_fmt(rail_val, "%d.%d", soot / 100, (soot / 10) % 10);
    lv_obj_set_style_bg_color(rail_bar, lv_color_hex(sc), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(rail_val, lv_color_hex(sc), 0);

    switch (cur_page) {
    case PG_HLAVNI:
        lv_arc_set_value(arc_speed, (int)sp);
        lv_arc_set_value(arc_rpm, (int)v_rpm.shown);
        lv_label_set_text_fmt(lbl_speed_v, "%d", (int)sp);
        lv_label_set_text_fmt(lbl_rpm_v, "%d", (int)v_rpm.shown);
        break;
    case PG_MOTOR:
        lv_arc_set_value(arc_boost, (int)v_boost.shown);
        lv_label_set_text_fmt(lbl_boost, "%d", (int)v_boost.shown);
        lv_label_set_text_fmt(lbl_boost_sp, "%d", (int)v_boost_sp.shown);
        lv_label_set_text_fmt(lbl_vnt, "%d %%", (int)v_vnt.shown);
        lv_label_set_text_fmt(lbl_railp, "%d", (int)v_rail.shown);
        break;
    case PG_TLAK: {
        int gap = (int)(v_boost_sp.shown - v_boost.shown);
        lv_label_set_text_fmt(lbl_gap, "%d", gap);
        lv_obj_set_style_text_color(lbl_gap,
            lv_color_hex(gap > 300 ? C_DEFICIT : C_SAGE), 0);
        break; }
    case PG_NASTAV:
        lv_label_set_text_fmt(lbl_heap, "heap %u kB   PSRAM %u kB   %s",
            (host && host->heap_kb) ? host->heap_kb() : 0u,
            (host && host->psram_kb) ? host->psram_kb() : 0u,
            host_wifi_ip());
        if (host_wifi_connected()) {
            lv_label_set_text(lbl_wifi, host_wifi_ssid());
            lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(C_SAGE), 0);
        } else {
            lv_label_set_text(lbl_wifi, T(STR_WIFI_NOT_CONN));
            lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(C_LABEL), 0);
        }
        break;
    }

    wifi_poll();

    /* Lista i dlazdice spadnou samy zpet na uchyt, aby nekradly plochu.
     * Dotek kamkoliv po obrazovce odpocet resetuje. */
    if (nav_state > 0) {
        if (lv_disp_get_inactive_time(NULL) < 300) nav_shown_ms = now;
        if (now - nav_shown_ms > NAV_IDLE_MS) set_nav_state(0);
    }

    /* Zhasinani displeje po necinnosti. lv_disp_get_inactive_time pocita
     * od posledni akce uzivatele, takze resi probuzeni i usnuti jednim
     * merenim. Kdyz jde jas regulovat, zhasnuti zacina pulvterinovym
     * stahnutim jasu (blank_begin/blank_step) a teprve pak padne panel;
     * probuzeni je naopak okamzite, bez jakehokoli prejezdu. */
    uint32_t idle = lv_disp_get_inactive_time(NULL);
    if (idle < 300 && (!backlight_on || bl_fading)) {
        /* ochranna lhuta po rucnim zhasnuti - jinak by displej probudil
         * tentyz dotek, kterym se zhasinalo */
        if (now - blank_guard_ms > 1500) backlight(true);
    } else if (cfg.blank_s && backlight_on && idle > cfg.blank_s * 1000UL) {
        blank_begin();
    }
    blank_step();
}

/* ---------------- stavba ---------------- */
void ui_build(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_INK), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, gesture_cb, LV_EVENT_GESTURE, NULL);
    /* zacatek tahu potrebujeme pro rozliseni gesta od leveho okraje */
    lv_obj_add_event_cb(scr, press_cb, LV_EVENT_PRESSED, NULL);

    build_chrome(scr);
    build_hlavni(scr);
    build_motor(scr);
    build_tlak(scr);
    build_chyby(scr);
    build_dpf(scr);
    build_nastav(scr);
    ui_show_page(PG_HLAVNI);

    /* Navigace je prekryv, takze musi byt v z-poradi nad strankami.
     * Stranky vznikaji az po chrome, takze bez tohohle by se lista
     * kreslila pod ne a pri vysunuti by nebyla videt. */
    lv_obj_move_foreground(navbar);
    lv_obj_move_foreground(navgrid);
    lv_obj_move_foreground(navhandle);

    /* Jas z NVS musi platit od prvniho snimku, ne az od prvniho tuknuti
     * do nastaveni. Deska uz PWM nastavenou ma, tohle do ni jen posle
     * ulozenou uroven prislusneho rezimu. */
    bl_apply();

    /* Jen pri prvni stavbe. Po zmene jazyka se UI stavi znovu a namerene
     * hodnoty si musi zustat tam, kde byly - jinak by plnici tlak skocilo
     * zpatky na vychozi a graf by melo zub. */
    static bool first = true;
    if (first) {
        first = false;
        v_boost.shown = v_boost.target = 1000;
    }
}
