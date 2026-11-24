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
#include <stdarg.h>
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
static lv_obj_t *rail;
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
static lv_obj_t *btn_bright, *btn_profile;
/* DPF */
static lv_obj_t *btn_regen_state;
/* horni lista: prepinac profilu - jmeno a tri rysky vedle sebe */
static lv_obj_t *prof_btn, *prof_name, *prof_mark[UI_PROFILES];
/* diagnostika: prekryv s FPS a pameti zije porad, jen byva schovany */
static lv_obj_t *perf_lbl;
static bool perf_on = false;
static uint32_t perf_frames, perf_draw_sum, perf_win_start;
static int perf_fps10, perf_draw10;

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

static unsigned host_heap_kb(void)
{
    return (host && host->heap_kb) ? host->heap_kb() : 0u;
}

static unsigned host_psram_kb(void)
{
    return (host && host->psram_kb) ? host->psram_kb() : 0u;
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
 *
 * KOLIK TO ZABERE V NVS. Profil jsou dve cisla: jas (int32) a zhasnuti
 * (uint32). NVS uklada kazdou takovou polozku do jedne polozky stranky
 * o 32 B vcetne klice, takze tri profily = 6 polozek = 192 B. Spolecna
 * cast jsou dalsi ctyri polozky (jednotky, jazyk, cislo profilu) = 128 B,
 * dohromady 320 B. Vedle toho stoji sest ulozenych WiFi siti, kde kazda
 * zabere SSID (32 B hlavicka + 64 B dat) a heslo (32 + 96 B), tedy asi
 * 1,4 kB. Cely oddil NVS ma 20 kB, takze jsme zhruba na desetine a
 * dalsi profil by stal 64 B.
 * ------------------------------------------------------------------- */
#define BL_LEVELS 5
static const int BL_LEVEL[BL_LEVELS] = {100, 75, 50, 35, 25};
#define BL_MIN  (BL_LEVEL[BL_LEVELS - 1])

static ui_cfg_t cfg;

/* Profil, ktery prave plati. Pise se na tolika mistech, ze zkratka je
 * citelnejsi nez cfg.prof[cfg.profile] vsude. */
#define PROF (cfg.prof[cfg.profile])

static const int PROF_STR[UI_PROFILES] = {
    STR_PROF_SHOP, STR_PROF_DAY, STR_PROF_NIGHT
};

static int bl_clamp(int p)
{
    if (p > 100)    return 100;
    if (p < BL_MIN) return BL_MIN;      /* nesmysl z NVS nesmi zhasnout displej */
    return p;
}

ui_profile_t ui_profile_default(int i)
{
    /* DILNA zhasina po deseti minutach: deska na stole bezi cele
     * hodiny a vetsinu z nich u ni nikdo nestoji. DEN a NOC zhasinani
     * vypnute maji schvalne - zhasinani se pocita od posledniho DOTEKU
     * a za jizdy se displeje nikdo nedotyka, takze by zhaslo prave
     * uprostred jizdy. */
    static const ui_profile_t def[UI_PROFILES] = {
        [UI_PROF_SHOP]  = { .bright = 100, .blank_s = 600 },
        [UI_PROF_DAY]   = { .bright = 100, .blank_s = 0 },
        [UI_PROF_NIGHT] = { .bright = 35,  .blank_s = 0 },
    };
    if (i < 0 || i >= UI_PROFILES) i = UI_PROF_DAY;
    return def[i];
}

void ui_set_cfg(const ui_cfg_t *c)
{
    if (!c) return;
    cfg = *c;
    for (int i = 0; i < UI_PROFILES; i++)
        cfg.prof[i].bright = bl_clamp(cfg.prof[i].bright);
    /* Profil mimo rozsah by indexoval mimo pole, takze se musi orezat
     * drive, nez se podle nej cokoliv nastavi. */
    if (cfg.profile < 0 || cfg.profile >= UI_PROFILES) cfg.profile = UI_PROF_DAY;
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

/* Pripojeni na konec bufferu, ktere nemuze prerust jeho velikost.
 * snprintf vraci delku, kterou by ZAPSAL, ne kolik zapsal - prosty soucet
 * by pri delsim textu ukazal za konec pole a dalsi volani by dostalo
 * zapornou velikost. Pouziva to kazde misto, ktere sklada text po
 * kouscich: diagnostika, seznam profilu i citace sbernice v resetu. */
static int str_add(char *b, int at, int n, const char *fmt, ...)
{
    if (at >= n - 1) return n - 1;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(b + at, (size_t)(n - at), fmt, ap);
    va_end(ap);
    if (w < 0) return at;
    at += w;
    return at > n - 1 ? n - 1 : at;
}

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

/* Napoveda gesta zpet a priznak napnuteho gesta; definice obsluhy je az
 * u gest, sem patri jen proto, ze je shazuje kazdy novy stisk. */
static void gest_hint_show(bool on);
static bool gest_armed;

static void press_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *in = lv_indev_get_act();
    if (!in) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    press_x = p.x;
    press_y = p.y;
    /* Novy tah, nova sance: gesto z toho predchoziho uz neplati. */
    gest_armed = false;
    gest_hint_show(false);
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
    /* Panel je vyzdoba, ne ovladaci prvek. Dotek na nem musi propadnout
     * tomu, kdo ho ma vyridit - strance nebo rovnou obrazovce, ktera z
     * nej sklada gesta. Kdo chce dotek sam (uchyt, panel navigace), si
     * priznak zase prida. */
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
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
    /* Dotyk tlacitka musi dojit i jeho rodici: panel navigace z nej
     * pozna tah prstem a obrazovka z nej vi, kde tah zacal a kdy skoncil
     * (gesta se vyhodnocuji az pri pusteni). Klik si tlacitko vyridi
     * samo, rodice na nej zadnou obsluhu nemaji. */
    lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
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
 * rana do oka a clovek nepozna, jestli deska jeste zije.
 *
 * Probuzeni je zrcadlo toho sameho, jen pomalejsi: panel se zapina
 * jeste tmavy a jas najede na ulozenou uroven za vterinu, aby to v noci
 * neprasklo do oci. Nabeh NIC NEBLOKUJE - pocita ho bl_step() z
 * ui_tick(), takze dotek i prekreslovani jedou po celou dobu dal a
 * tuknout jde hned, i kdyz jas jeste najizdi. Uroven zvolena rucne
 * (bl_apply) nabeh zrusi a plati okamzite; cekat vterinu na vlastni
 * tuknuti by bylo matouci.
 * ------------------------------------------------------------------- */
#define BL_FADE_MS 500
#define BL_WAKE_MS 1000

static bool backlight_on = true;
static bool bl_fading = false;      /* stmivani pred zhasnutim */
static bool bl_waking = false;      /* nabeh po probuzeni */
static uint32_t bl_fade_start = 0;
static uint32_t bl_wake_start = 0;
static int bl_wake_from = 0;        /* jas, ze ktereho nabeh zacal */
static int bl_shown = -1;           /* naposled poslany jas; -1 = nic */

/* dialog jasu, resetu a diagnostiky - zije jen po dobu otevreneho
 * dialogu, uklizi modal_forget() */
static lv_obj_t *btn_bl_level[UI_PROFILES][BL_LEVELS];
static lv_obj_t *btn_bl_prof[UI_PROFILES];
static lv_obj_t *dbg_raw, *dbg_mem, *dbg_perf_btn;

static bool bl_can_dim(void) { return host && host->bl_set_percent; }

static int bl_level(void) { return PROF.bright; }

static void bl_set(int percent)
{
    if (!bl_can_dim() || percent == bl_shown) return;
    bl_shown = percent;
    host->bl_set_percent(percent);
}

static void bl_apply(void)
{
    bl_waking = false;              /* rucni volba prebiji nabeh */
    bl_set(bl_level());
}

static void backlight(bool on)
{
    bl_fading = false;
    if (on) {
        if (!bl_can_dim()) {
            bl_apply();             /* bez stmivani je zapnuti tvrde */
        } else {
            /* Panel se zapina uz tmavy, aby se neukazal se stridou,
             * ktera zbyla po stmivani - a hlavne aby nabeh zacal od
             * nuly, ne od posledni urovne. */
            if (!backlight_on) bl_set(0);
            bl_wake_from = bl_shown < 0 ? 0 : bl_shown;
            bl_waking = true;
            bl_wake_start = lv_tick_get();
        }
    }
    if (on == backlight_on) return;
    backlight_on = on;
    if (host && host->backlight) host->backlight(on);
}

static void blank_begin(void)
{
    if (bl_fading) return;
    if (!bl_can_dim()) { backlight(false); return; }
    bl_waking = false;
    bl_fading = true;
    bl_fade_start = lv_tick_get();
}

/* Jeden krok prejezdu jasu, volany z ui_tick(). Zhasinani ma prednost:
 * kdyz se zhasina, nabeh uz nebezi. */
static void bl_step(void)
{
    if (bl_fading) {
        uint32_t el = lv_tick_get() - bl_fade_start;
        if (el >= BL_FADE_MS) {
            bl_set(0);              /* strida na doraz, LEDky uz nesviti */
            backlight(false);       /* az ted vypnout panel */
            return;
        }
        bl_set((int)((uint32_t)bl_level() * (BL_FADE_MS - el) / BL_FADE_MS));
        return;
    }
    if (!bl_waking) return;
    int target = bl_level();
    uint32_t el = lv_tick_get() - bl_wake_start;
    if (el >= BL_WAKE_MS) {
        bl_waking = false;
        bl_set(target);
        return;
    }
    bl_set(bl_wake_from + (target - bl_wake_from) * (int)el / BL_WAKE_MS);
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
 *   0  uchyt      - tenky pruh s "=" uprostred, jako na Androidu
 *   1  lista      - sest tlacitek stranek
 *   2  mrizka     - tataz lista a POD NI dlazdice dalsich funkci
 *
 * Lista a mrizka jsou JEDEN panel, ne dva. Panel ma porad vysku
 * NAV_FULL_H, tlacitka stranek sedi u jeho horni hrany a dlazdice pod
 * nimi; menit se smi jen jeho y. Ve stavu 1 tedy nad spodni hranu
 * obrazovky kouka jen pruh s tlacitky, ve stavu 2 cely panel a lista
 * zustane nahore videt. Driv to byly dva samostatne panely a mrizka
 * listu prekryla, takze z mrizky nebylo kam prepnout stranku.
 *
 * Navigace je PREKRYV nad strankami. Driv si stranky i sloupec DPF
 * prepocitavaly vysku podle toho, kolik navigace zabirala, takze se
 * obsah pri kazdem vysunuti scvrkl a ukazatele zmenily prumer. Ovladani
 * nema prekreslovat obsah, takze stranky maji vzdy PAGE_H a navigace se
 * vysouva pres ne.
 *
 * Prepina se trojim zpusobem: tuknutim na uchyt, gestem a tazenim prstu
 * jako u bottom sheetu na Androidu - nahoru, drzet, dolu zpatky, bez
 * pusteni. Pri tazeni panel jen jezdi po ose y a nemeni velikost: neni
 * co preskladat a prekresli se jen pruh, o ktery se panel posunul.
 *
 * ROZMERY. Panel ma hustotu 8,54 px/mm (docs/ui.md), takze:
 *
 *     tlacitko stranky  129 x 140 px = 15,1 x 16,4 mm
 *     dlazdice          129 x 120 px = 15,1 x 14,0 mm
 *
 * Doporuceni pro ovladani za jizdy je 14 mm (120 px) a to je MINIMUM,
 * ne cil. Tlacitka stranek se mackaji za jizdy nejcasteji, takze maji
 * na vysku 16,4 mm, o sestinu vic; puvodnich 110 px (12,9 mm) bylo pod
 * minimem a uzivatel je hlasil jako "male".
 *
 * JEDNA RADA, NE DVE. Sest stranek na 800 px da 133 px na sloupec, tedy
 * tlacitko siroke 129 px = 15,1 mm - nad minimem i s plnymi popisky,
 * takze zkracovat je netreba. Dve rady po trech by sice dala tlacitka
 * 262 px siroka, ale lista by narostla pres 290 px a pod ni by z 416 px
 * plochy pod horni listou zbylo na dlazdice uz jen asi 120 px, tedy
 * jedina rada. Sirka byla v poradku, chybela vyska - a ta se v jedne
 * rade da dat obema.
 *
 * Panel vysunuty nadoraz je presne tak vysoky jako plocha pod horni
 * listou, aby mrizka nekoncila kousek pod ni a nenechavala zbytecny
 * prouzek stranky. Vyska dlazdice je proto to, co zbyde pod listou -
 * kdyz lista povyroste, ubere dlazdicim, a _Static_assert nize hlida,
 * ze pritom nespadnou pod dotykovy cil.
 * ------------------------------------------------------------------- */
#define NAV_HANDLE_H  44
#define NAV_PAD       6
#define NAV_BTN_W     129
#define NAV_BTN_H     140
#define NAV_BTN_Y     NAV_PAD           /* lista je u HORNI hrany panelu */
#define NAV_BTN_X(i)  (3 + (i) * 133)
#define NAV_BAR_H     (NAV_PAD + NAV_BTN_H + NAV_PAD)
#define NAV_CAP_H     20                /* radek s popiskem nad dlazdicemi */
#define NAV_TILE_GAP  4
#define NAV_TILE_COLS 6
#define NAV_TILE_ROWS 2
#define NAV_FULL_H    PAGE_H
#define NAV_TILE_H    ((NAV_FULL_H - NAV_BAR_H - NAV_CAP_H - \
                        (NAV_TILE_ROWS - 1) * NAV_TILE_GAP) / NAV_TILE_ROWS)
#define NAV_TILE_Y(r) (NAV_BAR_H + NAV_CAP_H + (r) * (NAV_TILE_H + NAV_TILE_GAP))
#define NAV_TILE_DEBUG 0                /* ktera dlazdice otevira diagnostiku */

_Static_assert(NAV_TILE_H >= 120,
               "dlazdice klesly pod dotykovy cil 14 mm, lista je prilis vysoka");

#define NAV_IDLE_MS   5000
#define NAV_ANIM_MS   180       /* zapadnuti po pusteni */
#define NAV_FLING     8         /* px mezi dvema ctenimi doteku (~20 ms),
                                   tedy asi 400 px/s = svihnuti */

static lv_obj_t *navhandle, *navpanel;
static int nav_state = 0;
static uint32_t nav_shown_ms = 0;

/* kolik navigace prave zabira zespodu; 0 = jeste neni postavena */
static lv_coord_t nav_h_shown = 0;

static lv_coord_t nav_state_h(int s)
{
    switch (s) {
        case 1:  return NAV_BAR_H;
        case 2:  return NAV_FULL_H;
        default: return NAV_HANDLE_H;
    }
}

static void nav_apply(lv_coord_t h)
{
    if (h < NAV_HANDLE_H) h = NAV_HANDLE_H;
    if (h > NAV_FULL_H)   h = NAV_FULL_H;
    nav_h_shown = h;

    /* Panel si drzi svou velikost a jen vyjizdi zpod spodni hrany, takze
     * se v nem nic nepreklada. Uchyt je videt jen dokud panel nevyjel. */
    bool out = h > NAV_HANDLE_H;
    if (out) {
        lv_obj_clear_flag(navpanel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(navhandle, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(navpanel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(navhandle, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_y(navpanel, 480 - h);
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
     * tretinach: stavy nejsou stejne daleko (44 / 152 / 416 px). */
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

/* Panel navigace reaguje na tah prstem. Tlacitka stranek i dlazdice maji
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
 * Gesta
 *
 * Akce se spousti az PO PUSTENI prstu. LVGL ohlasi gesto uz ve chvili,
 * kdy prst prekroci prah, a driv se podle toho rovnou prepinalo: komu
 * sjel prst pri jine praci, tomu to udelalo "zpet" omylem a nedalo se
 * to vzit zpatky. Ted gesto jen NAPNE priznak a pri pusteni se jeste
 * jednou zmeri, kam prst doopravdy dosel - kdyz se vratil pod prah,
 * neudela se nic. Behem napnuteho gesta zpet sviti u leveho okraje
 * pruh, takze je videt, co se chysta.
 *
 * Tim se zaroven vyresilo, ze jedno svihnuti proletelo dve az tri
 * stranky: LVGL po zmene objektu pod prstem posilalo gesto znovu, ale
 * ted se akce deje jen pri pusteni a priznak se shazuje pri kazdem
 * stisku, takze jeden tah udela nejvys jednu vec i bez
 * lv_indev_wait_release().
 *
 * Plati pro vsechna gesta stejne - prepnuti stranky, tah od okraje i
 * vysouvani navigace.
 * ------------------------------------------------------------------- */
#define GEST_PX   50    /* stejny prah jako LV_INDEV_DEF_GESTURE_LIMIT */
#define GEST_EDGE 70    /* odkud se pocita tah "od leveho okraje" */

static lv_obj_t *gest_hint;     /* pruh u leveho okraje, napoveda ZPET */

/* Napoveda zije na HORNI VRSTVE, ne na obrazovce: musi byt videt i nad
 * otevrenym dialogem a prezit prestavbu UI po zmene jazyka. */
static void gest_hint_build(void)
{
    if (gest_hint) return;
    gest_hint = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(gest_hint, 0, TOP_H);
    lv_obj_set_size(gest_hint, 10, 480 - TOP_H);
    lv_obj_set_style_bg_color(gest_hint, lv_color_hex(C_DEMAND), 0);
    lv_obj_set_style_bg_opa(gest_hint, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(gest_hint, 0, 0);
    lv_obj_set_style_radius(gest_hint, 0, 0);
    lv_obj_clear_flag(gest_hint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(gest_hint, LV_OBJ_FLAG_HIDDEN);
}

static void gest_hint_show(bool on)
{
    if (!gest_hint) return;
    if (on) lv_obj_clear_flag(gest_hint, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(gest_hint, LV_OBJ_FLAG_HIDDEN);
}

/* Smer podle toho, kde prst JE TED - ne podle toho, kudy sel. */
static lv_dir_t gest_dir_now(void)
{
    lv_indev_t *in = lv_indev_get_act();
    if (!in) return LV_DIR_NONE;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_coord_t dx = p.x - press_x, dy = p.y - press_y;
    if (LV_ABS(dx) < GEST_PX && LV_ABS(dy) < GEST_PX) return LV_DIR_NONE;
    if (LV_ABS(dx) >= LV_ABS(dy)) return dx > 0 ? LV_DIR_RIGHT : LV_DIR_LEFT;
    return dy > 0 ? LV_DIR_BOTTOM : LV_DIR_TOP;
}

static bool gest_back_ready(bool edge_only)
{
    return gest_armed && gest_dir_now() == LV_DIR_RIGHT &&
           (!edge_only || press_x < GEST_EDGE);
}

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    gest_armed = true;
    gest_hint_show(gest_back_ready(true));
}

/* Napoveda jede s prstem: kdyz se vrati, zhasne a akce uz nebude. */
static void gest_pressing_cb(lv_event_t *e)
{
    (void)e;
    gest_hint_show(gest_back_ready(true));
}

static void gest_release_cb(lv_event_t *e)
{
    (void)e;
    gest_hint_show(false);
    if (!gest_armed) return;
    gest_armed = false;
    switch (gest_dir_now()) {
        case LV_DIR_RIGHT:
            /* tah od leveho okraje = zpet na hlavni obrazovku */
            if (press_x < GEST_EDGE && cur_page != PG_HLAVNI) ui_show_page(PG_HLAVNI);
            else ui_show_page(cur_page + 1);
            break;
        case LV_DIR_LEFT: ui_show_page(cur_page - 1); break;
        /* Kdyz tah zacal na navigaci, vyridilo si ho tazeni prstem -
         * jinak by se stav prepnul dvakrat. */
        case LV_DIR_TOP:    if (!nav_drag) set_nav_state(nav_state + 1); break;
        case LV_DIR_BOTTOM: if (!nav_drag) set_nav_state(nav_state - 1); break;
        default: break;     /* prst se vratil, gesto se rusi */
    }
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
    dbg_raw     = NULL;
    dbg_mem     = NULL;
    dbg_perf_btn = NULL;
    for (int m = 0; m < UI_PROFILES; m++) {
        btn_bl_prof[m] = NULL;
        for (int i = 0; i < BL_LEVELS; i++) btn_bl_level[m][i] = NULL;
    }
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

/* Zpetne gesto dela totez co tlacitko ZPET, a to kdekoliv na dialogu -
 * tedy i nad klavesnici nebo seznamem siti. Stejne jako gesta na
 * obrazovce se ale provede az PRI PUSTENI, takze se da vzit zpatky, a
 * zbytek tahu uz nikam nepropadne: dialog mizi ve chvili, kdy je prst
 * nahore. */
static void modal_gesture_cb(lv_event_t *e)
{
    (void)e;
    gest_armed = true;
    gest_hint_show(gest_back_ready(false));
}

static void modal_pressing_cb(lv_event_t *e)
{
    (void)e;
    gest_hint_show(gest_back_ready(false));
}

static void modal_release_cb(lv_event_t *e)
{
    (void)e;
    gest_hint_show(false);
    if (!gest_armed) return;
    gest_armed = false;
    if (gest_dir_now() == LV_DIR_RIGHT) modal_close_cb(NULL);
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
    /* Dialog musi vedet, kde tah zacal a kdy skoncil - gesto se
     * vyhodnocuje az pri pusteni. Tlacitka, klavesnice i seznam siti
     * maji EVENT_BUBBLE, takze sem dojde i tah zacaty na nich. */
    lv_obj_add_event_cb(modal, press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(modal, modal_pressing_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(modal, modal_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(modal, modal_release_cb, LV_EVENT_PRESS_LOST, NULL);

    lv_obj_t *s = lv_obj_create(modal);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
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
    lv_obj_clear_flag(hold_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
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
#define WIFI_ROWS      12       /* kolik radku se do seznamu postavi;
                                   zbytek se spocita do posledniho radku */

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
    /* Vstupni pole si dotek nechava (kurzor), takze ho musi poslat dal
     * dialogu - ten z nej sklada gesto zpet. */
    lv_obj_add_flag(wifi_ta, LV_OBJ_FLAG_EVENT_BUBBLE);

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
    /* Dotek klavesnice musi dojit dialogu, jinak by z ni neslo udelat
     * gesto zpet - to se vyhodnocuje az pri pusteni. */
    lv_obj_add_flag(wifi_kb, LV_OBJ_FLAG_EVENT_BUBBLE);
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
     * dialog, misto aby se zaradil v seznamu - dotek ale musi dojit az
     * dialogu, ktery gesto vyhodnocuje pri pusteni. */
    lv_obj_set_scroll_dir(wifi_list, LV_DIR_VER);
    lv_obj_add_flag(wifi_list, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* Jedno tlacitko navic, ne dve: ZAVRIT delalo totez co ZPET vpravo
     * nahore, ktere ma kazdy dialog. ZNOVU proto stoji samo ve stejnem
     * sloupci jako ZPET - stejne x i sirka, at nejsou na pohled
     * rozhazena. */
    mk_button(m, 660, 150, 124, 120, T(STR_WIFI_RETRY), UI_FONT_20,
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
        lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
        /* SHORT_CLICKED, ne CLICKED: po dlouhem stisku posila LVGL oboji,
         * takze by se sit zaroven zapomnela i pripojila. */
        lv_obj_add_event_cb(b, drag_guard_cb, LV_EVENT_SHORT_CLICKED, NULL);
        lv_obj_add_event_cb(b, ssid_pick_cb, LV_EVENT_SHORT_CLICKED, NULL);
        if (known) lv_obj_add_event_cb(b, ssid_forget_cb, LV_EVENT_LONG_PRESSED, NULL);
    }

    /* Co se nevejde, se nesmi tise ztratit: v dilne s vic pristupovymi
     * body by hledana sit proste nebyla a nic by nenaznacilo proc. */
    if (n > WIFI_ROWS) {
        char more[32];
        snprintf(more, sizeof(more), T(STR_WIFI_MORE), n - WIFI_ROWS);
        wifi_row_style(lv_list_add_text(wifi_list, more), C_LABEL);
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

/* Doba necinnosti slovem. Je zvlast, protoze ji krome tlacitka
 * "ZHASNOUT PO" ukazuje i radek profilu v dialogu jasu. */
static void blank_text(char *b, size_t n, uint32_t s)
{
    if (s == 0)      snprintf(b, n, "%s", T(STR_BLANK_NEVER));
    else if (s < 60) snprintf(b, n, "%u s", (unsigned)s);
    else             snprintf(b, n, "%u min", (unsigned)(s / 60));
}

/* Popisek tlacitka "ZHASNOUT PO". Vola ho i build_nastav() a prepnuti
 * profilu, aby tlacitko ukazovalo hodnotu toho profilu, ktery prave
 * plati, a ne tu, co pri stavbe nahodou byla. */
static void blank_btn_paint(void)
{
    if (!btn_blank) return;
    char b[24];
    blank_text(b, sizeof(b), PROF.blank_s);
    lv_label_set_text(lv_obj_get_child(btn_blank, 0), b);
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
    for (int k = 0; k < 4; k++) if (PROF.blank_s == steps[k]) i = k;
    PROF.blank_s = steps[(i + 1) % 4];
    save_cfg();
    blank_btn_paint();
}

/* ---------------------------------------------------------------------
 * Jas podsviceni a profily
 *
 * Tlacitka, ne posuvnik. Posuvnik vyzaduje trefit se a pak jeste
 * udrzet prst na jednom miste; za jizdy se obe veci nepovedou. Tlacitko
 * ma pevnou polohu, takze se da zmacknout naslepo.
 *
 * Dialog ma RADEK NA KAZDY PROFIL: vlevo jmeno profilu i s tim, co v
 * nem je (jas a zhasnuti), vpravo pet urovni jasu. Driv tu byly dva
 * radky DEN a NOC a prepinalo se mezi nimi; profily to zobecnily na
 * tri a pribralo k nim zhasinani. Zustalo to podstatne: na jeden
 * pohled je videt, ktery profil bezi (silna svetla obruba), co maji
 * ulozene ty ostatni, a uroven kterehokoliv z nich jde zmenit, aniz by
 * se do nej muselo prepnout.
 *
 * PODLE VOZU je ctvrta moznost, ktera zatim nejde: vozidlo stav
 * osvetleni po sbernici hlasi, ale CAN jeste na UI napojeny neni.
 * Stoji proto u nadpisu jako poznamka, ne jako tlacitko - predstirat
 * funkci, ktera neni, je horsi nez ji nenabidnout.
 *
 * Mrizka je 5 urovni x 3 profily. Na 800 px vyjde uroven 104 px siroka
 * = 12,2 mm, tedy pod 14 mm z docs/ui.md - pet urovni vedle jmena
 * profilu se sirsich nevejde. Vyska radku 120 px = 14,1 mm uz
 * doporuceni splnuje a tri radky i s mezerami se do 480 px vejdou.
 * ------------------------------------------------------------------- */
#define BL_ROW_H    120
#define BL_ROW_GAP  6
#define BL_ROW_Y(m) (96 + (m) * (BL_ROW_H + BL_ROW_GAP))
#define BL_PROF_W   180
#define BL_PROF_X   20
#define BL_LVL_W    104
#define BL_LVL_X(i) (212 + (i) * (BL_LVL_W + 8))
#define BL_NOTE_X   330
#define BL_NOTE_W   320

_Static_assert(BL_ROW_Y(UI_PROFILES - 1) + BL_ROW_H <= 480,
               "radky profilu by prelezly spodni hranu obrazovky");

/* obsluha tlacitka urovne dostane profil i procenta v jednom cisle */
#define BL_UD(m, p) ((void *)(intptr_t)((m) * 1000 + (p)))

/* Popisek tlacitka JAS v nastaveni. Je i v build_nastav(), aby tlacitko
 * uz pri stavbe ukazovalo uroven z NVS a ne natvrdo zadanou. */
static void bright_btn_paint(void)
{
    if (!btn_bright) return;
    char b[16];
    snprintf(b, sizeof(b), "%d %%", bl_level());
    lv_label_set_text(lv_obj_get_child(btn_bright, 0), b);
}

/* Tlacitko PROFIL v nastaveni rika, ktery profil prave plati. */
static void profile_btn_paint(void)
{
    if (!btn_profile) return;
    lv_label_set_text(lv_obj_get_child(btn_profile, 0), T(PROF_STR[cfg.profile]));
}

/* Vsechny radky dialogu najednou: jmeno profilu s tim, co v nem je, a
 * vybrana uroven v kazdem z nich. Vypln tlacitek se nemeni - modra a
 * kostena maji v teto aplikaci pevny vyznam (zadano / skutecnost) a
 * vyber mezi ne nepatri, takze vyber nese obruba. */
static void bl_rows_paint(void)
{
    for (int m = 0; m < UI_PROFILES; m++) {
        bool live = (m == cfg.profile);
        if (btn_bl_prof[m]) {
            char blank[24], cap[64];
            blank_text(blank, sizeof(blank), cfg.prof[m].blank_s);
            snprintf(cap, sizeof(cap), "%s\n%d %%\n%s",
                     T(PROF_STR[m]), cfg.prof[m].bright, blank);
            lv_label_set_text(lv_obj_get_child(btn_bl_prof[m], 0), cap);
            lv_obj_set_style_bg_color(btn_bl_prof[m],
                lv_color_hex(live ? C_RULE : C_PANEL), 0);
            lv_obj_set_style_border_color(btn_bl_prof[m],
                lv_color_hex(live ? C_BRIGHT : C_RULE), 0);
            lv_obj_set_style_border_width(btn_bl_prof[m], live ? 3 : 1, 0);
        }
        for (int i = 0; i < BL_LEVELS; i++) {
            if (!btn_bl_level[m][i]) continue;
            bool sel = BL_LEVEL[i] == cfg.prof[m].bright;
            lv_obj_set_style_border_color(btn_bl_level[m][i],
                lv_color_hex(sel ? (live ? C_BRIGHT : C_LABEL) : C_RULE), 0);
            lv_obj_set_style_border_width(btn_bl_level[m][i], sel ? 3 : 1, 0);
        }
    }
}

/* Prepinac v horni liste se prekresluje spolu se zbytkem; definice je
 * az u jeho kresby, tady staci ohlaska. */
static void prof_bar_paint(void);

/* Jedine misto, kde se profil meni - at uz na nej tukne radek v
 * dialogu, tlacitko v nastaveni nebo prepinac v horni liste. Vsechny
 * tri cesty tim delaji totez vcetne ulozeni do NVS a vsechna mista v
 * UI se prekresli, takze se nemuzou rozejit. */
static void profile_apply(void)
{
    bl_apply();                 /* jas noveho profilu plati hned */
    save_cfg();
    profile_btn_paint();
    prof_bar_paint();
    blank_btn_paint();
    bl_rows_paint();
    bright_btn_paint();
}

static void profile_set(int p)
{
    if (p < 0 || p >= UI_PROFILES || p == cfg.profile) return;
    cfg.profile = p;
    profile_apply();
}

/* V dialogu se profil VOLI - radek rika, ktery to bude... */
static void bl_prof_cb(lv_event_t *e)
{
    profile_set((int)(intptr_t)lv_event_get_user_data(e));
}

/* ...kdezto tlacitko v nastaveni a prepinac v liste profily STRIDAJI.
 * Tri profily znamenaji nanejvys dve tuknuti na kterykoliv z nich,
 * takze samostatna nabidka by stala vic doteku, nez kolik usetri. */
static void profile_cycle_cb(lv_event_t *e)
{
    (void)e;
    profile_set((cfg.profile + 1) % UI_PROFILES);
}

static void bl_level_cb(lv_event_t *e)
{
    int ud = (int)(intptr_t)lv_event_get_user_data(e);
    int m = ud / 1000, p = ud % 1000;
    cfg.prof[m].bright = p;
    /* Uroven ciziho profilu se jen ulozi: menit kvuli ni jas, ktery
     * prave sviti, by bylo prekvapeni. */
    if (m == cfg.profile) bl_apply();
    save_cfg();
    bl_rows_paint();
    bright_btn_paint();
}

static void bright_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *m = modal_open(T(STR_BRIGHT_TITLE), C_ACTUAL);

    /* Vedle nadpisu dve vety: co profil nese a co plati pro vsechny
     * (aby nikdo nehledal, proc prepnuti profilu nezmeni jazyk), a
     * ctvrta moznost i s duvodem, proc zatim neni. Jeden odstavec, ne
     * dva popisky - at se pri delsim prekladu nemuzou prekryt. */
    char note[256];
    snprintf(note, sizeof(note), "%s\n%s - %s", T(STR_PROF_HINT),
             T(STR_BRIGHT_VEHICLE), T(STR_BRIGHT_VEHICLE_NA));
    lv_obj_t *nl = mk_lbl(m, note, UI_FONT_14, C_LABEL, BL_NOTE_X, 12);
    lv_label_set_long_mode(nl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(nl, BL_NOTE_W);

    for (int md = 0; md < UI_PROFILES; md++) {
        btn_bl_prof[md] = mk_button(m, BL_PROF_X, BL_ROW_Y(md), BL_PROF_W,
                                    BL_ROW_H, "", UI_FONT_24, C_DEMAND,
                                    bl_prof_cb, (void *)(intptr_t)md);
        for (int i = 0; i < BL_LEVELS; i++) {
            char cap[8];
            snprintf(cap, sizeof(cap), "%d %%", BL_LEVEL[i]);
            btn_bl_level[md][i] = mk_button(m, BL_LVL_X(i), BL_ROW_Y(md),
                                            BL_LVL_W, BL_ROW_H, cap,
                                            UI_FONT_24, C_ACTUAL,
                                            bl_level_cb, BL_UD(md, BL_LEVEL[i]));
        }
    }
    bl_rows_paint();
}

/* ---------------------------------------------------------------------
 * Reset a udrzba
 *
 * Ctyri akce, ktere bud neco nevratne smazou, nebo desku restartuji.
 * Kazda je za potvrzenim drzenim (show_confirm) a u kazde je PRED
 * potvrzenim videt, co presne zmizi, a to skutecnymi cisly: kolik siti
 * je ulozeno a jak se jmenuji, co maji profily v sobe, kolik ramcu
 * citace napocitaly. "Reset nastaveni" bez dalsiho je slib naslepo a
 * clovek ho odklikne, aniz by vedel, o co prijde.
 *
 * Cisla se ctou az pri otevreni dialogu, takze nemuzou zestarnout, a
 * jsou uz na tlacitku - ne az v potvrzeni. Akce, kterou deska neumi
 * (host na ni nema ukazatel), tlacitko vubec nedostane; na jeho miste
 * je tlumeny panel s duvodem.
 *
 * Jazyk reset nemeni. Je to jedina polozka, ktera umi zpusobit, ze
 * clovek po resetu neprecte, co na desce stoji - vcetne cesty, kudy to
 * vzit zpatky. Je to napsane i v potvrzeni, aby to nebylo prekvapeni.
 * ------------------------------------------------------------------- */
#define RST_W    360
#define RST_H    120
#define RST_X(c) (30 + (c) * (RST_W + 20))
#define RST_Y(r) (96 + (r) * (RST_H + 16))

static int host_wifi_saved_count(void)
{
    return (host && host->wifi_saved_count) ? host->wifi_saved_count() : 0;
}

static const char *host_wifi_saved_ssid(int i)
{
    const char *s = (host && host->wifi_saved_ssid) ? host->wifi_saved_ssid(i) : NULL;
    return s ? s : "";
}

static bool host_can_stats(void)
{
    return host && host->can_stats && host->can_stats_reset;
}

/* Doba behu jako h:mm:ss. Ukazuje ji potvrzeni restartu i diagnostika. */
static void uptime_text(char *b, size_t n)
{
    unsigned up = (unsigned)(lv_tick_get() / 1000);
    snprintf(b, n, "%u:%02u:%02u", up / 3600, (up / 60) % 60, up % 60);
}

/* Co je v profilech prave ted, radek na profil, a pod nimi spolecna
 * cast. Presne tohle reset prepise. */
static void profiles_text(char *b, int n)
{
    int at = 0;
    for (int i = 0; i < UI_PROFILES; i++) {
        char blank[24];
        blank_text(blank, sizeof(blank), cfg.prof[i].blank_s);
        at = str_add(b, at, n, "%s   %d %%   %s\n",
                     T(PROF_STR[i]), cfg.prof[i].bright, blank);
    }
    /* Jednotky nepatri zadnemu profilu, takze stoji oddelene a
     * s popiskem - samotne "mph  C" by se cetlo jako dalsi profil. */
    str_add(b, at, n, "\n%s %s   %s %s",
            T(STR_SET_SPEED), cfg.mph ? "mph" : "km/h",
            T(STR_SET_TEMP), cfg.fahrenheit ? "F" : "C");
}

/* Vsechny citace sbernice, jak je drzi CAN. Jmena jsou shodna s
 * diagnostikou, at se to da porovnat. */
static void can_stats_text(char *b, int n)
{
    const can_stats_t *s = host->can_stats();
    str_add(b, 0, n,
            "rx %u   tx %u\n"
            "tx_fail %u   rx_lost %u   ring_lost %u\n"
            "bus_err %u   arb_lost %u   bus_off %u",
            (unsigned)s->rx, (unsigned)s->tx, (unsigned)s->tx_fail,
            (unsigned)s->rx_lost, (unsigned)s->ring_lost,
            (unsigned)s->bus_err, (unsigned)s->arb_lost,
            (unsigned)s->bus_off);
}

static void reset_cb(lv_event_t *e);

/* ---- vychozi nastaveni ---- */
static void reset_cfg_ok_cb(lv_event_t *e)
{
    (void)e;
    for (int i = 0; i < UI_PROFILES; i++) cfg.prof[i] = ui_profile_default(i);
    cfg.profile    = UI_PROF_DAY;
    cfg.mph        = false;
    cfg.fahrenheit = false;
    save_cfg();
    /* Zmenilo se vsechno naraz vcetne jednotek na ukazatelich, takze se
     * UI postavi znovu - stejne jako po zmene jazyka. Hotovou stranku
     * nastaveni s vychozimi hodnotami zaroven clovek uvidi jako doklad,
     * ze se reset opravdu stal. */
    ui_rebuild();
}

static void reset_cfg_cb(lv_event_t *e)
{
    (void)e;
    char list[192], body[448];
    profiles_text(list, (int)sizeof(list));
    snprintf(body, sizeof(body), T(STR_RESET_CFG_BODY), UI_PROFILES, list);
    show_confirm(T(STR_RESET_CFG), body, C_DEFICIT, reset_cfg_ok_cb);
}

/* ---- vsechny ulozene site ---- */
static void reset_wifi_ok_cb(lv_event_t *e)
{
    (void)e;
    if (host && host->wifi_forget_all) host->wifi_forget_all();
    reset_cb(NULL);             /* zpatky do nabidky, uz s novymi cisly */
}

static void reset_wifi_cb(lv_event_t *e)
{
    (void)e;
    int wn = host_wifi_saved_count();
    if (wn <= 0) return;        /* neni co mazat; tlacitko je nekliklatelne */
    char list[256], body[512];
    int at = 0;
    for (int i = 0; i < wn; i++)
        at = str_add(list, at, (int)sizeof(list), "%s%s",
                     i ? "\n" : "", host_wifi_saved_ssid(i));
    snprintf(body, sizeof(body), T(STR_RESET_WIFI_BODY), wn, list);
    show_confirm(T(STR_RESET_WIFI), body, C_DEFICIT, reset_wifi_ok_cb);
}

/* ---- citace sbernice ---- */
static void reset_can_ok_cb(lv_event_t *e)
{
    (void)e;
    if (host_can_stats()) host->can_stats_reset();
    reset_cb(NULL);
}

static void reset_can_cb(lv_event_t *e)
{
    (void)e;
    if (!host_can_stats()) return;
    char rows[192], body[448];
    can_stats_text(rows, (int)sizeof(rows));
    snprintf(body, sizeof(body), T(STR_RESET_CAN_BODY), rows);
    show_confirm(T(STR_RESET_CAN), body, C_DEFICIT, reset_can_ok_cb);
}

/* ---- restart desky ---- */
static void reset_reboot_ok_cb(lv_event_t *e)
{
    (void)e;
    /* Na desce se odtud uz nevraci. V simulatoru ano, takze se dialog
     * musi zavrit, jinak by zustal viset nad strankou. */
    if (host && host->reboot) host->reboot();
    modal_close_cb(NULL);
}

static void reset_reboot_cb(lv_event_t *e)
{
    (void)e;
    char up[24], body[320];
    uptime_text(up, sizeof(up));
    snprintf(body, sizeof(body), T(STR_RESET_REBOOT_BODY), up);
    show_confirm(T(STR_RESET_REBOOT), body, C_ACTUAL, reset_reboot_ok_cb);
}

/* Tlacitko nabidky: nahore co to udela, dole cislo, ktereho se to
 * tyka. Bez obsluhy (cb == NULL) vznikne tlumene, protoze neni co delat
 * - mazat nulu siti neni akce, jen zklamani. */
static void reset_btn(lv_obj_t *m, int col, int row, int title,
                      const char *sub, lv_event_cb_t cb)
{
    char cap[128];
    snprintf(cap, sizeof(cap), "%s\n%s", T(title), sub);
    lv_obj_t *b = mk_button(m, RST_X(col), RST_Y(row), RST_W, RST_H, cap,
                            UI_FONT_20, C_DEFICIT, cb, NULL);
    lv_obj_set_style_text_align(lv_obj_get_child(b, 0), LV_TEXT_ALIGN_CENTER, 0);
    if (!cb) {
        lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(lv_obj_get_child(b, 0),
                                    lv_color_hex(C_LABEL), 0);
    }
}

static void reset_open(void)
{
    lv_obj_t *m = modal_open(T(STR_RESET_TITLE), C_DEFICIT);
    char sub[96];

    snprintf(sub, sizeof(sub), T(STR_RESET_CFG_SUB), UI_PROFILES);
    reset_btn(m, 0, 0, STR_RESET_CFG, sub, reset_cfg_cb);

    int wn = host_wifi_saved_count();
    snprintf(sub, sizeof(sub), T(STR_RESET_WIFI_SUB), wn);
    reset_btn(m, 1, 0, STR_RESET_WIFI, sub, wn > 0 ? reset_wifi_cb : NULL);

    if (host_can_stats()) {
        const can_stats_t *s = host->can_stats();
        snprintf(sub, sizeof(sub), T(STR_RESET_CAN_SUB),
                 (unsigned)s->rx, (unsigned)s->tx);
        reset_btn(m, 0, 1, STR_RESET_CAN, sub, reset_can_cb);
    } else {
        reset_btn(m, 0, 1, STR_RESET_CAN, T(STR_RESET_NA), NULL);
    }

    if (host && host->reboot) {
        char up[24];
        uptime_text(up, sizeof(up));
        snprintf(sub, sizeof(sub), T(STR_RESET_REBOOT_SUB), up);
        reset_btn(m, 1, 1, STR_RESET_REBOOT, sub, reset_reboot_cb);
    } else {
        reset_btn(m, 1, 1, STR_RESET_REBOOT, T(STR_RESET_NA), NULL);
    }
}

static void reset_cb(lv_event_t *e) { (void)e; reset_open(); }

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
    /* Tah zacaty na strance musi dojit obrazovce: ta z nej vyhodnocuje
     * gesta, a to az pri pusteni prstu. */
    lv_obj_add_flag(p, LV_OBJ_FLAG_EVENT_BUBBLE);
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
    /* Graf se necte dotykem, takze dotek musi propadnout strance a z ni
     * obrazovce - jinak by z nej neslo svihnout na jinou stranku. */
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);
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

    /* Vys nez driv: vysunuta lista stranek zacina uz na 264 px. */
    mk_lbl(p, T(STR_DPF_NO_VEHICLE), UI_FONT_16, C_LABEL, 20, 242);
}

/* Nastaveni ma tri rady. Prvni ctyri tlacitka prepinaji jednu hodnotu
 * tuknutim, druha rada otevira dialogy a strida profily, treti nese
 * vypinac displeje, reset a stav pripojeni.
 *
 * Poradi v druhe rade je PROFIL a teprve pak JAS: jas uz je vlastnost
 * profilu, takze se nejdriv rekne ktereho a pak kolik. Na desce, ktera
 * stmivat neumi, zustane profil na svem miste a misto tlacitka jasu je
 * vysvetleni proc - predstirat ovladani, ktere nic nedela, by bylo
 * horsi nez ho nemit.
 *
 * Nic se nesmi dostat pod SET_BOTTOM: od tam dolu stranku prekryva
 * vysunuta lista stranek. Lista vyrostla kvuli vetsim tlacitkum
 * stranek, takze rady sedi vys a tlacitka jsou o 8 px nizsi: 68 px =
 * 8,0 mm pri 8,54 px/mm. Pod doporucenim 14 mm byla uz driv (76 px =
 * 8,9 mm); nastaveni se dela na stojacku, kdezto stranky se prepinaji
 * za jizdy, takze misto dostaly ony. */
#define SET_BOTTOM  (PAGE_H - NAV_BAR_H)
#define SET_BW      160
#define SET_BH      68
#define SET_PITCH   176
#define SET_X(i)    (20 + (i) * SET_PITCH)
#define SET_ROW1    28
#define SET_ROW2    (SET_ROW1 + SET_BH + 24)    /* nad radou je jeste popisek */
#define SET_ROW3    (SET_ROW2 + SET_BH + 6)

_Static_assert(SET_ROW3 + SET_BH <= SET_BOTTOM,
               "treti rada nastaveni by zmizela pod vysunutou listou");

static void build_nastav(lv_obj_t *scr)
{
    lv_obj_t *p = page[PG_NASTAV] = mk_page(scr);
    const lv_coord_t BW = SET_BW, BH = SET_BH;

    mk_lbl(p, T(STR_SET_SPEED), UI_FONT_14, C_LABEL, SET_X(0), 8);
    btn_units_v = mk_button(p, SET_X(0), SET_ROW1, BW, BH,
                            cfg.mph ? "mph" : "km/h",
                            UI_FONT_24, C_DEMAND, units_v_cb, NULL);
    mk_lbl(p, T(STR_SET_TEMP), UI_FONT_14, C_LABEL, SET_X(1), 8);
    btn_units_t = mk_button(p, SET_X(1), SET_ROW1, BW, BH,
                            cfg.fahrenheit ? "F" : "C",
                            UI_FONT_24, C_DEMAND, units_t_cb, NULL);
    /* ZHASNOUT PO patri profilu, takze popisek tlacitka dosadi az
     * blank_btn_paint() podle toho, ktery profil prave plati. */
    mk_lbl(p, T(STR_SET_BLANK), UI_FONT_14, C_LABEL, SET_X(2), 8);
    btn_blank = mk_button(p, SET_X(2), SET_ROW1, BW, BH, "",
                          UI_FONT_24, C_DEMAND, blank_cb, NULL);
    blank_btn_paint();
    mk_lbl(p, T(STR_SET_LANG), UI_FONT_14, C_LABEL, SET_X(3), 8);
    mk_button(p, SET_X(3), SET_ROW1, BW, BH, i18n_name(cfg.lang), UI_FONT_24,
              C_SAGE, lang_cb, NULL);

    mk_lbl(p, "WIFI", UI_FONT_14, C_LABEL, 20, SET_ROW2 - 20);
    mk_button(p, 20, SET_ROW2, 320, BH, T(STR_WIFI_PICK), UI_FONT_24,
              C_DEMAND, wifi_scan_cb, NULL);

    mk_lbl(p, T(STR_SET_PROFILE), UI_FONT_14, C_LABEL, 360, SET_ROW2 - 20);
    btn_profile = mk_button(p, 360, SET_ROW2, BW, BH, "", UI_FONT_24,
                            C_SAGE, profile_cycle_cb, NULL);
    profile_btn_paint();

    mk_lbl(p, T(STR_SET_BRIGHT), UI_FONT_14, C_LABEL, 540, SET_ROW2 - 20);
    btn_bright = NULL;
    if (bl_can_dim()) {
        btn_bright = mk_button(p, 540, SET_ROW2, 170, BH, "", UI_FONT_24,
                               C_ACTUAL, bright_cb, NULL);
        bright_btn_paint();
    } else {
        lv_obj_t *na = mk_lbl(p, T(STR_BRIGHT_NA), UI_FONT_14, C_LABEL,
                              540, SET_ROW2);
        lv_label_set_long_mode(na, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(na, 176);
    }

    lbl_wifi = mk_lbl(p, T(STR_WIFI_NOT_CONN), UI_FONT_20, C_LABEL,
                      20, SET_ROW3);
    lbl_heap = mk_lbl(p, "", UI_FONT_16, C_LABEL, 20, SET_ROW3 + 32);

    /* Primy vypinac podsviceni. Probuzeni je dotekem kamkoliv, stejne jako
     * u automatickeho zhasnuti - resi to ui_tick() pres necinnost LVGL.
     * Zhasina natvrdo, bez stmivani: hlavni duvod existence je merit na
     * ploskach BL/GND a tam je potreba znamy stav, ne prejezd. */
    mk_button(p, 360, SET_ROW3, 170, BH, T(STR_SET_DISPLAY_OFF), UI_FONT_24,
              C_ACTUAL, display_off_cb, NULL);

    /* Reset stoji az za vsim, co se da nastavit, a jeste jednou za
     * dialogem - nemuze se na nej sahnout omylem pri hrani s jednotkami.
     * Vic nez po 716 px nesmi nic sahnout: od 736 px je pravy sloupec
     * DPF a stranka je od tam orezana. */
    mk_button(p, 546, SET_ROW3, 170, BH, T(STR_SET_MAINT), UI_FONT_24,
              C_DEFICIT, reset_cb, NULL);
}

/* Sloupec je vysoky jako stranka, ale jeho obsah musi zustat nad uchytem
 * navigace - ten je videt porad. Listu a mrizku UI naopak prekryt necha:
 * obe samy spadnou po peti vterinach a drzet kvuli nim volnou spodni
 * tretinu obrazovky by znamenalo prijit o ni i po zbytek casu. */
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

/* ---------------------------------------------------------------------
 * Prepinac profilu v horni liste
 *
 * Profil se meni casteji nez cokoliv jineho - pri kazdem vjezdu do
 * tmy, pri kazdem navratu k verstaku - takze musi byt na dosah z
 * KAZDE stranky, ne tri obrazovky hluboko. Horni lista je jedine
 * misto, ktere je videt porad. Driv tu byl prepinac den/noc a byl
 * volitelny; profilu se to uz netyka, protoze je to jedina rychla
 * cesta a schovany vypinac by z ni udelal hledacku.
 *
 * Tuknuti strida profily dokola a vede do profile_cycle_cb() stejne
 * jako tlacitko v nastaveni, takze se obe cesty nemuzou rozejit.
 *
 * CO JE VIDET: jmeno profilu a vedle nej tri rysky, z nichz sviti ta
 * prave platna. Samotne jmeno by stacilo tomu, kdo profily zna; rysky
 * navic rikaji, KOLIK jich je a kolikaty je ten dnesni, takze se da
 * prepnuti zkontrolovat kratkym pohledem i periferne. Kresba je z
 * obycejnych objektu LVGL, ne ze znaku fontu - Montserrat zadnou
 * takovou znacku nema a lv_canvas by si rekl o vlastni obrazovou
 * pamet.
 *
 * DOTYKOVY CIL je 164 x 64 px = 19,2 x 7,5 mm. Na sirku doporuceni
 * 14 mm z docs/ui.md plati, na vysku NE a splnit se neda: horni lista
 * je vysoka 64 px a kvuli jednomu tlacitku by musela narust na 120 px,
 * tedy kazda stranka prijit o 56 px. Minuti stoji jedno tuknuti navic,
 * ne chybnou akci, a nahrada je v nastaveni.
 * ------------------------------------------------------------------- */
#define DN_W   164
#define DN_X   (800 - DN_W - 8)
#define DN_MW  8                        /* ryska profilu: sirka */
#define DN_MG  5                        /* mezera mezi ryskami */
#define DN_MH  28                       /* vyska platne rysky */
#define DN_MHD 12                       /* a te ostatni */
#define DN_MX  8                        /* rysky vlevo, jmeno vpravo */
#define DN_MB  ((TOP_H + DN_MH) / 2)    /* spolecna spodni hrana rysek */
#define DN_LX  (DN_MX + UI_PROFILES * (DN_MW + DN_MG) + 4)

static lv_obj_t *mk_shape(lv_obj_t *p, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, lv_coord_t h, lv_coord_t r, uint32_t c)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void prof_bar_paint(void)
{
    if (!prof_btn) return;
    for (int i = 0; i < UI_PROFILES; i++) {
        bool live = (i == cfg.profile);
        lv_obj_set_pos(prof_mark[i], DN_MX + i * (DN_MW + DN_MG),
                       DN_MB - (live ? DN_MH : DN_MHD));
        lv_obj_set_size(prof_mark[i], DN_MW, live ? DN_MH : DN_MHD);
        lv_obj_set_style_bg_color(prof_mark[i],
                                  lv_color_hex(live ? C_BRIGHT : C_RULE), 0);
    }
    lv_label_set_text(prof_name, T(PROF_STR[cfg.profile]));
}

static void prof_build(lv_obj_t *bar)
{
    /* Dotykova oblast je cely obdelnik v liste; kresba i jmeno zabiraji
     * jen jeho cast, takze je cil vetsi nez to, co je videt. */
    prof_btn = mk_shape(bar, DN_X, 0, DN_W, TOP_H, 0, C_PANEL);
    lv_obj_add_flag(prof_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(prof_btn, press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(prof_btn, drag_guard_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(prof_btn, profile_cycle_cb, LV_EVENT_CLICKED, NULL);

    for (int i = 0; i < UI_PROFILES; i++)
        prof_mark[i] = mk_shape(prof_btn, 0, 0, DN_MW, DN_MH, 1, C_RULE);

    prof_name = mk_lbl(prof_btn, "", UI_FONT_16, C_BRIGHT, DN_LX, 22);
    prof_bar_paint();
}

/* ---------------------------------------------------------------------
 * Diagnostika (dlazdice DEBUG v mrizce navigace)
 *
 * LVGL ma LV_USE_PERF_MONITOR i LV_USE_MEM_MONITOR, jenze to jsou
 * PREKLADOVE prepinace: z menu se zapnout nedaji a prekryv by byl videt
 * porad. Mereni je proto vlastni. monitor_cb ovladace displeje se vola
 * po kazdem OPRAVDU prekreslenem snimku, takze staci scitat snimky a
 * cas kresleni a jednou za vterinu z toho udelat FPS.
 *
 * Volnou pamet LVGL rika lv_mem_monitor(). Pri LV_MEM_CUSTOM = 1 - a tak
 * je to tady - zadny vlastni pool neni a funkce vraci nuly; vypsat
 * "0 kB volno" by byla lez, takze se v tom pripade rekne rovnou, ze
 * LVGL bere pamet ze systemu, a plati cisla z desky (heap, PSRAM).
 *
 * Statistiky sbernice jsou zatim PRAZDNE RADKY. can_stats_t v
 * src/can/twai.h existuje, ale CAN na UI jeste napojeny neni. Radky se
 * jmenuji stejne jako polozky te struktury a misto cisel maji pomlcku,
 * aby bylo videt, ze data nejsou - vymyslet si je by bylo horsi nez je
 * neukazat.
 * ------------------------------------------------------------------- */
#define PERF_WIN_MS 1000
#define DBG_COL2    400

static const char *const DBG_BUS_ROW[] = {
    "rx", "tx", "tx_fail", "rx_lost", "ring_lost", "bus_err", "arb_lost", "bus_off"
};
#define DBG_BUS_ROWS ((int)(sizeof(DBG_BUS_ROW) / sizeof(DBG_BUS_ROW[0])))

static void perf_monitor_cb(lv_disp_drv_t *drv, uint32_t time, uint32_t px)
{
    (void)drv; (void)px;
    perf_frames++;
    perf_draw_sum += time;
}

/* Radek o pameti LVGL. Pri LV_MEM_CUSTOM = 1 zadny vlastni pool neni a
 * lv_mem_monitor() vraci nuly - vypsat "0 kB volno" by byla lez, takze
 * se rekne rovnou, odkud LVGL pamet bere. */
static int lvgl_mem_line(char *b, size_t n)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    if (mon.total_size)
        return snprintf(b, n, "lvgl %u kB volno  frag %u %%",
                        (unsigned)(mon.free_size / 1024), (unsigned)mon.frag_pct);
    return snprintf(b, n, "lvgl bere pamet ze systemu");
}

static void perf_paint(void)
{
    if (!perf_lbl) return;
    char b[96];
    int at = snprintf(b, sizeof(b), "FPS %d.%d   KRESLENI %d.%d ms\n",
                      perf_fps10 / 10, perf_fps10 % 10,
                      perf_draw10 / 10, perf_draw10 % 10);
    snprintf(b + at, sizeof(b) - (size_t)at, "HEAP %u kB  PSRAM %u kB",
             host_heap_kb(), host_psram_kb());
    lv_label_set_text(perf_lbl, b);
}

static void perf_tick(uint32_t now)
{
    uint32_t el = now - perf_win_start;
    if (el < PERF_WIN_MS) return;
    perf_fps10  = (int)(perf_frames * 10000u / el);
    perf_draw10 = perf_frames ? (int)(perf_draw_sum * 10u / perf_frames) : 0;
    perf_frames = 0;
    perf_draw_sum = 0;
    perf_win_start = now;
    if (perf_on) perf_paint();
}

static void dbg_perf_paint(void)
{
    if (!dbg_perf_btn) return;
    char cap[48];
    snprintf(cap, sizeof(cap), "%s\n%s", T(STR_DBG_PERF),
             T(perf_on ? STR_ON : STR_OFF));
    lv_label_set_text(lv_obj_get_child(dbg_perf_btn, 0), cap);
}

static void dbg_perf_cb(lv_event_t *e)
{
    (void)e;
    perf_on = !perf_on;
    if (perf_lbl) {
        if (perf_on) lv_obj_clear_flag(perf_lbl, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(perf_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    perf_paint();
    dbg_perf_paint();
}

/* Hodnoty jsou syrove: zadne jednotky, zadne zaokrouhleni na ukazatele,
 * jen cilova a zobrazena hodnota v setinach, jak je UI drzi. */
static int dbg_val(char *b, int at, int n, const char *name, const val_t *v)
{
    return str_add(b, at, n, "%s %d %d\n", name,
                   (int)(v->target * 100.0f), (int)(v->shown * 100.0f));
}

static void dbg_paint(void)
{
    if (!dbg_raw) return;

    char b[384];
    int n = (int)sizeof(b);
    int at = str_add(b, 0, n, "page %d  nav %d  regen %d %d\n",
                     cur_page, nav_state, (int)regen_state, regen_pct);
    at = dbg_val(b, at, n, "speed", &v_speed);
    at = dbg_val(b, at, n, "rpm", &v_rpm);
    at = dbg_val(b, at, n, "temp", &v_temp);
    at = dbg_val(b, at, n, "boost", &v_boost);
    at = dbg_val(b, at, n, "boost_sp", &v_boost_sp);
    at = dbg_val(b, at, n, "vnt", &v_vnt);
    at = dbg_val(b, at, n, "rail", &v_rail);
    dbg_val(b, at, n, "soot", &v_soot);
    lv_label_set_text(dbg_raw, b);

    if (!dbg_mem) return;
    char m[192];
    int mn = (int)sizeof(m);
    char up[24];
    uptime_text(up, sizeof(up));
    at = str_add(m, 0, mn, "heap %u kB\npsram %u kB\n",
                 host_heap_kb(), host_psram_kb());
    at += lvgl_mem_line(m + at, (size_t)(mn - at));
    if (at > mn - 1) at = mn - 1;
    str_add(m, at, mn, "\nbeh %s\nfps %d.%d\nkresleni %d.%d ms", up,
            perf_fps10 / 10, perf_fps10 % 10,
            perf_draw10 / 10, perf_draw10 % 10);
    lv_label_set_text(dbg_mem, m);
}

static void debug_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *m = modal_open(T(STR_DBG_TITLE), C_DEMAND);

    dbg_perf_btn = mk_button(m, 30, 96, 300, 120, "", UI_FONT_20,
                             C_ACTUAL, dbg_perf_cb, NULL);
    dbg_perf_paint();

    mk_lbl(m, T(STR_DBG_RAW), UI_FONT_14, C_LABEL, 30, 232);
    dbg_raw = mk_lbl(m, "", UI_FONT_16, C_BRIGHT, 30, 254);

    mk_lbl(m, T(STR_DBG_BUS), UI_FONT_14, C_LABEL, DBG_COL2, 96);
    mk_lbl(m, T(STR_DBG_BUS_NA), UI_FONT_20, C_DEFICIT, DBG_COL2, 116);
    char rows[192];
    int at = 0;
    for (int i = 0; i < DBG_BUS_ROWS; i++)
        at += snprintf(rows + at, sizeof(rows) - (size_t)at, "%s  -\n",
                       DBG_BUS_ROW[i]);
    mk_lbl(m, rows, UI_FONT_16, C_LABEL, DBG_COL2, 150);

    mk_lbl(m, T(STR_DBG_MEM), UI_FONT_14, C_LABEL, DBG_COL2, 322);
    dbg_mem = mk_lbl(m, "", UI_FONT_16, C_BRIGHT, DBG_COL2, 344);
    dbg_paint();
}

/* ---------------- chrome ---------------- */
static void build_chrome(lv_obj_t *scr)
{
    lv_obj_t *bar = mk_panel(scr, 0, 0, 800, TOP_H);
    mk_lbl(bar, "TALENTO", UI_FONT_20, C_BRIGHT, 16, 20);
    mk_lbl(bar, T(STR_TOP_TEMP), UI_FONT_14, C_LABEL, 320, 8);
    lbl_temp = mk_lbl(bar, "--", UI_FONT_32, C_BRIGHT, 320, 24);
    lbl_state = mk_lbl(bar, "DEMO", UI_FONT_16, C_DEFICIT, 560, 22);
    prof_build(bar);

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
    /* Sloupec je ukazatel, ne ovladaci prvek. Na dotek nereaguje ani on,
     * ani nic v nem, takze tah zacaty u praveho okraje je porad gesto
     * obrazovky - a tam zacina svihnuti na dalsi stranku. */
    lv_obj_clear_flag(rail, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    rail_rule = lv_obj_create(rail);
    lv_obj_set_pos(rail_rule, 0, 0);
    lv_obj_set_style_bg_color(rail_rule, lv_color_hex(C_RULE), 0);
    lv_obj_set_style_bg_opa(rail_rule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rail_rule, 0, 0);
    lv_obj_set_style_radius(rail_rule, 0, 0);
    lv_obj_clear_flag(rail_rule, LV_OBJ_FLAG_CLICKABLE);

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
    lv_obj_clear_flag(rail_bar, LV_OBJ_FLAG_CLICKABLE);

    /* Rysky vznikaji az po baru, takze se kresli nad vypln. */
    for (int i = 0; i < 4; i++) {
        lv_obj_t *t = rail_tick[i] = lv_obj_create(rail);
        lv_obj_set_size(t, RAIL_TICK_W, RAIL_TICK_H);
        lv_obj_set_style_bg_color(t, lv_color_hex(C_LABEL), 0);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(t, 0, 0);
        lv_obj_set_style_radius(t, 0, 0);
        lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE);
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

    /* Lista stranek a mrizka dlazdic jsou JEDEN panel, ne dva: lista
     * nahore, dlazdice pod ni. Panel ma porad vysku NAV_FULL_H a jen
     * vyjizdi zespodu, takze ve stavu 1 kouka nad spodni hranu jeho
     * horni pruh s tlacitky stranek a ve stavu 2 se pod nimi odkryji
     * dlazdice. Driv to byly dva panely, mrizka listu prekryla a z
     * mrizky nebylo kam prepnout stranku. */
    navpanel = mk_panel(scr, 0, 480 - NAV_FULL_H, 800, NAV_FULL_H);
    /* Panel navigace je vyjimka z pravidla u mk_panel(): dotek na nem
     * chce sam, protoze se jim tahne. */
    lv_obj_add_flag(navpanel, LV_OBJ_FLAG_CLICKABLE);
    nav_attach_drag(navpanel);
    for (int i = 0; i < N_PAGES; i++)
        navbtn[i] = mk_button(navpanel, NAV_BTN_X(i), NAV_BTN_Y,
                              NAV_BTN_W, NAV_BTN_H, T(PAGE_STR[i]),
                              UI_FONT_28, C_DEMAND, nav_cb,
                              (void *)(intptr_t)i);

    mk_lbl(navpanel, T(STR_NAV_PLACEHOLDER), UI_FONT_14, C_LABEL,
           16, NAV_BAR_H + 2);
    for (int r = 0; r < NAV_TILE_ROWS; r++)
        for (int c = 0; c < NAV_TILE_COLS; c++) {
            lv_coord_t x = NAV_BTN_X(c), y = NAV_TILE_Y(r);
            if (r * NAV_TILE_COLS + c == NAV_TILE_DEBUG) {
                mk_button(navpanel, x, y, NAV_BTN_W, NAV_TILE_H,
                          T(STR_NAV_DEBUG), UI_FONT_20, C_DEMAND,
                          debug_cb, NULL);
                continue;
            }
            /* zatim prazdne misto; dotek na nem patri panelu, takze se
             * z dlazdice tahne stejne jako odkudkoliv jinud */
            lv_obj_t *t = mk_panel(navpanel, x, y, NAV_BTN_W, NAV_TILE_H);
            lv_obj_set_style_bg_color(t, lv_color_hex(C_INK), 0);
            lv_obj_set_style_border_color(t, lv_color_hex(C_RULE), 0);
        }

    /* Prekryv s FPS a pameti. Zije porad a je vetsinou schovany, aby ho
     * slo z diagnostiky zapnout bez prestavby UI. */
    perf_lbl = mk_lbl(scr, "", UI_FONT_14, C_BRIGHT, 496, 70);
    lv_obj_set_style_bg_color(perf_lbl, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_bg_opa(perf_lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(perf_lbl, lv_color_hex(C_RULE), 0);
    lv_obj_set_style_border_width(perf_lbl, 1, 0);
    lv_obj_set_style_pad_all(perf_lbl, 6, 0);
    if (!perf_on) lv_obj_add_flag(perf_lbl, LV_OBJ_FLAG_HIDDEN);
    perf_paint();

    gest_hint_build();
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
                /* Cas se orezava PRED nasobenim: el * 100 pretece po
                 * necelych dvanacti hodinach a orez az na vysledku by
                 * to nezachranil - vypln by skocila na nahodnou uroven,
                 * ktera vypada jako namerene cislo. Prepinac zapnuty
                 * pres noc je presne to pouziti, pro ktere existuje. */
                uint32_t t = el > RGN_SIM_MS ? RGN_SIM_MS : el;
                regen_pct = (int)(t * 100 / RGN_SIM_MS);
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
            host_heap_kb(), host_psram_kb(), host_wifi_ip());
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
    perf_tick(now);
    dbg_paint();        /* jen kdyz je otevrena diagnostika */

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
    } else if (PROF.blank_s && backlight_on && idle > PROF.blank_s * 1000UL) {
        blank_begin();
    }
    bl_step();
}

/* ---------------- stavba ---------------- */
void ui_build(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_INK), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    /* Gesto se jen napne, provede se az pri pusteni - a k tomu je
     * potreba vedet, kde tah zacal (press_cb) a kam dosel. Stranky i
     * tlacitka maji EVENT_BUBBLE, takze sem dojde i tah zacaty na nich. */
    lv_obj_add_event_cb(scr, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(scr, press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, gest_pressing_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, gest_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(scr, gest_release_cb, LV_EVENT_PRESS_LOST, NULL);

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
    lv_obj_move_foreground(navpanel);
    lv_obj_move_foreground(navhandle);
    lv_obj_move_foreground(perf_lbl);

    /* Mereni snimku. LV_USE_PERF_MONITOR je PREKLADOVY prepinac, takze
     * se z menu zapnout neda; monitor_cb ovladace displeje se zavola po
     * kazdem opravdu prekreslenem snimku a stoji jen dve scitani. */
    lv_disp_t *disp = lv_disp_get_default();
    if (disp && disp->driver) disp->driver->monitor_cb = perf_monitor_cb;

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
