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

/* Sloupce stavoveho radku. Vejit se musi vsechno a nic se nesmi
 * prekryt: prepinac profilu zacina na 628 px (DN_X), takze stav
 * vozidla ma 150 px - dost i na nejdelsi preklad, "KEINE ANTWORT". */
#define TOP_WIFI_X  12      /* carky signalu */
#define TOP_SSID_X  52      /* jmeno site */
#define TOP_TEMP_X  270     /* teplota chladici kapaliny */
#define TOP_XFER_X  440     /* sipky prenosu po sbernici */
#define TOP_STATE_X 478     /* stav pripojeni k vozidlu */

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

/* Ktery pohled diagnostiky kazda stranka potrebuje. Vrstva podle toho
 * cte jen to, co je opravdu videt - viz src/diag/poller.h. */
static const diag_view_t PAGE_VIEW[N_PAGES] = {
    DIAG_VIEW_MAIN, DIAG_VIEW_ENGINE, DIAG_VIEW_BOOST,
    DIAG_VIEW_FAULTS, DIAG_VIEW_DPF, DIAG_VIEW_OTHER,
};

static lv_obj_t *page[N_PAGES], *navbtn[N_PAGES];
static lv_obj_t *rail;
static int cur_page = PG_HLAVNI;

/* ---------------- trvale prvky ---------------- */
static lv_obj_t *lbl_temp, *lbl_state, *lbl_state_age, *rail_bar, *rail_val;
static lv_obj_t *rail_rule, *rail_tick[4];
/* naposled nastavene pozadi sloupce; prekresluje se jen pri zmene */
static uint32_t rail_bg_shown = C_INK;
/* hlavni */
static lv_obj_t *arc_speed, *arc_rpm, *lbl_speed_v, *lbl_rpm_v, *lbl_speed_u, *lbl_rpm_u;
/* motor */
static lv_obj_t *arc_boost, *lbl_boost, *lbl_boost_sp, *lbl_vnt;
static lv_obj_t *lbl_railp, *lbl_rail_sp, *lbl_maf, *lbl_maf_sp, *lbl_egr, *lbl_iat;
/* tlak */
static lv_obj_t *chart, *lbl_gap;
static lv_chart_series_t *ser_sp, *ser_act;
/* chyby */
#define DTC_ROWS 4
static lv_obj_t *dtc_head, *dtc_odo, *dtc_row[DTC_ROWS], *dtc_hint;
static lv_obj_t *dtc_ext, *dtc_ff;
/* nastaveni */
static lv_obj_t *btn_units_v, *btn_units_t, *btn_blank, *lbl_wifi, *lbl_heap;
static lv_obj_t *btn_bright, *btn_profile, *btn_canmode, *btn_wifi;
/* DPF */
static lv_obj_t *lbl_dpf[6], *lbl_regen;
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

/* Prepinani rezimu sbernice. Kdyz deska jednu z dvojice nema, UI
 * prepinani vubec nenabidne - predstirat ovladani, ktere nic nedela,
 * je horsi nez ho nemit. */
static bool host_can_mode(void)
{
    return host && host->can_is_normal && host->can_set_normal;
}

static bool host_can_normal(void)
{
    return host_can_mode() ? host->can_is_normal() : false;
}

/* ---------------------------------------------------------------------
 * Diagnostika
 *
 * Jediny zdroj cisel v cele UI. Dokud je NULL, nema UI co ukazat a
 * nikde se neobjevi zadna hodnota - to je spravne chovani, ne chyba.
 * ------------------------------------------------------------------- */
static diag_poller_t *dp = NULL;

void ui_set_diag(diag_poller_t *p) { dp = p; }

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
    /* DILNA ztlumi po deseti minutach: deska na stole bezi cele hodiny
     * a vetsinu z nich u ni nikdo nestoji. DEN a NOC maji ztlumeni
     * vypnute schvalne - pocita se od posledniho DOTEKU a za jizdy se
     * displeje nikdo nedotyka, takze by se ztlumilo prave uprostred
     * jizdy, kdy je cteni nejpotrebnejsi.
     *
     * Ztlumena uroven je nejnizsi nabizena (BL_MIN): cilem je prestat
     * svitit do kabiny, ne setrit proud, a cokoliv vyssiho uz je v noci
     * videt v cele kabine. */
    static const ui_profile_t def[UI_PROFILES] = {
        [UI_PROF_SHOP]  = { .bright = 100, .dim_s = 600, .dim_bright = BL_MIN,
                            .perf = false },
        [UI_PROF_DAY]   = { .bright = 100, .dim_s = 0,   .dim_bright = BL_MIN,
                            .perf = false },
        [UI_PROF_NIGHT] = { .bright = 35,  .dim_s = 0,   .dim_bright = BL_MIN,
                            .perf = false },
    };
    if (i < 0 || i >= UI_PROFILES) i = UI_PROF_DAY;
    return def[i];
}

void ui_set_cfg(const ui_cfg_t *c)
{
    if (!c) return;
    cfg = *c;
    for (int i = 0; i < UI_PROFILES; i++) {
        cfg.prof[i].bright     = bl_clamp(cfg.prof[i].bright);
        cfg.prof[i].dim_bright = bl_clamp(cfg.prof[i].dim_bright);
    }
    /* Profil mimo rozsah by indexoval mimo pole, takze se musi orezat
     * drive, nez se podle nej cokoliv nastavi. */
    if (cfg.profile < 0 || cfg.profile >= UI_PROFILES) cfg.profile = UI_PROF_DAY;
    /* i18n_set() necha neplatny index byt, takze cfg.lang musi projit
     * stejnou kontrolou - jinak by ui_rebuild() cyklovalo od nesmyslu. */
    i18n_set(cfg.lang);
    cfg.lang = i18n_get();
}

/* ---------------------------------------------------------------------
 * Hodnoty
 *
 * Z vrstvy diagnostiky chodi nova hodnota desetkrat za vterinu (ta
 * nejrychlejsi skupina), kdezto UI se prekresluje padesatkrat - proto
 * se mezi zmerenymi body dojizdi. Vyhlazeni je POUZE kosmeticke: co
 * se smi ukazat, rozhoduje vyhradne diag_poller_show(), a dokud ta
 * rekne DIAG_SHOW_NONE, nenakresli se zadne cislo.
 *
 * Rychlost dojezdu se bere ze skupiny kanalu. Rychla skupina dojizdi
 * za ctyri snimky, pomala za sedmnact - pomala velicina, ktera skace,
 * vypada jako sum, a plynule se tak kresli i teplota ctena po vterine.
 * ------------------------------------------------------------------- */
static float   v_shown[DIAG_CH__COUNT];
static bool    v_held[DIAG_CH__COUNT];   /* uz mame z ceho dojizdet */

static const float TIER_K[DIAG_TIER__COUNT] = { 0.25f, 0.12f, 0.06f };

/* Kratka jmena kanalu. Nejsou to texty pro uzivatele, ale oznaceni
 * velicin ve stejnem tvaru, v jakem jsou v profilu vozidla
 * (docs/vehicles, sloupec Key), takze nechodi pres i18n - stejne jako
 * nazvy citacu sbernice v diagnostice. Cte je freeze frame na strance
 * chyb a tabulka syrovych hodnot. */
static const char *const CH_NAME[DIAG_CH__COUNT] = {
    "rpm", "speed", "boost", "boost_sp", "rail", "rail_sp", "vnt",
    "maf", "maf_sp", "egr", "iat", "coolant", "dpf_up", "dpf_dn",
    "dpf_sp", "soot", "load", "rgn_dur", "rgn_end", "odo"
};

static diag_show_t ch_show(int ch)
{
    return dp ? diag_poller_show(dp, ch) : DIAG_SHOW_NONE;
}

/* true = hodnota existuje, tedy se SMI ukazat jako cislo. */
static bool ch_has(int ch)
{
    diag_show_t s = ch_show(ch);

    return s == DIAG_SHOW_LIVE || s == DIAG_SHOW_STALE;
}

static float ch_val(int ch) { return v_shown[ch]; }

static void vals_step(void)
{
    for (int ch = 0; ch < DIAG_CH__COUNT; ch++) {
        if (!ch_has(ch)) {
            /* Bez hodnoty se nedojizdi nikam. Az nejaka prijde, zacne
             * se od ni, ne od toho, co tu zbylo po minule jizde. */
            v_held[ch] = false;
            continue;
        }
        float t = diag_poller_value(dp, ch);
        if (!v_held[ch]) {
            v_held[ch] = true;
            v_shown[ch] = t;
        } else {
            v_shown[ch] += (t - v_shown[ch]) * TIER_K[diag_poller_tier(ch)];
        }
    }
}

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
 * Jak se kresli jedna hodnota
 *
 * Jedno misto pro vsechny stranky, aby se ctyri stavy nemohly nikde
 * rozejit. Rozdil musi byt poznat na prvni pohled, ne po precteni:
 *
 *   ziva          cislo v jeho barve (kostena skutecnost, modra zadani)
 *   stara         totez cislo, ale tlumene sede - uz to neni ted
 *   neprectena    VAL_NONE, tedy pomlcky; ZADNE cislo, ani nula
 *   odmitnuta     "7F xx" cervene, kde xx je duvod od jednotky
 *
 * Odmitnuti ma vlastni podobu zamerne. "Tenhle DID tahle verze
 * softwaru nema" je uplne jina informace nez "jeste jsem se nezeptal",
 * a kdo cte diagnostiku, ctyri znaky 7F 31 pozna rovnou. Cervena a
 * sestnactkova soustava navic nejdou splest s merenou hodnotou.
 *
 * Format se vyhodnocuje jen tehdy, kdyz hodnota opravdu je. Volajici
 * tedy muze pocitat prepocty do jednotek bez rozmysleni - kdyz neni
 * co ukazat, nikdo se na jeho cislo nepodiva.
 * ------------------------------------------------------------------- */
#define VAL_NONE "---"

