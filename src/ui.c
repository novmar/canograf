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

/* ---------------- rozvrzeni ---------------- */
#define TOP_H   64
#define NAV_H   124             /* 14,5 mm */
#define RAIL_W  64
#define CONT_W  (800 - RAIL_W)

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
static bool nav_visible = true;

/* ---------------- trvale prvky ---------------- */
static lv_obj_t *lbl_temp, *lbl_state, *rail_bar, *rail_val;
static lv_obj_t *rail_rule, *rail_tick[4];
/* hlavni */
static lv_obj_t *arc_speed, *arc_rpm, *lbl_speed_v, *lbl_rpm_v, *lbl_speed_u, *lbl_rpm_u;
/* motor */
static lv_obj_t *arc_boost, *lbl_boost, *lbl_boost_sp, *lbl_vnt, *lbl_railp;
/* tlak */
static lv_obj_t *chart, *lbl_gap;
static lv_chart_series_t *ser_sp, *ser_act;
/* nastaveni */
static lv_obj_t *btn_units_v, *btn_units_t, *btn_blank, *lbl_wifi, *lbl_heap;

/* ---------------- deska ---------------- */
static const ui_host_t *host = NULL;

void ui_set_host(const ui_host_t *h) { host = h; }

static bool host_pwm_running(void)
{
    return (host && host->bl_pwm_running) ? host->bl_pwm_running() : false;
}

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

/* ---------------- nastaveni ---------------- */
static struct {
    bool mph;                   /* false = km/h */
    bool fahrenheit;            /* false = C    */
    uint32_t blank_s;           /* 0 = nikdy zhasinat */
    int lang;                   /* index do tabulky v i18n.c, 0 = English */
} cfg;

void ui_set_cfg(bool mph, bool fahrenheit, uint32_t blank_s, int lang)
{
    cfg.mph = mph;
    cfg.fahrenheit = fahrenheit;
    cfg.blank_s = blank_s;
    /* i18n_set() necha neplatny index byt, takze cfg.lang musi projit
     * stejnou kontrolou - jinak by ui_rebuild() cyklovalo od nesmyslu. */
    i18n_set(lang);
    cfg.lang = i18n_get();
}

static bool backlight_on = true;

/* ---------------- hodnoty ---------------- */
typedef struct { float target, shown; } val_t;
static val_t v_speed, v_rpm, v_temp, v_boost, v_boost_sp, v_vnt, v_rail, v_soot;
static float sim_t = 0;
static uint32_t last_sample = 0;
static inline void approach(val_t *v, float k) { v->shown += (v->target - v->shown) * k; }

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
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
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

/* ---------------- podsviceni ---------------- */
static void backlight(bool on)
{
    if (!host || !host->backlight || on == backlight_on) return;
    host->backlight(on);
    backlight_on = on;
}

/* ---------------------------------------------------------------------
 * Navigace ma tri stavy, prepinaji se tazenim nahoru:
 *   0  jen uchyt  - tenky pruh s "=" uprostred, jako na Androidu
 *   1  lista      - sest tlacitek stranek; po 5 s necinnosti spadne na 0
 *   2  dlazdice   - mrizka 3x6 pro budouci ikony, zatim prazdna
 * Tazeni dolu jde vzdy o stupen zpet.
 * ------------------------------------------------------------------- */
#define NAV_HANDLE_H  44
#define NAV_GRID_H    300
#define NAV_IDLE_MS   5000

static lv_obj_t *navhandle, *navgrid;
static int nav_state = 0;
static uint32_t nav_shown_ms = 0;

static void layout_hlavni(void);
static void layout_rail(lv_coord_t h);

static lv_coord_t nav_height(void)
{
    switch (nav_state) {
        case 1:  return NAV_H;
        case 2:  return NAV_GRID_H;
        default: return NAV_HANDLE_H;
    }
}

