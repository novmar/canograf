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

static const ui_host_t FAKE_HOST = {
    NULL,                   /* backlight */
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
    NULL, NULL, NULL, NULL, /* pokus s PWM podsvicenim */
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

/* Otisk obsahove casti obrazovky (pod horni listou, nad prekryvem
 * navigace). Kdyz se nezmeni, nezmenila se ani stranka. */
static unsigned content_hash(void)
{
    unsigned h = 2166136261u;
    for (int y = 64; y < 64 + 228; y++)
        for (int x = 0; x < 736; x++) {
            h ^= (unsigned)lv_color_to32(frame[y * SCR_W + x]);
            h *= 16777619u;
        }
    return h;
}

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

static void phase_navigace(void)
{
    ui_show_page(0);
    run_ms(400, true);

    tap(TAP_HANDLE_X, TAP_HANDLE_Y, true);
    shot("nav-lista");

    /* tah nahoru zacaty NA tlacitku stranky - nesmi prepnout stranku */
    unsigned before = content_hash();
    swipe(NAVBTN_X(2), NAVBTN_Y, NAVBTN_X(2), 180, 6, false, true);
    shot("nav-mrizka");

    /* tazeni na pulce cesty, prst jeste dole */
    swipe(400, 200, 400, 340, 5, true, true);
    shot("nav-tazeni");
    touch_release(true);

    /* tahem dolu zpatky na uchyt */
    swipe(400, 200, 400, 470, 6, false, true);
    unsigned after = content_hash();
    check("tah z tlacitka navigace neprepne stranku", before == after);

    /* obycejne tuknuti na tlacitko stranku prepnout musi */
    tap(TAP_HANDLE_X, TAP_HANDLE_Y, true);
    before = content_hash();
    tap(NAVBTN_X(2), NAVBTN_Y, true);
    check("tuknuti na tlacitko navigace stranku prepne", before != content_hash());

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

    /* ---- zavreni dialogu driv, nez dobehne sken ---- */
    tap(520, PAGE_Y(168), false);           /* VYBRAT SIT */
    shot("wifi-hledam");
    /* zavrit driv, nez sken dobehne - presne ta cesta, po ktere to padalo */
    swipe(120, 240, 700, 250, 4, false, false);
    run_ms(2000, false);                    /* sken dobehne do prazdna */
    check("zavreni dialogu behem skenu nic nerozbije", page_before == content_hash());

    /* ---- seznam siti ---- */
    tap(520, PAGE_Y(168), false);
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
    tap(520, PAGE_Y(168), false);
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

    ui_set_host(&FAKE_HOST);
    ui_set_cfg(false, false, 0, lang);
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

    free(rgb);
    if (checks_failed) printf("%d kontrol selhalo\n", checks_failed);
    return (shot_err || checks_failed) ? 1 : 0;
}
