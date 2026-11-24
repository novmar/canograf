/*
 * Talento HMI - testovaci firmware v3
 * Waveshare ESP32-S3-Touch-LCD-4.3B, 800x480, GT911 (kapacitni), CH422G
 *
 * JESTE NECTE VOZIDLO - hodnoty jsou simulovane.
 *
 * Novinky proti v2:
 *   - uvodni obrazovka se dvema velkymi ukazateli (rychlost, otacky)
 *   - prejeti NAHORU skryje spodni navigaci a ukazatele se zvetsi,
 *     prejeti DOLU ji zase vytahne
 *   - obrazovka Nastaveni: jednotky, zhasinani displeje, WiFi
 *   - WiFi + OTA: po pripojeni jde dalsi firmware nahrat bezdratove
 *
 * Rozvrzeni vychazi z fyzickych rozmeru: 93,7 x 56,2 mm, 8,54 px/mm.
 * Dotykove cile >= 120 px = 14 mm.
 *
 * Pozn.: vestaveny Montserrat nema ceskou diakritiku -> texty bez hacku.
 */
#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <esp_chip_info.h>
#include <esp_flash.h>

using namespace esp_panel::drivers;
using namespace esp_panel::board;

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
#define RAIL_W  84
#define CONT_W  (800 - RAIL_W)

#define N_PAGES 6
enum { PG_HLAVNI = 0, PG_MOTOR, PG_TLAK, PG_CHYBY, PG_DPF, PG_NASTAV };
static const char *PAGE_NAME[N_PAGES] = {"HLAVNI", "MOTOR", "TLAK", "CHYBY", "DPF", "NASTAV"};

static lv_obj_t *page[N_PAGES], *navbtn[N_PAGES];
static lv_obj_t *navbar, *rail;
static int cur_page = PG_HLAVNI;
static bool nav_visible = true;

/* ---------------- trvale prvky ---------------- */
static lv_obj_t *lbl_temp, *lbl_state, *rail_bar, *rail_val;
/* hlavni */
static lv_obj_t *arc_speed, *arc_rpm, *lbl_speed_v, *lbl_rpm_v, *lbl_speed_u;
/* motor */
static lv_obj_t *arc_boost, *lbl_boost, *lbl_boost_sp, *lbl_vnt, *lbl_railp;
/* tlak */
static lv_obj_t *chart, *lbl_gap;
static lv_chart_series_t *ser_sp, *ser_act;
/* nastaveni */
static lv_obj_t *btn_units_v, *btn_units_t, *btn_blank, *lbl_wifi, *lbl_heap;

/* ---------------- nastaveni ---------------- */
static Preferences prefs;
struct {
    bool mph = false;           /* false = km/h */
    bool fahrenheit = false;    /* false = C    */
    uint32_t blank_s = 0;       /* 0 = nikdy zhasinat */
} cfg;
static uint32_t last_touch_ms = 0;
static bool backlight_on = true;
static Board *g_board = nullptr;

/* ---------------- hodnoty ---------------- */
typedef struct { float target, shown; } val_t;
static val_t v_speed, v_rpm, v_temp, v_boost, v_boost_sp, v_vnt, v_rail, v_soot;
static float sim_t = 0;
static uint32_t last_sample = 0;
static inline void approach(val_t *v, float k) { v->shown += (v->target - v->shown) * k; }