static void set_nav_state(int s)
{
    if (s < 0) s = 0;
    if (s > 2) s = 2;
    nav_state = s;
    nav_visible = (s > 0);
    nav_shown_ms = lv_tick_get();

    if (s == 0) lv_obj_clear_flag(navhandle, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_add_flag(navhandle, LV_OBJ_FLAG_HIDDEN);
    if (s == 1) lv_obj_clear_flag(navbar, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_add_flag(navbar, LV_OBJ_FLAG_HIDDEN);
    if (s == 2) lv_obj_clear_flag(navgrid, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_add_flag(navgrid, LV_OBJ_FLAG_HIDDEN);

    lv_coord_t h = 480 - TOP_H - nav_height();
    /* build_chrome() vola set_nav_state(0) jeste pred stavbou stranek,
     * takze pri prvnim pruchodu je pole stranek prazdne - NULL preskocit.
     * Stranky si do te doby nechavaji vysku z mk_page(). */
    for (int i = 0; i < N_PAGES; i++)
        if (page[i]) lv_obj_set_size(page[i], CONT_W, h);
    lv_obj_set_size(rail, RAIL_W, h);
    layout_rail(h);
    layout_hlavni();
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

/* Zacatek tahu si pamatujeme kvuli gestu od leveho okraje = zpet,
 * jak je zvykem na Androidu. */
static lv_coord_t press_x = 0;
static void press_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *in = lv_indev_get_act();
    if (!in) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    press_x = p.x;
}

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
    switch (d) {
        case LV_DIR_RIGHT:
            /* tah od leveho okraje = zpet na hlavni obrazovku */
            if (press_x < 70 && cur_page != PG_HLAVNI) ui_show_page(PG_HLAVNI);
            else ui_show_page(cur_page + 1);
            break;
        case LV_DIR_LEFT:  ui_show_page(cur_page - 1); break;
        case LV_DIR_TOP:    set_nav_state(nav_state + 1); break;
        case LV_DIR_BOTTOM: set_nav_state(nav_state - 1); break;
        default: break;
    }
}

/* ---------------- modal ---------------- */
static lv_obj_t *modal = NULL;
static void modal_close_cb(lv_event_t *e) { (void)e; if (modal) { lv_obj_del(modal); modal = NULL; } }

static lv_obj_t *modal_open(const char *title, uint32_t accent)
{
    if (modal) lv_obj_del(modal);
    modal = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(modal, 0, 0); lv_obj_set_size(modal, 800, 480);
    lv_obj_set_style_bg_color(modal, lv_color_hex(C_INK), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_radius(modal, 0, 0);
    lv_obj_set_style_pad_all(modal, 0, 0);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
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
 * Proti show_info() ma tri veci navic:
 *   - dve STEJNE VELIKA tlacitka. Kdyby bylo zruseni mensi, cetlo by se jako
 *     druhoradá volba, a u akce, ktera umi poskodit vuz, to je spatne.
 *   - potvrzeni se musi DRZET 3 vteriny a odpocet je videt primo na tlacitku.
 *     Omylem se takove tlacitko stisknout neda.
 *   - gesto od leveho okraje dialog zavre, stejne jako jinde v UI
 * ------------------------------------------------------------------- */
#define HOLD_MS 3000

static lv_obj_t *hold_btn_lbl = NULL;
static lv_timer_t *hold_timer = NULL;
static uint32_t hold_start = 0;
static lv_event_cb_t hold_action = NULL;

static void hold_reset(void)
{
    if (hold_timer) { lv_timer_del(hold_timer); hold_timer = NULL; }
    if (hold_btn_lbl) {
        char b[32];
        snprintf(b, sizeof(b), T(STR_HOLD_FMT), HOLD_MS / 1000);
        lv_label_set_text(hold_btn_lbl, b);
    }
}

static void hold_tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t el = lv_tick_get() - hold_start;
    if (el >= HOLD_MS) {
        lv_event_cb_t a = hold_action;
        hold_reset();
        if (a) a(NULL);
        else modal_close_cb(NULL);
        return;
    }
    if (hold_btn_lbl) {
        char b[32];
        snprintf(b, sizeof(b), T(STR_HOLD_FMT),
                 (int)((HOLD_MS - el + 999) / 1000));
        lv_label_set_text(hold_btn_lbl, b);
    }
}

static void hold_press_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_PRESSED) {
        hold_start = lv_tick_get();
        if (!hold_timer) hold_timer = lv_timer_create(hold_tick_cb, 100, NULL);
    } else {
        hold_reset();       /* pusteni driv = zacina se znovu */
    }
}

static void modal_gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (d == LV_DIR_RIGHT) { hold_reset(); modal_close_cb(NULL); }
}

