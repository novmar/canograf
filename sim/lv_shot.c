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

static char fake_saved[6][33] = { "dilna", "domov", "UPC1234567" };
static int  fake_saved_n = 3;
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
    if (fake_find(ssid) < 0 && fake_saved_n < (int)(sizeof(fake_saved) / 33))
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

static int fake_saved_count(void) { return fake_saved_n; }

static const char *fake_saved_ssid(int i)
{
    return (i >= 0 && i < fake_saved_n) ? fake_saved[i] : NULL;
}

static void fake_forget_all(void) { fake_saved_n = 0; }

/* Citace sbernice. Zadny CAN na hostiteli neni, takze jsou to vymyslena
 * cisla - jde o to, ze dialog ukazuje jejich SKUTECNY obsah a ze ho
 * potvrzeny reset opravdu vynuluje. */
static can_stats_t fake_can = {
    .rx = 1842, .tx = 57, .tx_fail = 2, .rx_lost = 0,
    .ring_lost = 0, .bus_err = 3, .arb_lost = 1, .bus_off = 0,
};
static const can_stats_t *fake_can_stats(void) { return &fake_can; }
static void fake_can_reset(void) { memset(&fake_can, 0, sizeof(fake_can)); }

/* Restart desky se na hostiteli jen spocita. */
static int fake_reboots = 0;
static void fake_reboot(void) { fake_reboots++; }

static bool fake_connected(void) { return fake_conn[0] != '\0'; }
static const char *fake_ssid(void) { return fake_conn; }
static const char *fake_ip(void) { return fake_conn[0] ? "192.168.1.42" : ""; }

/* Sila signalu. Nepripojena deska nema co hlasit, takze 0 - stejne jako
 * na desce, viz host_wifi_rssi() v main.cpp. Pristupovy bod predstirana
 * deska nezaklada. */
static int fake_rssi(void) { return fake_conn[0] ? -58 : 0; }
static bool fake_ap(void) { return false; }

/* Rezim sbernice. Predstirana deska se chova jako radic: po startu je v
 * listen-only a do normalniho rezimu se dostane jen tim, ze o to nekdo
 * rekne. Kontroly se na tuhle promennou divaji, takze "v listen-only se
 * nic neposila" je merene, ne slibene. */
static bool fake_can_normal = false;
static bool fake_can_is_normal(void) { return fake_can_normal; }
static bool fake_can_set_normal(bool normal)
{
    fake_can_normal = normal;
    return true;
}

/* Hlavni vypinac podsviceni (EXIO2 na desce). Predstirana deska si jen
 * pamatuje, jestli sviti - jedine tak jde zmerit, ze ztlumeni po
 * necinnosti displej NEZHASINA. */
static bool fake_bl_on = true;
static void fake_backlight(bool on) { fake_bl_on = on; }

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
    .backlight          = fake_backlight,
    .bl_set_percent     = fake_bl_set,
    .wifi_scan_start    = fake_scan_start,
    .wifi_scan_result   = fake_scan_result,
    .wifi_scan_ssid     = fake_scan_ssid,
    .wifi_connect       = fake_connect,
    .wifi_connected     = fake_connected,
    .wifi_ssid          = fake_ssid,
    .wifi_ip            = fake_ip,
    .wifi_rssi          = fake_rssi,
    .wifi_ap            = fake_ap,
    .wifi_known         = fake_known,
    .wifi_connect_known = fake_connect_known,
    .wifi_forget        = fake_forget,
    .wifi_saved_count   = fake_saved_count,
    .wifi_saved_ssid    = fake_saved_ssid,
    .wifi_forget_all    = fake_forget_all,
    .can_stats          = fake_can_stats,
    .can_stats_reset    = fake_can_reset,
    .can_is_normal      = fake_can_is_normal,
    .can_set_normal     = fake_can_set_normal,
    .reboot             = fake_reboot,
    .heap_kb            = fake_heap_kb,
    .psram_kb           = fake_psram_kb,
};

/* Tataz deska, ale bez dratku z GPIO6 na plosku BL: stmivat neumi.
 * Nastaveni na ni nesmi nabidnout ani jedno tlacitko jasu. */
static const ui_host_t FAKE_HOST_NODIM = {
    .backlight          = fake_backlight,
    .bl_set_percent     = NULL,     /* deska stmivat neumi */
    .wifi_scan_start    = fake_scan_start,
    .wifi_scan_result   = fake_scan_result,
    .wifi_scan_ssid     = fake_scan_ssid,
    .wifi_connect       = fake_connect,
    .wifi_connected     = fake_connected,
    .wifi_ssid          = fake_ssid,
    .wifi_ip            = fake_ip,
    .wifi_rssi          = fake_rssi,
    .wifi_ap            = fake_ap,
    .wifi_known         = fake_known,
    .wifi_connect_known = fake_connect_known,
    .wifi_forget        = fake_forget,
    .wifi_saved_count   = fake_saved_count,
    .wifi_saved_ssid    = fake_saved_ssid,
    .wifi_forget_all    = fake_forget_all,
    .can_stats          = fake_can_stats,
    .can_stats_reset    = fake_can_reset,
    .can_is_normal      = fake_can_is_normal,
    .can_set_normal     = fake_can_set_normal,
    .reboot             = fake_reboot,
    .heap_kb            = fake_heap_kb,
    .psram_kb           = fake_psram_kb,
};

/* ---------------------------------------------------------------------
 * Predstirana jednotka
 *
 * Aby slo zkouset, CO UI s hodnotami udela, musi na druhem konci neco
 * odpovidat. Neni to simulace vozu - je to jednotka, ktera na znamy
 * dotaz vrati pevnou hodnotu, jeden DID nezna vubec a jeden v odpovedi
 * vynecha. Prave na techto trech koncich se pozna, jestli UI rozlisuje
 * "zmereno", "odmitnuto" a "neprecteno".
 *
 * ODESILANI jde pres isotp_link_t: odpoved na tri DIDy ani rozsirene
 * zaznamy se do jednoho ramce nevejdou. PRIJEM pres nej NEJDE - isotp.h
 * je klient a zacatek zpravy bere jen ve stavu ISOTP_WAITING, tedy kdyz
 * sam na odpoved ceka. Pozadavky se proto rozebiraji rucne; vsechny se
 * vejdou do single framu, takze je to par radku. Spoj presto dostava
 * kazdy ramec od desky, protoze potrebuje videt flow control, kterym si
 * tester rika o dalsi consecutive frames.
 * ------------------------------------------------------------------- */
#define ECU_REQ_ID 0x7E0u       /* deska -> jednotka */
#define ECU_RSP_ID 0x7E8u       /* jednotka -> deska */

/* Vybrane chyby. Kodovani je trojice bajtu, jak ji vraci 19 02; prevod
 * na "P2263-22" dela diag_dtc_text() v poller.c. */
#define ECU_DTC1 0x226322u      /* P2263-22 */
#define ECU_DTC2 0x040164u      /* P0401-64 */

static diag_poller_t g_dp;
static isotp_link_t  ecu_link;

static bool ecu_present   = true;   /* odpovida jednotka vubec? */
static int  bus_tx_frames = 0;      /* kolik ramcu se deska pokusila poslat */
static int  ecu_requests  = 0;      /* kolik pozadavku jednotka slysela */

/* Co jednotka umi. Jsou to SYROVA cisla pred skalovanim z
 * src/diag/poller.c, aby byl na obrazovce videt i prepocet.
 *
 * 0x2527 (VNT) tu schvalne NENI: na davku, ve ktere je, jednotka
 * odpovi zaporne a UI musi takovy kanal nakreslit jinak nez ten, ktery
 * jeste nikdo necetl.
 * 0x240D (nasavany vzduch) tu je, ale ecu_skip ho z odpovedi vynecha -
 * to je ten druhy konec, "odpoved prisla, tenhle udaj v ni nebyl". */