/* ---------------- stavebni prvky ---------------- */
static lv_obj_t *mk_panel(lv_obj_t *p, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, lv_coord_t h, bool border = true)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(C_RULE), 0);
    lv_obj_set_style_border_width(o, border ? 1 : 0, 0);
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
    if (!g_board || on == backlight_on) return;
    auto bl = g_board->getBacklight();
    if (bl) { on ? bl->on() : bl->off(); backlight_on = on; }
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
    nav_shown_ms = millis();

    if (s == 0) lv_obj_clear_flag(navhandle, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_add_flag(navhandle, LV_OBJ_FLAG_HIDDEN);
    if (s == 1) lv_obj_clear_flag(navbar, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_add_flag(navbar, LV_OBJ_FLAG_HIDDEN);
    if (s == 2) lv_obj_clear_flag(navgrid, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_add_flag(navgrid, LV_OBJ_FLAG_HIDDEN);

    lv_coord_t h = 480 - TOP_H - nav_height();
    for (int i = 0; i < N_PAGES; i++) lv_obj_set_size(page[i], CONT_W, h);
    lv_obj_set_size(rail, RAIL_W, h);
    layout_hlavni();
}

static void show_page(int idx)
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
    show_page((int)(intptr_t)lv_event_get_user_data(e));
    nav_shown_ms = millis();        /* prace s listou odklada jeji schovani */
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
            if (press_x < 70 && cur_page != PG_HLAVNI) show_page(PG_HLAVNI);
            else show_page(cur_page + 1);
            break;
        case LV_DIR_LEFT:  show_page(cur_page - 1); break;
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
        mk_lbl(modal, title, &lv_font_montserrat_32, C_BRIGHT, 30, 26);
        /* Kazdy dialog ma vlastni "zpet" vpravo nahore. Bez nej se z nekterych
         * obrazovek nedalo vycouvat jinak nez tlacitkem s jinym vyznamem.
         * Dialog s klavesnici predava prazdny nadpis - tam by se tlacitko
         * prekryvalo se vstupnim polem a klavesnice ma vlastni zavreni. */
        mk_button(modal, 660, 14, 124, 70, "ZPET", &lv_font_montserrat_20,
                  C_LABEL, modal_close_cb, NULL);
    }
    return modal;
}

static void show_info(const char *title, const char *body, uint32_t accent)
{
    lv_obj_t *m = modal_open(title, accent);
    lv_obj_t *b = mk_lbl(m, body, &lv_font_montserrat_20, C_LABEL, 30, 100);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, 740);
    mk_button(m, 30, 330, 360, 120, "ROZUMIM", &lv_font_montserrat_32,
              accent, modal_close_cb, NULL);
}

static void regen_cb(lv_event_t *e)
{
    (void)e;
    show_info("Regenerace DPF",
              "Vozidlo neni pripojeno.\n\nAz bude, tohle tlacitko odesle "
              "31 01 02 0D. Teplota vyfuku pritom prekroci 600 C. Jen na "
              "stojicim voze venku, se zahratym motorem, a nesmi se prerusit.",
              C_DEFICIT);
}

/* ---------------- WiFi ---------------- */
static lv_obj_t *wifi_list = NULL, *wifi_ta = NULL, *wifi_kb = NULL;
static char wifi_ssid[33] = {0};
static int scan_state = 0;      /* 0 klid, 1 bezi, 2 hotovo */