static void show_confirm(const char *title, const char *body,
                         uint32_t accent, lv_event_cb_t on_ok)
{
    lv_obj_t *m = modal_open(title, accent);
    lv_obj_add_flag(m, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(m, modal_gesture_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_t *b = mk_lbl(m, body, UI_FONT_20, C_LABEL, 30, 100);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, 740);

    hold_action = on_ok;
    char cap[32];
    snprintf(cap, sizeof(cap), T(STR_HOLD_FMT), HOLD_MS / 1000);
    lv_obj_t *ok = mk_button(m, 30, 330, 360, 120, cap, UI_FONT_32,
                             accent, NULL, NULL);
    hold_btn_lbl = lv_obj_get_child(ok, 0);
    lv_obj_add_event_cb(ok, hold_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ok, hold_press_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(ok, hold_press_cb, LV_EVENT_PRESS_LOST, NULL);

    mk_button(m, 410, 330, 360, 120, T(STR_CANCEL), UI_FONT_32,
              C_LABEL, modal_close_cb, NULL);
}

static void regen_cb(lv_event_t *e)
{
    (void)e;
    /* Zatim jen informuje - vozidlo neni pripojene. Az bude, sem prijde
     * skutecna akce; potvrzeni drzenim uz je na svem miste. */
    show_confirm(T(STR_REGEN_TITLE), T(STR_REGEN_BODY), C_DEFICIT, NULL);
}

/* ---------------- WiFi ---------------- */
static lv_obj_t *wifi_list = NULL, *wifi_ta = NULL, *wifi_kb = NULL;
static char wifi_ssid[33] = {0};
static int scan_state = 0;      /* 0 klid, 1 bezi, 2 hotovo */

static void wifi_save_connect(const char *ssid, const char *pass)
{
    if (host && host->wifi_connect) host->wifi_connect(ssid, pass);
}

static void kb_ok_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_READY) return;
    wifi_save_connect(wifi_ssid, lv_textarea_get_text(wifi_ta));
    modal_close_cb(e);
    wifi_list = NULL; wifi_ta = NULL; wifi_kb = NULL;
}

static void ssid_pick_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    strncpy(wifi_ssid, lv_list_get_btn_text(wifi_list, btn), sizeof(wifi_ssid) - 1);
    wifi_list = NULL;

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

static void wifi_scan_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *m = modal_open(T(STR_WIFI_TITLE), C_DEMAND);
    wifi_list = lv_list_create(m);
    lv_obj_set_pos(wifi_list, 30, 90);
    lv_obj_set_size(wifi_list, 600, 300);
    lv_obj_set_style_bg_color(wifi_list, lv_color_hex(C_PANEL), 0);
    lv_obj_t *l = lv_list_add_btn(wifi_list, NULL, T(STR_WIFI_SEARCHING));
    lv_obj_set_style_text_font(l, UI_FONT_20, 0);
    mk_button(m, 650, 90, 120, 120, T(STR_CLOSE), UI_FONT_20,
              C_DEMAND, modal_close_cb, NULL);
    if (host && host->wifi_scan_start) host->wifi_scan_start();
    scan_state = 1;
}