typedef struct {
    uint16_t did;
    uint8_t  len;
    uint32_t raw;
} ecu_did_t;

static const ecu_did_t ECU_DID[] = {
    { 0x2002u, 2u,  7200u },   /* otacky   x0,25  = 1800 1/min */
    { 0x2003u, 2u,  8600u },   /* rychlost x0,01  = 86 km/h */
    { 0x2401u, 2u,  1840u },   /* plnici tlak     = 1840 mbar */
    { 0x2402u, 2u,  1900u },   /* zadany tlak     = 1900 mbar */
    { 0x2801u, 2u,  1150u },   /* rail            = 1150 bar */
    { 0x2802u, 2u,  1180u },   /* rail zadano     = 1180 bar */
    { 0x2412u, 2u,  4120u },   /* MAF      x0,1   = 412 mg */
    { 0x2413u, 2u,  4300u },   /* MAF zadano      = 430 mg */
    { 0x2407u, 2u,   512u },   /* EGR      x0,0488 = 25 % */
    { 0x240Du, 2u,  3010u },   /* nasavany vzduch x0,1 - 273 = 28 C */
    { 0x2001u, 2u,  3611u },   /* chladici kapalina = 88,1 C */
    { 0x2442u, 2u,  5730u },   /* pred filtrem    = 300 C */
    { 0x2441u, 2u,  5460u },   /* za filtrem      = 273 C */
    { 0x2432u, 2u,  8230u },   /* cil regenerace  = 550 C */
    { 0x2487u, 2u,     0u },   /* regenerace nebezi */
    { 0x242Cu, 2u,   840u },   /* saze     x0,01  = 8,4 g */
    { 0x24BAu, 2u,   112u },   /* zatizeni x0,1   = 11,2 g */
    { 0x2557u, 3u, 12400u },   /* od regenerace x0,1 = 1240 s */
    { 0x2006u, 3u, 214763u },  /* tachometr       = 214763 km */
};
#define ECU_DIDS ((int)(sizeof(ECU_DID) / sizeof(ECU_DID[0])))

/* DID, ktery jednotka v odpovedi vynecha, i kdyz ho zna. */
#define ECU_SKIP_DID 0x240Du

static const ecu_did_t *ecu_find(uint16_t did)
{
    for (int i = 0; i < ECU_DIDS; i++)
        if (ECU_DID[i].did == did) return &ECU_DID[i];
    return NULL;
}

static void ecu_put(uint8_t *b, int *at, uint32_t v, uint8_t len)
{
    for (int i = len - 1; i >= 0; i--) b[(*at)++] = (uint8_t)(v >> (8 * i));
}

/* Slozi odpoved na jeden pozadavek. Vraci jeji delku, 0 = mlcet. */
static int ecu_reply(const uint8_t *q, int qn, uint8_t *r)
{
    int at = 0;

    if (qn >= 2 && q[0] == 0x10u) {              /* zahajeni session */
        r[0] = 0x50u; r[1] = q[1];
        r[2] = 0x00u; r[3] = 0x32u; r[4] = 0x01u; r[5] = 0xF4u;
        return 6;
    }
    if (qn >= 2 && q[0] == 0x3Eu) {              /* tester present */
        r[0] = 0x7Eu; r[1] = q[1];
        return 2;
    }
    if (qn >= 3 && q[0] == 0x22u) {              /* cteni DIDu */
        r[at++] = 0x62u;
        for (int p = 1; p + 1 < qn; p += 2) {
            uint16_t did = (uint16_t)((q[p] << 8) | q[p + 1]);
            const ecu_did_t *d = ecu_find(did);

            /* Neznamy DID odmita CELOU davku - tak se chova skutecna
             * jednotka, odpoved na 22 nema misto na chybu u jednoho
             * udaje. */
            if (!d) {
                r[0] = 0x7Fu; r[1] = 0x22u; r[2] = 0x31u;
                return 3;
            }
            if (did == ECU_SKIP_DID) continue;
            ecu_put(r, &at, did, 2u);
            ecu_put(r, &at, d->raw, d->len);
        }
        return at;
    }
    if (qn >= 3 && q[0] == 0x19u && q[1] == 0x02u) {   /* pamet chyb */
        r[at++] = 0x59u; r[at++] = 0x02u; r[at++] = 0xFFu;
        ecu_put(r, &at, ECU_DTC1, 3u); r[at++] = 0x09u;
        ecu_put(r, &at, ECU_DTC2, 3u); r[at++] = 0x08u;
        return at;
    }
    if (qn >= 6 && q[0] == 0x19u && q[1] == 0x06u) {   /* rozsirene zaznamy */
        r[at++] = 0x59u; r[at++] = 0x06u;
        r[at++] = q[2]; r[at++] = q[3]; r[at++] = q[4];
        r[at++] = 0x09u;
        r[at++] = 0x80u; ecu_put(r, &at, 214310u, 3u);  /* tachometr pri vyskytu */
        r[at++] = 0x81u; r[at++] = 0x28u;               /* starnuti */
        r[at++] = 0x82u; r[at++] = 0x03u;               /* pocet vyskytu */
        r[at++] = 0x87u; ecu_put(r, &at, 0xFFFFFFu, 3u);/* same FF = nema */
        return at;
    }
    if (qn >= 6 && q[0] == 0x19u && q[1] == 0x04u) {   /* freeze frame */
        r[at++] = 0x59u; r[at++] = 0x04u;
        r[at++] = q[2]; r[at++] = q[3]; r[at++] = q[4];
        r[at++] = 0x09u;
        r[at++] = 0x01u; r[at++] = 0x03u;               /* zaznam 1, tri DIDy */
        ecu_put(r, &at, 0x2002u, 2u); ecu_put(r, &at, 5600u, 2u);
        ecu_put(r, &at, 0x2003u, 2u); ecu_put(r, &at, 4200u, 2u);
        ecu_put(r, &at, 0x2001u, 2u); ecu_put(r, &at, 3540u, 2u);
        return at;
    }
    return 0;
}

/* ---------------------------------------------------------------------
 * Sbernice
 *
 * Ramce se NEDORUCUJI hned. Na desce jdou pres kruhovy zasobnik a
 * vyzvedne je az dalsi obratka smycky; kdyby se tady predavaly rovnou,
 * dorazila by odpoved jednotky jeste uvnitr isotp_send() testeru - tedy
 * ve stavu ISOTP_SENDING, kdy spoj zacatek zpravy nebere - a zahodila
 * by se. Fronta tenhle rozdil drzi a chova se jako deska.
 * ------------------------------------------------------------------- */
#define BUSQ_MAX 64

typedef struct {
    uint32_t id;
    uint8_t  d[8];
    uint8_t  dlc;
    bool     tx;        /* poslala deska */
} busf_t;

static busf_t busq[BUSQ_MAX];
static int    busq_n;

static void busq_put(uint32_t id, const uint8_t *d, uint8_t dlc, bool tx)
{
    if (busq_n >= BUSQ_MAX) return;
    busq[busq_n].id  = id;
    busq[busq_n].dlc = dlc;
    busq[busq_n].tx  = tx;
    memcpy(busq[busq_n].d, d, dlc > 8u ? 8u : dlc);
    busq_n++;
}

/* Ramec od jednotky k desce. */
static bool ecu_send(void *user, uint32_t id, const uint8_t *data, uint8_t dlc)
{
    (void)user;
    busq_put(id, data, dlc, false);
    return true;
}

