/*
 * lv_shot - vyfoti kazdou obrazovku UI do PNG, bez displeje a bez SDL.
 *
 * LVGL kresli do pametoveho bufferu, vlastni flush callback si ho jen
 * prekopiruje a pak se z nej udela PNG. Cas je virtualni (lv_tick_inc),
 * takze beh je deterministicky a nezavisi na rychlosti stroje.
 *
 * Druha cast je zkouska ovladani: registruje se predstirany dotyk a
 * predstirana deska (sken WiFi vraci vymyslene site), takze jdou
 * vyfotit i stavy, do kterych se UI dostane az po doteku - vysunuta
 * navigace, dialogy, odpocet u potvrzeni drzenim a stav regenerace.
 * Zaroven se tim da overit chovani, ktere na snimku videt neni, treba
 * ze tah z tlacitka navigace neprepne stranku; takove kontroly pisou
 * PASS/FAIL a nenulovy navratovy kod.
 *
 * Pouziti:  lv_shot <vystupni adresar> [kod jazyka]
 *
 * Bez kodu jazyka se kresli vychozi anglictina. "lv_shot /tmp/shots cs"
 * vyfoti ceskou verzi - tim se da zkontrolovat, ze se diakritika kresli
 * a ze se delsi preklady vejdou do rozvrzeni.
 */
#include <lvgl.h>

#include "ui.h"
#include "i18n.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define SCR_W 800
#define SCR_H 480

/* krok virtualniho casu; shodny s periodou lv_timeru na desce */
#define STEP_MS   20
/* kolik kroku nechat probehnout, nez se obrazovka vyfoti - pri 20 ms
 * to je 1,2 s, dost na dojeti sinusovek i na naplneni grafu */
#define WARM_STEPS 60

static lv_color_t draw_buf[SCR_W * SCR_H];
static lv_color_t frame[SCR_W * SCR_H];

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    for (lv_coord_t y = area->y1; y <= area->y2; y++) {
        if (y < 0 || y >= SCR_H) continue;
        for (lv_coord_t x = area->x1; x <= area->x2; x++) {
            if (x < 0 || x >= SCR_W) continue;
            frame[y * SCR_W + x] = color_p[(y - area->y1) * (area->x2 - area->x1 + 1)
                                           + (x - area->x1)];
        }
    }
    lv_disp_flush_ready(drv);
}

/* LVGL 8 umi ruzne hloubky barev; lv_color_to32 prevede kteroukoliv
 * na ARGB8888, takze zapis PNG na LV_COLOR_DEPTH nezavisi. */
static void frame_to_rgb(unsigned char *rgb)
{
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        uint32_t c = lv_color_to32(frame[i]);
        rgb[i * 3 + 0] = (unsigned char)((c >> 16) & 0xFF);
        rgb[i * 3 + 1] = (unsigned char)((c >> 8) & 0xFF);
        rgb[i * 3 + 2] = (unsigned char)(c & 0xFF);
    }
}

/* Nazvy stranek jsou prelozene, takze muzou obsahovat diakritiku i tecku.
 * Do nazvu souboru pustime jen holy ASCII. */
static void safe_name(char *dst, size_t n, const char *src)
{
    size_t k = 0;
    for (; *src && k + 1 < n; src++)
        if ((*src >= 'A' && *src <= 'Z') || (*src >= 'a' && *src <= 'z') ||
            (*src >= '0' && *src <= '9'))
            dst[k++] = *src;
    dst[k] = '\0';
}

/* ---------------------------------------------------------------------
 * Predstirana deska
 *
 * Sken vraci petici vymyslenych siti, dve z nich jsou "ulozene", aby
 * bylo videt odliseni v seznamu i zapominani. Hesla nikam nejdou, drzi
 * se jen v pameti teto zkousky.
 * ------------------------------------------------------------------- */
#define FAKE_SCAN_MS 1200       /* jak dlouho predstirany sken "bezi" */

static const char *const FAKE_SSID[] = {
    "dilna", "Trafic-hotspot", "domov", "UPC1234567", "sousedi"
};
#define FAKE_N ((int)(sizeof(FAKE_SSID) / sizeof(FAKE_SSID[0])))

static char fake_saved[4][33] = { "dilna", "domov" };
static int  fake_saved_n = 2;
static uint32_t fake_scan_done_at = 0;
static bool fake_scan_running = false;
static char fake_conn[33] = "";

static int fake_find(const char *ssid)
{
    for (int i = 0; i < fake_saved_n; i++)
        if (strcmp(fake_saved[i], ssid) == 0) return i;
    return -1;
}

static void fake_scan_start(void)
{
    fake_scan_running = true;
    fake_scan_done_at = lv_tick_get() + FAKE_SCAN_MS;
}

static int fake_scan_result(void)
{
    if (!fake_scan_running) return -2;
    if (lv_tick_get() < fake_scan_done_at) return -1;
    return FAKE_N;
}

static const char *fake_scan_ssid(int i)
{
    return (i >= 0 && i < FAKE_N) ? FAKE_SSID[i] : "";
}

static void fake_connect(const char *ssid, const char *pass)
{
    (void)pass;
    if (fake_find(ssid) < 0 && fake_saved_n < 4)
        snprintf(fake_saved[fake_saved_n++], 33, "%s", ssid);
    snprintf(fake_conn, sizeof(fake_conn), "%s", ssid);
}

static bool fake_known(const char *ssid) { return fake_find(ssid) >= 0; }

static bool fake_connect_known(const char *ssid)
{
    if (fake_find(ssid) < 0) return false;
    snprintf(fake_conn, sizeof(fake_conn), "%s", ssid);
    return true;
}

static void fake_forget(const char *ssid)
{
    int i = fake_find(ssid);
    if (i < 0) return;
    for (int k = i; k + 1 < fake_saved_n; k++)
        memcpy(fake_saved[k], fake_saved[k + 1], 33);
    fake_saved_n--;
}