static void wifi_poll(void)
{
    if (scan_state != 1) return;
    if (!host || !host->wifi_scan_result || !host->wifi_scan_ssid) return;
    int n = host->wifi_scan_result();
    if (n < 0) return;                       /* -1 bezi, -2 nespusteno */
    scan_state = 2;
    if (!wifi_list) return;
    lv_obj_clean(wifi_list);
    for (int i = 0; i < n && i < 12; i++) {
        const char *ssid = host->wifi_scan_ssid(i);
        lv_obj_t *b = lv_list_add_btn(wifi_list, NULL, ssid ? ssid : "");
        lv_obj_set_style_text_font(b, UI_FONT_20, 0);
        lv_obj_set_style_min_height(b, 56, 0);
        lv_obj_add_event_cb(b, ssid_pick_cb, LV_EVENT_CLICKED, NULL);
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
    modal     = NULL;
    wifi_list = NULL;
    wifi_ta   = NULL;
    wifi_kb   = NULL;
    scan_state = 0;

    int keep_page = cur_page;
    int keep_nav  = nav_state;
    ui_build();
    ui_show_page(keep_page);
    set_nav_state(keep_nav);
}

/* ---------------- nastaveni: obsluha ---------------- */
static void save_cfg(void)
{
    if (host && host->save_cfg)
        host->save_cfg(cfg.mph, cfg.fahrenheit, cfg.blank_s, cfg.lang);
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
 * POKUS: softwarove PWM podsviceni pres I2C expander
 *
 * Podsviceni je na teto desce jen spinac (EXIO2 na CH422G), takze jedina
 * cesta k plynulemu jasu bez pajeni je klepat tim spinacem dost rychle.
 * Pri 100 Hz to je 200 I2C zapisu za sekundu, cili ~3 % zatizeni sbernice
 * - to neni problem. Problem muze byt viditelne blikani a to, ze AP3032
 * je boost menic, takze jas nemusi vyjit linearne a budic muze pistet.
 *
 * Samotne klepani resi deska (ui_host_t), tady zustava jen dialog.
 * ------------------------------------------------------------------- */
static int pwm_duty = 10;            /* 0..10, 10 = trvale sviti */
static int pwm_period_ms = 10;       /* 10 = 100 Hz, 20 = 50, 40 = 25 */
static uint32_t bl_toggle_us = 0;    /* zmerena doba jednoho prepnuti */

static void pwm_set_cb(lv_event_t *e)
{
    pwm_duty = (int)(intptr_t)lv_event_get_user_data(e);
    if (host && host->bl_pwm_set) host->bl_pwm_set(pwm_duty, pwm_period_ms);
}

static void pwm_stop_cb(lv_event_t *e)
{
    if (host && host->bl_pwm_stop) host->bl_pwm_stop();
    pwm_duty = 10;
    backlight_on = true;
    modal_close_cb(e);
}

static lv_obj_t *lbl_pwm_info = NULL, *btn_pwm_freq = NULL;

static void pwm_freq_cb(lv_event_t *e)
{
    (void)e;
    pwm_period_ms = (pwm_period_ms == 10) ? 20 : (pwm_period_ms == 20 ? 40 : 10);
    char b[24];
    snprintf(b, sizeof(b), "%d Hz", 1000 / pwm_period_ms);
    lv_label_set_text(lv_obj_get_child(btn_pwm_freq, 0), b);
    /* bezici klepani ma zmenu frekvence prevzit hned */
    if (host_pwm_running() && host->bl_pwm_set) host->bl_pwm_set(pwm_duty, pwm_period_ms);
}

static void bright_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *m = modal_open(T(STR_BRIGHT_TITLE), C_ACTUAL);

    if (host && host->bl_measure_toggle_us) bl_toggle_us = host->bl_measure_toggle_us();
    /* Preklad je delsi nez puvodni cestina bez hacku a v UTF-8 zabere
     * znak s diakritikou dva bajty - nemecka verze ma pres 200 bajtu. */
    char info[256];
    snprintf(info, sizeof(info), T(STR_BRIGHT_INFO),
             (unsigned)bl_toggle_us, pwm_period_ms, pwm_period_ms / 10);
    lbl_pwm_info = mk_lbl(m, info, UI_FONT_16, C_LABEL, 20, 80);
    lv_label_set_long_mode(lbl_pwm_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_pwm_info, 760);

    const int duty[5] = {10, 7, 5, 3, 1};
    const char *cap[5] = {"100 %", "70 %", "50 %", "30 %", "10 %"};
    for (int i = 0; i < 5; i++)
        mk_button(m, 16 + i * 128, 170, 120, 110, cap[i], UI_FONT_24,
                  C_ACTUAL, pwm_set_cb, (void *)(intptr_t)duty[i]);

    mk_lbl(m, T(STR_FREQUENCY), UI_FONT_14, C_LABEL, 660, 150);
    char fb[24];
    snprintf(fb, sizeof(fb), "%d Hz", 1000 / pwm_period_ms);
    btn_pwm_freq = mk_button(m, 660, 170, 124, 110, fb, UI_FONT_24,
                             C_DEMAND, pwm_freq_cb, NULL);

    mk_button(m, 16, 330, 380, 120, T(STR_FULL_BRIGHT), UI_FONT_24,
              C_SAGE, pwm_stop_cb, NULL);
}

/* ---------------- stranky ---------------- */
static lv_obj_t *mk_page(lv_obj_t *scr)
{
    lv_obj_t *p = lv_obj_create(scr);
    lv_obj_set_pos(p, 0, TOP_H);
    lv_obj_set_size(p, CONT_W, 480 - TOP_H - NAV_H);
    lv_obj_set_style_bg_color(p, lv_color_hex(C_INK), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

/* Prepocet velikosti dvou hlavnich ukazatelu. Prumer je omezeny jak vyskou
 * (kolik zbylo po navigaci), tak sirkou - dva oblouky vedle sebe se musi
 * vejit do CONT_W. Driv byl prumer napevno 360 px a pravy oblouk tim
 * prelezal pod pravy sloupec s DPF; odhalil to az vyrenderovany snimek. */
static void layout_hlavni(void)
{
    if (!arc_speed) return;
    const lv_coord_t margin = 16;
    lv_coord_t h = 480 - TOP_H - nav_height();
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
    mk_lbl(p, T(STR_DPF_NO_VEHICLE), UI_FONT_16, C_LABEL, 390, 150);
}

/* Prvni rada nastaveni: pet tlacitek vedle sebe. Rozestup je tesny,
 * protoze sesty sloupec uz by se do CONT_W nevesel - kdyby pribylo dalsi
 * nastaveni, bude potreba druha rada, ne uzsi tlacitka. */
#define SET_BW      130
#define SET_BH      76
#define SET_PITCH   143
#define SET_X(i)    (20 + (i) * SET_PITCH)

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
    mk_lbl(p, T(STR_SET_BRIGHT), UI_FONT_14, C_LABEL, SET_X(3), 8);
    mk_button(p, SET_X(3), 28, BW, BH, T(STR_PWM_TEST), UI_FONT_20,
              C_ACTUAL, bright_cb, NULL);
    mk_lbl(p, T(STR_SET_LANG), UI_FONT_14, C_LABEL, SET_X(4), 8);
    mk_button(p, SET_X(4), 28, BW, BH, i18n_name(cfg.lang), UI_FONT_24,
              C_SAGE, lang_cb, NULL);

    mk_lbl(p, "WIFI", UI_FONT_14, C_LABEL, 20, 120);
    lbl_wifi = mk_lbl(p, T(STR_WIFI_NOT_CONN), UI_FONT_24, C_LABEL, 20, 140);
    mk_button(p, 360, 120, 320, BH + 20, T(STR_WIFI_PICK), UI_FONT_24,
              C_DEMAND, wifi_scan_cb, NULL);

    /* Primy vypinac podsviceni. Probuzeni je dotekem kamkoliv, stejne jako
     * u automatickeho zhasnuti - resi to ui_tick() pres necinnost LVGL.
     * Hlavni duvod existence: merit na ploskach BL/GND se zhasnutym
     * podsvicenim, aniz by se cekalo na timeout. */
    mk_button(p, 20, 230, 320, BH, T(STR_SET_DISPLAY_OFF), UI_FONT_24,
              C_ACTUAL, display_off_cb, NULL);

    lbl_heap = mk_lbl(p, "", UI_FONT_16, C_LABEL, 20, 320);
}

/* Sloupec se musi vejit do toho, co po sobe nechala navigace, takze se
 * jeho obsah prepocitava stejne jako hlavni ukazatele. Bar dostane celou
 * volnou vysku, hodnota sedi uplne dole pod nim. */
static const int RAIL_TICK_G[2] = {12, 18};

static void layout_rail(lv_coord_t h)
{
    if (!rail_bar) return;
    lv_coord_t bh = h - RAIL_TOP - RAIL_BOT - RAIL_VAL_H;
    if (bh < 40) bh = 40;
    lv_obj_set_size(rail_rule, 1, h);
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
    lv_obj_set_size(rail, RAIL_W, 480 - TOP_H - NAV_H);
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
    layout_rail(480 - TOP_H - NAV_H);

    /* uchyt - to, co je videt vetsinu casu */
    navhandle = mk_panel(scr, 0, 480 - NAV_HANDLE_H, 800, NAV_HANDLE_H);
    lv_obj_add_flag(navhandle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(navhandle, lv_color_hex(C_RULE), LV_STATE_PRESSED);
    lv_obj_add_event_cb(navhandle, handle_cb, LV_EVENT_CLICKED, NULL);
    /* dve kratke carky nad sebou = univerzalni znacka "da se vytahnout" */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *d = lv_obj_create(navhandle);
        lv_obj_set_size(d, 86, 4);
        lv_obj_set_pos(d, (800 - 86) / 2, 16 + i * 10);
        lv_obj_set_style_bg_color(d, lv_color_hex(C_LABEL), 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_radius(d, 2, 0);
    }

    /* lista se sesti strankami */
    navbar = mk_panel(scr, 0, 480 - NAV_H, 800, NAV_H);
    for (int i = 0; i < N_PAGES; i++)
        navbtn[i] = mk_button(navbar, 4 + i * 132, 6, 128, NAV_H - 14,
                              T(PAGE_STR[i]), UI_FONT_20,
                              C_DEMAND, nav_cb, (void *)(intptr_t)i);

    /* mrizka 3 rady x 6 sloupcu pro budouci ikony - zatim prazdna */
    navgrid = mk_panel(scr, 0, 480 - NAV_GRID_H, 800, NAV_GRID_H);
    mk_lbl(navgrid, T(STR_NAV_PLACEHOLDER), UI_FONT_14,
           C_LABEL, 16, 6);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 6; c++) {
            lv_obj_t *t = mk_panel(navgrid, 6 + c * 132, 28 + r * 90, 126, 84);
            lv_obj_set_style_bg_color(t, lv_color_hex(C_INK), 0);
            lv_obj_set_style_border_color(t, lv_color_hex(C_RULE), 0);
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

    int soot = (int)v_soot.shown;
    lv_bar_set_value(rail_bar, soot, LV_ANIM_OFF);
    lv_label_set_text_fmt(rail_val, "%d.%d", soot / 100, (soot / 10) % 10);
    uint32_t sc = soot > 1800 ? C_DEFICIT : (soot > 1200 ? C_ACTUAL : C_SAGE);
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

    /* Zhasinani displeje po necinnosti - jedina softwarova uspora, ktera
     * na teto desce existuje (podsviceni je jen zap/vyp, ne PWM).
     * lv_disp_get_inactive_time pocita od posledni akce uzivatele, takze
     * resi probuzeni i usnuti jednim merenim. */
    uint32_t idle = lv_disp_get_inactive_time(NULL);
    /* kdyz bezi pokus s PWM, nesmi mu do podsviceni mluvit zhasinani */
    if (!host_pwm_running()) {
        /* ochranna lhuta po rucnim zhasnuti - jinak by displej probudil
         * tentyz dotek, kterym se zhasinalo */
        if (idle < 300 && !backlight_on &&
            (lv_tick_get() - blank_guard_ms) > 1500) backlight(true);
        if (cfg.blank_s && backlight_on && idle > cfg.blank_s * 1000UL) backlight(false);
    }
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

    /* Jen pri prvni stavbe. Po zmene jazyka se UI stavi znovu a namerene
     * hodnoty si musi zustat tam, kde byly - jinak by plnici tlak skocilo
     * zpatky na vychozi a graf by melo zub. */
    static bool first = true;
    if (first) {
        first = false;
        v_boost.shown = v_boost.target = 1000;
    }
}