static void val_fmt(lv_obj_t *l, int ch, uint32_t color, const char *fmt, ...)
{
    if (!l) return;

    char b[24];
    uint32_t c = color;

    switch (ch_show(ch)) {
    case DIAG_SHOW_LIVE:
    case DIAG_SHOW_STALE: {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(b, sizeof(b), fmt, ap);
        va_end(ap);
        if (ch_show(ch) == DIAG_SHOW_STALE) c = C_LABEL;
        break; }
    case DIAG_SHOW_REFUSED:
        snprintf(b, sizeof(b), "7F %02X", diag_poller_nrc(dp, ch));
        c = C_DEFICIT;
        break;
    default:
        snprintf(b, sizeof(b), VAL_NONE);
        c = C_LABEL;
        break;
    }
    lv_label_set_text(l, b);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
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

/* ---------------------------------------------------------------------
 * Tlacitko, ktere OTEVIRA dialog
 *
 * V nastaveni stoji vedle sebe dva druhy tlacitek a az dosud vypadaly
 * stejne. Jedno tuknutim rovnou meni hodnotu (km/h <-> mph, jazyk,
 * profil), druhe otevira dalsi obrazovku (jas, ztlumeni, WiFi, udrzba).
 * Tlacitko JAS pritom ukazovalo "75 %", tedy presne to, co ukazuje
 * tlacitko prepinaci - takze se cetlo jako prepinac procent.
 *
 * Rozlisuji se DVEMA vecmi najednou, protoze jedna by se na slunci v
 * dilne ztratila:
 *
 *   - popisek konci trema teckami. Je to stara a srozumitelna znacka
 *     "tohle jeste neco otevre" a prezije i preklad.
 *   - obruba ma barvu akce misto vlasove linky, takze tlacitko ma
 *     viditelny ramecek uz v klidu, ne az pri stisku.
 *
 * Hodnota na tlacitku zustava - rict "JAS" a neprozradit kolik by byla
 * ztrata informace. Mluvi se tedy o tom, co je ted, a tecky rikaji,
 * ze se to zmeni az jinde.
 * ------------------------------------------------------------------- */
static void dlg_btn_text(lv_obj_t *b, const char *text)
{
    char s[48];

    if (!b) return;
    snprintf(s, sizeof(s), "%s ...", text);
    lv_label_set_text(lv_obj_get_child(b, 0), s);
}

static lv_obj_t *mk_dlg_button(lv_obj_t *p, lv_coord_t x, lv_coord_t y,
                               lv_coord_t w, lv_coord_t h, const char *text,
                               const lv_font_t *font, uint32_t accent,
                               lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = mk_button(p, x, y, w, h, "", font, accent, cb, user);

    lv_obj_set_style_border_color(b, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    dlg_btn_text(b, text);
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
 * Po necinnosti se jas za pul vteriny STAHNE NA ZVOLENOU UROVEN a tam
 * zustane. Displej se nevypina: zhasly displej za jizdy nerika nic -
 * clovek nepozna, jestli deska bezi - kdezto ztlumeny je porad
 * citelny a v noci dokonce prijemnejsi. Uplne zhasnout jde uz jen
 * rucne (tlacitko ZHASNOUT DISPLEJ), coz je jina vec: to je zamer, ne
 * setreni oci.
 *
 * Deska bez dratku na plosce BL stmivat neumi, takze na ni ztlumeni
 * nema cim probehnout a zustava drivejsi zhasnuti. UI to u volby
 * rekne; mlcet by znamenalo slibit uroven, ktera se nikde neprojevi.
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
static bool bl_dimmed = false;      /* jsme na ztlumene urovni */
static bool bl_fading = false;      /* prejezd dolu na ztlumenou uroven */
static bool bl_waking = false;      /* nabeh zpet na plnou */
static uint32_t bl_fade_start = 0;
static uint32_t bl_wake_start = 0;
static int bl_fade_from = 0, bl_fade_to = 0;
static int bl_wake_from = 0;        /* jas, ze ktereho nabeh zacal */
static int bl_shown = -1;           /* naposled poslany jas; -1 = nic */

/* dialog jasu, resetu a diagnostiky - zije jen po dobu otevreneho
 * dialogu, uklizi modal_forget() */
static lv_obj_t *btn_bl_level[UI_PROFILES][BL_LEVELS];
static lv_obj_t *btn_bl_prof[UI_PROFILES];
static lv_obj_t *dbg_raw, *dbg_mem, *dbg_bus, *dbg_poll;
static lv_obj_t *dbg_perf_btn, *dbg_save_btn;
#define DIM_STEPS 4
static lv_obj_t *btn_dim_time[DIM_STEPS], *btn_dim_lvl[BL_LEVELS];

static bool bl_can_dim(void) { return host && host->bl_set_percent; }

static int bl_level(void) { return PROF.bright; }

/* Uroven, na kterou se ztlumuje. Nikdy nesmi byt vyssi nez bezny jas
 * profilu - ztlumeni, ktere pridava svetlo, by bylo proti smyslu slova
 * i proti tomu, co clovek v noci ceka. */
static int bl_dim_level(void)
{
    int d = PROF.dim_bright;

    return (d > PROF.bright) ? PROF.bright : d;
}

static void bl_set(int percent)
{
    if (!bl_can_dim() || percent == bl_shown) return;
    bl_shown = percent;
    host->bl_set_percent(percent);
}

static void bl_apply(void)
{
    bl_waking = false;              /* rucni volba prebiji nabeh */
    bl_fading = false;
    bl_dimmed = false;
    bl_set(bl_level());
}

static void backlight(bool on)
{
    bl_fading = false;
    if (on) {
        bl_dimmed = false;
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

/* Zacatek ztlumeni po necinnosti. Na desce, ktera stmivat neumi, se
 * misto nej zhasina - jina moznost tam neni a UI to u volby rika. */
static void dim_begin(void)
{
    if (bl_fading || bl_dimmed) return;
    if (!bl_can_dim()) { backlight(false); return; }
    bl_waking    = false;
    bl_fading    = true;
    bl_fade_from = (bl_shown < 0) ? bl_level() : bl_shown;
    bl_fade_to   = bl_dim_level();
    bl_fade_start = lv_tick_get();
}

/* Navrat na plny jas po doteku. */
static void dim_end(void)
{
    if (!bl_dimmed && !bl_fading) return;
    bl_fading    = false;
    bl_dimmed    = false;
    bl_wake_from = (bl_shown < 0) ? 0 : bl_shown;
    bl_waking    = true;
    bl_wake_start = lv_tick_get();
}

/* Jeden krok prejezdu jasu, volany z ui_tick(). Ztlumovani ma prednost:
 * kdyz se stmiva, nabeh uz nebezi. */
static void bl_step(void)
{
    if (bl_fading) {
        uint32_t el = lv_tick_get() - bl_fade_start;

        if (el >= BL_FADE_MS) {
            bl_fading = false;
            bl_dimmed = true;
            bl_set(bl_fade_to);
            return;
        }
        bl_set(bl_fade_from +
               (bl_fade_to - bl_fade_from) * (int)el / BL_FADE_MS);
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
/* Ktera dlazdice otevira diagnostiku. Druha zprava v DOLNI rade:
 * diagnostika je nastroj, ne hlavni funkce, takze nepatri na misto, na
 * ktere padne oko jako na prvni. Index je radek * sloupce + sloupec. */
#define NAV_TILE_DEBUG ((NAV_TILE_ROWS - 1) * NAV_TILE_COLS + NAV_TILE_COLS - 2)

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
    /* Vrstva diagnostiky cte jen to, co je prave videt. Odejiti ze
     * stranky tim zaroven uvolni rozpocet tem velicinam, ktere na te
     * nove jsou. */
    if (dp) diag_poller_set_view(dp, PAGE_VIEW[idx]);
    /* Vyber chyby plati jen po dobu, co se na ni nekdo diva. */
    if (dp && idx != PG_CHYBY) diag_poller_dtc_select(dp, -1);
}

static void nav_cb(lv_event_t *e)
{
    ui_show_page((int)(intptr_t)lv_event_get_user_data(e));
    /* Lista se po vyberu zasune. Kdo si rekl o stranku, chce ji videt,
     * ne se na ni divat pres vysunute menu; drive se zasouvala az po
     * peti vterinach necinnosti, takze prvni pohled na novou stranku
     * byl vzdycky zpoza listy. */
    set_nav_state(0);
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
    /* Smer: tah DOLEVA posune na dalsi stranku, tah DOPRAVA na
     * predchozi - obsah jde za prstem, jako kdyz se listuje papirem.
     * Tah od LEVEHO OKRAJE je vyjimka a znamena zpet na hlavni
     * obrazovku, takze se vyhodnocuje driv nez obycejny tah doprava. */
    switch (gest_dir_now()) {
        case LV_DIR_RIGHT:
            if (press_x < GEST_EDGE && cur_page != PG_HLAVNI) ui_show_page(PG_HLAVNI);
            else ui_show_page(cur_page - 1);
            break;
        case LV_DIR_LEFT: ui_show_page(cur_page + 1); break;
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
    dbg_bus     = NULL;
    dbg_poll    = NULL;
    dbg_perf_btn = NULL;
    dbg_save_btn = NULL;
    for (int m = 0; m < UI_PROFILES; m++) {
        btn_bl_prof[m] = NULL;
        for (int i = 0; i < BL_LEVELS; i++) btn_bl_level[m][i] = NULL;
    }
    for (int i = 0; i < DIM_STEPS; i++) btn_dim_time[i] = NULL;
    for (int i = 0; i < BL_LEVELS; i++) btn_dim_lvl[i] = NULL;
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
 * Stav se kresli na pravy sloupec, protoze ten je videt na kazde
 * strance - jedine misto, kde si bezici regenerace vsimne i ten, kdo
 * je zrovna jinde. Bezici regenerace sloupec podkresli cervene a blika.
 *
 * Stav se nikde nenastavuje rucne: bere se z DID 2487, delky prave
 * bezici regenerace. Nenulova delka znamena, ze bezi. Driv tu byl
 * rucni prepinac a odhad postupu po petinach - oboje zmizelo, protoze
 * jednotka postup nehlasi a vymyslet ho znamena kreslit cislo, ktere
 * nikdo nezmeril.
 *
 * Tlacitko na spusteni regenerace tu neni. Je to zapis (31 01 02 0D),
 * ten patri do src/write/ za branu z docs/safety.md a v tomhle obrazu
 * neni prilinkovany; tlacitko, ktere nic neposle, by bylo horsi nez
 * zadne. Na strance je misto nej vysvetleni.
 * ------------------------------------------------------------------- */
#define RGN_BLINK_MS  500

static bool regen_on(void)
{
    return ch_has(DIAG_CH_RGN_DUR) && ch_val(DIAG_CH_RGN_DUR) >= 1.0f;
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

/* ---------------------------------------------------------------------
 * Ztlumeni po necinnosti
 *
 * Dve volby, ktere spolu souvisi: ZA JAK DLOUHO a NA KOLIK. Proto maji
 * jeden dialog a ne dve tlacitka v nastaveni - "ztlumit po minute" bez
 * udaje na kolik je polovina informace a druha polovina by byla o dve
 * obrazovky dal.
 *
 * Obe volby patri PROFILU, stejne jako jas: na stole, ve dne a v noci
 * chce clovek jine. Dialog proto meni prave platny profil a jmeno
 * profilu je v jeho nadpisu.
 * ------------------------------------------------------------------- */
static const uint32_t DIM_STEP_S[DIM_STEPS] = { 0, 30, 120, 600 };

/* Doba necinnosti slovem. Je zvlast, protoze ji krome tlacitka v
 * nastaveni ukazuje i radek profilu v dialogu jasu a seznam profilu
 * pred resetem. */
static void dim_time_text(char *b, size_t n, uint32_t s)
{
    if (s == 0)      snprintf(b, n, "%s", T(STR_BLANK_NEVER));
    else if (s < 60) snprintf(b, n, "%u s", (unsigned)s);
    else             snprintf(b, n, "%u min", (unsigned)(s / 60));
}

/* Cele nastaveni ztlumeni jednou vetou: "2 min / 25 %", nebo jen
 * "nikdy". Na desce bez stmivani se misto urovne rekne, ze se zhasina
 * - uroven by tam byla slib naprazdno. */
static void dim_text(char *b, size_t n, const ui_profile_t *p)
{
    char t[24];

    dim_time_text(t, sizeof(t), p->dim_s);
    if (p->dim_s == 0)   snprintf(b, n, "%s", t);
    else if (bl_can_dim()) snprintf(b, n, "%s / %d %%", t, p->dim_bright);
    else                 snprintf(b, n, "%s / %s", t, T(STR_DIM_OFF_ONLY));
}

/* Popisek tlacitka ZTLUMIT. Vola ho i build_nastav() a prepnuti
 * profilu, aby tlacitko ukazovalo hodnotu toho profilu, ktery prave
 * plati, a ne tu, co pri stavbe nahodou byla. */
static void dim_btn_paint(void)
{
    char b[40];

    if (!btn_blank) return;
    dim_text(b, sizeof(b), &PROF);
    dlg_btn_text(btn_blank, b);
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

/* Vybrana volba nese obrubu, ne vypln: modra a kostena maji v teto
 * aplikaci pevny vyznam (zadano / skutecnost) a vyber mezi ne nepatri.
 * Stejne jako v dialogu jasu, aby se oba dialogy cetly stejne. */
static void dim_rows_paint(void)
{
    for (int i = 0; i < DIM_STEPS; i++) {
        if (!btn_dim_time[i]) continue;
        bool on = PROF.dim_s == DIM_STEP_S[i];
        lv_obj_set_style_border_color(btn_dim_time[i],
            lv_color_hex(on ? C_ACTUAL : C_RULE), 0);
        lv_obj_set_style_border_width(btn_dim_time[i], on ? 3 : 1, 0);
    }
    for (int i = 0; i < BL_LEVELS; i++) {
        if (!btn_dim_lvl[i]) continue;
        bool on = PROF.dim_bright == BL_LEVEL[i];
        lv_obj_set_style_border_color(btn_dim_lvl[i],
            lv_color_hex(on ? C_ACTUAL : C_RULE), 0);
        lv_obj_set_style_border_width(btn_dim_lvl[i], on ? 3 : 1, 0);
    }
}

static void dim_time_cb(lv_event_t *e)
{
    PROF.dim_s = DIM_STEP_S[(int)(intptr_t)lv_event_get_user_data(e)];
    save_cfg();
    dim_rows_paint();
    dim_btn_paint();
    /* Zmena doby se musi projevit hned: kdyz uz jsme ztlumeni a
     * uzivatel ztlumeni vypne, nesmi displej zustat tmavy. */
    if (PROF.dim_s == 0) dim_end();
}

static void dim_lvl_cb(lv_event_t *e)
{
    PROF.dim_bright = BL_LEVEL[(int)(intptr_t)lv_event_get_user_data(e)];
    save_cfg();
    dim_rows_paint();
    dim_btn_paint();
}

#define DIM_T_W   160
#define DIM_T_X(i) (30 + (i) * (DIM_T_W + 10))
#define DIM_L_W   120
#define DIM_L_X(i) (30 + (i) * (DIM_L_W + 10))
#define DIM_ROW_H 120
#define DIM_ROW1  120
#define DIM_ROW2  300

_Static_assert(DIM_ROW2 + DIM_ROW_H <= 480,
               "druha rada dialogu ztlumeni by prelezla spodni hranu");
_Static_assert(DIM_L_X(BL_LEVELS - 1) + DIM_L_W <= 800,
               "urovne ztlumeni by prelezly pravou hranu");

/* Male tlacitko vedle vysvetleni, proc deska stmivat neumi. Kresba je
 * mala zamerne - je to poznamka, ne nabidka - ale dotykovy cil musi
 * zustat 120 px, takze obalova oblast je vetsi nez pismo. */
#define DIM_HW_W  150
#define DIM_HW_H  120
#define DIM_HW_X  (800 - 30 - DIM_HW_W)

/* ---------------------------------------------------------------------
 * "Stmivani jde dodelat"
 *
 * Ukaze se JEN na desce, ktera stmivat neumi, tedy pri
 * CANOGRAF_BL_PWM_PIN = -1 (ui_host_t.bl_set_percent == NULL). Kde uz
 * dratek je, nema co radit a tlacitko tam proto vubec nevznikne.
 *
 * Je to KONSTATOVANI MOZNOSTI, ne navod a ne doporuceni. Kdo na desku
 * sahne, ma si precist docs/hardware/backlight.md - tam je cely rozbor
 * i s merenim. Text proto rika, co to je, co to stoji a kde je zbytek,
 * a nic vic.
 * ------------------------------------------------------------------- */
static void hw_dim_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *m = modal_open(T(STR_HW_DIM_TITLE), C_LABEL);
    lv_obj_t *b = mk_lbl(m, T(STR_HW_DIM_BODY), UI_FONT_20, C_BRIGHT, 30, 100);

    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, 740);
}

static void dim_cb(lv_event_t *e)
{
    (void)e;

    char title[64];
    snprintf(title, sizeof(title), "%s  %s", T(STR_DIM_TITLE),
             T(PROF_STR[cfg.profile]));
    lv_obj_t *m = modal_open(title, C_ACTUAL);

    mk_lbl(m, T(STR_DIM_AFTER), UI_FONT_14, C_LABEL, 30, DIM_ROW1 - 22);
    for (int i = 0; i < DIM_STEPS; i++) {
        char b[24];
        dim_time_text(b, sizeof(b), DIM_STEP_S[i]);
        btn_dim_time[i] = mk_button(m, DIM_T_X(i), DIM_ROW1, DIM_T_W,
                                    DIM_ROW_H, b, UI_FONT_24, C_ACTUAL,
                                    dim_time_cb, (void *)(intptr_t)i);
    }

    if (!bl_can_dim()) {
        /* Bez dratku na plosce BL se da jen zhasnout. Nabidnout tu
         * urovne by znamenalo slibit neco, co se nikde neprojevi.
         * Vedle vysvetleni ale stoji, ze to jde dodelat - mlcet o tom
         * by znamenalo tvarit se, ze deska stmivat neumi natrvalo. */
        lv_obj_t *na = mk_lbl(m, T(STR_DIM_NA), UI_FONT_20, C_LABEL,
                              30, DIM_ROW2);
        lv_label_set_long_mode(na, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(na, DIM_HW_X - 30 - 20);
        mk_dlg_button(m, DIM_HW_X, DIM_ROW2, DIM_HW_W, DIM_HW_H,
                      T(STR_HW_DIM_BTN), UI_FONT_16, C_SAGE, hw_dim_cb, NULL);
        dim_rows_paint();
        return;
    }

    mk_lbl(m, T(STR_DIM_TO), UI_FONT_14, C_LABEL, 30, DIM_ROW2 - 22);
    for (int i = 0; i < BL_LEVELS; i++) {
        char b[16];
        snprintf(b, sizeof(b), "%d %%", BL_LEVEL[i]);
        btn_dim_lvl[i] = mk_button(m, DIM_L_X(i), DIM_ROW2, DIM_L_W,
                                   DIM_ROW_H, b, UI_FONT_24, C_ACTUAL,
                                   dim_lvl_cb, (void *)(intptr_t)i);
    }
    dim_rows_paint();
}

/* ---------------------------------------------------------------------
 * Rezim sbernice
 *
 * Radic startuje v listen-only a v tom rezimu neodejde ani jeden bit -
 * ani potvrzovaci. Nic se tedy neda precist, protoze kazdy dotaz je
 * odeslani. Prepnuti do normalniho rezimu je jediny okamzik, kdy
 * deska zacne na cizi vuz vysilat, takze:
 *
 *   - nikdy se nestane samo, ani po startu, ani po pripojeni kabelu,
 *   - smerem NAHORU (zacit vysilat) je za potvrzenim drzenim, kde
 *     stoji, co to znamena,
 *   - smerem DOLU (prestat vysilat) jde hned. Ztisit se musi jit
 *     okamzite; cekat tri vteriny na to, aby deska prestala rusit, by
 *     bylo presne obracene.
 *
 * Kdyz se radic v novem rezimu nerozjede, can_set_mode() vrati puvodni
 * nastaveni a tlacitko to rekne - nesmi zustat napsane NORMAL nad
 * radicem, ktery posloucha.
 * ------------------------------------------------------------------- */
static void canmode_btn_paint(void)
{
    if (!btn_canmode) return;

    bool normal = host_can_normal();
    lv_obj_t *l = lv_obj_get_child(btn_canmode, 0);

    lv_label_set_text(l, T(normal ? STR_CAN_NORMAL : STR_CAN_LISTEN));
    lv_obj_set_style_text_color(l, lv_color_hex(normal ? C_DEFICIT : C_BRIGHT), 0);
}

static void canmode_ok_cb(lv_event_t *e)
{
    (void)e;
    bool ok = host->can_set_normal(true);

    modal_close_cb(NULL);
    canmode_btn_paint();
    if (!ok) {
        /* Tlacitko ukazuje skutecny rezim radice, takze by po neuspechu
         * jen tise zustalo na JEN POSLECH a vypadalo to, jako by se
         * tuknuti ztratilo. Duvod musi byt videt. */
        lv_obj_t *m = modal_open(T(STR_CAN_TITLE), C_DEFICIT);
        lv_obj_t *l = mk_lbl(m, T(STR_CAN_FAILED), UI_FONT_24, C_DEFICIT,
                             30, 120);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, 740);
    }
}

static void canmode_cb(lv_event_t *e)
{
    (void)e;
    if (!host_can_mode()) return;
    if (host_can_normal()) {
        host->can_set_normal(false);
        canmode_btn_paint();
        return;
    }
    show_confirm(T(STR_CAN_TITLE), T(STR_CAN_BODY), C_DEFICIT, canmode_ok_cb);
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
 * uz pri stavbe ukazovalo uroven z NVS a ne natvrdo zadanou. Tecky na
 * konci pridava dlg_btn_text() - tlacitko dialog otevira, nemeni
 * hodnotu tuknutim. */
static void bright_btn_paint(void)
{
    char b[16];

    if (!btn_bright) return;
    snprintf(b, sizeof(b), "%d %%", bl_level());
    dlg_btn_text(btn_bright, b);
}

/* Tlacitko WIFI ukazuje, na ktere siti deska je. "VYBRAT SIT" tam
 * stalo porad a o stavu nerikalo nic - prave to byla otazka, kvuli
 * ktere na nej clovek sahal. */
static void wifi_btn_paint(void)
{
    if (!btn_wifi) return;
    dlg_btn_text(btn_wifi, host_wifi_connected() ? host_wifi_ssid()
                                                 : T(STR_NONE));
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
            char dim[40], cap[72];
            dim_text(dim, sizeof(dim), &cfg.prof[m]);
            snprintf(cap, sizeof(cap), "%s\n%d %%\n%s",
                     T(PROF_STR[m]), cfg.prof[m].bright, dim);
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
static void perf_apply(void);

static void profile_apply(void)
{
    bl_apply();                 /* jas noveho profilu plati hned */
    /* Prekryv s FPS patri profilu stejne jako jas, takze se pri
     * prepnuti prebira taky - jinak by zustal viset z profilu, ze
     * ktereho se odchazi. */
    perf_on = PROF.perf;
    perf_apply();
    save_cfg();
    profile_btn_paint();
    prof_bar_paint();
    dim_btn_paint();
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
        char dim[40];
        dim_text(dim, sizeof(dim), &cfg.prof[i]);
        at = str_add(b, at, n, "%s   %d %%   %s\n",
                     T(PROF_STR[i]), cfg.prof[i].bright, dim);
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

/* Motor: vlevo plnici tlak jako ukazatel, uprostred to, co k nemu
 * patri (zadani, lopatky, rail), vpravo vzduchova cesta. Dvojice
 * skutecnost/zadani se kresli vedle sebe a barvou, ne popiskem -
 * kostena je vzdycky skutecnost, modra vzdycky zadani, stejne jako v
 * legende grafu. */
#define ENG_C2   290
#define ENG_C3   500
#define ENG_ROW1 10
#define ENG_ROW2 86
#define ENG_ROW3 162

static void build_motor(lv_obj_t *scr)
{
    lv_obj_t *p = page[PG_MOTOR] = mk_page(scr);
    arc_boost = mk_arc(p, 14, 6, 230, 800, 2900, C_ACTUAL, 18);
    lbl_boost = mk_lbl(p, VAL_NONE, UI_FONT_40, C_ACTUAL, 62, 96);
    mk_lbl(p, "mbar", UI_FONT_16, C_LABEL, 106, 142);
    mk_lbl(p, T(STR_BOOST_PRESSURE), UI_FONT_14, C_LABEL, 76, 240);

    mk_lbl(p, T(STR_DEMAND), UI_FONT_14, C_LABEL, ENG_C2, ENG_ROW1);
    lbl_boost_sp = mk_lbl(p, VAL_NONE, UI_FONT_36, C_DEMAND, ENG_C2, ENG_ROW1 + 18);
    mk_lbl(p, "VNT %", UI_FONT_14, C_LABEL, ENG_C2, ENG_ROW2);
    lbl_vnt = mk_lbl(p, VAL_NONE, UI_FONT_36, C_ACTUAL, ENG_C2, ENG_ROW2 + 18);
    mk_lbl(p, "RAIL bar", UI_FONT_14, C_LABEL, ENG_C2, ENG_ROW3);
    lbl_railp = mk_lbl(p, VAL_NONE, UI_FONT_36, C_ACTUAL, ENG_C2, ENG_ROW3 + 18);
    lbl_rail_sp = mk_lbl(p, VAL_NONE, UI_FONT_20, C_DEMAND, ENG_C2 + 120,
                         ENG_ROW3 + 34);

    mk_lbl(p, "MAF mg", UI_FONT_14, C_LABEL, ENG_C3, ENG_ROW1);
    lbl_maf = mk_lbl(p, VAL_NONE, UI_FONT_36, C_ACTUAL, ENG_C3, ENG_ROW1 + 18);
    lbl_maf_sp = mk_lbl(p, VAL_NONE, UI_FONT_20, C_DEMAND, ENG_C3 + 120,
                        ENG_ROW1 + 34);
    mk_lbl(p, "EGR %", UI_FONT_14, C_LABEL, ENG_C3, ENG_ROW2);
    lbl_egr = mk_lbl(p, VAL_NONE, UI_FONT_36, C_ACTUAL, ENG_C3, ENG_ROW2 + 18);
    mk_lbl(p, T(STR_INTAKE_AIR), UI_FONT_14, C_LABEL, ENG_C3, ENG_ROW3);
    lbl_iat = mk_lbl(p, VAL_NONE, UI_FONT_36, C_ACTUAL, ENG_C3, ENG_ROW3 + 18);
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

/* ---------------------------------------------------------------------
 * Chyby
 *
 * Vlevo seznam toho, co jednotka vydala na 19 02 s maskou 0x2F
 * (klidovy stav 0x50 odfiltruje uz jednotka, jinak by jich prislo pres
 * sto). Kazdy radek nese kod a rozlozeny stavovy bajt slovy - samotny
 * kod rika CO, stavove bity rikaji JESTLI to hori ted.
 *
 * Vpravo detail vybrane chyby: rozsirene zaznamy (19 06) a freeze
 * frame (19 04). Rozsirene zaznamy jsou nahore zamerne - prave ty
 * zmizi, kdyz se chyba sama vyleci nebo ji nekdo smaze.
 *
 * Mazani pameti tu neni a nebude, viz ROADMAP.md: zahodilo by presne
 * ty zaznamy, kvuli kterym se sem clovek diva.
 *
 * Radku je DTC_ROWS a vic se jich na stranku nevejde. Kolik chyb
 * jednotka opravdu hlasi, stoji v hlavicce, takze se nic neschova
 * potichu.
 * ------------------------------------------------------------------- */
#define DTC_ROW_H  48
#define DTC_ROW_W  350
#define DTC_ROW_Y(i) (44 + (i) * (DTC_ROW_H + 8))
#define DTC_COL2   400

_Static_assert(DTC_ROW_Y(DTC_ROWS - 1) + DTC_ROW_H <= PAGE_H - NAV_BAR_H,
               "posledni radek chyb by zmizel pod vysunutou listou");

static void dtc_row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);

    if (!dp) return;
    /* Druhe tuknuti na tentyz radek vyber zrusi - jinak by se z detailu
     * nedalo odejit jinak nez odchodem ze stranky. */
    diag_poller_dtc_select(dp, (diag_poller_dtc_selected(dp) == i) ? -1 : i);
}

static void build_chyby(lv_obj_t *scr)
{
    lv_obj_t *p = page[PG_CHYBY] = mk_page(scr);

    dtc_head = mk_lbl(p, T(STR_DTC_UNREAD), UI_FONT_24, C_LABEL, 20, 8);
    dtc_odo  = mk_lbl(p, "", UI_FONT_16, C_LABEL, DTC_COL2, 14);

    for (int i = 0; i < DTC_ROWS; i++) {
        dtc_row[i] = mk_button(p, 20, DTC_ROW_Y(i), DTC_ROW_W, DTC_ROW_H, "",
                               UI_FONT_20, C_DEFICIT, dtc_row_cb,
                               (void *)(intptr_t)i);
        lv_obj_add_flag(dtc_row[i], LV_OBJ_FLAG_HIDDEN);
    }
    dtc_hint = mk_lbl(p, T(STR_DTC_TAP), UI_FONT_14, C_LABEL, 20,
                      DTC_ROW_Y(DTC_ROWS - 1) + DTC_ROW_H + 6);

    mk_lbl(p, T(STR_DTC_EXT_T), UI_FONT_14, C_LABEL, DTC_COL2, 44);
    dtc_ext = mk_lbl(p, "", UI_FONT_16, C_BRIGHT, DTC_COL2, 62);
    mk_lbl(p, T(STR_DTC_FREEZE_T), UI_FONT_14, C_LABEL, DTC_COL2, 158);
    dtc_ff = mk_lbl(p, "", UI_FONT_16, C_BRIGHT, DTC_COL2, 176);
}

/* DPF: sest hodnot ve dvou sloupcich, vpravo stav regenerace a
 * vysvetleni, proc tu neni tlacitko na jeji spusteni. */
#define DPF_C1   20
#define DPF_C2   215
#define DPF_C3   420
#define DPF_RY(r) (6 + (r) * 68)

static void build_dpf(lv_obj_t *scr)
{
    static const int CAP[6] = {
        STR_DPF_TEMP_IN, STR_DPF_TEMP_OUT, STR_DPF_TEMP_TARGET,
        STR_DPF_SOOT, STR_DPF_LOAD, STR_DPF_SINCE
    };
    lv_obj_t *p = page[PG_DPF] = mk_page(scr);

    for (int i = 0; i < 6; i++) {
        lv_coord_t x = (i & 1) ? DPF_C2 : DPF_C1;
        lv_coord_t y = DPF_RY(i / 2);
        mk_lbl(p, T(CAP[i]), UI_FONT_14, C_LABEL, x, y);
        lbl_dpf[i] = mk_lbl(p, VAL_NONE, UI_FONT_32, C_ACTUAL, x, y + 18);
    }

    mk_lbl(p, T(STR_REGEN_STATE), UI_FONT_14, C_LABEL, DPF_C3, DPF_RY(0));
    lbl_regen = mk_lbl(p, T(STR_REGEN_OFF), UI_FONT_32, C_LABEL,
                       DPF_C3, DPF_RY(0) + 18);

    lv_obj_t *na = mk_lbl(p, T(STR_DPF_REGEN_NA), UI_FONT_16, C_LABEL,
                          DPF_C3, DPF_RY(1) + 10);
    lv_label_set_long_mode(na, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(na, 290);
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
    /* ZTLUMENI patri profilu, takze popisek tlacitka dosadi az
     * dim_btn_paint() podle toho, ktery profil prave plati. */
    mk_lbl(p, T(STR_SET_DIM), UI_FONT_14, C_LABEL, SET_X(2), 8);
    btn_blank = mk_dlg_button(p, SET_X(2), SET_ROW1, BW, BH, "",
                              UI_FONT_20, C_ACTUAL, dim_cb, NULL);
    dim_btn_paint();
    mk_lbl(p, T(STR_SET_LANG), UI_FONT_14, C_LABEL, SET_X(3), 8);
    mk_button(p, SET_X(3), SET_ROW1, BW, BH, i18n_name(cfg.lang), UI_FONT_24,
              C_SAGE, lang_cb, NULL);

    /* WIFI je uzsi nez driv: vedle nej je ted rezim sbernice. Rezim je
     * nejdulezitejsi prepinac na cele strance - bez nej se neprecte nic
     * - takze patri do rady, ktera je videt, ne do dialogu.
     *
     * Tlacitka s teckami otviraji dalsi obrazovku, tlacitka bez tecek
     * meni hodnotu rovnou. Viz mk_dlg_button(). */
    mk_lbl(p, "WIFI", UI_FONT_14, C_LABEL, SET_X(0), SET_ROW2 - 20);
    btn_wifi = mk_dlg_button(p, SET_X(0), SET_ROW2, BW, BH, "", UI_FONT_20,
                             C_DEMAND, wifi_scan_cb, NULL);
    wifi_btn_paint();

    mk_lbl(p, "CAN", UI_FONT_14, C_LABEL, SET_X(1), SET_ROW2 - 20);
    btn_canmode = NULL;
    if (host_can_mode()) {
        /* Tecky tu jsou kvuli smeru, ktery je dulezity: zacit vysilat
         * jde jen pres potvrzovaci dialog. Zpet do poslechu se vraci
         * rovnou, protoze ztisit se musi dat okamzite. */
        btn_canmode = mk_dlg_button(p, SET_X(1), SET_ROW2, BW, BH, "",
                                    UI_FONT_20, C_DEFICIT, canmode_cb, NULL);
        canmode_btn_paint();
    } else {
        lv_obj_t *na = mk_lbl(p, T(STR_CAN_NA), UI_FONT_14, C_LABEL,
                              SET_X(1), SET_ROW2);
        lv_label_set_long_mode(na, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(na, SET_BW);
    }

    mk_lbl(p, T(STR_SET_PROFILE), UI_FONT_14, C_LABEL, SET_X(2), SET_ROW2 - 20);
    btn_profile = mk_button(p, SET_X(2), SET_ROW2, BW, BH, "", UI_FONT_24,
                            C_SAGE, profile_cycle_cb, NULL);
    profile_btn_paint();

    mk_lbl(p, T(STR_SET_BRIGHT), UI_FONT_14, C_LABEL, SET_X(3), SET_ROW2 - 20);
    btn_bright = NULL;
    if (bl_can_dim()) {
        btn_bright = mk_dlg_button(p, SET_X(3), SET_ROW2, BW, BH, "",
                                   UI_FONT_20, C_ACTUAL, bright_cb, NULL);
        bright_btn_paint();
    } else {
        lv_obj_t *na = mk_lbl(p, T(STR_BRIGHT_NA), UI_FONT_14, C_LABEL,
                              SET_X(3), SET_ROW2);
        lv_label_set_long_mode(na, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(na, SET_BW);
    }

    lbl_wifi = mk_lbl(p, "", UI_FONT_20, C_LABEL, 20, SET_ROW3);
    lbl_heap = mk_lbl(p, "", UI_FONT_16, C_LABEL, 20, SET_ROW3 + 32);

    /* Primy vypinac podsviceni. Probuzeni je dotekem kamkoliv, stejne
     * jako po ztlumeni - resi to ui_tick() pres necinnost LVGL.
     * Zhasina natvrdo, bez prejezdu: hlavni duvod existence je merit na
     * ploskach BL/GND a tam je potreba znamy stav. Neni to dialog ani
     * prepinac hodnoty, je to akce, takze nema tecky ani je mit nema. */
    mk_button(p, 360, SET_ROW3, 170, BH, T(STR_SET_DISPLAY_OFF), UI_FONT_24,
              C_ACTUAL, display_off_cb, NULL);

    /* Reset stoji az za vsim, co se da nastavit, a jeste jednou za
     * dialogem - nemuze se na nej sahnout omylem pri hrani s jednotkami.
     * Vic nez po 716 px nesmi nic sahnout: od 736 px je pravy sloupec
     * DPF a stranka je od tam orezana. */
    mk_dlg_button(p, 546, SET_ROW3, 170, BH, T(STR_SET_MAINT), UI_FONT_20,
                  C_DEFICIT, reset_cb, NULL);
}

/* ---------------------------------------------------------------------
 * Dotek na sloupci DPF
 *
 * Z jednoho stisku na sloupci vedou tri cesty a kazda konci jinde:
 *
 *   stisk              sloupec se OZNACI - delici linka zesili a zmodra
 *   pusteni bez pohybu nestane se NIC, oznaceni za chvili zmizi
 *   pohyb              oznaceni i odpocet se rusi a je z toho obycejny
 *                      tah na dalsi stranku
 *   podrzeni           skok na stranku DPF
 *
 * PROC SE TO NEPLETE. Sloupec lezi u praveho okraje a prave odtud
 * clovek zacina tah na dalsi stranku, takze vsechny tri veci zacinaji
 * uplne stejne. Rozhoduje az to, co se stane potom:
 *
 *   - tah ma EVENT_BUBBLE a vyhodnocuje ho obrazovka az pri PUSTENI,
 *     tedy uplne jinou cestou,
 *   - CLICKED projde jen tehdy, kdyz was_drag() rekne, ze prst nikam
 *     neslo (hlidac drag_guard_cb, stejny jako na vsech tlacitkach),
 *   - ODPOCET PODRZENI SE RUSI POHYBEM. To je ta podstatna vec: bez ni
 *     by zavahani uprostred tahu skocilo na stranku DPF a tah uz by
 *     nikam nevedl. Proto se podrzeni nepocita pres LVGL
 *     LV_EVENT_LONG_PRESSED (ten chodi podle casu bez ohledu na pohyb),
 *     ale rucne z LV_EVENT_PRESSING, ktery zaroven vi, kde prst je.
 *
 * DELKA PODRZENI je 1,2 s. Je to trojnasobek vychoziho dlouheho stisku
 * LVGL (400 ms), takze obycejne zavahani na nej nedosahne, a zaroven
 * polovina tri vterin od potvrzeni drzenim - ty patri necemu, co meni
 * vuz, kdezto tohle je jen navigace, kterou vezme zpet jedno svihnuti.
 * Protoze odpocet rusi pohyb, nic nestoji v ceste tomu, aby byl kratky;
 * delsi uz by jen zdrzoval.
 *
 * CO JE VIDET. Delici linka sloupce je zaroven ukazatel postupu:
 *
 *   klid        1 px, barva vlasove linky
 *   drzim       3 px, modra, roste odspodu nahoru podle odpoctu
 *   tuknuto     3 px, modra, cela - na 800 ms, pak zpatky
 *
 * Je to jediny objekt, zadny dalsi prvek nevznika, a v 64 px sirokem
 * sloupci je pres celou vysku stranky videt i periferne. Zmena je
 * TVAROVA, ne ciselna, a modra je v teto palete barva zadane hodnoty -
 * na sloupci se nikdy nepouziva (ten ma zelenou, kostenou a cervenou),
 * takze ji nejde splest se zmenou namerene hodnoty.
 * ------------------------------------------------------------------- */
#define RAIL_HOLD_MS  1200
#define RAIL_FLASH_MS 800

static uint32_t rail_press_at = 0;    /* kdy zacal stisk */
static bool     rail_hold_live = false;
static bool     rail_hold_done = false;
static uint32_t rail_flash_until = 0;
/* Naposled nakreslena vyska delici linky. Po prestavbe UI (zmena
 * jazyka) je rail_rule uz jiny objekt, takze se musi zneplatnit -
 * jinak by se prekresleni preskocilo a linka zustala useknuta. */
static lv_coord_t rail_rule_h = -1;

static void rail_press_cb(lv_event_t *e)
{
    (void)e;
    rail_press_at  = lv_tick_get();
    rail_hold_live = true;
    rail_hold_done = false;
    rail_flash_until = 0;
}

/* Chodi pri kazdem cteni doteku, tedy i kdyz prst stoji. Odtud se
 * podrzeni hlida i rusi - obe veci potrebuji vedet, kde prst je. */
static void rail_pressing_cb(lv_event_t *e)
{
    (void)e;
    if (!rail_hold_live) return;
    if (was_drag()) {               /* uz to neni podrzeni, je to tah */
        rail_hold_live = false;
        return;
    }
    if (lv_tick_get() - rail_press_at < RAIL_HOLD_MS) return;
    rail_hold_live = false;
    rail_hold_done = true;          /* at nasledny CLICKED uz nic nedela */
    ui_show_page(PG_DPF);
}

static void rail_release_cb(lv_event_t *e)
{
    (void)e;
    rail_hold_live = false;
}

static void rail_click_cb(lv_event_t *e)
{
    (void)e;
    if (rail_hold_done) return;     /* uz to vyridilo podrzeni */
    rail_flash_until = lv_tick_get() + RAIL_FLASH_MS;
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
    rail_rule_h = -1;                           /* viz rail_paint() */
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
/* Tri sloupce: vlevo hodnoty vsech kanalu, uprostred sbernice a merena
 * rychlost vycitani, vpravo pamet a prepinac prekryvu s FPS. */
#define DBG_C1 30
#define DBG_C2 260
#define DBG_C3 500

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

/* ---------------------------------------------------------------------
 * Nastaveni diagnostiky prezije restart
 *
 * Prekryv s FPS patri PROFILU, stejne jako jas: na stole si ho clovek
 * necha svitit cele hodiny, za jizdy ho videt nechce. Ulozeni je ale
 * zvlast tlacitko a ne automaticke - zapnout si prekryv na dve minuty
 * pri hledani zadrhnuti je bezna vec a nemelo by to znamenat, ze ho
 * deska bude mit i priste.
 * ------------------------------------------------------------------- */
static void perf_apply(void)
{
    if (!perf_lbl) return;
    if (perf_on) lv_obj_clear_flag(perf_lbl, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(perf_lbl, LV_OBJ_FLAG_HIDDEN);
}

static void dbg_btn_paint(void);

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
    perf_apply();
    perf_paint();
    dbg_perf_paint();
    dbg_btn_paint();
}

static void dbg_save_cb(lv_event_t *e)
{
    (void)e;
    PROF.perf = perf_on;
    save_cfg();
    dbg_btn_paint();
}

static void dbg_default_cb(lv_event_t *e)
{
    (void)e;
    PROF.perf = ui_profile_default(cfg.profile).perf;
    perf_on   = PROF.perf;
    perf_apply();
    perf_paint();
    save_cfg();
    dbg_perf_paint();
    dbg_btn_paint();
}

/* Tlacitko ulozeni rika, do ktereho profilu se uklada, a jestli uz se
 * ulozene od zobrazeneho lisi - jinak by nebylo poznat, ze je co
 * ukladat. */
static void dbg_btn_paint(void)
{
    char b[64];

    if (!dbg_save_btn) return;
    snprintf(b, sizeof(b), T(STR_DBG_SAVE), T(PROF_STR[cfg.profile]));
    lv_label_set_text(lv_obj_get_child(dbg_save_btn, 0), b);
    lv_obj_set_style_border_color(dbg_save_btn,
        lv_color_hex(PROF.perf == perf_on ? C_RULE : C_ACTUAL), 0);
    lv_obj_set_style_border_width(dbg_save_btn, PROF.perf == perf_on ? 1 : 3, 0);
}

/* Syrove hodnoty: stav kanalu a cislo v setinach, jak ho UI drzi.
 * Jmena jsou vyvojarska, ne pro uzivatele - stejne jako nazvy citacu
 * sbernice o kus dal - takze nejdou pres i18n.
 *
 * Stav je jedno pismeno a je to to nejdulezitejsi na cele tabulce:
 *   -  nic neprislo       L  ziva
 *   S  stara              X  jednotka DID odmitla
 * Kdyz je na strance pomlcka, musi tu byt '-'; kdyz je na strance
 * cislo, musi tu byt L nebo S. Jinak UI kresli neco, co nezmerilo. */
static const char *const DBG_CH_NAME[DIAG_CH__COUNT] = {
    "rpm", "speed", "boost", "boost_sp", "rail", "rail_sp", "vnt",
    "maf", "maf_sp", "egr", "iat", "coolant", "dpf_up", "dpf_dn",
    "dpf_sp", "soot", "load", "rgn_dur", "rgn_end", "odo"
};

static char dbg_show_char(int ch)
{
    switch (ch_show(ch)) {
    case DIAG_SHOW_LIVE:    return 'L';
    case DIAG_SHOW_STALE:   return 'S';
    case DIAG_SHOW_REFUSED: return 'X';
    default:                return '-';
    }
}

static void dbg_paint(void)
{
    if (!dbg_raw) return;

    char b[768];
    int n = (int)sizeof(b);
    int at = str_add(b, 0, n, "page %d  nav %d\n", cur_page, nav_state);

    for (int ch = 0; ch < DIAG_CH__COUNT; ch++)
        at = str_add(b, at, n, "%-8s %c %d\n", DBG_CH_NAME[ch],
                     dbg_show_char(ch), (int)(v_shown[ch] * 100.0f));
    lv_label_set_text(dbg_raw, b);

    if (dbg_bus) {
        char s[256];
        int sn = (int)sizeof(s);
        const can_stats_t *cs = (host && host->can_stats) ? host->can_stats()
                                                          : NULL;
        if (!cs) {
            str_add(s, 0, sn, "%s", VAL_NONE);
        } else {
            at = str_add(s, 0, sn, "rx %u\ntx %u\ntx_fail %u\nrx_lost %u\n",
                         cs->rx, cs->tx, cs->tx_fail, cs->rx_lost);
            str_add(s, at, sn, "ring_lost %u\nbus_err %u\narb_lost %u\n"
                               "bus_off %u", cs->ring_lost, cs->bus_err,
                    cs->arb_lost, cs->bus_off);
        }
        lv_label_set_text(dbg_bus, s);
    }

    if (dbg_poll) {
        char s[256];
        int sn = (int)sizeof(s);
        if (!dp) {
            str_add(s, 0, sn, "%s", VAL_NONE);
        } else {
            uint32_t r = diag_poller_rate_x100(dp);
            at = str_add(s, 0, sn, "%s\n%u.%02u req/s\n",
                         diag_poller_armed(dp) ? "normal" : "listen-only",
                         r / 100u, r % 100u);
            at = str_add(s, at, sn, "rtt %u ms\npozadavku %u\n",
                         diag_poller_rtt_ms(dp), diag_poller_requests(dp));
            str_add(s, at, sn, "vzorku %u\nchyb %u\nramcu %u",
                    diag_poller_samples(dp), diag_poller_failures(dp),
                    diag_poller_link(dp)->n_frames);
        }
        lv_label_set_text(dbg_poll, s);
    }

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

    mk_lbl(m, T(STR_DBG_RAW), UI_FONT_14, C_LABEL, DBG_C1, 96);
    dbg_raw = mk_lbl(m, "", UI_FONT_14, C_BRIGHT, DBG_C1, 114);

    mk_lbl(m, T(STR_DBG_BUS), UI_FONT_14, C_LABEL, DBG_C2, 96);
    dbg_bus = mk_lbl(m, "", UI_FONT_16, C_BRIGHT, DBG_C2, 114);

    mk_lbl(m, T(STR_DBG_POLL), UI_FONT_14, C_LABEL, DBG_C2, 274);
    dbg_poll = mk_lbl(m, "", UI_FONT_16, C_BRIGHT, DBG_C2, 292);

    dbg_perf_btn = mk_button(m, DBG_C3, 96, 260, 100, "", UI_FONT_20,
                             C_ACTUAL, dbg_perf_cb, NULL);
    dbg_perf_paint();

    mk_lbl(m, T(STR_DBG_MEM), UI_FONT_14, C_LABEL, DBG_C3, 214);
    dbg_mem = mk_lbl(m, "", UI_FONT_16, C_BRIGHT, DBG_C3, 232);

    /* Ulozeni do profilu a navrat do vychoziho stavu. Jsou vedle sebe
     * pod pameti, protoze patri k sobe: jedno zapisuje to, co je prave
     * nastavene, druhe to zahazuje. Levy sloupec je cely zabrany
     * tabulkou hodnot, takze sem. */
    dbg_save_btn = mk_button(m, DBG_C3, 356, 128, 100, "", UI_FONT_16,
                             C_ACTUAL, dbg_save_cb, NULL);
    dbg_btn_paint();
    mk_button(m, DBG_C3 + 136, 356, 128, 100, T(STR_DBG_DEFAULT), UI_FONT_16,
              C_DEFICIT, dbg_default_cb, NULL);
    dbg_paint();
}

/* ---------------------------------------------------------------------
 * Sila signalu WiFi
 *
 * Pet carek, jak je zvykem z telefonu. Prevod z dBm je skokovy a
 * prahy jsou tyhle:
 *
 *     >= -55 dBm   5 carek   silnejsi signal uz nic nezlepsi
 *     >= -64 dBm   4
 *     >= -73 dBm   3
 *     >= -82 dBm   2
 *     >= -90 dBm   1         posledni, co jeste drzi spojeni
 *      < -90 dBm   0
 *
 * Proc zrovna takhle: -55 dBm je saturace - od te urovne vys uz se
 * propustnost nezlepsuje, takze plny stav ma zacinat tam a ne u nuly.
 * Pod -90 dBm spojeni na 2,4 GHz prakticky nestoji. Mezi temi dvema
 * cisly je 35 dB a ty jsou rozdelene na ctyri stejne kroky po 9 dB.
 * Devet decibelu je zhruba osminasobek vykonu - tedy rozdil, se kterym
 * se da neco udelat (posunout desku, prejit blit k AP), coz je jediny
 * duvod, proc carky vubec kreslit. Jemnejsi deleni by jen poskakovalo.
 *
 * Kresli se z obycejnych obdelniku, ne ze znaku fontu: Montserrat
 * zadne takove znaky nema a jeden chybejici glyf znamena, ze LVGL
 * tricetkrat za vterinu hlasi "glyph dsc. not found".
 * ------------------------------------------------------------------- */
#define WIFI_BARS    5
#define WIFI_BAR_W   4
#define WIFI_BAR_G   3
#define WIFI_BAR_B   42                  /* spolecna spodni hrana */
#define WIFI_BAR_H(i) (8 + (i) * 3)

static lv_obj_t *wifi_bar[WIFI_BARS], *lbl_ssid;

static int host_wifi_rssi(void)
{
    return (host && host->wifi_rssi) ? host->wifi_rssi() : 0;
}

static bool host_wifi_ap(void)
{
    return (host && host->wifi_ap) ? host->wifi_ap() : false;
}

static int wifi_bars_from_rssi(int rssi)
{
    static const int STEP[WIFI_BARS] = { -55, -64, -73, -82, -90 };

    if (rssi == 0) return 0;            /* radic nic nehlasi */
    for (int i = 0; i < WIFI_BARS; i++)
        if (rssi >= STEP[i]) return WIFI_BARS - i;
    return 0;
}

static void wifi_status_build(lv_obj_t *bar)
{
    for (int i = 0; i < WIFI_BARS; i++)
        wifi_bar[i] = mk_shape(bar, TOP_WIFI_X + i * (WIFI_BAR_W + WIFI_BAR_G),
                               WIFI_BAR_B - WIFI_BAR_H(i),
                               WIFI_BAR_W, WIFI_BAR_H(i), 1, C_RULE);
    lbl_ssid = mk_lbl(bar, "", UI_FONT_16, C_LABEL, TOP_SSID_X, 22);
    lv_label_set_long_mode(lbl_ssid, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_ssid, TOP_TEMP_X - TOP_SSID_X - 12);
}

static void wifi_status_paint(void)
{
    if (!lbl_ssid) return;

    bool ap = host_wifi_ap();
    bool on = host_wifi_connected();
    int  n  = (ap || !on) ? 0 : wifi_bars_from_rssi(host_wifi_rssi());

    /* Pristupovy bod neni pripojeni k cizi siti, takze sila signalu v
     * nem nic neznamena a carky zustanou tmave. */
    lv_label_set_text(lbl_ssid, ap ? T(STR_WIFI_AP)
                                   : (on ? host_wifi_ssid() : T(STR_NONE)));
    lv_obj_set_style_text_color(lbl_ssid,
        lv_color_hex((ap || on) ? C_BRIGHT : C_LABEL), 0);
    for (int i = 0; i < WIFI_BARS; i++)
        lv_obj_set_style_bg_color(wifi_bar[i],
            lv_color_hex(i < n ? C_SAGE : C_RULE), 0);
}

/* ---------------------------------------------------------------------
 * Sipky prenosu
 *
 * Dve sipky nad sebou: nahoru vysilame, dolu prijimame. Kresli se
 * vektorem (lv_line), ne znakem - Montserrat sipky nema.
 *
 * Jsou o SBERNICI, ne o WiFi. Pro WiFi zadny skutecny zdroj dat nemame
 * (Arduino citace bajtu nevystavuje) a blikat necim, co se nemeri, je
 * presne to, co tahle aplikace delat nesmi. Citace ramcu CAN naopak
 * mame a je to prave ten prenos, ktery clovek u diagnostiky hleda:
 * dolni sipka rika, ze vuz mluvi, horni ze mluvime my. V listen-only
 * zustane horni tmava vzdycky - a to je spravne, nic neposilame.
 * ------------------------------------------------------------------- */
#define XFER_W   18
#define XFER_H   11
#define XFER_LW  3

static lv_obj_t *xfer_tx, *xfer_rx;
static uint32_t xfer_rx_seen, xfer_tx_seen;    /* posledni stav citacu */
static uint32_t xfer_rx_at, xfer_tx_at;        /* kdy se naposled zmenily */

static lv_obj_t *mk_chevron(lv_obj_t *p, lv_coord_t x, lv_coord_t y, bool up)
{
    /* Body musi prezit cely beh - lv_line si je jen zapamatuje. */
    static lv_point_t up_pts[3], dn_pts[3];
    lv_point_t *pts = up ? up_pts : dn_pts;

    pts[0].x = 0;          pts[0].y = up ? XFER_H : 0;
    pts[1].x = XFER_W / 2; pts[1].y = up ? 0 : XFER_H;
    pts[2].x = XFER_W;     pts[2].y = pts[0].y;

    lv_obj_t *l = lv_line_create(p);
    lv_line_set_points(l, pts, 3);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_line_width(l, XFER_LW, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(C_RULE), 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static void xfer_build(lv_obj_t *bar)
{
    xfer_tx = mk_chevron(bar, TOP_XFER_X, 14, true);
    xfer_rx = mk_chevron(bar, TOP_XFER_X, 38, false);
}

/* Jak dlouho sipka sviti po poslednim ramci. Kratsi nez 200 ms uz oko
 * nestihne, delsi by splynulo v trvale svetlo a prestalo by rozlisovat
 * zivy provoz od toho, ktery prave skoncil. */
#define XFER_HOLD_MS 300

static void xfer_paint(uint32_t now)
{
    if (!xfer_tx) return;

    const can_stats_t *cs = (host && host->can_stats) ? host->can_stats() : NULL;

    if (cs) {
        if (cs->rx != xfer_rx_seen) { xfer_rx_seen = cs->rx; xfer_rx_at = now; }
        if (cs->tx != xfer_tx_seen) { xfer_tx_seen = cs->tx; xfer_tx_at = now; }
    }
    bool rx = cs && xfer_rx_at && (now - xfer_rx_at) < XFER_HOLD_MS;
    bool tx = cs && xfer_tx_at && (now - xfer_tx_at) < XFER_HOLD_MS;

    lv_obj_set_style_line_color(xfer_rx, lv_color_hex(rx ? C_SAGE : C_RULE), 0);
    lv_obj_set_style_line_color(xfer_tx, lv_color_hex(tx ? C_ACTUAL : C_RULE), 0);
}

/* ---------------- horni lista ---------------- */

/* Horni lista neni nadpis, je to STAVOVY RADEK. Zleva doprava:
 *
 *   carky + jmeno site   jsme na WiFi, a na ktere
 *   teplota              jediny mereny udaj, ktery je videt porad
 *   sipky                jede neco po sbernici, a kterym smerem
 *   stav vozidla         jsou cisla na obrazovce z auta?
 *   profil               ktery profil plati
 *
 * Slovo TALENTO odtud zmizelo. Jmeno vyrobku si clovek za jizdy
 * precte jednou za zivot, kdezto tyhle ctyri veci potrebuje porad, a
 * 110 px na nadpis byla ta nejdrazsi plocha na cele obrazovce.
 * ------------------------------------------------------------------- */
static void build_chrome(lv_obj_t *scr)
{
    lv_obj_t *bar = mk_panel(scr, 0, 0, 800, TOP_H);

    wifi_status_build(bar);

    mk_lbl(bar, T(STR_TOP_TEMP), UI_FONT_14, C_LABEL, TOP_TEMP_X, 8);
    lbl_temp = mk_lbl(bar, VAL_NONE, UI_FONT_32, C_BRIGHT, TOP_TEMP_X, 24);

    xfer_build(bar);

    /* Misto drivejsiho natvrdo napsaneho DEMO: dva radky, ktere rikaji,
     * jestli jsou cisla na obrazovce z auta. Nahore uroven pripojeni,
     * pod ni stari nejmladsiho udaje od jednotky. */
    lbl_state     = mk_lbl(bar, T(STR_LINK_NO_BUS), UI_FONT_16, C_DEFICIT,
                           TOP_STATE_X, 8);
    lbl_state_age = mk_lbl(bar, VAL_NONE, UI_FONT_14, C_LABEL,
                           TOP_STATE_X, 32);
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
    /* Sloupec je ukazatel, ale DA SE NA NEJ SAHNOUT: stisk ho oznaci,
     * podrzeni prejde na stranku DPF, pohyb z toho udela obycejny tah.
     * Lezi u praveho okraje, odkud tah na dalsi stranku zacina, takze
     * ma EVENT_BUBBLE (tah dojde obrazovce) a obe obsluhy se ptaji
     * was_drag(). Viz rail_press_cb() a spol. */
    lv_obj_clear_flag(rail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(rail, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(rail, press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(rail, rail_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(rail, rail_pressing_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(rail, rail_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(rail, rail_release_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(rail, drag_guard_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(rail, rail_click_cb, LV_EVENT_CLICKED, NULL);

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
    /* Prekryv patri profilu, takze stav z NVS plati od prvniho snimku. */
    perf_on = PROF.perf;
    perf_apply();
    perf_paint();

    gest_hint_build();
    set_nav_state(0);
}

/* ---------------- obnovovani ---------------- */

/* Stari udaje slovem. Do sta vterin i s desetinou, pak uz jen cele -
 * po dvou minutach nikoho desetina nezajima a cislo by jen blikalo. */
static void age_text(char *b, size_t n, uint32_t ms)
{
    if (ms == UINT32_MAX)   snprintf(b, n, "%s", VAL_NONE);
    else if (ms < 100000u)  snprintf(b, n, "%u.%u s", ms / 1000u,
                                     (ms / 100u) % 10u);
    else                    snprintf(b, n, "%u s", ms / 1000u);
}

/* Stav pripojeni k vozidlu. Ctyri stavy, ne dva - rozdil mezi "nikdo
 * neodpovida" a "neptam se" je to hlavni, co tam clovek hleda:
 *
 *   BEZ SBERNICE  cervene   nic nejezdi; vypnute zapalovani, spatna
 *                           rychlost, prohozene piny, deska na stole
 *   POSLOUCHAM    jantarove sbernice zije, ale jsme jen posluchac, takze
 *                           cisla na obrazovce nemuzou byt ziva
 *   BEZ ODPOVEDI  cervene   ptame se a nikdo neodpovida
 *   JEDNOTKA      zelene    jednotka odpovida, cisla jsou z auta
 *
 * Pod tim stari nejmladsiho udaje. Dokud neni ani jeden, jsou tam
 * pomlcky - nula by tvrdila, ze prave ted neco prislo. */
static void link_state_paint(void)
{
    diag_link_state_t ls = dp ? diag_link_state(diag_poller_link(dp))
                              : DIAG_LINK_DOWN;
    bool armed = dp && diag_poller_armed(dp);
    int  s;
    uint32_t c;

    if (ls == DIAG_LINK_ECU) {
        s = STR_LINK_ECU;    c = C_SAGE;
    } else if (ls == DIAG_LINK_DOWN) {
        s = STR_LINK_NO_BUS; c = C_DEFICIT;
    } else if (armed) {
        s = STR_LINK_NO_ECU; c = C_DEFICIT;
    } else {
        s = STR_LINK_LISTEN; c = C_ACTUAL;
    }
    lv_label_set_text(lbl_state, T(s));
    lv_obj_set_style_text_color(lbl_state, lv_color_hex(c), 0);

    char age[16];
    age_text(age, sizeof(age), dp ? diag_poller_fresh_ms(dp) : UINT32_MAX);
    lv_label_set_text(lbl_state_age, age);
}

/* Cely stavovy radek jednim volanim, at se jeho casti nemuzou rozejit. */
static void status_bar_paint(uint32_t now)
{
    wifi_status_paint();
    xfer_paint(now);
    link_state_paint();
}

/* Pravy sloupec: nasyceni filtru a bezici regenerace. Bez prectenych
 * sazi se vypln vubec nekresli - prazdny obrys s pomlckami znamena
 * "nevim", kdezto vypln na nule by znamenala "nula gramu". */
static void rail_paint(uint32_t now)
{
    bool has = ch_has(DIAG_CH_SOOT);
    int  soot = has ? (int)(ch_val(DIAG_CH_SOOT) * 100.0f) : 0;
    uint32_t sc = soot > 1800 ? C_DEFICIT : (soot > 1200 ? C_ACTUAL : C_SAGE);
    uint32_t bg = C_INK;

    if (regen_on()) {
        sc = C_DEFICIT;
        bg = ((now / RGN_BLINK_MS) & 1u) ? C_REGEN_HOT : C_REGEN_DIM;
    }
    if (bg != rail_bg_shown) {          /* prekreslovat jen pri zmene */
        rail_bg_shown = bg;
        lv_obj_set_style_bg_color(rail, lv_color_hex(bg), 0);
    }
    lv_bar_set_value(rail_bar, soot, LV_ANIM_OFF);
    lv_obj_set_style_bg_opa(rail_bar, has ? LV_OPA_COVER : LV_OPA_TRANSP,
                            LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(rail_bar, lv_color_hex(sc), LV_PART_INDICATOR);
    val_fmt(rail_val, DIAG_CH_SOOT, sc, "%d.%d", soot / 100, (soot / 10) % 10);

    /* Delici linka sloupce je zaroven odezva na dotek a ukazatel
     * postupu podrzeni. Tri podoby, viz komentar u rail_press_cb():
     * vlasova v klidu, modra rostouci pri drzeni, modra cela po
     * tuknuti. Prekresluje se jen pri zmene vysky nebo barvy - kazdy
     * snimek by to byla prace pro nic. */
    lv_coord_t h = 0;

    if (rail_hold_live) {
        uint32_t el = now - rail_press_at;
        if (el > RAIL_HOLD_MS) el = RAIL_HOLD_MS;
        h = (lv_coord_t)((uint32_t)PAGE_H * el / RAIL_HOLD_MS);
        if (h < 2) h = 2;           /* at je oznaceni videt hned */
    } else if ((int32_t)(rail_flash_until - now) > 0) {
        h = PAGE_H;
    }

    if (h != rail_rule_h) {
        rail_rule_h = h;
        /* Roste odspodu nahoru: zacatek u palce, ktery drzi, a smer
         * nahoru je tentyz jako u vyplne sazi vedle. */
        lv_obj_set_size(rail_rule, h ? 3 : 1, h ? h : PAGE_H);
        lv_obj_set_pos(rail_rule, 0, h ? PAGE_H - h : 0);
        lv_obj_set_style_bg_color(rail_rule,
            lv_color_hex(h ? C_DEMAND : C_RULE), 0);
    }
}

static void dtc_tags(char *b, int n, uint8_t st)
{
    int at = 0;

    b[0] = '\0';
    if (st & UDS_DTC_CONFIRMED)    at = str_add(b, at, n, "%s ", T(STR_DTC_CONF));
    if (st & UDS_DTC_FAILED_CYCLE) at = str_add(b, at, n, "%s ", T(STR_DTC_CYCLE));
    if (st & UDS_DTC_PENDING)      at = str_add(b, at, n, "%s ", T(STR_DTC_PEND));
    if (st & UDS_DTC_WARNING)      str_add(b, at, n, "%s", T(STR_DTC_LAMP));
}

static int rec_str(uint8_t kind)
{
    switch (kind) {
    case DIAG_REC_ODO_AT:     return STR_REC_ODO_AT;
    case DIAG_REC_AGING:      return STR_REC_AGING;
    case DIAG_REC_COUNT:      return STR_REC_COUNT;
    case DIAG_REC_ODO_HEALED: return STR_REC_ODO_HEALED;
    case DIAG_REC_DIST_WITH:  return STR_REC_DIST_WITH;
    default:                  return STR_REC_ODO_FIRST;
    }
}

static void dtc_paint(void)
{
    if (!dtc_head) return;

    uint16_t n = dp ? diag_poller_dtc_count(dp) : 0u;
    int sel = dp ? diag_poller_dtc_selected(dp) : -1;
    char b[192];

    /* Hlavicka. "Zadna chyba" se smi napsat az po odpovedi jednotky -
     * prazdny seznam a neprectena pamet vypadaji stejne jen tomu, kdo
     * se neptal. */
    if (!dp || !diag_poller_dtc_known(dp)) {
        bool ecu = dp && diag_link_state(diag_poller_link(dp)) == DIAG_LINK_ECU;
        lv_label_set_text(dtc_head, T(ecu ? STR_DTC_UNREAD : STR_NO_CAN));
        lv_obj_set_style_text_color(dtc_head, lv_color_hex(C_LABEL), 0);
    } else if (n == 0u) {
        lv_label_set_text(dtc_head, T(STR_DTC_NONE));
        lv_obj_set_style_text_color(dtc_head, lv_color_hex(C_SAGE), 0);
    } else {
        snprintf(b, sizeof(b), T(STR_DTC_SOME), (unsigned)n);
        lv_label_set_text(dtc_head, b);
        lv_obj_set_style_text_color(dtc_head, lv_color_hex(C_DEFICIT), 0);
    }

    val_fmt(dtc_odo, DIAG_CH_ODO, C_LABEL, "%s %u", T(STR_ODOMETER),
            (unsigned)ch_val(DIAG_CH_ODO));

    for (int i = 0; i < DTC_ROWS; i++) {
        const diag_dtc_t *d = dp ? diag_poller_dtc_at(dp, (uint16_t)i) : NULL;

        if (!d) {
            lv_obj_add_flag(dtc_row[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(dtc_row[i], LV_OBJ_FLAG_HIDDEN);

        char code[12], tags[96];
        diag_dtc_text(d->dtc, code, sizeof(code));
        dtc_tags(tags, (int)sizeof(tags), d->status);
        snprintf(b, sizeof(b), "%s\n%s", code, tags);
        lv_label_set_text(lv_obj_get_child(dtc_row[i], 0), b);
        lv_obj_set_style_border_color(dtc_row[i],
            lv_color_hex(i == sel ? C_DEFICIT : C_RULE), 0);
    }

    if (n > DTC_ROWS) {
        snprintf(b, sizeof(b), T(STR_DTC_SHOWN), (unsigned)DTC_ROWS);
        lv_label_set_text(dtc_hint, b);
    } else {
        lv_label_set_text(dtc_hint, T(STR_DTC_TAP));
    }

    /* Detail vybrane chyby. */
    if (sel < 0) {
        lv_label_set_text(dtc_ext, "");
        lv_label_set_text(dtc_ff, "");
        return;
    }
    int at = 0;
    if (!diag_poller_rec_known(dp)) {
        str_add(b, 0, (int)sizeof(b), "%s", T(STR_DTC_WAIT));
    } else if (diag_poller_rec_count(dp) == 0u) {
        str_add(b, 0, (int)sizeof(b), "%s", T(STR_DTC_EMPTY));
    } else {
        for (uint8_t i = 0; i < diag_poller_rec_count(dp); i++) {
            const diag_rec_t *r = diag_poller_rec_at(dp, i);

            at = r->unset
                 ? str_add(b, at, (int)sizeof(b), "%s %s\n",
                           T(rec_str(r->kind)), T(STR_REC_UNSET))
                 : str_add(b, at, (int)sizeof(b), "%s %u\n",
                           T(rec_str(r->kind)), r->val);
        }
    }
    lv_label_set_text(dtc_ext, b);

    at = 0;
    if (!diag_poller_ff_known(dp)) {
        str_add(b, 0, (int)sizeof(b), "%s", T(STR_DTC_WAIT));
    } else if (diag_poller_ff_count(dp) == 0u) {
        str_add(b, 0, (int)sizeof(b), "%s", T(STR_DTC_EMPTY));
    } else {
        for (uint8_t i = 0; i < diag_poller_ff_count(dp); i++) {
            const diag_ff_t *f = diag_poller_ff_at(dp, i);

            at = str_add(b, at, (int)sizeof(b), "%-9s %d\n",
                         CH_NAME[f->ch], (int)f->val);
        }
    }
    lv_label_set_text(dtc_ff, b);
}

static void dpf_paint(void)
{
    val_fmt(lbl_dpf[0], DIAG_CH_DPF_UP, C_ACTUAL, "%d",
            (int)ch_val(DIAG_CH_DPF_UP));
    val_fmt(lbl_dpf[1], DIAG_CH_DPF_DN, C_ACTUAL, "%d",
            (int)ch_val(DIAG_CH_DPF_DN));
    val_fmt(lbl_dpf[2], DIAG_CH_DPF_SP, C_DEMAND, "%d",
            (int)ch_val(DIAG_CH_DPF_SP));
    int soot = (int)(ch_val(DIAG_CH_SOOT) * 100.0f);
    val_fmt(lbl_dpf[3], DIAG_CH_SOOT, C_ACTUAL, "%d.%d",
            soot / 100, (soot / 10) % 10);
    int load = (int)(ch_val(DIAG_CH_CSF_LOAD) * 10.0f);
    val_fmt(lbl_dpf[4], DIAG_CH_CSF_LOAD, C_ACTUAL, "%d.%d",
            load / 10, load % 10);
    val_fmt(lbl_dpf[5], DIAG_CH_RGN_END, C_ACTUAL, "%d",
            (int)(ch_val(DIAG_CH_RGN_END) / 60.0f));

    if (!ch_has(DIAG_CH_RGN_DUR)) {
        lv_label_set_text(lbl_regen, VAL_NONE);
        lv_obj_set_style_text_color(lbl_regen, lv_color_hex(C_LABEL), 0);
    } else if (regen_on()) {
        lv_label_set_text_fmt(lbl_regen, "%s %d s", T(STR_REGEN_RUN),
                              (int)ch_val(DIAG_CH_RGN_DUR));
        lv_obj_set_style_text_color(lbl_regen, lv_color_hex(C_DEFICIT), 0);
    } else {
        lv_label_set_text(lbl_regen, T(STR_REGEN_OFF));
        lv_obj_set_style_text_color(lbl_regen, lv_color_hex(C_LABEL), 0);
    }
}

void ui_tick(void)
{
    uint32_t now = lv_tick_get();

    vals_step();
    status_bar_paint(now);

    /* Graf plnici tlaku je casova osa, ne seznam poslednich hodnot:
     * bod se pridava kazdych DIAG_FAST_MS bez ohledu na to, jestli
     * neco prislo. Kdyz jednotka prestane odpovidat, prazdne body
     * posunou caru pryc a je na prvni pohled videt dira - zmrzla cara
     * by vypadala jako ustaleny stav. */
    static uint32_t chart_at = 0;
    if (now - chart_at >= DIAG_FAST_MS) {
        chart_at = now;
        bool ok = ch_show(DIAG_CH_BOOST) == DIAG_SHOW_LIVE &&
                  ch_show(DIAG_CH_BOOST_SP) == DIAG_SHOW_LIVE;
        lv_chart_set_next_value(chart, ser_sp, ok ?
            (lv_coord_t)diag_poller_value(dp, DIAG_CH_BOOST_SP)
            : LV_CHART_POINT_NONE);
        lv_chart_set_next_value(chart, ser_act, ok ?
            (lv_coord_t)diag_poller_value(dp, DIAG_CH_BOOST)
            : LV_CHART_POINT_NONE);
    }

    float tp = cfg.fahrenheit ? ch_val(DIAG_CH_COOLANT) * 1.8f + 32.0f
                              : ch_val(DIAG_CH_COOLANT);
    val_fmt(lbl_temp, DIAG_CH_COOLANT, C_BRIGHT, "%d", (int)tp);

    rail_paint(now);

    switch (cur_page) {
    case PG_HLAVNI: {
        float sp = cfg.mph ? ch_val(DIAG_CH_SPEED) * 0.621371f
                           : ch_val(DIAG_CH_SPEED);
        lv_arc_set_value(arc_speed, ch_has(DIAG_CH_SPEED) ? (int)sp : 0);
        lv_arc_set_value(arc_rpm, ch_has(DIAG_CH_RPM)
                                  ? (int)ch_val(DIAG_CH_RPM) : 0);
        val_fmt(lbl_speed_v, DIAG_CH_SPEED, C_ACTUAL, "%d", (int)sp);
        val_fmt(lbl_rpm_v, DIAG_CH_RPM, C_DEMAND, "%d",
                (int)ch_val(DIAG_CH_RPM));
        break; }
    case PG_MOTOR:
        lv_arc_set_value(arc_boost, ch_has(DIAG_CH_BOOST)
                                    ? (int)ch_val(DIAG_CH_BOOST) : 800);
        val_fmt(lbl_boost, DIAG_CH_BOOST, C_ACTUAL, "%d",
                (int)ch_val(DIAG_CH_BOOST));
        val_fmt(lbl_boost_sp, DIAG_CH_BOOST_SP, C_DEMAND, "%d",
                (int)ch_val(DIAG_CH_BOOST_SP));
        val_fmt(lbl_vnt, DIAG_CH_VNT, C_ACTUAL, "%d",
                (int)ch_val(DIAG_CH_VNT));
        val_fmt(lbl_railp, DIAG_CH_RAIL, C_ACTUAL, "%d",
                (int)ch_val(DIAG_CH_RAIL));
        val_fmt(lbl_rail_sp, DIAG_CH_RAIL_SP, C_DEMAND, "%d",
                (int)ch_val(DIAG_CH_RAIL_SP));
        val_fmt(lbl_maf, DIAG_CH_MAF, C_ACTUAL, "%d",
                (int)ch_val(DIAG_CH_MAF));
        val_fmt(lbl_maf_sp, DIAG_CH_MAF_SP, C_DEMAND, "%d",
                (int)ch_val(DIAG_CH_MAF_SP));
        val_fmt(lbl_egr, DIAG_CH_EGR, C_ACTUAL, "%d",
                (int)ch_val(DIAG_CH_EGR));
        val_fmt(lbl_iat, DIAG_CH_IAT, C_ACTUAL, "%d",
                (int)ch_val(DIAG_CH_IAT));
        break;
    case PG_TLAK:
        /* Schodek ma smysl jen z dvojice, kterou mame OBOU. Jedna
         * chybejici hodnota znamena, ze schodek nikdo nezmeril. */
        if (ch_has(DIAG_CH_BOOST) && ch_has(DIAG_CH_BOOST_SP)) {
            int gap = (int)(ch_val(DIAG_CH_BOOST_SP) - ch_val(DIAG_CH_BOOST));
            bool fresh = ch_show(DIAG_CH_BOOST) == DIAG_SHOW_LIVE &&
                         ch_show(DIAG_CH_BOOST_SP) == DIAG_SHOW_LIVE;
            lv_label_set_text_fmt(lbl_gap, "%d", gap);
            lv_obj_set_style_text_color(lbl_gap, lv_color_hex(
                !fresh ? C_LABEL : (gap > 300 ? C_DEFICIT : C_SAGE)), 0);
        } else {
            lv_label_set_text(lbl_gap, VAL_NONE);
            lv_obj_set_style_text_color(lbl_gap, lv_color_hex(C_LABEL), 0);
        }
        break;
    case PG_CHYBY:
        dtc_paint();
        break;
    case PG_DPF:
        dpf_paint();
        break;
    case PG_NASTAV:
        /* Jmeno site uz nese tlacitko WIFI, takze radek pod nim rika
         * to, co se na tlacitko nevejde: adresu a pamet. */
        lv_label_set_text(lbl_wifi, host_wifi_ip());
        lv_obj_set_style_text_color(lbl_wifi,
            lv_color_hex(host_wifi_connected() ? C_SAGE : C_LABEL), 0);
        lv_label_set_text_fmt(lbl_heap, "heap %u kB   PSRAM %u kB",
                              host_heap_kb(), host_psram_kb());
        wifi_btn_paint();
        if (btn_canmode) canmode_btn_paint();
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

    /* Ztlumeni po necinnosti. lv_disp_get_inactive_time pocita od
     * posledni akce uzivatele, takze resi probuzeni i ztlumeni jednim
     * merenim.
     *
     * Tri stavy, ktere se nesmi plest: panel vypnuty (to udelal clovek
     * tlacitkem ZHASNOUT DISPLEJ), ztlumeny, a plny jas. Dotek vraci
     * vzdycky na plny jas, jen z vypnuteho panelu az po ochranne
     * lhute - jinak by displej probudil tentyz dotek, kterym se
     * zhasinalo. */
    uint32_t idle = lv_disp_get_inactive_time(NULL);
    if (idle < 300) {
        if (!backlight_on) {
            if (now - blank_guard_ms > 1500) backlight(true);
        } else {
            dim_end();
        }
    } else if (PROF.dim_s && backlight_on && !bl_dimmed && !bl_fading &&
               idle > PROF.dim_s * 1000UL) {
        dim_begin();
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
}