static bool fake_connected(void) { return fake_conn[0] != '\0'; }
static const char *fake_ssid(void) { return fake_conn; }
static const char *fake_ip(void) { return fake_conn[0] ? "192.168.1.42" : ""; }

/* Stmivani: predstirana deska si jen zapamatuje, co UI poslalo. Na
 * obrazovce neni jas videt, takze je to jedina cesta, jak zkontrolovat,
 * ze tlacitka jasu opravdu neco delaji. */
static int fake_bl_percent = -1;
static void fake_bl_set(int percent) { fake_bl_percent = percent; }

/* Volna pamet, kterou deska hlasi diagnostice. Cisla jsou vymyslena -
 * jde o to, ze dialog ma co ukazat a ze se vubec zeptal. */
static unsigned fake_heap_kb(void)  { return 184; }
static unsigned fake_psram_kb(void) { return 7844; }

/* Polozky jsou jmenovane ze stejneho duvodu jako v main.cpp: tri dvojice
 * v ui_host_t maji shodny typ, takze pozicni seznam by prehozeni dvou
 * deklaraci prelozil bez varovani - a kdyby byly pozicni oba konce,
 * neodhalila by to ani tahle zkouska. */
static const ui_host_t FAKE_HOST = {
    .bl_set_percent     = fake_bl_set,
    .wifi_scan_start    = fake_scan_start,
    .wifi_scan_result   = fake_scan_result,
    .wifi_scan_ssid     = fake_scan_ssid,
    .wifi_connect       = fake_connect,
    .wifi_connected     = fake_connected,
    .wifi_ssid          = fake_ssid,
    .wifi_ip            = fake_ip,
    .wifi_known         = fake_known,
    .wifi_connect_known = fake_connect_known,
    .wifi_forget        = fake_forget,
    .heap_kb            = fake_heap_kb,
    .psram_kb           = fake_psram_kb,
};

/* Tataz deska, ale bez dratku z GPIO6 na plosku BL: stmivat neumi.
 * Nastaveni na ni nesmi nabidnout ani jedno tlacitko jasu. */
static const ui_host_t FAKE_HOST_NODIM = {
    .bl_set_percent     = NULL,     /* deska stmivat neumi */
    .wifi_scan_start    = fake_scan_start,
    .wifi_scan_result   = fake_scan_result,
    .wifi_scan_ssid     = fake_scan_ssid,
    .wifi_connect       = fake_connect,
    .wifi_connected     = fake_connected,
    .wifi_ssid          = fake_ssid,
    .wifi_ip            = fake_ip,
    .wifi_known         = fake_known,
    .wifi_connect_known = fake_connect_known,
    .wifi_forget        = fake_forget,
    .heap_kb            = fake_heap_kb,
    .psram_kb           = fake_psram_kb,
};

/* ---------------------------------------------------------------------
 * Predstirany dotyk
 *
 * LVGL si cte polohu a stav z techto dvou promennych, takze tah se sklada
 * z posouvani bodu mezi volanimi lv_timer_handler().
 * ------------------------------------------------------------------- */
static lv_point_t touch_pt;
static bool touch_down;

static void read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    data->point = touch_pt;
    data->state = touch_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Beh virtualniho casu. anim_only necha stranou ui_tick(), takze se
 * namerene hodnoty nehybou a dva snimky jdou porovnat. */
static void run_ms(int ms, bool anim_only)
{
    for (int t = 0; t < ms; t += STEP_MS) {
        lv_tick_inc(STEP_MS);
        if (!anim_only) ui_tick();
        lv_timer_handler();
    }
}

/* Dlouhe cekani na necinnost. Krok je hruby schvalne: desitky vterin po
 * 20 ms by znamenaly tisice prekresleni cele obrazovky a zkouska by
 * kvuli tomu bezela minuty. Jemny krok je potreba az tam, kde se neco
 * deje, a na to je run_ms(). */
static void idle_ms(int ms)
{
    for (int t = 0; t < ms; t += 200) {
        lv_tick_inc(200);
        ui_tick();
        lv_timer_handler();
    }
}

static void touch_at(lv_coord_t x, lv_coord_t y) { touch_pt.x = x; touch_pt.y = y; }

static void tap(lv_coord_t x, lv_coord_t y, bool anim_only)
{
    touch_at(x, y);
    touch_down = true;
    run_ms(120, anim_only);
    touch_down = false;
    run_ms(200, anim_only);
}

/* Tah z (x1,y1) do (x2,y2) po krocich; drzi prst dole, kdyz keep_down. */
static void swipe(lv_coord_t x1, lv_coord_t y1, lv_coord_t x2, lv_coord_t y2,
                  int steps, bool keep_down, bool anim_only)
{
    touch_at(x1, y1);
    touch_down = true;
    run_ms(60, anim_only);
    for (int i = 1; i <= steps; i++) {
        touch_at((lv_coord_t)(x1 + (x2 - x1) * i / steps),
                 (lv_coord_t)(y1 + (y2 - y1) * i / steps));
        run_ms(40, anim_only);
    }
    if (keep_down) return;
    touch_down = false;
    run_ms(300, anim_only);
}

static void touch_release(bool anim_only)
{
    touch_down = false;
    run_ms(300, anim_only);
}

/* Otisk obdelniku na obrazovce.
 *
 * Obraz se pred ctenim vzdy obnovi, aby otisk patril tomu, co je na
 * obrazovce TED. Bez toho by se cetl frame, ktery naposled nakreslil
 * shot() nebo bezici animace, a kontrola by porovnavala dva ruzne
 * okamziky. */
static unsigned region_hash(int x0, int y0, int x1, int y1)
{
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);

    unsigned h = 2166136261u;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            h ^= (unsigned)lv_color_to32(frame[y * SCR_W + x]);
            h *= 16777619u;
        }
    return h;
}

/* Obsah stranky pod horni listou a nad prekryvem navigace. Kdyz se
 * nezmeni, nezmenila se ani stranka. */
static unsigned content_hash(void) { return region_hash(0, 64, 736, 64 + 228); }

