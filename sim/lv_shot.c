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

static const ui_host_t FAKE_HOST = {
    NULL,                   /* backlight */
    fake_bl_set,
    NULL,                   /* save_cfg */
    fake_scan_start,
    fake_scan_result,
    fake_scan_ssid,
    fake_connect,
    fake_connected,
    fake_ssid,
    fake_ip,
    fake_known,
    fake_connect_known,
    fake_forget,
    NULL, NULL,             /* heap_kb, psram_kb */
};

/* Tataz deska, ale bez dratku z GPIO6 na plosku BL: stmivat neumi.
 * Nastaveni na ni nesmi nabidnout ani jedno tlacitko jasu. */
static const ui_host_t FAKE_HOST_NODIM = {
    NULL,                   /* backlight */
    NULL,                   /* bl_set_percent - deska stmivat neumi */
    NULL,                   /* save_cfg */
    fake_scan_start,
    fake_scan_result,
    fake_scan_ssid,
    fake_connect,
    fake_connected,
    fake_ssid,
    fake_ip,
    fake_known,
    fake_connect_known,
    fake_forget,
    NULL, NULL,             /* heap_kb, psram_kb */
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

static int checks_failed = 0;

static void check(const char *what, bool ok)
{
    printf("%-52s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) checks_failed++;
}

/* ---------------- zapis snimku ---------------- */
static unsigned char *rgb;
static const char *g_outdir;
static int shot_no;
static int shot_err;

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
}

/* Souradnice, ktere zkouska poklepava. Vychazeji z rozvrzeni v ui.c;
 * kdyz se rozvrzeni posune, posunou se i tady. */
#define TAP_HANDLE_X   400
#define TAP_HANDLE_Y   460              /* uchyt navigace */
#define NAVBTN_X(i)    (4 + (i) * 132 + 64)
#define NAVBTN_Y       (480 - 124 + 58) /* stred tlacitka stranky v liste */
#define PAGE_Y(y)      (64 + (y))       /* stranka zacina pod horni listou */

/* Stranka nastaveni: stredy tlacitek druhe rady (SET_ROW2 = 132, vyska
 * 76) a tlacitek v dialogu jasu (BL_BTN_* v ui.c). */
#define SET_BLANK_X    (20 + 2 * 176 + 80)
#define SET_BLANK_Y    PAGE_Y(28 + 38)
#define SET_WIFI_X     (20 + 160)
#define SET_WIFI_Y     PAGE_Y(132 + 38)
#define SET_BRIGHT_X   (360 + 80)
#define SET_BRIGHT_Y   PAGE_Y(132 + 38)
#define SET_DAYNIGHT_X (540 + 85)
#define SET_DAYNIGHT_Y PAGE_Y(132 + 38)
/* dialog jasu lezi na cele obrazovce, takze PAGE_Y se na nej nevztahuje */
#define BL_LVL_X(i)    (30 + (i) * 148 + 70)
#define BL_LVL_Y       (200 + 60)
#define BL_MODE_X      (30 + 150)
#define BL_MODE_Y      (358 + 55)

static void phase_navigace(void)
{
    ui_show_page(0);
    run_ms(400, true);

    tap(TAP_HANDLE_X, TAP_HANDLE_Y, true);
    shot("nav-lista");

    /* -----------------------------------------------------------------
     * Tah zacaty NA tlacitku stranky nesmi stranku prepnout. To hlida
     * drag_guard_cb v ui.c; na zeleze se bez nej vysouvani listy
     * prepinalo stranky.
     *
     * Tah je dlouhy 40 px zamerne:
     *   - vic nez DRAG_PX (30 px), takze uz to neni tuknuti,
     *   - min nez LV_INDEV_DEF_GESTURE_LIMIT (50 px), aby se do toho
     *     nepletlo prepnuti stranky gestem,
     *   - a prst zustane na tomtez tlacitku (vysoke 110 px), takze LVGL
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
    swipe(NAVBTN_X(2), NAVBTN_Y, NAVBTN_X(2), NAVBTN_Y - 40, 4, false, true);
    check("tah z tlacitka navigace neprepne stranku", before == page_hash());

    /* snimky mrizky a tazeni na pulce cesty */
    swipe(NAVBTN_X(2), NAVBTN_Y, NAVBTN_X(2), 180, 6, false, true);
    shot("nav-mrizka");
    swipe(400, 200, 400, 340, 5, true, true);
    shot("nav-tazeni");
    touch_release(true);
    swipe(400, 200, 400, 470, 6, false, true);     /* tahem dolu na uchyt */

    /* Druha pulka tehoz pravidla: obycejne tuknuti stranku prepnout
     * MUSI. Kdyby byla pojistka prisna az moc (was_drag() vzdy true),
     * propadlo by i tuknuti a kontrola vyse by porad hlasila PASS. */
    tap(TAP_HANDLE_X, TAP_HANDLE_Y, true);
    before = page_hash();
    tap(NAVBTN_X(2), NAVBTN_Y, true);
    check("tuknuti na tlacitko navigace stranku prepne", before != page_hash());

    swipe(400, 200, 400, 470, 6, false, true);     /* uklid: zpatky na uchyt */
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
    tap(710, 300, false);
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
    tap(BL_LVL_X(1), BL_LVL_Y, false);          /* 75 % */
    check("tlacitko urovne posle jas na desku", fake_bl_percent == 75);

    /* Nocni rezim ma VLASTNI ulozenou uroven: prepnuti musi poslat ji,
     * ne tu denni. Mutacni zkouska: v bl_level() vracet vzdy
     * cfg.bright_day. */
    tap(BL_MODE_X, BL_MODE_Y, false);           /* DEN -> NOC */
    check("prepnuti na noc posle nocni uroven", fake_bl_percent == 35);
    shot("jas-noc");

    tap(BL_LVL_X(4), BL_LVL_Y, false);          /* nocni uroven na 25 % */
    check("uroven se uklada do prave zvoleneho rezimu", fake_bl_percent == 25);

    /* Navrat na den musi vratit denni uroven nedotcenou tim, co se
     * stalo v noci - to je cely smysl dvou ulozenych urovni. */
    tap(BL_MODE_X, BL_MODE_Y, false);           /* NOC -> DEN */
    check("navrat na den vrati denni uroven", fake_bl_percent == 75);

    swipe(120, 240, 700, 250, 8, false, false);     /* zavrit dialog */
    run_ms(200, false);
    shot("nastaveni-jas-75");

    /* -----------------------------------------------------------------
     * Zhasnuti po necinnosti se musi nejdriv stahnout jasem a teprve pak
     * vypnout panel. Tvrdy skok z plneho jasu do tmy je v noci rana do
     * oka. Probuzeni naopak zadny prejezd mit nesmi.
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

    tap(200, 400, false);                       /* dotek kamkoliv po strance */
    check("probuzeni vrati jas okamzite", fake_bl_percent == 75);

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

    int n = ui_page_count();
    for (int i = 0; i < n; i++) {
        ui_show_page(i);
        run_ms(WARM_STEPS * STEP_MS, false);

        char name[64];
        safe_name(name, sizeof(name), ui_page_name(i));
        shot(name);
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