/* Ramec od desky. Tohle je jedine misto, kde se pozna, jestli se deska
 * vubec o neco pokusila - a v listen-only se pokusit NESMI. */
static bool bus_send(void *user, uint32_t id, const uint8_t *data, uint8_t dlc)
{
    (void)user;
    bus_tx_frames++;
    if (!fake_can_normal) {
        fake_can.tx_fail++;     /* stejne jako can_send() v listen-only */
        return false;
    }
    fake_can.tx++;
    busq_put(id, data, dlc, true);
    return true;
}

/* Jeden prijaty ramec na strane jednotky. */
static void ecu_rx(const busf_t *f, uint32_t now)
{
    uint8_t rsp[64];
    int qn, rn;

    /* Flow control patri odesilacimu spoji jednotky. */
    isotp_on_frame(&ecu_link, f->id, f->d, f->dlc, now);

    /* Pozadavek je vzdycky single frame - horni pulbajt 0, delka v
     * dolnim. Vic nez sedm bajtu se tester nepta a viceramcovy prijem
     * by znamenal druhy spoj. */
    if (f->id != ECU_REQ_ID || f->dlc < 1u || (f->d[0] >> 4) != 0u) return;
    qn = f->d[0] & 0x0Fu;
    if (qn < 1 || qn > 7 || qn + 1 > (int)f->dlc) return;

    ecu_requests++;
    rn = ecu_reply(&f->d[1], qn, rsp);
    if (rn > 0) isotp_send(&ecu_link, rsp, (uint16_t)rn, now);
}

/* Jak casto vozidlo samo od sebe neco posle. Na X82 jezdi po HS CAN
 * desitky ramcu za zlomek vteriny; ctyri za vterinu uz staci, aby
 * diag_link uznal sbernici za zivou (DIAG_BUS_MIN_FRAMES). */
#define ECU_CHAT_MS 100u
static uint32_t ecu_chat_at = 0;

/* Jeden krok sbernice: doruci se, co se minule poslalo, jednotka dosila
 * rozdelanou odpoved a deska dostane cas. Vola se ze stejneho mista
 * jako ui_tick(), takze se pri anim_only nehybe ani sbernice a dva
 * snimky jdou porovnat. */
static void bus_step(void)
{
    uint32_t now = lv_tick_get();
    busf_t   q[BUSQ_MAX];
    int      n = busq_n;

    memcpy(q, busq, sizeof(busf_t) * (size_t)n);
    busq_n = 0;

    for (int i = 0; i < n; i++) {
        diag_poller_on_frame(&g_dp, q[i].id, q[i].d, q[i].dlc, q[i].tx, now);
        if (q[i].tx && ecu_present) ecu_rx(&q[i], now);
    }

    /* Vozidlo mluvi samo od sebe. Prave z toho pozna diag_link, ze deska
     * opravdu visi v aute - v listen-only je to jedina dostupna
     * informace, protoze zeptat se nesmi. */
    if (ecu_present && (now - ecu_chat_at) >= ECU_CHAT_MS) {
        static const uint8_t chat[8] = { 0 };
        ecu_chat_at = now;
        busq_put(0x0C6u, chat, 8u, false);
        fake_can.rx++;
    }

    isotp_poll(&ecu_link, now);
    diag_poller_arm(&g_dp, fake_can_normal);
    diag_poller_poll(&g_dp, now);
}

static void bus_init(void)
{
    isotp_cfg_t ic;
    diag_poller_cfg_t dc;

    isotp_cfg_default(&ic);
    ic.tx_id = ECU_RSP_ID;
    ic.rx_id = ECU_REQ_ID;
    ic.send  = ecu_send;
    isotp_init(&ecu_link, &ic);

    diag_poller_cfg_default(&dc);
    dc.send = bus_send;
    diag_poller_init(&g_dp, &dc);
    ui_set_diag(&g_dp);
}

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

/* Beh virtualniho casu. anim_only necha stranou ui_tick() i sbernici,
 * takze se namerene hodnoty nehybou a dva snimky jdou porovnat. */