/* Jen horni pruh stranky. Mrizka navigace zacina az na 180 px, takze
 * tenhle otisk rika, KTERA stranka je videt, bez ohledu na to, kolik
 * navigace prave zabira - a da se vzit i uprostred tazeni. */
static unsigned page_hash(void) { return region_hash(0, 64, 736, 180); }

/* Jako page_hash(), ale bez leveho okraje: behem napnuteho gesta tam
 * sviti napoveda a ta se meni s prstem, ne se strankou. */
static unsigned page_hash_body(void) { return region_hash(16, 64, 736, 180); }

/* Ma cely obdelnik jednu jedinou barvu? Takhle se pozna, ze v nem neni
 * nic nez pozadi. */
static bool region_uniform(int x0, int y0, int x1, int y1)
{
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);

    uint32_t first = lv_color_to32(frame[y0 * SCR_W + x0]);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (lv_color_to32(frame[y * SCR_W + x]) != first) return false;
    return true;
}

/* Podil pixelu jedne barvy v obdelniku, v procentech. */
static int region_share(int x0, int y0, int x1, int y1, uint32_t color)
{
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);

    long hit = 0, all = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++, all++)
            if (lv_color_to32(frame[y * SCR_W + x]) == color) hit++;
    return all ? (int)(hit * 100 / all) : 0;
}

/* Projde strom LVGL a zmeri VIDITELNA tlacitka, ktera lezi v zadanem
 * vodorovnem pruhu. Velikost tlacitka jinak zvenku zmerit nejde: ui.c si
 * objekty drzi sam a na obrazovce ma tlacitko stejne pozadi jako panel
 * pod nim. Schovane vetve stromu se preskakuji - tlacitka stranky, ktera
 * zrovna neni videt, nikdo zmacknout nemuze. */
static void scan_btns(lv_obj_t *o, lv_coord_t y0, lv_coord_t y1,
                      int *n, lv_coord_t *minw, lv_coord_t *minh)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    uint32_t cnt = lv_obj_get_child_cnt(o);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *c = lv_obj_get_child(o, i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
        if (lv_obj_check_type(c, &lv_btn_class)) {
            lv_area_t a;
            lv_obj_get_coords(c, &a);
            if (a.y1 >= y0 && a.y2 < y1) {
                lv_coord_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);
                (*n)++;
                if (w < *minw) *minw = w;
                if (h < *minh) *minh = h;
            }
        }
        scan_btns(c, y0, y1, n, minw, minh);
    }
}

static int checks_failed = 0;

static void check(const char *what, bool ok)
{
    printf("%-52s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) checks_failed++;
}

/* ---------------------------------------------------------------------
 * Zapis snimku
 *
 * Snimek se nesmi jen ulozit - musi se taky podivat, jestli na nem neco
 * je. Zmena pozadi stranky na jednu barvu vyrobi sest cistych PNG, vsech
 * kontrol projde, navratovy kod je nula a job, ktery snimky commituje do
 * docs/img/screens, by takovy "vzhled vyrobku" zverejnil.
 *
 * Meri se dve veci: kolik ruznych barev snimek ma (prazdna nebo
 * jednobarevna obrazovka jich ma par) a jestli se lisi od predchoziho
 * snimku (dve po sobe jdouci obrazovky se nesmi shodovat do posledniho
 * pixelu - to by znamenalo, ze tuknuti nic neudelalo).
 *
 * Tabulka otisku v repozitari schvalne NENI: byla by presnejsi, ale
 * musela by se prepsat po kazde zmene vzhledu, takze by se brzy
 * aktualizovala naslepo a prestala cokoliv hlidat. Tohle hlida to, co
 * se nesmi stat nikdy, a nestoji to zadnou udrzbu.
 * ------------------------------------------------------------------- */
#define SHOT_MIN_COLORS 24

static unsigned char *rgb;
static const char *g_outdir;
static int shot_no;
static int shot_err;
static unsigned shot_prev_hash;

/* Pocet ruznych barev; staci hruby odhad, takze se cte kazdy osmy pixel
 * a barvy se zarazuji do male tabulky. */
static int shot_colors(void)
{
    uint32_t seen[SHOT_MIN_COLORS];
    int n = 0;
    for (int i = 0; i < SCR_W * SCR_H && n < SHOT_MIN_COLORS; i += 8) {
        uint32_t c = lv_color_to32(frame[i]);
        int j = 0;
        while (j < n && seen[j] != c) j++;
        if (j == n) seen[n++] = c;
    }
    return n;
}

static void shot(const char *name)
{
    /* vynutit prekresleni cele obrazovky, at ve frame neni nic stareho */
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);
    frame_to_rgb(rgb);

    char path[512];
    snprintf(path, sizeof(path), "%s/%02d-%s.png", g_outdir, shot_no++, name);
    if (!stbi_write_png(path, SCR_W, SCR_H, 3, rgb, SCR_W * 3)) {
        fprintf(stderr, "zapis %s selhal\n", path);
        shot_err = 1;
        return;
    }
    printf("%s\n", path);

    char what[80];
    unsigned h = region_hash(0, 0, SCR_W, SCR_H);
    snprintf(what, sizeof(what), "snimek %s neni prazdny", name);
    check(what, shot_colors() >= SHOT_MIN_COLORS && h != shot_prev_hash);
    shot_prev_hash = h;
}

/* Souradnice, ktere zkouska poklepava. Vychazeji z rozvrzeni v ui.c;
 * kdyz se rozvrzeni posune, posunou se i tady. */
#define TAP_HANDLE_X   400
#define TAP_HANDLE_Y   460              /* uchyt navigace */
#define PAGE_Y(y)      (64 + (y))       /* stranka zacina pod horni listou */

/* Navigace: jeden panel, lista nahore, pod ni dve rady dlazdic.
 * NAV_* musi sedet s ui.c. */