static void wifi_save_connect(const char *ssid, const char *pass)
{
    prefs.begin("talento", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
    WiFi.begin(ssid, pass);
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
    snprintf(cap, sizeof(cap), "HESLO PRO  %s", wifi_ssid);
    mk_lbl(m, cap, &lv_font_montserrat_16, C_LABEL, 20, 12);

    wifi_ta = lv_textarea_create(m);
    lv_obj_set_size(wifi_ta, 760, 64);
    lv_obj_align(wifi_ta, LV_ALIGN_TOP_MID, 0, 36);
    lv_textarea_set_one_line(wifi_ta, true);
    lv_textarea_set_placeholder_text(wifi_ta, "heslo");
    lv_obj_set_style_text_font(wifi_ta, &lv_font_montserrat_28, 0);
    lv_obj_set_style_bg_color(wifi_ta, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_text_color(wifi_ta, lv_color_hex(C_BRIGHT), 0);
    lv_obj_set_style_border_color(wifi_ta, lv_color_hex(C_DEMAND), 0);

    wifi_kb = lv_keyboard_create(m);
    lv_obj_set_size(wifi_kb, 800, 370);
    /* zarovnani je spolehlivejsi nez absolutni pozice - klavesnice si
     * pri zmene velikosti sama prepocita umisteni */
    lv_obj_align(wifi_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(wifi_kb, &lv_font_montserrat_20, 0);
    lv_obj_set_style_pad_all(wifi_kb, 4, 0);
    lv_keyboard_set_textarea(wifi_kb, wifi_ta);
    lv_obj_add_event_cb(wifi_kb, kb_ok_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(wifi_kb, modal_close_cb, LV_EVENT_CANCEL, NULL);
}

static void wifi_scan_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *m = modal_open("WiFi site", C_DEMAND);
    wifi_list = lv_list_create(m);
    lv_obj_set_pos(wifi_list, 30, 90);
    lv_obj_set_size(wifi_list, 600, 300);
    lv_obj_set_style_bg_color(wifi_list, lv_color_hex(C_PANEL), 0);
    lv_obj_t *l = lv_list_add_btn(wifi_list, NULL, "hledam...");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    mk_button(m, 650, 90, 120, 120, "ZAVRIT", &lv_font_montserrat_20,
              C_DEMAND, modal_close_cb, NULL);
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
    scan_state = 1;
}

static void wifi_poll(void)
{
    if (scan_state != 1) return;
    int n = WiFi.scanComplete();
    if (n < 0) return;                       /* -1 bezi, -2 nespusteno */
    scan_state = 2;
    if (!wifi_list) return;
    lv_obj_clean(wifi_list);
    for (int i = 0; i < n && i < 12; i++) {
        lv_obj_t *b = lv_list_add_btn(wifi_list, NULL, WiFi.SSID(i).c_str());
        lv_obj_set_style_text_font(b, &lv_font_montserrat_20, 0);
        lv_obj_set_style_min_height(b, 56, 0);
        lv_obj_add_event_cb(b, ssid_pick_cb, LV_EVENT_CLICKED, NULL);
    }
}

/* ---------------- nastaveni: obsluha ---------------- */
static void save_cfg(void)
{
    prefs.begin("talento", false);
    prefs.putBool("mph", cfg.mph);
    prefs.putBool("fahr", cfg.fahrenheit);
    prefs.putUInt("blank", cfg.blank_s);
    prefs.end();
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
static void blank_cb(lv_event_t *e)
{
    (void)e;
    static const uint32_t steps[] = {0, 30, 120, 600};
    int i = 0;
    for (int k = 0; k < 4; k++) if (cfg.blank_s == steps[k]) i = k;
    cfg.blank_s = steps[(i + 1) % 4];
    save_cfg();
    char b[24];
    if (cfg.blank_s == 0) snprintf(b, sizeof(b), "nikdy");
    else if (cfg.blank_s < 60) snprintf(b, sizeof(b), "%u s", (unsigned)cfg.blank_s);
    else snprintf(b, sizeof(b), "%u min", (unsigned)(cfg.blank_s / 60));
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
 * Uloha bezi na jadre 0, aby nekradla cas LVGL. Jeden krok = 1 ms, tedy
 * 10 urovni jasu. Pri zavreni dialogu se vzdy vraci plny jas.
 * ------------------------------------------------------------------- */
static volatile int pwm_duty = 10;        /* 0..10, 10 = trvale sviti */
static volatile int pwm_period_ms = 10;   /* 10 = 100 Hz, 20 = 50, 40 = 25 */
static TaskHandle_t pwm_task_h = NULL;
static uint32_t bl_toggle_us = 0;         /* zmerena doba jednoho prepnuti */

static void bl_raw(bool on)
{
    if (!g_board) return;
    auto bl = g_board->getBacklight();
    if (bl) on ? bl->on() : bl->off();
}

static void pwm_task(void *arg)
{
    (void)arg;
    for (;;) {
        int d = pwm_duty, per = pwm_period_ms;
        int on_ms = per * d / 10;
        int off_ms = per - on_ms;
        if (on_ms <= 0)       { bl_raw(false); vTaskDelay(pdMS_TO_TICKS(per)); }
        else if (off_ms <= 0) { bl_raw(true);  vTaskDelay(pdMS_TO_TICKS(per)); }
        else {
            bl_raw(true);  vTaskDelay(pdMS_TO_TICKS(on_ms));
            bl_raw(false); vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}

/* Zmeri, jak dlouho trva jedno prepnuti podsviceni pres I2C expander.
 * Pokud to vyjde v radu milisekund, kratke pulzy nemaji sanci a softwarove
 * PWM je slepa ulicka bez ohledu na zvolenou frekvenci. */
static void measure_toggle(void)
{
    uint32_t t0 = micros();
    for (int i = 0; i < 20; i++) { bl_raw(true); bl_raw(false); }
    bl_toggle_us = (micros() - t0) / 40;
    bl_raw(true);
    Serial.printf("prepnuti podsviceni: %u us\n", (unsigned)bl_toggle_us);
}

static void pwm_set_cb(lv_event_t *e)
{
    pwm_duty = (int)(intptr_t)lv_event_get_user_data(e);
    if (!pwm_task_h)
        xTaskCreatePinnedToCore(pwm_task, "blpwm", 2048, NULL, 1, &pwm_task_h, 0);
}

static void pwm_stop_cb(lv_event_t *e)
{
    if (pwm_task_h) { vTaskDelete(pwm_task_h); pwm_task_h = NULL; }
    pwm_duty = 10;
    bl_raw(true);
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
}

static void bright_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *m = modal_open("Jas - pokus se softwarovym PWM", C_ACTUAL);

    measure_toggle();
    char info[160];
    snprintf(info, sizeof(info),
             "Jedno prepnuti pres I2C trva %u us. Pri perode %d ms je "
             "nejkratsi pulz %d ms - jestli pri nizkem jasu zhasne uplne, "
             "nestiha nabehnout boost menic AP3032.",
             (unsigned)bl_toggle_us, pwm_period_ms, pwm_period_ms / 10);
    lbl_pwm_info = mk_lbl(m, info, &lv_font_montserrat_16, C_LABEL, 20, 80);
    lv_label_set_long_mode(lbl_pwm_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_pwm_info, 760);

    const int duty[5] = {10, 7, 5, 3, 1};
    const char *cap[5] = {"100 %", "70 %", "50 %", "30 %", "10 %"};
    for (int i = 0; i < 5; i++)
        mk_button(m, 16 + i * 128, 170, 120, 110, cap[i], &lv_font_montserrat_24,
                  C_ACTUAL, pwm_set_cb, (void *)(intptr_t)duty[i]);

    mk_lbl(m, "FREKVENCE", &lv_font_montserrat_14, C_LABEL, 660, 150);
    char fb[24];
    snprintf(fb, sizeof(fb), "%d Hz", 1000 / pwm_period_ms);
    btn_pwm_freq = mk_button(m, 660, 170, 124, 110, fb, &lv_font_montserrat_24,
                             C_DEMAND, pwm_freq_cb, NULL);

    mk_button(m, 16, 330, 380, 120, "ZPET NA PLNY JAS", &lv_font_montserrat_24,
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

/* Prepocet velikosti dvou hlavnich ukazatelu podle toho,
 * jestli je videt spodni navigace. */
static void layout_hlavni(void)
{
    if (!arc_speed) return;
    lv_coord_t h = 480 - TOP_H - nav_height();
    lv_coord_t d = (h < 260) ? 190 : ((h < 330) ? 250 : 360);
    lv_coord_t y = (h - d) / 2;
    lv_obj_set_size(arc_speed, d, d);
    lv_obj_set_size(arc_rpm, d, d);
    lv_obj_set_pos(arc_speed, 30, y);
    lv_obj_set_pos(arc_rpm, 390, y);
    lv_obj_align_to(lbl_speed_v, arc_speed, LV_ALIGN_CENTER, 0, -12);
    lv_obj_align_to(lbl_speed_u, arc_speed, LV_ALIGN_CENTER, 0, 34);
    lv_obj_align_to(lbl_rpm_v, arc_rpm, LV_ALIGN_CENTER, 0, -12);
}

static void build_hlavni(lv_obj_t *scr)
{
    lv_obj_t *p = page[PG_HLAVNI] = mk_page(scr);
    arc_speed = mk_arc(p, 30, 10, 350, 0, 160, C_ACTUAL, 22);
    arc_rpm   = mk_arc(p, 390, 10, 350, 0, 5000, C_DEMAND, 22);
    lbl_speed_v = mk_lbl(p, "0", &lv_font_montserrat_48, C_ACTUAL, 0, 0);
    lbl_speed_u = mk_lbl(p, "km/h", &lv_font_montserrat_20, C_LABEL, 0, 0);
    lbl_rpm_v   = mk_lbl(p, "0", &lv_font_montserrat_48, C_DEMAND, 0, 0);
    lv_obj_t *u2 = mk_lbl(p, "ot/min", &lv_font_montserrat_20, C_LABEL, 0, 0);
    lv_obj_align_to(u2, arc_rpm, LV_ALIGN_CENTER, 0, 34);
    layout_hlavni();
}

static void build_motor(lv_obj_t *scr)
{
    lv_obj_t *p = page[PG_MOTOR] = mk_page(scr);
    arc_boost = mk_arc(p, 14, 6, 230, 800, 2900, C_ACTUAL, 18);
    lbl_boost = mk_lbl(p, "----", &lv_font_montserrat_40, C_ACTUAL, 62, 96);
    mk_lbl(p, "mbar", &lv_font_montserrat_16, C_LABEL, 106, 142);
    mk_lbl(p, "PLNICI TLAK", &lv_font_montserrat_14, C_LABEL, 76, 240);

    mk_lbl(p, "ZADANO", &lv_font_montserrat_14, C_LABEL, 290, 10);
    lbl_boost_sp = mk_lbl(p, "----", &lv_font_montserrat_36, C_DEMAND, 290, 30);
    mk_lbl(p, "VNT", &lv_font_montserrat_14, C_LABEL, 290, 90);
    lbl_vnt = mk_lbl(p, "--", &lv_font_montserrat_36, C_BRIGHT, 290, 110);
    mk_lbl(p, "RAIL bar", &lv_font_montserrat_14, C_LABEL, 290, 170);
    lbl_railp = mk_lbl(p, "---", &lv_font_montserrat_36, C_BRIGHT, 290, 190);
    mk_lbl(p, "simulovana data", &lv_font_montserrat_14, C_DEFICIT, 540, 10);
}

static void build_tlak(lv_obj_t *scr)
{
    lv_obj_t *p = page[PG_TLAK] = mk_page(scr);
    mk_lbl(p, "SCHODEK mbar", &lv_font_montserrat_14, C_LABEL, 560, 8);
    lbl_gap = mk_lbl(p, "--", &lv_font_montserrat_48, C_SAGE, 560, 28);
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
    mk_lbl(p, "modra = zadano", &lv_font_montserrat_14, C_DEMAND, 560, 100);
    mk_lbl(p, "svetla = skutecnost", &lv_font_montserrat_14, C_ACTUAL, 560, 124);
}

static void build_chyby(lv_obj_t *scr)
{
    lv_obj_t *p = page[PG_CHYBY] = mk_page(scr);
    mk_lbl(p, "Bez pripojeni na CAN", &lv_font_montserrat_28, C_BRIGHT, 20, 12);
    mk_lbl(p, "19 02 FF    seznam vsech kodu", &lv_font_montserrat_20, C_DEMAND, 20, 70);
    mk_lbl(p, "19 02 01    jen to, co prave selhava", &lv_font_montserrat_20, C_DEMAND, 20, 106);
    mk_lbl(p, "19 04       freeze frame", &lv_font_montserrat_20, C_DEMAND, 20, 142);
    mk_lbl(p, "19 06       km a pocet vyskytu", &lv_font_montserrat_20, C_DEMAND, 20, 178);
}

static void build_dpf(lv_obj_t *scr)
{
    lv_obj_t *p = page[PG_DPF] = mk_page(scr);
    mk_lbl(p, "TEPLOTA PRED", &lv_font_montserrat_14, C_LABEL, 20, 10);
    mk_lbl(p, "-- C", &lv_font_montserrat_36, C_BRIGHT, 20, 30);
    mk_lbl(p, "TEPLOTA ZA", &lv_font_montserrat_14, C_LABEL, 240, 10);
    mk_lbl(p, "-- C", &lv_font_montserrat_36, C_BRIGHT, 240, 30);
    mk_button(p, 20, 110, 340, 130, "SPUSTIT REGENERACI",
              &lv_font_montserrat_24, C_DEFICIT, regen_cb, NULL);
    mk_lbl(p, "zatim bez vozidla", &lv_font_montserrat_16, C_LABEL, 390, 150);
}

static void build_nastav(lv_obj_t *scr)
{
    lv_obj_t *p = page[PG_NASTAV] = mk_page(scr);
    const lv_coord_t BW = 150, BH = 76;

    mk_lbl(p, "RYCHLOST", &lv_font_montserrat_14, C_LABEL, 20, 8);
    btn_units_v = mk_button(p, 20, 28, BW, BH, cfg.mph ? "mph" : "km/h",
                            &lv_font_montserrat_24, C_DEMAND, units_v_cb, NULL);
    mk_lbl(p, "TEPLOTA", &lv_font_montserrat_14, C_LABEL, 190, 8);
    btn_units_t = mk_button(p, 190, 28, BW, BH, cfg.fahrenheit ? "F" : "C",
                            &lv_font_montserrat_24, C_DEMAND, units_t_cb, NULL);
    mk_lbl(p, "ZHASNOUT PO", &lv_font_montserrat_14, C_LABEL, 360, 8);
    btn_blank = mk_button(p, 360, 28, BW, BH, "nikdy",
                          &lv_font_montserrat_24, C_DEMAND, blank_cb, NULL);
    mk_lbl(p, "JAS", &lv_font_montserrat_14, C_LABEL, 530, 8);
    mk_button(p, 530, 28, BW, BH, "PWM test", &lv_font_montserrat_20,
              C_ACTUAL, bright_cb, NULL);

    mk_lbl(p, "WIFI", &lv_font_montserrat_14, C_LABEL, 20, 120);
    lbl_wifi = mk_lbl(p, "nepripojeno", &lv_font_montserrat_24, C_LABEL, 20, 140);
    mk_button(p, 360, 120, 320, BH + 20, "VYBRAT SIT", &lv_font_montserrat_24,
              C_DEMAND, wifi_scan_cb, NULL);

    lbl_heap = mk_lbl(p, "", &lv_font_montserrat_16, C_LABEL, 20, 215);
}

/* ---------------- chrome ---------------- */
static void build_chrome(lv_obj_t *scr)
{
    lv_obj_t *bar = mk_panel(scr, 0, 0, 800, TOP_H);
    mk_lbl(bar, "TALENTO", &lv_font_montserrat_20, C_BRIGHT, 16, 20);
    mk_lbl(bar, "TEPLOTA", &lv_font_montserrat_14, C_LABEL, 320, 8);
    lbl_temp = mk_lbl(bar, "--", &lv_font_montserrat_32, C_BRIGHT, 320, 24);
    lbl_state = mk_lbl(bar, "DEMO", &lv_font_montserrat_16, C_DEFICIT, 640, 22);

    rail = mk_panel(scr, CONT_W, TOP_H, RAIL_W, 480 - TOP_H - NAV_H);
    mk_lbl(rail, "DPF", &lv_font_montserrat_16, C_LABEL, 22, 6);
    rail_bar = lv_bar_create(rail);
    lv_obj_set_pos(rail_bar, 28, 32);
    lv_obj_set_size(rail_bar, 26, 160);
    lv_bar_set_range(rail_bar, 0, 2400);
    lv_obj_set_style_bg_color(rail_bar, lv_color_hex(C_RULE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(rail_bar, lv_color_hex(C_SAGE), LV_PART_INDICATOR);
    lv_obj_set_style_radius(rail_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(rail_bar, 0, LV_PART_INDICATOR);
    rail_val = mk_lbl(rail, "--", &lv_font_montserrat_24, C_SAGE, 12, 202);

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
                              PAGE_NAME[i], &lv_font_montserrat_20,
                              C_DEMAND, nav_cb, (void *)(intptr_t)i);

    /* mrizka 3 rady x 6 sloupcu pro budouci ikony - zatim prazdna */
    navgrid = mk_panel(scr, 0, 480 - NAV_GRID_H, 800, NAV_GRID_H);
    mk_lbl(navgrid, "MISTO PRO DALSI FUNKCE", &lv_font_montserrat_14,
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

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t now = millis();
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
            (unsigned)(ESP.getFreeHeap() / 1024),
            (unsigned)(ESP.getFreePsram() / 1024),
            WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "");
        if (WiFi.status() == WL_CONNECTED) {
            lv_label_set_text(lbl_wifi, WiFi.SSID().c_str());
            lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(C_SAGE), 0);
        } else {
            lv_label_set_text(lbl_wifi, "nepripojeno");
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
    if (!pwm_task_h) {
        if (idle < 300 && !backlight_on) backlight(true);
        if (cfg.blank_s && backlight_on && idle > cfg.blank_s * 1000UL) backlight(false);
    }
}

/* ---------------- start ---------------- */
void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("\nTalento HMI v5 - start");

    prefs.begin("talento", true);
    cfg.mph = prefs.getBool("mph", false);
    cfg.fahrenheit = prefs.getBool("fahr", false);
    cfg.blank_s = prefs.getUInt("blank", 0);
    String ss = prefs.getString("ssid", ""), pw = prefs.getString("pass", "");
    prefs.end();

    g_board = new Board();
    g_board->init();
    if (!g_board->begin()) { Serial.println("CHYBA: board->begin()"); return; }
    Serial.println("Panel OK");
    if (!lvgl_port_init(g_board->getLCD(), g_board->getTouch())) {
        Serial.println("CHYBA: lvgl_port_init"); return;
    }
    Serial.println("LVGL OK");

    WiFi.mode(WIFI_STA);
    if (ss.length()) {
        WiFi.begin(ss.c_str(), pw.c_str());
        Serial.printf("WiFi: zkousim %s\n", ss.c_str());
    }
    ArduinoOTA.setHostname("talento-hmi");
    ArduinoOTA.begin();

    lvgl_port_lock(-1);
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
    show_page(PG_HLAVNI);
    v_boost.shown = v_boost.target = 1000;
    last_touch_ms = millis();
    lv_timer_create(tick_cb, 20, NULL);
    lvgl_port_unlock();

    Serial.printf("UI hotovo. Heap %u kB, PSRAM %u kB\n",
                  (unsigned)(ESP.getFreeHeap() / 1024),
                  (unsigned)(ESP.getFreePsram() / 1024));
}

void loop()
{
    ArduinoOTA.handle();
    delay(20);
}