static void run_ms(int ms, bool anim_only)
{
    for (int t = 0; t < ms; t += STEP_MS) {
        lv_tick_inc(STEP_MS);
        if (!anim_only) { bus_step(); ui_tick(); }
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
        bus_step();
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

/* Je v obdelniku aspon jeden pixel presne teto barvy? Na rozdil od
 * otisku to je ABSOLUTNI tvrzeni, nezavisle na tom, jak vypadal tentyz
 * obdelnik predtim: barva merene hodnoty tam, kde zadna hodnota neni,
 * znamena, ze si UI cislo vymyslelo. */
static bool region_has(int x0, int y0, int x1, int y1, uint32_t color)
{
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);

    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (lv_color_to32(frame[y * SCR_W + x]) == color) return true;
    return false;
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

/* Stranka nastaveni: tri rady tlacitek 160 x 68 px po 176 px
 * (SET_* v ui.c). Pocita se stred tlacitka, at se tref nezavisi na tom,
 * jak je siroke. */
#define SET_CX(i)      (20 + (i) * 176 + 80)
#define SET_ROW1_Y     PAGE_Y(28 + 34)
#define SET_ROW2_Y     PAGE_Y(120 + 34)
#define SET_ROW3_Y     PAGE_Y(194 + 34)
#define SET_UNITS_X    SET_CX(0)
#define SET_DIM_X      SET_CX(2)        /* ZTLUMIT PO - otevira dialog */
#define SET_WIFI_X     SET_CX(0)
#define SET_CAN_X      SET_CX(1)        /* rezim sbernice */
#define SET_PROFILE_X  SET_CX(2)
#define SET_BRIGHT_X   SET_CX(3)
#define SET_RESET_X    (546 + 85)       /* UDRZBA, sirka 170 */
/* dialog ztlumeni: rada dob nahore, rada urovni dole (DIM_* v ui.c) */
#define DIM_T_X(i)     (30 + (i) * 170 + 80)
#define DIM_T_Y        (120 + 60)
#define DIM_L_X(i)     (30 + (i) * 130 + 60)
#define DIM_L_Y        (300 + 60)
/* dialog jasu lezi na cele obrazovce, takze PAGE_Y se na nej nevztahuje:
 * radek na profil (vyska 120, mezera 6) a pet urovni v nem */
#define BL_ROW_Y(m)    (96 + (m) * 126 + 60)
#define BL_LVL_X(i)    (212 + (i) * 112 + 52)
#define BL_PROF_X      (20 + 90)
/* potvrzeni drzenim: tlacitko DRZ 3 s je 360 x 120 na (30, 330) */
#define HOLD_X         210
#define HOLD_Y         390
/* nabidka resetu: ctyri tlacitka 360 x 120 ve dvou sloupcich */
#define RST_X(c)       (30 + (c) * 380 + 180)
#define RST_Y(r)       (96 + (r) * 136 + 60)
/* prepinac profilu v horni liste */
#define PROF_BAR_X     (800 - 164 - 8 + 80)
#define PROF_BAR_Y     32
/* horni lista: stav pripojeni a jmeno site. Stari udaje (radek pod
 * stavem) se do otisku zamerne nebere - meni se samo od sebe. */
#define STATE_BOX   470, 4, 620, 30
#define SSID_BOX    50, 18, 258, 44
/* stranka motoru: tri hodnoty, na kterych se pozna ctveri osud udaje */
#define BOOSTSP_BOX 290, PAGE_Y(26), 430, PAGE_Y(76)
#define VNT_BOX     290, PAGE_Y(102), 430, PAGE_Y(152)
#define IAT_BOX     500, PAGE_Y(178), 640, PAGE_Y(228)
#define EGR_BOX     500, PAGE_Y(102), 640, PAGE_Y(152)
/* Barva, kterou se kresli ZMERENA skutecnost (C_ACTUAL v ui.c).
 * Neprectena hodnota ma barvu popisku, takze v jejim miste se tahle
 * barva nesmi objevit ani jednim pixelem. */
#define C_ACTUAL32  lv_color_to32(lv_color_hex(0xF0DFB8))
/* prvni radek chybove pameti */
#define DTC_ROW0_X  195
#define DTC_ROW0_Y  PAGE_Y(44 + 24)
/* pravy sloupec DPF: je 64 px siroky a saha po cele vysce stranky */
#define RAIL_X      768
#define RAIL_Y      240

/* Prepnuti radice pres UI, vcetne potvrzeni drzenim. Tudy to dela
 * uzivatel a jinudy to nejde - proto i zkouska. */
static void can_go_normal(void)
{
    ui_show_page(5);
    run_ms(200, false);
    tap(SET_CAN_X, SET_ROW2_Y, false);
    touch_at(HOLD_X, HOLD_Y);
    touch_down = true;
    run_ms(3400, false);
    touch_release(false);
    run_ms(900, false);
}

/* Zpet do poslechu, bez potvrzovani - tak to dela i UI. */
static void can_go_listen(void)
{
    ui_show_page(5);
    run_ms(200, false);
    if (fake_can_normal) tap(SET_CAN_X, SET_ROW2_Y, false);
    run_ms(200, false);
}

/* ---------------------------------------------------------------------
 * Co se smi ukazat jako cislo
 *
 * Ctyri osudy jednoho udaje a kazdy musi vypadat jinak:
 *
 *   neprecteny   jednotka o nem nic nerekla  -> pomlcky, ZADNE cislo
 *   odmitnuty    jednotka davku odmitla      -> "7F 31" cervene
 *   zivy         prave precteny              -> cislo v jeho barve
 *   stary        precteny, ale uz davno      -> totez cislo tlumene
 *
 * Predstirana jednotka je nastavena tak, aby na strance motoru byly
 * videt vsechny ctyri najednou: 0x2527 (VNT) nezna, takze davku s nim
 * odmitne, a 0x240D (nasavany vzduch) z odpovedi vynecha.
 *
 * Zaroven se tu zkousi cesta k datum: na stole neni sbernice, v
 * listen-only sbernice zije ale nic se neposila, a teprve v normalnim
 * rezimu odpovida jednotka.
 * ------------------------------------------------------------------- */
static void phase_hodnoty(void)
{
    const diag_link_t *dl = diag_poller_link(&g_dp);

    /* ---- deska na stole: nic nejezdi, nic se necetlo ---- */
    ecu_present = false;
    ui_show_page(1);                            /* motor */
    run_ms(1400, false);

    check("bez sbernice hlasi vrstva DOWN", diag_link_state(dl) == DIAG_LINK_DOWN);
    unsigned bar_down = region_hash(STATE_BOX);
    unsigned sp_none  = region_hash(BOOSTSP_BOX);
    unsigned vnt_none = region_hash(VNT_BOX);
    unsigned iat_none = region_hash(IAT_BOX);
    check("dokud se nic necetlo, vypadaji vsechny hodnoty stejne",
          sp_none == vnt_none && vnt_none == iat_none);
    shot("motor-bez-vozu");

    /* -----------------------------------------------------------------
     * 3. V LISTEN-ONLY NEODEJDE ZADNY DOTAZ.
     *
     * Vuz je na sbernici a mluvi, takze je co poslouchat - a prave ted
     * se nesmi nic zeptat. Meri se to na jedinem miste, kterym ramec
     * muze odejit (bus_send), ne na tom, co dorazilo k jednotce.
     *
     * Mutacni zkouska: v diag_poller_poll() v poller.c smazat
     *     if (!p->armed) { diag_link_poll(...); return; }
     * Dotazy zacnou odchazet i v poslechu a kontrola MUSI selhat.
     * ----------------------------------------------------------------- */
    ecu_present   = true;
    bus_tx_frames = 0;
    ecu_requests  = 0;
    run_ms(2000, false);

    check("v listen-only neodejde zadny dotaz",
          bus_tx_frames == 0 && ecu_requests == 0);
    check("sbernice zije, i kdyz se nikdo neptal",
          diag_link_state(dl) == DIAG_LINK_BUS);
    unsigned bar_bus = region_hash(STATE_BOX);
    check("v poslechu porad neni zadna hodnota",
          vnt_none == region_hash(VNT_BOX) && sp_none == region_hash(BOOSTSP_BOX));
    shot("motor-jen-poslech");

    /* ---- normalni rezim: jednotka odpovida ---- */
    can_go_normal();
    ui_show_page(1);
    run_ms(3000, false);
    check("po prepnuti jednotka odpovida", diag_link_state(dl) == DIAG_LINK_ECU);
    check("deska se opravdu zeptala", ecu_requests > 0);

    /* -----------------------------------------------------------------
     * 4. STAV PRIPOJENI V LISTE ODPOVIDA diag_link_state().
     *
     * Tri urovne, tri ruzne obrazky - a pokazde ten, ktery k urovni
     * patri. Otisk sam o sobe nerekne, ktery je ktery, proto se vedle
     * nej porovnava i hodnota z vrstvy; dohromady to tvrdi, ze lista
     * nekresli neco jineho, nez vrstva hlasi.
     *
     * Mutacni zkouska: v link_state_paint() v ui.c vzit misto
     * diag_link_state() natvrdo DIAG_LINK_ECU. Vsechny tri otisky pak
     * budou shodne a kontrola MUSI selhat.
     * ----------------------------------------------------------------- */
    unsigned bar_ecu = region_hash(STATE_BOX);
    check("stav pripojeni v liste odpovida vrstve",
          bar_down != bar_bus && bar_bus != bar_ecu && bar_down != bar_ecu);

    /* -----------------------------------------------------------------
     * 1. NEPRECTENA HODNOTA SE NEZOBRAZI JAKO CISLO.
     *
     * 0x240D jednotka zna, ale v odpovedi ho vynechava. Kolem nej uz
     * hodnoty chodi, takze "porad pomlcky" neni tim, ze by nefungovalo
     * cteni - je to tim, ze UI si cislo nevymysli.
     *
     * Netvrdi se to otiskem, ale PRITOMNOSTI BARVY: zmerena skutecnost
     * se kresli C_ACTUAL, neprectena hodnota barvou popisku. Otisk by
     * tuhle vec neuhlidal - porovnaval by se s obrazkem z tehoz behu,
     * takze by vymyslene cislo proslo, kdyby vypadalo porad stejne.
     *
     * Mutacni zkouska: v val_fmt() v ui.c nechat vetev default spadnout
     * do formatovani cisla misto VAL_NONE a nechat puvodni barvu.
     * ----------------------------------------------------------------- */
    check("hodnota, kterou jednotka nevydala, se nekresli jako cislo",
          !region_has(IAT_BOX, C_ACTUAL32));
    check("vedle ni se ziva hodnota jako cislo kresli",
          region_has(EGR_BOX, C_ACTUAL32));
    check("neprecteny udaj vypada presne jako pred dotazem",
          iat_none == region_hash(IAT_BOX));

    /* -----------------------------------------------------------------
     * 2. ODMITNUTY DID VYPADA JINAK NEZ CHYBEJICI.
     *
     * "Tahle verze softwaru ten udaj nema" je jina informace nez
     * "jeste jsem se nezeptal" a nesmi se kreslit stejne.
     *
     * Mutacni zkouska: v val_fmt() v ui.c smazat vetev
     * DIAG_SHOW_REFUSED, aby propadla do default. Odmitnuty udaj pak
     * vypada jako neprecteny a kontrola MUSI selhat.
     * ----------------------------------------------------------------- */
    check("odmitnuty udaj vypada jinak nez chybejici",
          region_hash(VNT_BOX) != region_hash(IAT_BOX));
    check("odmitnuty udaj vypada jinak nez pred dotazem",
          region_hash(VNT_BOX) != vnt_none);
    shot("motor-ziva-data");

    /* -----------------------------------------------------------------
     * Ctvrty osud: jednotka zmlkne. Cislo zustane - posledni zmerena
     * hodnota je porad informace - ale musi byt poznat, ze uz to neni
     * ted. Bez toho by stojici cislo vypadalo jako zive.
     *
     * Mutacni zkouska: v val_fmt() v ui.c smazat radek, ktery pri
     * DIAG_SHOW_STALE prebarvuje text na C_LABEL.
     * ----------------------------------------------------------------- */
    unsigned sp_live = region_hash(BOOSTSP_BOX);
    ecu_present = false;
    run_ms(2500, false);
    check("po utichnuti jednotky je cislo videt jinak nez zive",
          region_hash(BOOSTSP_BOX) != sp_live);
    check("stara hodnota ale nezmizi", region_hash(BOOSTSP_BOX) != sp_none);
    shot("motor-stara-data");

    ecu_present = true;
    run_ms(2000, false);

    /* Chybova pamet: vybrany zaznam docte 19 06 a 19 04, oboje pres
     * vic ramcu, takze tohle zaroven projde cely ISO-TP tam i zpet. */
    ui_show_page(3);
    run_ms(1200, false);
    tap(DTC_ROW0_X, DTC_ROW0_Y, false);
    run_ms(1600, false);
    check("vybrana chyba ma docteny detail",
          diag_poller_rec_known(&g_dp) && diag_poller_ff_known(&g_dp) &&
          diag_poller_rec_count(&g_dp) > 0u && diag_poller_ff_count(&g_dp) > 0u);
    shot("chyby-detail");

    ui_show_page(0);
    run_ms(600, false);
}

/* ---------------------------------------------------------------------
 * Dotek na pravem sloupci DPF
 *
 * Sloupec lezi u praveho okraje, tedy presne tam, kde zacina tah na
 * dalsi stranku. Ze stejneho zacatku vedou tri ruzne konce a prave
 * jejich odliseni se tu zkousi.
 * ------------------------------------------------------------------- */
static void phase_sloupec(void)
{
    ui_show_page(4);                            /* DPF */
    run_ms(600, true);
    unsigned dpf = page_hash_body();
    ui_show_page(1);                            /* motor */
    run_ms(600, true);
    unsigned eng = page_hash_body();
    ui_show_page(0);                            /* hlavni */
    run_ms(600, true);
    unsigned home = page_hash_body();

    /* -----------------------------------------------------------------
     * 8. TUKNUTI BEZ POHYBU NEUDELA NIC.
     *
     * Sloupec je ukazatel, ne tlacitko. Tuknuti na nej jen bliknutim
     * potvrdi, ze dotek dosel, a tim to konci.
     *
     * Mutacni zkouska: v rail_click_cb() v ui.c pridat
     * ui_show_page(PG_DPF). Obe kontroly pak MUSI selhat.
     * ----------------------------------------------------------------- */
    tap(RAIL_X, RAIL_Y, true);
    check("tuknuti na sloupec DPF neprepne stranku", home == page_hash_body());
    check("tuknuti na sloupec DPF neotevre DPF", dpf != page_hash_body());

    /* -----------------------------------------------------------------
     * 9. PODRZENI OTEVRE DPF, POHYB ODPOCET ZRUSI.
     *
     * Druha cast je ta podstatna: bez ni by zavahani uprostred tahu
     * skocilo na DPF a tah uz by nikam nevedl. Tah proto trva DELE nez
     * RAIL_HOLD_MS - kdyby odpocet pohyb nerusil, stihl by dobehnout.
     *
     * Mutacni zkouska: v rail_pressing_cb() v ui.c vyhodit vetev
     * s was_drag(). Prvni kontrola projde dal, druha MUSI selhat.
     * ----------------------------------------------------------------- */
    touch_at(RAIL_X, RAIL_Y);
    touch_down = true;
    run_ms(1400, true);                         /* pres RAIL_HOLD_MS */
    touch_release(true);
    check("podrzeni na sloupci otevre DPF", dpf == page_hash_body());

    ui_show_page(0);
    run_ms(600, true);
    swipe(RAIL_X, RAIL_Y, 420, RAIL_Y + 10, 30, false, true);   /* 1,3 s tahu */
    check("pohyb odpocet zrusi a z tahu je prepnuti stranky",
          eng == page_hash_body() && dpf != page_hash_body());

    ui_show_page(0);
    run_ms(400, true);
}

/* ---------------------------------------------------------------------
 * Jmeno site ve stavovem radku
 *
 * 6. Pripojena sit se musi jmenovat, nepripojena se musi priznat.
 * Prazdne misto by znamenalo "nevim" a to je jine nez "nikde".
 * ------------------------------------------------------------------- */
static void phase_stav_wifi(void)
{
    ui_show_page(5);
    run_ms(400, false);
    unsigned none = region_hash(SSID_BOX);
    check("bez site neni stavovy radek prazdny",
          !region_uniform(SSID_BOX));

    tap(SET_WIFI_X, SET_ROW2_Y, false);         /* VYBRAT SIT */
    run_ms(1600, false);                        /* sken dobehne */
    tap(300, 126, false);                       /* prvni radek je ulozena sit */
    run_ms(600, false);

    /* Mutacni zkouska: ve wifi_status_paint() v ui.c psat do lbl_ssid
     * vzdy T(STR_NONE). Druha kontrola pak MUSI selhat. */
    check("deska je pripojena", fake_connected());
    check("pripojena sit stoji ve stavovem radku", none != region_hash(SSID_BOX));
    shot("lista-wifi");

    fake_conn[0] = '\0';                        /* sit spadla */
    run_ms(600, false);
    check("bez pripojeni se v liste vrati 'none'", none == region_hash(SSID_BOX));
}


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
     * Dlazdice DEBUG otevira diagnostiku. Je druha zprava v DOLNI rade
     * (NAV_TILE_DEBUG v ui.c), takze radek 1, sloupec 4.
     * Mutacni zkouska: v ui.c dat NAV_TILE_DEBUG mimo mrizku (treba 99),
     * na tom miste pak neni tlacitko a nic se neotevre.
     * ----------------------------------------------------------------- */
    unsigned grid = page_hash();
    tap(NAVTILE_X(4), NAVTILE_Y(1), false);
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

    /* -----------------------------------------------------------------
     * 5. VYBER STRANKY MENU ZASUNE.
     *
     * Kdo si rekl o stranku, chce ji videt, ne se na ni divat pres
     * vysunutou listu. Meri se to na objektech LVGL, ne otiskem: po
     * zasunuti nesmi v pruhu, kde lista stala, zustat ani jedno
     * viditelne tlacitko stranky.
     *
     * Mutacni zkouska: v nav_cb() v ui.c vyhodit set_nav_state(0).
     * Lista zustane venku a kontrola MUSI selhat.
     * ----------------------------------------------------------------- */
    run_ms(400, true);                  /* zapadnuti dojede */
    int left = 0;
    lv_coord_t lw = 9999, lh = 9999;
    scan_btns(lv_scr_act(), 480 - NAV_BAR_H, 480, &left, &lw, &lh);
    check("vyber stranky z listy menu zasune", left == 0);

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

/* ---------------------------------------------------------------------
 * Potvrzeni drzenim
 *
 * Jedina vec, kterou tenhle obraz umi poslat ven, je prepnuti radice do
 * normalniho rezimu - tedy "zacni na cizi sbernici vysilat". Je proto
 * za potvrzenim drzenim a prave na nem se dialog zkousi. Drive tu bylo
 * spusteni regenerace; to je zapis do jednotky a ze stranky DPF zmizelo
 * (viz docs/safety.md), takze uz neni na cem potvrzovani ukazat jinde.
 * ------------------------------------------------------------------- */
static void phase_dialogy(void)
{
    can_go_listen();            /* dialog se nabidne jen smerem nahoru */
    unsigned page_before = content_hash();

    tap(SET_CAN_X, SET_ROW2_Y, false);      /* JEN POSLECH -> potvrzeni */
    shot("potvrzeni");

    /* Gesto zprava musi dialog zavrit, NEsmi pod nim prepnout stranku a
     * uz vubec nesmi rezim zmenit.
     * Mutacni zkouska: v modal_open() v ui.c vyhodit
     *     lv_obj_clear_flag(modal, LV_OBJ_FLAG_GESTURE_BUBBLE);
     * prvni kontrola pak selze. */
    swipe(120, 240, 700, 250, 8, false, false);
    check("gesto zprava zavre potvrzovaci dialog", page_before == content_hash());
    check("zavreny dialog rezim sbernice nezmeni", !fake_can_normal);

    /* znovu a tentokrat opravdu podrzet */
    tap(SET_CAN_X, SET_ROW2_Y, false);
    touch_at(HOLD_X, HOLD_Y);               /* tlacitko DRZ 3 s */
    touch_down = true;
    run_ms(1040, false);                    /* tesne po preskoceni na "2" */
    shot("potvrzeni-blik");
    run_ms(460, false);
    shot("potvrzeni-odpocet");
    run_ms(1600, false);                    /* pres tri vteriny */
    shot("potvrzeni-hotovo");
    touch_release(false);
    run_ms(900, false);                     /* akce a zavreni dialogu */
    check("podrzeni prepne radic do normalniho rezimu", fake_can_normal);

    /* Zpatky do poslechu se jde rovnou, bez potvrzeni: ztisit se musi
     * dat okamzite. Mutacni zkouska: v canmode_cb() v ui.c poslat i
     * cestu dolu pres show_confirm(). */
    tap(SET_CAN_X, SET_ROW2_Y, false);
    run_ms(200, false);
    check("tuknuti vrati radic do poslechu", !fake_can_normal);
    shot("can-poslech");
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
    tap(SET_WIFI_X, SET_ROW2_Y, false);     /* VYBRAT SIT */
    shot("wifi-hledam");
    swipe(120, 240, 700, 250, 4, false, false);
    run_ms(2000, false);                    /* sken dobehne do prazdna */
    check("gesto zprava zavre dialog i behem skenu",
          page_before == content_hash());

    /* ---- seznam siti ---- */
    tap(SET_WIFI_X, SET_ROW2_Y, false);
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
    tap(SET_WIFI_X, SET_ROW2_Y, false);
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
    touch_at(HOLD_X, HOLD_Y);
    touch_down = true;
    run_ms(4200, false);                    /* 3 s odpocet a potvrzeni */
    touch_release(false);
    run_ms(1600, false);                    /* novy sken po navratu */
    check("podrzeni ulozenou sit zapomene", fake_saved_n == saved_before - 1);
    shot("wifi-po-zapomenuti");

    swipe(120, 240, 700, 250, 8, false, false);
    run_ms(200, false);
}

/* Doba do ztlumeni se nastavuje v dialogu ZTLUMIT PO, ne stridanim
 * hodnot na tlacitku: otevrit, tuknout na jednu ze ctyr dob, zavrit
 * gestem. DIM_STEP_S v ui.c je { nikdy, 30 s, 2 min, 10 min }.
 * Gesto vede mezi obema radami dialogu, aby se cestou netrefilo do
 * zadneho tlacitka. */
static void set_dim_step(int i)
{
    tap(SET_DIM_X, SET_ROW1_Y, false);
    tap(DIM_T_X(i), DIM_T_Y, false);
    swipe(120, 270, 700, 275, 8, false, false);
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

    tap(SET_BRIGHT_X, SET_ROW2_Y, false);       /* tlacitko JAS */
    shot("jas-dialog");

    /* Tlacitko urovne musi novou hodnotu opravdu poslat na desku.
     * Mutacni zkouska: vyhodit bl_apply() z bl_level_cb() v ui.c. */
    tap(BL_LVL_X(1), BL_ROW_Y(1), false);       /* profil DEN na 75 % */
    check("tlacitko urovne posle jas na desku", fake_bl_percent == 75);

    /* -----------------------------------------------------------------
     * Dialog ma radek na kazdy profil, takze uroven CIZIHO profilu jde
     * nastavit, aniz by se do nej prepinalo. Jas, ktery prave sviti, se
     * tim nesmi hnout - a po prepnuti do nej musi prijit prave ta
     * hodnota.
     *
     * Mutacni zkouska: v bl_level_cb() v ui.c ukladat vzdy do prave
     * platneho profilu (cfg.prof[cfg.profile].bright = p). Nocni uroven
     * pak zustane na 35 a posledni dve kontroly selzou.
     * ----------------------------------------------------------------- */
    tap(BL_LVL_X(4), BL_ROW_Y(2), false);       /* profil NOC na 25 % */
    check("nastaveni urovne ciziho profilu nehne jasem", fake_bl_percent == 75);
    shot("jas-tri-profily");

    /* Prepnuti profilu musi poslat jeho vlastni ulozenou uroven.
     * Mutacni zkouska: v bl_level() vracet cfg.prof[UI_PROF_DAY].bright. */
    tap(BL_PROF_X, BL_ROW_Y(2), false);         /* profil NOC */
    check("prepnuti na NOC posle nocni uroven", fake_bl_percent == 25);
    shot("jas-noc");

    /* Navrat na DEN musi vratit denni uroven nedotcenou tim, co se
     * stalo v noci - to je cely smysl ulozenych profilu. */
    tap(BL_PROF_X, BL_ROW_Y(1), false);         /* profil DEN */
    check("navrat na DEN vrati denni uroven", fake_bl_percent == 75);

    swipe(120, 240, 700, 250, 8, false, false);     /* zavrit dialog */
    run_ms(200, false);
    shot("nastaveni-jas-75");

    /* -----------------------------------------------------------------
     * 7. ZTLUMENI PO CASE SNIZI JAS A NEZHASNE.
     *
     * Zhasly displej za jizdy nerika, jestli deska vubec bezi; ztlumeny
     * je porad citelny. Tvrdi se tu oboji: jas klesl na zvolenou
     * uroven, prejezdem a ne skokem, a hlavni vypinac podsviceni
     * zustal zapnuty.
     *
     * Mutacni zkouska: v dim_begin() v ui.c nahradit telo za
     * backlight(false). Prvni kontrola selze (zadny mezistav), druha
     * taky (jas nekonci na 25) a treti taky (panel zhasnul).
     * ----------------------------------------------------------------- */
    set_dim_step(1);                            /* ztlumit po 30 s */
    idle_ms(29000);                             /* tesne pred limitem */

    /* Dokrokovat po 20 ms, dokud se jas nezacne menit, a hned se podivat,
     * kde skoncil. Tvrdy skok by preskocil rovnou na ztlumenou uroven,
     * prejezd musi byt nekde mezi. */
    int mid = 75;
    for (int i = 0; i < 100 && mid == 75; i++) {
        run_ms(20, false);
        mid = fake_bl_percent;
    }
    check("pred ztlumenim se jas stahuje postupne", mid > 25 && mid < 75);

    run_ms(600, false);                         /* pul vteriny prejezdu */
    check("ztlumeny displej sviti na zvolene urovni", fake_bl_percent == 25);
    check("ztlumeni displej NEZHASNE", fake_bl_on);

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
    check("probuzeni jas rozsviti postupne", wake > 25 && wake < 75);
    run_ms(900, false);
    check("nabeh po probuzeni dojede na ulozenou uroven",
          fake_bl_percent == 75);

    /* uklid: zpatky na "nikdy", at zbytek zkousky nestmiva */
    set_dim_step(0);
}

/* ---------------------------------------------------------------------
 * Profily
 *
 * Profil nese dve veci: jas a ztlumeni po necinnosti. Prepnuti musi
 * zmenit OBE a navrat do profilu musi vratit presne to, co v nem bylo -
 * jinak profil nic neslibuje. Jas jde zmerit skrz predstiranou desku,
 * ztlumeni je videt jen na popisku tlacitka, takze se porovnava otisk
 * jeho obdelniku.
 *
 * Prepina se ze dvou mist a obe musi delat totez: prepinac v horni
 * liste a tlacitko PROFIL v nastaveni.
 * ------------------------------------------------------------------- */
#define DIM_BOX     372, PAGE_Y(28), 532, PAGE_Y(96)
#define PROFBTN_BOX 372, PAGE_Y(120), 532, PAGE_Y(188)

static void phase_profily(void)
{
    ui_show_page(5);
    run_ms(200, false);

    /* V profilu DEN: jas 75 (zbyl po phase_jas), ztlumeni po 30 s. */
    set_dim_step(1);
    unsigned blank_den = region_hash(DIM_BOX);
    unsigned btn_den   = region_hash(PROFBTN_BOX);
    unsigned bar_den   = region_hash(620, 0, 800, 64);
    shot("profil-den");

    /* -----------------------------------------------------------------
     * Prepinac v horni liste. Musi poslat jas noveho profilu a zaroven
     * prepsat zhasinani - obe hodnoty patri profilu.
     *
     * Mutacni zkouska: v profile_apply() v ui.c vyhodit dim_btn_paint()
     * (popisek ztlumeni pak za profilem nejde a prvni kontrola selze)
     * nebo bl_apply() (druha kontrola selze).
     * ----------------------------------------------------------------- */
    tap(PROF_BAR_X, PROF_BAR_Y, false);         /* DEN -> NOC */
    check("prepnuti profilu zmeni ztlumeni", blank_den != region_hash(DIM_BOX));
    check("prepnuti profilu zmeni jas", fake_bl_percent == 25);
    check("prepnuti je videt v horni liste",
          bar_den != region_hash(620, 0, 800, 64));
    check("prepnuti je videt na tlacitku PROFIL",
          btn_den != region_hash(PROFBTN_BOX));
    shot("profil-noc");

    /* Dokola az zpatky na DEN: NOC -> DILNA -> DEN. Po ceste se overi,
     * ze DILNA ma svuj vlastni jas a ze to neni jen prepinac dvou
     * poloh. */
    tap(PROF_BAR_X, PROF_BAR_Y, false);         /* NOC -> DILNA */
    check("treti profil ma vlastni jas", fake_bl_percent == 100);
    shot("profil-dilna");

    /* Zpatky uz pres tlacitko v nastaveni - obe cesty vedou do
     * profile_cycle_cb(). Mutacni zkouska: v build_nastav() predat
     * tlacitku PROFIL misto profile_cycle_cb hodnotu NULL. */
    tap(SET_PROFILE_X, SET_ROW2_Y, false);      /* DILNA -> DEN */
    check("tlacitko PROFIL v nastaveni strida stejne jako lista",
          fake_bl_percent == 75);
    check("navrat do profilu vrati jeho ztlumeni",
          blank_den == region_hash(DIM_BOX));

    /* uklid: zpatky na "nikdy", at zbytek zkousky nestmiva */
    set_dim_step(0);
}

/* ---------------------------------------------------------------------
 * Reset a udrzba
 *
 * Kazda z techto akci neco nevratne smaze, takze se nesmi dat spustit
 * jednim tuknutim. U kazde se proto zkousi oboji: tuknuti na potvrzovaci
 * tlacitko (nesmi se stat nic) a teprve pak drzeni pres tri vteriny.
 * ------------------------------------------------------------------- */
static void hold_confirm(void)
{
    touch_at(HOLD_X, HOLD_Y);           /* tlacitko DRZ 3 s */
    touch_down = true;
    run_ms(4200, false);                /* 3 s odpocet a potvrzeni */
    touch_release(false);
    run_ms(600, false);                 /* akce a vymena dialogu */
}

static void phase_reset(void)
{
    ui_show_page(5);
    run_ms(200, false);
    shot("nastaveni-reset");

    tap(SET_RESET_X, SET_ROW3_Y, false);        /* tlacitko UDRZBA */
    shot("reset-nabidka");

    /* -----------------------------------------------------------------
     * Zapomenuti vsech siti. Pred resetem jich par ulozenych je, takze
     * je co smazat a cislo v nabidce neni vymyslene.
     *
     * Mutacni zkouska: v reset_wifi_ok_cb() v ui.c volat misto
     * wifi_forget_all() jen wifi_forget() prvni site. Posledni kontrola
     * pak selze, protoze seznam nezustane prazdny.
     * ----------------------------------------------------------------- */
    int saved_before = fake_saved_n;
    check("pred resetem je co zapomenout", saved_before > 0);
    tap(RST_X(1), RST_Y(0), false);             /* ZAPOMENOUT VSECHNY SITE */
    shot("reset-site");

    tap(HOLD_X, HOLD_Y, false);                 /* jen tuknuti na potvrzeni */
    check("tuknuti ulozene site nesmaze", fake_saved_n == saved_before);

    hold_confirm();
    check("podrzeni vyprazdni seznam ulozenych siti", fake_saved_n == 0);
    shot("reset-site-hotovo");                  /* nabidka uz s nulou */

    /* -----------------------------------------------------------------
     * Citace sbernice. Na nulu se po resetu nepta: vuz na sbernici
     * mluvi dal, takze prijem zacne rust hned. Tvrdi se tedy to, co
     * reset opravdu znamena - citace spadly hluboko pod to, co
     * ukazovaly, a nas vlastni vysilac je v poslechu porad na nule.
     *
     * Mutacni zkouska: v reset_can_ok_cb() v ui.c vynechat volani
     * can_stats_reset(). Citac prijmu pak jen roste a kontrola selze.
     * ----------------------------------------------------------------- */
    uint32_t rx_before = fake_can.rx;
    check("citace sbernice pred resetem neco ukazuji", rx_before > 0u);
    tap(RST_X(0), RST_Y(1), false);             /* VYNULOVAT CITACE CAN */
    shot("reset-citace");
    tap(HOLD_X, HOLD_Y, false);
    check("tuknuti citace nevynuluje", fake_can.rx >= rx_before);
    hold_confirm();
    check("podrzeni vynuluje citace sbernice",
          fake_can.rx < rx_before && fake_can.tx == 0u &&
          fake_can.bus_err == 0u);

    /* -----------------------------------------------------------------
     * Restart desky. Na hostiteli se jen spocita.
     * Mutacni zkouska: v reset_reboot_ok_cb() v ui.c vynechat volani
     * host->reboot().
     * ----------------------------------------------------------------- */
    tap(RST_X(1), RST_Y(1), false);             /* RESTART DESKY */
    shot("reset-restart");
    tap(HOLD_X, HOLD_Y, false);
    check("tuknuti desku nerestartuje", fake_reboots == 0);
    hold_confirm();
    check("podrzeni desku restartuje", fake_reboots == 1);

    /* -----------------------------------------------------------------
     * Vychozi nastaveni. Pred resetem je v profilu DEN jas 75 % (z
     * phase_jas) a jednotky prepnute na mph, takze je na cem poznat, ze
     * se opravdu neco vratilo.
     *
     * Mutacni zkouska: v reset_cfg_ok_cb() v ui.c prepsat jen profil
     * UI_PROF_DAY. Kontrola na jas po resetu pak projde, ale jednotky
     * zustanou na mph a kontrola otisku selze.
     * ----------------------------------------------------------------- */
    ui_show_page(5);
    run_ms(200, false);
    unsigned units_kmh = region_hash(20, PAGE_Y(28), 180, PAGE_Y(96));
    tap(SET_UNITS_X, SET_ROW1_Y, false);        /* km/h -> mph */
    check("jednotky se daly prepnout", units_kmh != region_hash(20, PAGE_Y(28),
                                                               180, PAGE_Y(96)));
    check("pred resetem neni jas vychozi", fake_bl_percent == 75);

    tap(SET_RESET_X, SET_ROW3_Y, false);
    tap(RST_X(0), RST_Y(0), false);             /* VYCHOZI NASTAVENI */
    shot("reset-nastaveni");

    tap(HOLD_X, HOLD_Y, false);
    check("tuknuti nastaveni neresetuje", fake_bl_percent == 75);

    hold_confirm();
    run_ms(400, false);                         /* prestavba UI */
    check("podrzeni vrati profily do vychoziho stavu", fake_bl_percent == 100);
    check("podrzeni vrati i jednotky",
          units_kmh == region_hash(20, PAGE_Y(28), 180, PAGE_Y(96)));
    shot("reset-nastaveni-hotovo");
}

/* Deska bez dratku z GPIO6 na plosku BL stmivat neumi a nesmi se tvarit,
 * ze ano. Postavi se cela UI znovu nad hostitelem, ktery bl_set_percent
 * nema, a misto tlacitka jasu musi byt v nastaveni vysvetleni, proc to
 * nejde. Porovnava se primo obdelnik, kde tlacitko jasu stoji - zbytek
 * stranky vypada stejne a otisk cele stranky by se tim rozmelnil.
 *
 * Mutacni zkouska: vyhodit podminku if (bl_can_dim()) z build_nastav()
 * v ui.c. Obe stranky pak vypadaji stejne a kontrola selze. */
#define BRIGHT_BOX 548, PAGE_Y(120), 708, PAGE_Y(188)
/* dolni rada dialogu ztlumeni: bud pet urovni, nebo jedina poznamka */
#define DIM_ROW2_BAND  300, 421
#define HW_BTN_X       (800 - 30 - 150 / 2)
#define HW_BTN_Y       DIM_L_Y

static void phase_bez_dratku(void)
{
    int nb;
    lv_coord_t bw, bh;

    ui_show_page(5);
    run_ms(200, false);
    unsigned with_dim = region_hash(BRIGHT_BOX);

    /* -----------------------------------------------------------------
     * S DRATKEM se o hardwaru nemluvi. Dolni rada dialogu ztlumeni nese
     * pet urovni jasu a zadnou poznamku - stmivani uz je udelane, takze
     * nabizet jeho dodelani by byl nesmysl.
     *
     * Mutacni zkouska: v dim_cb() v ui.c postavit tlacitko s poznamkou
     * bez ohledu na bl_can_dim(). V dolni rade bude sest tlacitek a
     * kontrola MUSI selhat.
     * ----------------------------------------------------------------- */
    tap(SET_DIM_X, SET_ROW1_Y, false);
    nb = 0; bw = 9999; bh = 9999;
    scan_btns(lv_scr_act(), DIM_ROW2_BAND, &nb, &bw, &bh);
    check("s dratkem nabizi dialog jen urovne ztlumeni", nb == 5);
    swipe(120, 270, 700, 275, 8, false, false);     /* zavrit mezi radami */
    run_ms(200, false);

    lv_obj_t *fresh = lv_obj_create(NULL);
    lv_scr_load(fresh);
    ui_set_host(&FAKE_HOST_NODIM);
    ui_build();
    ui_show_page(5);
    run_ms(200, false);

    check("bez stmivani neni na strance tlacitko jasu",
          with_dim != region_hash(BRIGHT_BOX));
    shot("jas-nelze");

    /* -----------------------------------------------------------------
     * BEZ DRATKU urovne zmizi a na jejich miste zustane jedina
     * poznamka: jde to dodelat. Kresba je mala, dotykovy cil ale musi
     * zustat 120 px jako u kazdeho jineho tlacitka.
     *
     * Mutacni zkouska: v dim_cb() v ui.c to tlacitko ve vetvi
     * !bl_can_dim() vynechat. Prvni kontrola MUSI selhat.
     * ----------------------------------------------------------------- */
    unsigned page = content_hash();
    tap(SET_DIM_X, SET_ROW1_Y, false);
    nb = 0; bw = 9999; bh = 9999;
    scan_btns(lv_scr_act(), DIM_ROW2_BAND, &nb, &bw, &bh);
    check("bez dratku zustane misto urovni jedina poznamka", nb == 1);
    check("poznamka ma porad dotykovy cil 120 px", bw >= 120 && bh >= 120);
    shot("ztlumeni-nelze");

    /* -----------------------------------------------------------------
     * Poznamka neco otevre a z te stranky se da ven stejne jako z
     * kazdeho jineho dialogu - gestem zprava.
     *
     * Mutacni zkouska: predat hw_dim_cb() jako NULL v dim_cb().
     * ----------------------------------------------------------------- */
    unsigned dlg = region_hash(0, 0, 800, 480);
    tap(HW_BTN_X, HW_BTN_Y, false);
    check("poznamka otevre stranku o dodelani stmivani",
          dlg != region_hash(0, 0, 800, 480));
    shot("stmivani-jde-dodelat");

    swipe(120, 240, 700, 250, 8, false, false);
    run_ms(200, false);
    check("ze stranky vede gesto zpet na nastaveni", page == content_hash());
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

    /* Vychozi nastaveni jako po prvnim startu desky, jen se ztlumenim
     * vypnutym ve vsech profilech - snimky nemaji proc stmivat. */
    ui_cfg_t cfg = {
        .mph = false,
        .fahrenheit = false,
        .lang = lang,
        .profile = UI_PROF_DAY,
        .prof = {
            [UI_PROF_SHOP]  = { .bright = 100, .dim_s = 0, .dim_bright = 25 },
            [UI_PROF_DAY]   = { .bright = 100, .dim_s = 0, .dim_bright = 25 },
            [UI_PROF_NIGHT] = { .bright = 35,  .dim_s = 0, .dim_bright = 25 },
        },
    };
    ui_set_host(&FAKE_HOST);
    bus_init();
    ui_set_cfg(&cfg);
    ui_build();

    rgb = malloc((size_t)SCR_W * SCR_H * 3);
    if (!rgb) { fprintf(stderr, "malloc selhal\n"); return 1; }

    /* Nejdriv cesta k datum: od desky na stole po odpovidajici
     * jednotku. Stranky se tim zaroven naplni zivymi hodnotami, takze
     * snimky nize uz ukazuji vyrobek tak, jak vypada v aute. */
    phase_hodnoty();

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
    phase_sloupec();
    phase_dialogy();        /* konci v listen-only */
    phase_stav_wifi();
    phase_wifi();
    phase_jas();
    phase_profily();
    phase_reset();
    phase_bez_dratku();     /* posledni - stavi UI nad jinym hostitelem */

    free(rgb);
    if (checks_failed) printf("%d kontrol selhalo\n", checks_failed);
    return (shot_err || checks_failed) ? 1 : 0;
}