#define NAV_HANDLE_H   44
#define NAV_BAR_H      152
#define NAV_FULL_H     416
#define NAV_BTN_H      140
#define NAVBTN_X(i)    (3 + (i) * 133 + 64)
#define NAVBTN_Y       (480 - NAV_BAR_H + 6 + NAV_BTN_H / 2)
/* dlazdice ve vysunutem stavu: panel zacina na 64 px */
#define NAVTILE_X(c)   (3 + (c) * 133 + 64)
#define NAVTILE_Y(r)   (64 + NAV_BAR_H + 20 + (r) * 124 + 60)

/* Stranka nastaveni: stredy tlacitek (SET_BH = 68, rady 28 / 120 / 194)
 * a tlacitek v dialogu jasu (BL_* v ui.c). */
#define SET_BLANK_X    (20 + 2 * 176 + 80)
#define SET_BLANK_Y    PAGE_Y(28 + 34)
#define SET_WIFI_X     (20 + 160)
#define SET_WIFI_Y     PAGE_Y(120 + 34)
#define SET_BRIGHT_X   (360 + 80)
#define SET_BRIGHT_Y   PAGE_Y(120 + 34)
#define SET_DAYNIGHT_X (540 + 85)
#define SET_DAYNIGHT_Y PAGE_Y(120 + 34)
/* dialog jasu lezi na cele obrazovce, takze PAGE_Y se na nej nevztahuje:
 * radek dne, radek noci, urovne a prepinac do horni listy */
#define BL_DAY_Y       (104 + 64)
#define BL_NIGHT_Y     (104 + 136 + 64)
#define BL_LVL_X(i)    (212 + (i) * 112 + 52)
#define BL_MODE_X      (20 + 90)
#define BL_TOPBAR_X    (548 + 108)
#define BL_TOPBAR_Y    (376 + 43)
/* ikonka den/noc v horni liste */
#define DN_ICON_X      (800 - 120 - 8 + 26)
#define DN_ICON_Y      32

static void phase_navigace(void)
{
    ui_show_page(0);
    run_ms(400, true);
    shot("nav-uchyt");

    /* -----------------------------------------------------------------
     * Stranka ma plnou vysku, navigace je jen prekryv. Jsou to dve
     * ruzna tvrzeni a kazde si zaslouzi vlastni kontrolu:
     *
     *   1. obsah stranky saha i do pruhu, ktery vysunuta lista zakryje,
     *      tedy stranka opravdu neni zkracena podle navigace,
     *   2. vysunutim listy se otisk obsahu nad ni ani nehne.
     *
     * Otisky se berou HNED po sobe, mezi nimi neni zadny dalsi vstup.
     *
     * Mutacni zkouska: v ui.c zmenit
     *     #define PAGE_H  (480 - TOP_H)  ->  (480 - TOP_H - NAV_BAR_H)
     * cimz se stranky vrati k puvodnimu "zmensi se podle navigace".
     * Pruh pod zkracenou strankou je pak hole pozadi a prvni kontrola
     * MUSI selhat.
     * ----------------------------------------------------------------- */
    check("obsah stranky saha i pod vysunutou listu",
          !region_uniform(0, 480 - NAV_BAR_H, 736, 480 - NAV_HANDLE_H));

    unsigned content = region_hash(0, 64, 736, 480 - NAV_BAR_H);
    tap(TAP_HANDLE_X, TAP_HANDLE_Y, true);          /* uchyt -> lista */
    check("vysunuti navigace obsah stranky nezmensi",
          content == region_hash(0, 64, 736, 480 - NAV_BAR_H));
    run_ms(300, true);          /* zapadnuti dojede, at je lista na miste */
    shot("nav-lista");

    /* -----------------------------------------------------------------
     * Tlacitko stranky musi byt aspon tak velke, jak zada docs/ui.md:
     * 120 px = 14 mm. Zmerit ho zvenku jinak nejde - na obrazovce ma
     * stejne pozadi jako panel pod nim - takze se meri primo objekty
     * LVGL v pruhu vysunute listy.
     *
     * Mutacni zkouska: v ui.c vratit NAV_BTN_H na puvodnich 110.
     * ----------------------------------------------------------------- */
    int nbtn = 0;
    lv_coord_t bw = 9999, bh = 9999;
    scan_btns(lv_scr_act(), 480 - NAV_BAR_H, 480, &nbtn, &bw, &bh);
    printf("tlacitka stranek: %d, nejmensi %d x %d px\n", nbtn, (int)bw, (int)bh);
    check("tlacitko stranky ma aspon 120 x 120 px",
          nbtn == ui_page_count() && bw >= 120 && bh >= 120);

    /* otisk listy, jak vypada ve stavu 1 - porovna se s mrizkou */
    unsigned bar_out = region_hash(0, 480 - NAV_BAR_H, 800, 480);

    /* -----------------------------------------------------------------
     * Tah zacaty NA tlacitku stranky nesmi stranku prepnout. To hlida
     * drag_guard_cb v ui.c; na zeleze se bez nej vysouvani listy
     * prepinalo stranky.
     *
     * Tah je dlouhy 40 px zamerne:
     *   - vic nez DRAG_PX (30 px), takze uz to neni tuknuti,
     *   - min nez LV_INDEV_DEF_GESTURE_LIMIT (50 px), aby se do toho
     *     nepletlo prepnuti stranky gestem,
     *   - a prst zustane na tomtez tlacitku (vysoke 140 px), takze LVGL
     *     na konci opravdu posle klik. Delsi tah prst z tlacitka svede
     *     a klik by nikam nedosel, cimz by kontrola prestala merit to,
     *     na co je.
     *
     * Otisk se bere HNED po tomhle jednom tahu. Driv se bral az po
     * dalsich ctyrech vstupech, ktere stav mezitim vracely zpatky.
     *
     * Mutacni zkouska - takhle se kdykoli overi, ze kontrola neni plana:
     *     sed -i 's/#define DRAG_PX  30/#define DRAG_PX  9000/' src/ui.c
     * s prahem 9000 pojistka neuhlida nic, klik projde a tohle MUSI
     * selhat. Prah se pak vrati na 30.
     * ----------------------------------------------------------------- */
    unsigned before = page_hash();
    swipe(NAVBTN_X(2), NAVBTN_Y, NAVBTN_X(2), NAVBTN_Y - 40, 8, false, true);
    check("tah z tlacitka navigace neprepne stranku", before == page_hash());

    /* -----------------------------------------------------------------
     * Mrizka. Lista a dlazdice jsou jeden panel, takze ve stavu 2 musi
     * byt lista porad videt - jen o 264 px vys. Tvrdi se tu oboji: ze
     * se pruh u horni hrany panelu otiskem shoduje s tim, jak lista
     * vypadala ve stavu 1, a ze v nem opravdu stoji vsech sest tlacitek
     * stranek. Samotny otisk by na dva prazdne pruhy taky sedel.
     *
     * Mutacni zkouska: v ui.c zmenit
     *     #define NAV_BTN_Y  NAV_PAD  ->  (NAV_FULL_H - NAV_BAR_H + NAV_PAD)
     * cimz lista sjede na spodek panelu - presne to, co delaly dva
     * samostatne panely. Ve stavu 2 je pak nahore mrizka a kontrola
     * MUSI selhat.
     * ----------------------------------------------------------------- */
    swipe(NAVBTN_X(2), NAVBTN_Y, NAVBTN_X(2), 180, 6, false, true);
    shot("nav-mrizka");
    int gbtn = 0;
    lv_coord_t gw = 9999, gh = 9999;
    scan_btns(lv_scr_act(), 64, 64 + NAV_BAR_H, &gbtn, &gw, &gh);
    check("lista stranek je videt i ve stavu mrizky",
          gbtn == ui_page_count() &&
          bar_out == region_hash(0, 64, 800, 64 + NAV_BAR_H));

    /* -----------------------------------------------------------------
     * Dlazdice DEBUG otevira diagnostiku.
     * Mutacni zkouska: v ui.c dat NAV_TILE_DEBUG mimo mrizku (treba 11),
     * na miste prvni dlazdice pak neni tlacitko a nic se neotevre.
     * ----------------------------------------------------------------- */
    unsigned grid = page_hash();
    tap(NAVTILE_X(0), NAVTILE_Y(0), false);
    shot("diagnostika");
    check("dlazdice DEBUG otevre diagnostiku", grid != page_hash());
    swipe(120, 240, 700, 250, 8, false, false);     /* gestem zpet */
    check("gesto zavre diagnostiku", grid == page_hash());

    /* snimek tazeni na pulce cesty */
    swipe(400, 200, 400, 340, 5, true, true);
    shot("nav-tazeni");
    touch_release(true);
    swipe(400, 200, 400, 470, 6, false, true);     /* tahem dolu na uchyt */

    /* Druha pulka pravidla o tahu: obycejne tuknuti stranku prepnout
     * MUSI. Kdyby byla pojistka prisna az moc (was_drag() vzdy true),
     * propadlo by i tuknuti a kontrola vyse by porad hlasila PASS. */
    tap(TAP_HANDLE_X, TAP_HANDLE_Y, true);
    before = page_hash();
    tap(NAVBTN_X(2), NAVBTN_Y, true);
    check("tuknuti na tlacitko navigace stranku prepne", before != page_hash());

    swipe(400, 200, 400, 470, 6, false, true);     /* uklid: zpatky na uchyt */

    /* -----------------------------------------------------------------
     * Gesto se provede az PO PUSTENI prstu a jen tehdy, kdyz je v tu
     * chvili jeste platne. Dokud je prst dole, nesmi se stat nic -
     * uzivatel tak muze tah vratit a akci zrusit.
     *
     * Otisk se bere bez leveho okraje: behem napnuteho gesta tam sviti
     * napoveda a ta se meni s prstem, ne se strankou.
     *
     * Mutacni zkouska: v ui_build() v ui.c zaregistrovat gest_release_cb
     * na LV_EVENT_PRESSING misto LV_EVENT_RELEASED. Akce se pak spusti
     * uprostred tahu a obe kontroly MUSI selhat.
     * ----------------------------------------------------------------- */
    ui_show_page(2);
    run_ms(200, true);
    unsigned pg = page_hash_body();
    swipe(20, 240, 300, 240, 6, true, true);        /* prst zustava dole */
    check("gesto zpet se nespusti, dokud je prst dole", pg == page_hash_body());
    touch_release(true);
    check("gesto zpet se provede po pusteni", pg != page_hash_body());

    /* Vraceny tah akci zrusi - to je ta stiznost "obcas udelam zpet
     * omylem". Mutacni zkouska: v gest_release_cb() brat smer z
     * lv_indev_get_gesture_dir() misto z gest_dir_now(). */
    ui_show_page(2);
    run_ms(200, true);
    pg = page_hash_body();
    swipe(20, 240, 300, 240, 6, true, true);
    swipe(300, 240, 30, 240, 6, false, true);       /* zpatky k okraji a pustit */
    check("vraceny tah gesto zrusi", pg == page_hash_body());

    /* -----------------------------------------------------------------
     * Tah zacaty NA PRAVEM SLOUPCI DPF musi prepnout stranku uplne
     * stejne jako tah zacaty na strance - a prave u praveho okraje tah
     * doleva zacina. Sloupec je ukazatel, ne ovladaci prvek; kdyz si
     * dotek nechal pro sebe, gesto se sice napnulo (LV_EVENT_GESTURE
     * chodi po jine ceste nez stisk), ale uz nebylo kdo by ho pri
     * pusteni vyhodnotil, a tah neudelal nic.
     *
     * Mutacni zkouska: v build_chrome() v ui.c smazat radek
     *     lv_obj_clear_flag(rail_bar, LV_OBJ_FLAG_CLICKABLE);
     * Tah zacne na sloupci baru, stisk se k obrazovce nedostane a
     * kontrola MUSI selhat.
     * ----------------------------------------------------------------- */
    ui_show_page(0);
    run_ms(200, true);
    pg = page_hash_body();
    swipe(770, 240, 470, 240, 6, false, true);      /* od praveho okraje */
    check("tah od praveho okraje pres sloupec DPF prepne stranku",
          pg != page_hash_body());

    ui_show_page(0);
    run_ms(200, true);
}

static void phase_dialogy(void)
{
    /* ---- potvrzeni drzenim na strance DPF ---- */
    ui_show_page(4);
    run_ms(200, false);
    unsigned page_before = content_hash();

    tap(190, PAGE_Y(175), false);           /* SPUSTIT REGENERACI */
    shot("potvrzeni");

    /* gesto zprava musi dialog zavrit a NEsmi pod nim prepnout stranku */
    swipe(120, 240, 700, 250, 8, false, false);
    check("gesto zprava zavre potvrzovaci dialog", page_before == content_hash());

    /* znovu a tentokrat opravdu podrzet */
    tap(190, PAGE_Y(175), false);
    touch_at(210, 390);                     /* tlacitko DRZ 3 s */
    touch_down = true;
    run_ms(1040, false);                    /* tesne po preskoceni na "2" */
    shot("potvrzeni-blik");
    run_ms(460, false);
    shot("potvrzeni-odpocet");
    run_ms(1600, false);                    /* pres tri vteriny */
    shot("potvrzeni-hotovo");
    touch_release(false);
    run_ms(600, false);                     /* akce a zavreni dialogu */

    /* ---- stavy regenerace na sloupci ---- */
    shot("regen-odeslano");
    tap(540, PAGE_Y(175), false);           /* STAV REGENERACE -> bezi */
    run_ms(520, false);                     /* at padne do rozsviceneho bliknuti */
    shot("regen-bezi");
    tap(540, PAGE_Y(175), false);           /* -> bezi s procenty */
    run_ms(9000, false);
    shot("regen-procenta");
    tap(540, PAGE_Y(175), false);           /* -> zpatky vypnuto */
    run_ms(200, false);
}

static void phase_wifi(void)
{
    ui_show_page(5);
    run_ms(200, false);
    unsigned page_before = content_hash();

    /* -----------------------------------------------------------------
     * Dialog se zavre driv, nez dobehne sken - presne ta cesta, po ktere
     * to padalo: dobihajici sken psal do seznamu, ktery uz byl smazany.
     *
     * Tvrzeni je tu jen jedno a je to to, co otisk opravdu umi zmerit:
     * gesto zprava dialog zavre a stranku pod nim necha byt. Pri mutaci
     *     lv_obj_clear_flag(modal, LV_OBJ_FLAG_GESTURE_BUBBLE)  -> pryc
     * kontrola selze.
     *
     * Vlastni zapis do uvolnene pameti (uklid na LV_EVENT_DELETE, viz
     * modal_forget v ui.c) otisk obrazovky videt NEMUZE - uvolnena pamet
     * se na obrazovce neprojevi. Tenhle usek ji proto jen projde; kdyz
     * uklid chybi, chytne to az sanitizer nebo pad. Tvrdit tady vic by
     * byla plana jistota.
     * ----------------------------------------------------------------- */
    tap(SET_WIFI_X, SET_WIFI_Y, false);     /* VYBRAT SIT */
    shot("wifi-hledam");
    swipe(120, 240, 700, 250, 4, false, false);
    run_ms(2000, false);                    /* sken dobehne do prazdna */
    check("gesto zprava zavre dialog i behem skenu",
          page_before == content_hash());

    /* ---- seznam siti ---- */
    tap(SET_WIFI_X, SET_WIFI_Y, false);
    run_ms(1600, false);                    /* sken dobehne */
    shot("wifi-seznam");

    /* ZNOVU postavi dialog odznova z obsluhy tlacitka, ktere v nem lezi */
    tap(722, 210, false);
    run_ms(1600, false);

    /* neznama sit se zepta na heslo */
    tap(300, 184, false);
    shot("wifi-heslo");
    swipe(120, 300, 700, 310, 8, false, false);     /* gesto pres klavesnici */
    check("gesto zprava zavre dialog s heslem", page_before == content_hash());

    /* znama sit: dlouhy stisk nabidne zapomenuti */
    tap(SET_WIFI_X, SET_WIFI_Y, false);
    run_ms(1600, false);
    touch_at(300, 126);
    touch_down = true;
    run_ms(700, false);                     /* dlouhy stisk */
    touch_release(false);
    shot("wifi-zapomenout");

    /* Potvrzeni drzenim sit smaze a vrati seznam. Akce se spousti z
     * timeru odpoctu a sama postavi novy dialog, takze je to zaroven
     * zkouska, ze se pri tom nesaha na zruseny. */
    int saved_before = fake_saved_n;
    touch_at(210, 390);
    touch_down = true;
    run_ms(4200, false);                    /* 3 s odpocet a potvrzeni */
    touch_release(false);
    run_ms(1600, false);                    /* novy sken po navratu */
    check("podrzeni ulozenou sit zapomene", fake_saved_n == saved_before - 1);
    shot("wifi-po-zapomenuti");

    swipe(120, 240, 700, 250, 8, false, false);
    run_ms(200, false);
}

/* ---------------------------------------------------------------------
 * Jas podsviceni
 *
 * Dve veci, ktere na obrazovce nejsou videt a daji se proto tise
 * rozbit: polarita prevodu procent na stridu a to, jestli tlacitka
 * vubec neco posilaji na desku.
 * ------------------------------------------------------------------- */
static void phase_jas(void)
{
    /* Prevod je ciste pocetni, takze jde zkontrolovat primo.
     *
     * POLARITA: napeti na plosce BL proud LEDkam UBIRA, takze vetsi jas
     * musi dat MENSI stridu. Kdyby to nekdo obratil, displej by na
     * nejnizsi urovni svitil naplno a na nejvyssi zhasl - a nikdo by
     * nepoznal proc.
     *
     * Mutacni zkouska: v ui_bl_duty() vratit
     *     (uint32_t)((uint64_t)duty_max * permille / 1000)
     * misto rozdilu. Prvni dve kontroly pak musi selhat. */
    const uint32_t DMAX = 4095;
    check("100 % jasu dava mensi stridu nez 10 %",
          ui_bl_duty(100, DMAX) < ui_bl_duty(10, DMAX));
    check("0 % jasu je plna strida a 100 % nulova",
          ui_bl_duty(0, DMAX) == DMAX && ui_bl_duty(100, DMAX) == 0);

    /* Prevod musi klesat po celem rozsahu. Jediny preklep v tabulce by
     * jinak udelal dolik, ktereho by si na desce nikdo nevsiml.
     * Mutacni zkouska: prohodit v CURVE dve sousedni cisla. */
    bool mono = true;
    for (int p = 1; p <= 100; p++)
        if (ui_bl_duty(p, DMAX) > ui_bl_duty(p - 1, DMAX)) mono = false;
    check("strida klesa po celem rozsahu jasu", mono);

    /* ---- ovladani v nastaveni ---- */
    ui_show_page(5);
    run_ms(200, false);
    shot("nastaveni-jas");

    tap(SET_BRIGHT_X, SET_BRIGHT_Y, false);     /* tlacitko JAS */
    shot("jas-dialog");

    /* Tlacitko urovne musi novou hodnotu opravdu poslat na desku.
     * Mutacni zkouska: vyhodit bl_apply() z bl_level_cb() v ui.c. */
    tap(BL_LVL_X(1), BL_DAY_Y, false);          /* denni uroven 75 % */
    check("tlacitko urovne posle jas na desku", fake_bl_percent == 75);

    /* -----------------------------------------------------------------
     * Dialog ma radek na kazdy rezim, takze NOCNI uroven jde nastavit,
     * aniz by se do noci prepinalo. Jas, ktery prave sviti, se tim
     * nesmi hnout - a po prepnuti na noc musi prijit prave ta hodnota.
     *
     * Mutacni zkouska: v bl_level_cb() v ui.c ukladat vzdy do prave
     * zapnuteho rezimu (if (cfg.night) cfg.bright_night = p; else
     * cfg.bright_day = p;). Nocni uroven pak zustane na 35 a posledni
     * dve kontroly selzou.
     * ----------------------------------------------------------------- */
    tap(BL_LVL_X(4), BL_NIGHT_Y, false);        /* nocni uroven na 25 % */
    check("nastaveni nocni urovne nehne dennim jasem", fake_bl_percent == 75);
    shot("jas-dva-rezimy");

    /* Prepnuti rezimu musi poslat jeho vlastni ulozenou uroven.
     * Mutacni zkouska: v bl_level() vracet vzdy cfg.bright_day. */
    tap(BL_MODE_X, BL_NIGHT_Y, false);          /* rezim NOC */
    check("prepnuti na noc posle nocni uroven", fake_bl_percent == 25);
    shot("jas-noc");

    /* Navrat na den musi vratit denni uroven nedotcenou tim, co se
     * stalo v noci - to je cely smysl dvou ulozenych urovni. */
    tap(BL_MODE_X, BL_DAY_Y, false);            /* rezim DEN */
    check("navrat na den vrati denni uroven", fake_bl_percent == 75);

    /* Rychly prepinac v horni liste se zapina tady. */
    tap(BL_TOPBAR_X, BL_TOPBAR_Y, false);
    shot("jas-prepinac-v-liste");

    swipe(120, 240, 700, 250, 8, false, false);     /* zavrit dialog */
    run_ms(200, false);
    shot("nastaveni-jas-75");

    /* -----------------------------------------------------------------
     * Ikonka v horni liste musi delat PRESNE totez co prepinac v
     * nastaveni, vcetne poslani jasu na desku - obe cesty vedou do
     * daynight_cb(). Mutacni zkouska: v dn_build() v ui.c vyradit
     * lv_obj_add_event_cb(dn_btn, daynight_cb, LV_EVENT_CLICKED, NULL).
     * ----------------------------------------------------------------- */
    unsigned bar_day = region_hash(660, 0, 800, 64);
    tap(DN_ICON_X, DN_ICON_Y, false);
    check("ikonka v liste prepne na nocni jas", fake_bl_percent == 25);
    check("ikonka ukazuje prepnuty rezim", bar_day != region_hash(660, 0, 800, 64));
    shot("lista-noc");
    tap(DN_ICON_X, DN_ICON_Y, false);
    check("ikonka prepne zpatky na den", fake_bl_percent == 75);

    /* -----------------------------------------------------------------
     * Zhasnuti po necinnosti se musi nejdriv stahnout jasem a teprve pak
     * vypnout panel. Tvrdy skok z plneho jasu do tmy je v noci rana do
     * oka.
     *
     * Mutacni zkouska: v ui_tick() volat misto blank_begin() rovnou
     * backlight(false). Jas pak pri zhasinani nikdy neni mezi nulou a
     * nastavenou urovni a prvni kontrola selze.
     * ----------------------------------------------------------------- */
    tap(SET_BLANK_X, SET_BLANK_Y, false);       /* ZHASNOUT PO: nikdy -> 30 s */
    idle_ms(29000);                             /* tesne pred limitem */

    /* Dokrokovat po 20 ms, dokud se jas nezacne menit, a hned se podivat,
     * kde skoncil. Tvrde zhasnuti by preskocilo rovnou na nulu, stmivani
     * musi byt nekde mezi. */
    int mid = 75;
    for (int i = 0; i < 100 && mid == 75; i++) {
        run_ms(20, false);
        mid = fake_bl_percent;
    }
    check("pred zhasnutim se jas stahuje postupne", mid > 0 && mid < 75);

    run_ms(600, false);                         /* pul vteriny stmivani dobehne */
    check("na konci stmivani je jas na nule", fake_bl_percent == 0);

    /* -----------------------------------------------------------------
     * Probuzeni naopak NABIHA, zhruba vterinu: plny jas rovnou do oci je
     * v noci rana. Dotek se vyridi hned (tap() trva 320 ms virtualniho
     * casu a jas je v tu chvili teprve nekde na ceste), nabeh tedy nic
     * neblokuje.
     *
     * Mutacni zkouska: v backlight() v ui.c volat pri zapnuti rovnou
     * bl_apply() misto nastartovani nabehu. Jas pak skoci na 75 hned a
     * prvni kontrola selze.
     * ----------------------------------------------------------------- */
    tap(200, 400, false);                       /* dotek kamkoliv po strance */
    int wake = fake_bl_percent;
    check("probuzeni jas rozsviti postupne", wake > 0 && wake < 75);
    run_ms(900, false);
    check("nabeh po probuzeni dojede na ulozenou uroven",
          fake_bl_percent == 75);

    /* uklid: zpatky na "nikdy", at zbytek zkousky nezhasina */
    tap(SET_BLANK_X, SET_BLANK_Y, false);       /* 30 s -> 2 min */
    tap(SET_BLANK_X, SET_BLANK_Y, false);       /* -> 10 min */
    tap(SET_BLANK_X, SET_BLANK_Y, false);       /* -> nikdy */
}

/* Deska bez dratku z GPIO6 na plosku BL stmivat neumi a nesmi se tvarit,
 * ze ano. Postavi se cela UI znovu nad hostitelem, ktery bl_set_percent
 * nema, a stranka nastaveni musi vypadat jinak - misto tlacitek jasu je
 * na ni vysvetleni, proc to nejde.
 *
 * Mutacni zkouska: vyhodit podminku if (bl_can_dim()) z build_nastav()
 * v ui.c. Obe stranky pak vypadaji stejne a kontrola selze. */
static void phase_bez_dratku(void)
{
    ui_show_page(5);
    run_ms(200, false);
    unsigned with_dim = page_hash();

    lv_obj_t *fresh = lv_obj_create(NULL);
    lv_scr_load(fresh);
    ui_set_host(&FAKE_HOST_NODIM);
    ui_build();
    ui_show_page(5);
    run_ms(200, false);

    check("bez stmivani nejsou na strance tlacitka jasu",
          with_dim != page_hash());
    shot("jas-nelze");
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "pouziti: %s <vystupni adresar> [kod jazyka]\n", argv[0]);
        return 2;
    }
    g_outdir = argv[1];

    int lang = 0;
    if (argc == 3) {
        lang = i18n_find(argv[2]);
        if (lang < 0) {
            fprintf(stderr, "neznamy jazyk %s; mam:", argv[2]);
            for (int i = 0; i < i18n_count(); i++)
                fprintf(stderr, " %s", i18n_code(i));
            fprintf(stderr, "\n");
            return 2;
        }
    }

    if (mkdir(g_outdir, 0777) != 0) {
        struct stat st;
        if (stat(g_outdir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            fprintf(stderr, "nelze vytvorit adresar %s\n", g_outdir);
            return 1;
        }
    }

    lv_init();

    static lv_disp_draw_buf_t dbuf;
    lv_disp_draw_buf_init(&dbuf, draw_buf, NULL, SCR_W * SCR_H);

    static lv_disp_drv_t ddrv;
    lv_disp_drv_init(&ddrv);
    ddrv.draw_buf = &dbuf;
    ddrv.hor_res = SCR_W;
    ddrv.ver_res = SCR_H;
    ddrv.flush_cb = flush_cb;
    ddrv.full_refresh = 1;      /* at je v bufferu vzdy cely obraz */
    lv_disp_drv_register(&ddrv);

    static lv_indev_drv_t idrv;
    lv_indev_drv_init(&idrv);
    idrv.type = LV_INDEV_TYPE_POINTER;
    idrv.read_cb = read_cb;
    lv_indev_drv_register(&idrv);

    /* Vychozi nastaveni jako po prvnim startu desky: denni jas naplno,
     * nocni na tretinu, zhasinani vypnute (aby snimky nezhasinaly). */
    ui_cfg_t cfg = {
        .mph = false,
        .fahrenheit = false,
        .blank_s = 0,
        .lang = lang,
        .bright_day = 100,
        .bright_night = 35,
        .night = false,
    };
    ui_set_host(&FAKE_HOST);
    ui_set_cfg(&cfg);
    ui_build();

    rgb = malloc((size_t)SCR_W * SCR_H * 3);
    if (!rgb) { fprintf(stderr, "malloc selhal\n"); return 1; }

    /* Snimky z teto smycky se publikuji jako dokumentovany vzhled
     * vyrobku, takze u nich nestaci, ze nejsou prazdne: stranka se musi
     * kreslit na pozadi z palety UI. Prebarveni pozadi je ta zmena,
     * kterou nikdo na snimku nehleda a projde az do docs/img/screens.
     *
     * Tabulka otisku v repozitari by byla presnejsi, ale musela by se
     * prepsat po kazde zmene vzhledu, takze by se brzy aktualizovala
     * naslepo. Tohle hlida jednu vec, ktera se menit nema, a neudrzuje
     * se nijak.
     *
     * Mutacni zkouska: v mk_page() v ui.c nastavit pozadi na
     * lv_color_hex(0xFF00FF). Vsech sest kontrol MUSI selhat. */
    const uint32_t INK = lv_color_to32(lv_color_hex(0x0B1016));  /* paleta ui.c */
    int n = ui_page_count();
    for (int i = 0; i < n; i++) {
        ui_show_page(i);
        run_ms(WARM_STEPS * STEP_MS, false);

        char name[64];
        safe_name(name, sizeof(name), ui_page_name(i));
        shot(name);

        char what[96];
        snprintf(what, sizeof(what), "stranka %s kresli na pozadi UI", name);
        check(what, region_share(0, 64, 736, 436, INK) >= 40);
    }

    phase_navigace();
    phase_dialogy();
    phase_wifi();
    phase_jas();
    phase_bez_dratku();     /* posledni - stavi UI nad jinym hostitelem */

    free(rgb);
    if (checks_failed) printf("%d kontrol selhalo\n", checks_failed);
    return (shot_err || checks_failed) ? 1 : 0;
}
