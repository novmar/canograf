/*
 * Talento HMI - testovaci firmware v3
 * Waveshare ESP32-S3-Touch-LCD-4.3B, 800x480, GT911 (kapacitni), CH422G
 *
 * Hodnoty cte z vozidla vrstva src/diag/poller.c; UI si zadnou nevymysli.
 *
 * Tenhle soubor uz obsahuje jen veci vazane na desku: panel, LVGL port,
 * WiFi, OTA, NVS, podsviceni. Cele uzivatelske rozhrani je v src/ui.c,
 * aby slo prelozit i na hostiteli a vyfotit (viz sim/).
 * Propojeni jde pres ui_host_t z ui.h.
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
#include "ui.h"
#include "i18n.h"
#include "can/twai.h"
#include "sleep_test.h"
#if CANOGRAF_MOD_WEB
/* Cela sluzba je v src/net/ a pri CANOGRAF_MOD_WEB=0 se do obrazu vubec
 * neprelozi - skript konfigurace vyradi cely adresar, viz
 * config/README.md. Tenhle soubor je jedine misto, kde se server potka
 * se siti; sam o ni nevi. */
#include "net/server.h"
#endif

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_chip_info.h>
#include <esp_flash.h>

using namespace esp_panel::drivers;
using namespace esp_panel::board;

/* Jazyk, ve kterem se deska rozjede, nez uzivatel neco zvoli. Kod podle
 * ISO 639-1; build ho muze prebit pres -D CANOGRAF_DEFAULT_LANG='"cs"'.
 * Neznamy kod spadne na prvni jazyk v tabulce v src/i18n.c. */
#ifndef CANOGRAF_DEFAULT_LANG
#define CANOGRAF_DEFAULT_LANG "en"
#endif

static Preferences prefs;
static Board *g_board = nullptr;

/* ---------------------------------------------------------------------
 * Podsviceni
 *
 * EXIO2 na expanderu CH422G je hlavni vypinac: povoluje budic podsviceni
 * i samotny panel, takze je to jedina cesta k uplne tme. Zustava, jak
 * byl, prepina ho Board::getBacklight().
 *
 * Jas se regulue PWM na plosce BL. Je to hardwarova uprava: jediny
 * dratek z CANOGRAF_BL_PWM_PIN na plosku BL vedle konektoru LCD pasku.
 * Ploska vede pres R11 10k na uzel s C11 1uF na zem - dolni propust s
 * casovou konstantou 10 ms - a odtud pres R7 68k na vstup FB budice
 * AP3032. PWM se tedy vyhladi na stejnosmerne napeti a to odecita proud
 * LEDkam (zmereno, docs/hardware/backlight.md):
 *
 *      0,0 V -> 64,6 mA (plny jas)     2,5 V -> 15,0 mA
 *      1,0 V -> 44,8 mA                3,3 V ->  0,0 mA (zhasnuto)
 *
 * POLARITA JE OBRACENA: vetsi strida znamena mensi jas. Prepocet procent
 * na stridu i s korekci pro lidske oko dela ui_bl_duty() v src/ui.c.
 * Je to jedine misto, kde ten prepocet je, a sim/lv_shot.c hlida, ze se
 * polarita nemuze tise prohodit.
 *
 * Frekvence 5 kHz je dve dekady nad filtrem, takze zvlneni na uzlu je
 * pod jednim promile a pritom je dost nizka, aby na nic nenarazela.
 *
 * POZOR: CANOGRAF_BL_PWM_PIN je na teto desce GPIO6 a to je zaroven
 * vstup INT hodin PCF85063. Vystup hodin ma otevreny kolektor, takze do
 * nej smime tlacit push-pull POTUD, DOKUD jsou alarmy a preruseni RTC
 * vypnute - po resetu jsou. Kdo bude chtit preruseni z RTC zapnout,
 * musi pro stmivani najit jinou cestu; moznosti jsou sepsane v
 * docs/hardware/backlight.md. Nezapinat preruseni RTC za zady tohohle
 * komentare.
 *
 * Deska bez dratku ma CANOGRAF_BL_PWM_PIN = -1. PWM se pak vubec
 * nerozjede, UI_HOST.bl_set_percent zustane nullptr a rozhrani stmivani
 * vubec nenabidne - zustane dnesni zap/vyp.
 * ------------------------------------------------------------------- */
#ifndef CANOGRAF_BL_PWM_PIN
#define CANOGRAF_BL_PWM_PIN -1
#endif
#ifndef CANOGRAF_BL_PWM_HZ
#define CANOGRAF_BL_PWM_HZ 5000
#endif

/* 12 bitu: nejmensi krok stridy je na dolnim konci stupnice hluboko pod
 * tim, co oko rozezna, a LEDC to pri 5 kHz zvladne (80 MHz / 4096). */
#define BL_PWM_BITS 12
#define BL_DUTY_MAX ((1u << BL_PWM_BITS) - 1u)

static void bl_raw(bool on)
{
    if (!g_board) return;
    auto bl = g_board->getBacklight();
    if (bl) on ? bl->on() : bl->off();
}

static void host_backlight(bool on) { bl_raw(on); }

#if CANOGRAF_BL_PWM_PIN >= 0
static void host_bl_percent(int percent)
{
    ledcWrite(CANOGRAF_BL_PWM_PIN, ui_bl_duty(percent, BL_DUTY_MAX));
}
#endif

/* false = stmivat se nebude. Volajici pak musi UI tu funkci odebrat,
 * aby se rozhrani netvarilo, ze stmiva. */
static bool bl_pwm_init(void)
{
#if CANOGRAF_BL_PWM_PIN >= 0
    if (!ledcAttach(CANOGRAF_BL_PWM_PIN, CANOGRAF_BL_PWM_HZ, BL_PWM_BITS)) {
        Serial.printf("podsviceni: LEDC na GPIO%d se nerozjelo\n",
                      CANOGRAF_BL_PWM_PIN);
        return false;
    }
    /* Nez UI posle ulozenou uroven, at deska sviti naplno. */
    ledcWrite(CANOGRAF_BL_PWM_PIN, ui_bl_duty(100, BL_DUTY_MAX));
    Serial.printf("podsviceni: PWM GPIO%d, %d Hz, %d bitu\n",
                  CANOGRAF_BL_PWM_PIN, CANOGRAF_BL_PWM_HZ, BL_PWM_BITS);
    return true;
#else
    Serial.println("podsviceni: jen zap/vyp, CANOGRAF_BL_PWM_PIN = -1");
    return false;
#endif
}

/* ---------------------------------------------------------------------
 * Nastaveni v NVS
 *
 * Spolecna cast ma klice "mph", "fahr", "lang" a "prof" (cislo prave
 * platneho profilu). Kazdy profil ma ctyri: "p<i>bl" jas, "p<i>dt" dobu
 * do ztlumeni, "p<i>db" uroven ztlumeni a "p<i>pf" prekryv s FPS. Tri
 * profily tedy stoji dvanact polozek po 32 B = 384 B, cela sekce
 * nastaveni asi 512 B z 20 kB oddilu.
 *
 * Klice "blank", "blday", "blnight", "blnight_on" a "tbdn" patrily
 * dvojici jasu den/noc, kterou profily nahradily; "p<i>bk" drzel dobu
 * do ZHASNUTI, nez se ze zhasinani stalo ztlumeni. Cte je uz jen
 * migrace v setup() a po prvnim ulozeni z NVS zmizi.
 * ------------------------------------------------------------------- */
static void prof_key(char *b, size_t n, int i, const char *what)
{
    snprintf(b, n, "p%d%s", i, what);
}

static void host_save_cfg(const ui_cfg_t *c)
{
    prefs.begin("talento", false);
    prefs.putBool("mph", c->mph);
    prefs.putBool("fahr", c->fahrenheit);
    prefs.putInt("lang", c->lang);
    prefs.putInt("prof", c->profile);
    for (int i = 0; i < UI_PROFILES; i++) {
        char k[8];
        prof_key(k, sizeof(k), i, "bl"); prefs.putInt(k, c->prof[i].bright);
        prof_key(k, sizeof(k), i, "dt"); prefs.putUInt(k, c->prof[i].dim_s);
        prof_key(k, sizeof(k), i, "db"); prefs.putInt(k, c->prof[i].dim_bright);
        prof_key(k, sizeof(k), i, "pf"); prefs.putBool(k, c->prof[i].perf);
        /* Doba do zhasnuti. Maze se az tady, aby mela migrace v setup()
         * co cist i po restartu, ktery nic neulozil. */
        prof_key(k, sizeof(k), i, "bk"); prefs.remove(k);
    }
    /* Totez pro pozustatky po dvojici jasu den/noc. */
    for (const char *old : {"blank", "blday", "blnight", "blnight_on", "tbdn"})
        prefs.remove(old);
    prefs.end();
}

/* ---------------------------------------------------------------------
 * WiFi
 *
 * Zapamatovanych siti je sest. Diagnostika potka dilnu, domov, telefon
 * a par zakaznickych siti; sest jich pokryje, vic uz by se do seznamu
 * nevesolo najednou a kazda stoji v NVS jen SSID a heslo (asi 100 B).
 * Poradi je podle posledniho pripojeni, takze pri zaplneni vypadne ta
 * nejdele nepouzita.
 *
 * Klice v NVS: "wn" pocet, "ws0".."ws5" SSID, "wp0".."wp5" heslo.
 * ------------------------------------------------------------------- */
#define WIFI_SLOTS 6

static char saved_ssid[WIFI_SLOTS][33];
static char saved_pass[WIFI_SLOTS][65];
static int  saved_n = 0;

/* LVGL dostava jen const char*, takze retezce z Arduino String musi prezit
 * navrat z funkce - proto staticke kopie. */
static char scan_ssid_buf[33];
static char conn_ssid_buf[33];
static char ip_buf[20];

static int saved_find(const char *ssid)
{
    if (!ssid || !ssid[0]) return -1;
    for (int i = 0; i < saved_n; i++)
        if (strcmp(saved_ssid[i], ssid) == 0) return i;
    return -1;
}

static void saved_store(void)
{
    prefs.begin("talento", false);
    prefs.putInt("wn", saved_n);
    for (int i = 0; i < saved_n; i++) {
        char k[8];
        snprintf(k, sizeof(k), "ws%d", i); prefs.putString(k, saved_ssid[i]);
        snprintf(k, sizeof(k), "wp%d", i); prefs.putString(k, saved_pass[i]);
    }
    prefs.end();
}

static void saved_load(void)
{
    prefs.begin("talento", false);
    saved_n = prefs.getInt("wn", -1);
    if (saved_n < 0) {
        /* Prechod z verze, ktera si pamatovala jedinou sit. Stare klice
         * uz nikdo necte, takze po prepsani zmizi. */
        saved_n = 0;
        String s = prefs.getString("ssid", ""), p = prefs.getString("pass", "");
        if (s.length()) {
            snprintf(saved_ssid[0], sizeof(saved_ssid[0]), "%s", s.c_str());
            snprintf(saved_pass[0], sizeof(saved_pass[0]), "%s", p.c_str());
            saved_n = 1;
        }
        prefs.remove("ssid");
        prefs.remove("pass");
        prefs.end();
        saved_store();
        return;
    }
    if (saved_n > WIFI_SLOTS) saved_n = WIFI_SLOTS;
    for (int i = 0; i < saved_n; i++) {
        char k[8];
        snprintf(k, sizeof(k), "ws%d", i);
        snprintf(saved_ssid[i], sizeof(saved_ssid[i]), "%s",
                 prefs.getString(k, "").c_str());
        snprintf(k, sizeof(k), "wp%d", i);
        snprintf(saved_pass[i], sizeof(saved_pass[i]), "%s",
                 prefs.getString(k, "").c_str());
    }
    prefs.end();
}

/* Ulozi sit na prvni misto. Kdyz uz tam je, jen se posune nahoru;
 * kdyz je plno, vypadne posledni. */
static void saved_put(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return;
    int at = saved_find(ssid);
    if (at < 0) {
        at = (saved_n < WIFI_SLOTS) ? saved_n++ : WIFI_SLOTS - 1;
    }
    for (int i = at; i > 0; i--) {
        memcpy(saved_ssid[i], saved_ssid[i - 1], sizeof(saved_ssid[0]));
        memcpy(saved_pass[i], saved_pass[i - 1], sizeof(saved_pass[0]));
    }
    snprintf(saved_ssid[0], sizeof(saved_ssid[0]), "%s", ssid);
    snprintf(saved_pass[0], sizeof(saved_pass[0]), "%s", pass ? pass : "");
    saved_store();
}
/* ---------------------------------------------------------------------
 * Sken siti
 *
 * Radic umi bud hledat, nebo se pripojovat, ne oboje najednou. Dokud
 * bezi NEDOKONCENY pokus o pripojeni - a ten bezi po celou dobu, co
 * deska marne hleda ulozenou sit, protoze ji Arduino samo porad zkousi
 * znovu - vraci esp_wifi_scan_start() chybu ESP_ERR_WIFI_STATE a sken
 * se vubec nerozjede. V UI to vypada jako "hledani se nezdarilo".
 *
 * Zmereno na desce (ulozena sit mimo dosah, 4 s po startu):
 *
 *     WiFi[pred]: mode=1 status=DISCONNECTED(6) scanComplete=-2
 *     A) esp_wifi_scan_start            -> ESP_ERR_WIFI_STATE
 *     B) esp_wifi_disconnect            -> ESP_OK
 *     C) esp_wifi_scan_start po disconnect -> ESP_OK   (a nasel 2 site)
 *
 * Podstatne je, ze WiFi.disconnect() z Arduina tohle NESPRAVI: kdyz
 * spojeni nestoji, tise se vrati a radic zustane ve stavu "pripojuji
 * se". Zastavit to umi az esp_wifi_disconnect() primo. Spolu s tim se
 * musi vypnout i automaticke obnovovani spojeni, jinak se pokus do
 * vteriny rozjede znovu a dalsi sken (tlacitko ZNOVU) by zase selhal.
 *
 * Po dobehnuti skenu se pokus zase obnovi - deska nesmi prestat hledat
 * ulozenou sit jen proto, ze se nekdo kouknul do seznamu.
 * ------------------------------------------------------------------- */
static bool wifi_connect_paused = false;

static void wifi_pause_connect(void)
{
    if (WiFi.status() == WL_CONNECTED) return;   /* hotove spojeni sken snese */
    WiFi.setAutoReconnect(false);
    esp_wifi_disconnect();
    wifi_connect_paused = true;
}

static void wifi_resume_connect(void)
{
    if (!wifi_connect_paused) return;
    wifi_connect_paused = false;
    WiFi.setAutoReconnect(true);
    if (saved_n && WiFi.status() != WL_CONNECTED)
        WiFi.begin(saved_ssid[0], saved_pass[0]);
}

static void host_wifi_scan_start(void)
{
    /* V rezimu pristupoveho bodu se NESKENUJE. Radic umi bud jedno,
     * nebo druhe, a WiFi.mode(WIFI_STA) nize by pristupovy bod slozil
     * pod rukama cloveku, ktery je prave pres nej pripojeny. UI sem v
     * tom rezimu nevede (seznam siti v nem vubec nevznika), tohle je
     * druha pojistka - vedle radice, ktery by na to odpovedel
     * ESP_ERR_WIFI_STATE a uz se nevzpamatoval. */
    if (WiFi.getMode() & WIFI_MODE_AP) {
        Serial.println("WiFi: jsme pristupovy bod, sken se nespousti");
        return;
    }
    WiFi.mode(WIFI_STA);
    wifi_pause_connect();
    WiFi.scanDelete();
    int16_t r = WiFi.scanNetworks(true);
    if (r == WIFI_SCAN_FAILED) {
        Serial.printf("WiFi: sken se nerozjel (mode=%d status=%d)\n",
                      (int)WiFi.getMode(), (int)WiFi.status());
        wifi_resume_connect();
    }
}

/* -1 jeste bezi, -2 nerozjelo se, jinak pocet siti */
static int host_wifi_scan_result(void)
{
    int n = WiFi.scanComplete();
    if (n != WIFI_SCAN_RUNNING) wifi_resume_connect();
    return n;
}

static const char *host_wifi_scan_ssid(int i)
{
    snprintf(scan_ssid_buf, sizeof(scan_ssid_buf), "%s", WiFi.SSID(i).c_str());
    return scan_ssid_buf;
}

static void host_wifi_connect(const char *ssid, const char *pass)
{
    saved_put(ssid, pass);
    wifi_connect_paused = false;    /* vlastni volba prebiji navrat k ulozene */
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, pass);
}

static bool host_wifi_known(const char *ssid) { return saved_find(ssid) >= 0; }

static bool host_wifi_connect_known(const char *ssid)
{
    int i = saved_find(ssid);
    if (i < 0) return false;
    /* kopie, protoze saved_put() sloty prehazi */
    char pass[sizeof(saved_pass[0])];
    memcpy(pass, saved_pass[i], sizeof(pass));
    saved_put(ssid, pass);
    wifi_connect_paused = false;
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, pass);
    return true;
}

static void host_wifi_forget(const char *ssid)
{
    int i = saved_find(ssid);
    if (i < 0) return;
    for (int k = i; k + 1 < saved_n; k++) {
        memcpy(saved_ssid[k], saved_ssid[k + 1], sizeof(saved_ssid[0]));
        memcpy(saved_pass[k], saved_pass[k + 1], sizeof(saved_pass[0]));
    }
    saved_n--;
    saved_ssid[saved_n][0] = 0;
    saved_pass[saved_n][0] = 0;
    saved_store();
}

static int host_wifi_saved_count(void) { return saved_n; }

static const char *host_wifi_saved_ssid(int i)
{
    return (i >= 0 && i < saved_n) ? saved_ssid[i] : nullptr;
}

/* Jeden zapis do NVS misto sesti po sobe jdoucich - UI tim navic nemusi
 * resit, ze se mu seznam pod rukama prehazuje. */
static void host_wifi_forget_all(void)
{
    for (int i = 0; i < WIFI_SLOTS; i++) {
        saved_ssid[i][0] = 0;
        saved_pass[i][0] = 0;
    }
    saved_n = 0;
    saved_store();
    /* Hesla uz nemame, takze se nema k cemu vracet: zastavime pokus o
     * pripojeni, at radic nezkousi donekonecna sit, kterou si UI uz
     * nepamatuje. */
    WiFi.setAutoReconnect(false);
    WiFi.disconnect();
}

/* ---------------------------------------------------------------------
 * Pristupovy bod: deska sit SAMA DELA
 *
 * V garazi, kde zadna sit neni, je to jediny zpusob, jak se k desce
 * telefonem dostat.
 *
 * OBE VECI SE VYLUCUJI. Radic umi bud skenovat a pripojovat se, nebo
 * byt pristupovym bodem. Pokus o oboji konci v ESP_ERR_WIFI_STATE a
 * sken uz se nerozjede nikdy (docs/findings.md, oddil o skenu). UI
 * proto v rezimu AP seznam siti vubec nepostavi a tenhle soubor si to
 * jeste hlida sam: host_wifi_scan_start() se v rezimu AP ani nezacne
 * ptat.
 *
 * JMENO je odvozene z MAC desky: nemeni se pri restartu, dve desky
 * vedle sebe se nepopletou a neni na nem co tajit.
 *
 * HESLO SE GENERUJE a nejde nastavit. Jsou k tomu ctyri duvody:
 *
 *   1. Stejne MUSI BYT VIDET na displeji, jinak se k siti nikdo
 *      nepripoji - deska nema kudy jinudy ho rict. Vlastni heslo tedy
 *      nekoupi zadne utajeni, lezi na skle.
 *   2. Opsat osm znaku z obrazovky je min prace nez je na te obrazovce
 *      naklikat a pak stejne opsat.
 *   3. Vlastni heslo clovek recykluje odjinud. Tohle je na displeji ve
 *      voze, kolem ktereho se chodi - a prave recyklace skutecneho
 *      hesla je tady to nebezpecne.
 *   4. Generovane heslo umi deska zarucit: aspon osm znaku, jak zada
 *      WPA2, a abeceda bez znaku, ktere se pri opisovani pletou.
 *
 * ABECEDA nema 0/O, 1/l/I, 2/Z, 5/S ani 8/B. Osm znaku z osmadvaceti
 * je 3,8 . 10^11 moznosti, coz proti nekomu, kdo stoji u auta, staci.
 *
 * Heslo zustava v NVS, takze prezije restart. Kdyby se menilo pri
 * kazdem startu, nesedelo by s tim, co si clovek opsal nebo vyfotil.
 * ------------------------------------------------------------------- */
#define AP_CLIENTS_MAX 4    /* tataz mez jako NET_SOCKETS_MAX, z druhe strany */
#define AP_PASS_LEN    8    /* minimum, ktere WPA2 pripousti */
#define AP_CHANNEL     1

static const char AP_ALPHABET[] = "abcdefghjkmnpqrstuvwxyz34679";

static char ap_ssid_buf[33];
static char ap_pass_buf[AP_PASS_LEN + 1];

static const char *host_wifi_ap_ssid(void)
{
    if (!ap_ssid_buf[0]) {
        uint64_t mac = ESP.getEfuseMac();
        snprintf(ap_ssid_buf, sizeof(ap_ssid_buf), "canograf-%04x",
                 (unsigned)(mac & 0xFFFFu));
    }
    return ap_ssid_buf;
}

static void ap_pass_make(void)
{
    const size_t n = sizeof(AP_ALPHABET) - 1;

    for (int i = 0; i < AP_PASS_LEN; i++)
        ap_pass_buf[i] = AP_ALPHABET[esp_random() % n];
    ap_pass_buf[AP_PASS_LEN] = '\0';
    prefs.begin("talento", false);
    prefs.putString("appw", ap_pass_buf);
    prefs.end();
}

static const char *host_wifi_ap_pass(void)
{
    if (!ap_pass_buf[0]) {
        prefs.begin("talento", true);
        snprintf(ap_pass_buf, sizeof(ap_pass_buf), "%s",
                 prefs.getString("appw", "").c_str());
        prefs.end();
        if (strlen(ap_pass_buf) < AP_PASS_LEN) ap_pass_make();
    }
    return ap_pass_buf;
}

/* Rozjede nebo slozi pristupovy bod. Volajici uz rozhodl; tady se to jen
 * provede, vcetne toho, ze se pokus o pripojeni k cizi siti opravdu
 * ZASTAVI - WiFi.disconnect() z Arduina to pri nestojicim spojeni tise
 * neudela (docs/findings.md). */
static void ap_apply(bool on)
{
    if (on) {
        WiFi.setAutoReconnect(false);
        esp_wifi_disconnect();
        wifi_connect_paused = false;    /* uz neni k cemu se vracet */
        WiFi.scanDelete();
        WiFi.mode(WIFI_AP);
        bool ok = WiFi.softAP(host_wifi_ap_ssid(), host_wifi_ap_pass(),
                              AP_CHANNEL, 0, AP_CLIENTS_MAX);
        Serial.printf("WiFi: pristupovy bod %s %s, max %d zarizeni\n",
                      host_wifi_ap_ssid(), ok ? "bezi" : "SE NEROZJEL",
                      AP_CLIENTS_MAX);
        return;
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    if (saved_n) WiFi.begin(saved_ssid[0], saved_pass[0]);
    Serial.println("WiFi: zpatky jako stanice");
}

static void host_wifi_set_ap(bool on)
{
    prefs.begin("talento", false);
    prefs.putBool("apon", on);
    prefs.end();
    ap_apply(on);
}

/* -1 znamena "nevim", ne nula. Mimo rezim AP se nikdo pripojit nemuze a
 * nula by tvrdila, ze se nikdo nepripojil. */
static int host_wifi_ap_clients(void)
{
    if (!(WiFi.getMode() & WIFI_MODE_AP)) return -1;
    return (int)WiFi.softAPgetStationNum();
}

/* Nove heslo vsechny pripojene rozpoji - softAP() se stejnym jmenem a
 * jinym heslem je pro telefon nova sit. UI o tom rekne dopredu. */
static void host_wifi_ap_newpass(void)
{
    ap_pass_make();
    if (WiFi.getMode() & WIFI_MODE_AP) ap_apply(true);
}

/* ---------------- CAN a restart ---------------- */
/* Ukazatel na citace plati porad, i pred can_start(); tam jsou same
 * nuly. UI si ho proto smi drzet. */
static const can_stats_t *host_can_stats(void) { return can_stats(); }
static void host_can_stats_reset(void) { can_stats_reset(); }

/* Rezim radice. Prepnuti do normalniho rezimu znamena, ze deska zacne
 * na cizi sbernici vysilat, takze o nej musi rict uzivatel; tady se jen
 * provede. Vraci se true, kdyz radic opravdu v zadanem rezimu skoncil. */
static bool host_can_is_normal(void) { return can_mode() == CAN_MODE_NORMAL; }

static bool host_can_set_normal(bool normal)
{
    can_mode_t want = normal ? CAN_MODE_NORMAL : CAN_MODE_LISTEN_ONLY;

    if (can_mode() == want) return true;
    can_set_mode(want);
    return can_mode() == want;
}

static void host_reboot(void) { ESP.restart(); }

static bool host_wifi_connected(void) { return WiFi.status() == WL_CONNECTED; }

static const char *host_wifi_ssid(void)
{
    snprintf(conn_ssid_buf, sizeof(conn_ssid_buf), "%s", WiFi.SSID().c_str());
    return conn_ssid_buf;
}

static const char *host_wifi_ip(void)
{
    if (WiFi.status() != WL_CONNECTED) return "";
    snprintf(ip_buf, sizeof(ip_buf), "%s", WiFi.localIP().toString().c_str());
    return ip_buf;
}

/* Sila signalu v dBm. Mimo pripojeni nema co hlasit, takze vraci 0 a UI
 * carky vubec nerozsviti - predstirany plny signal by byl horsi nez
 * zadny. */
static int host_wifi_rssi(void)
{
    if (WiFi.status() != WL_CONNECTED) return 0;
    return (int)WiFi.RSSI();
}

/* Deska bezi jako STA, pristupovy bod se nikde nezaklada. Funkce tu je
 * proto, aby se stavovy radek ptal radice a ne nasi predstavy o nem. */
static bool host_wifi_ap(void)
{
    return (WiFi.getMode() & WIFI_MODE_AP) != 0;
}

/* ---------------- diagnostika ---------------- */
static unsigned host_heap_kb(void)  { return (unsigned)(ESP.getFreeHeap() / 1024); }
static unsigned host_psram_kb(void) { return (unsigned)(ESP.getFreePsram() / 1024); }

/* ---------------------------------------------------------------------
 * Vrstva diagnostiky
 *
 * Cela logika cteni je v src/diag/poller.c a nezna ani hodiny, ani
 * hardware - cas i ramce dostava v parametrech. Deska tedy obstarava
 * jen dve veci: preklopit ramce z kruhoveho zasobniku do vrstvy a
 * pravidelne ji dat cas.
 *
 * Vlastni kurzor do zasobniku: UI si drzi svuj (odposlech na strance
 * ladeni), takze si ctenari ramce neberou.
 *
 * Vysilat se smi JEN v normalnim rezimu. Nestaci spolehnout se na to,
 * ze can_send() v listen-only vrati false - vrstva se podle rezimu
 * chova sama, takze se v listen-only ani nezacne ptat.
 * ------------------------------------------------------------------- */
static diag_poller_t g_diag;
static uint32_t      g_diag_cursor = 0;

static void diag_pump(void)
{
    can_frame_t f;

    can_poll();
    while (can_next(&g_diag_cursor, &f))
        diag_poller_on_frame(&g_diag, f.id, f.data, f.dlc, f.tx, f.ts_ms);
    diag_poller_arm(&g_diag, can_mode() == CAN_MODE_NORMAL);
    diag_poller_poll(&g_diag, can_now_ms());
}

/* ---------------------------------------------------------------------
 * Server se zivymi hodnotami
 *
 * Cela sluzba je v src/net/ a do obrazu se prelozi jen pri
 * CANOGRAF_MOD_WEB=1 (config/README.md). Pri vypnutem modulu tu
 * nezustane ani prepinac: UI_HOST.web_* jsou nullptr a rozhrani server
 * vubec nenabidne, takze deska nema cim port otevrit a da se to overit
 * na elf souboru.
 *
 * Tenhle soubor je JEDINE misto, kde se potkava server se siti. Server
 * sam o WiFi nevi a vedet nemuze: dostane jen report_net_t, ve kterem
 * ZADNE HESLO NENI a nebude - ani k cizi siti, ani k vlastnimu
 * pristupovemu bodu. Viz net/report.h.
 *
 * Port zustava zavreny, dokud ho nekdo nezapne v nastaveni. Volba
 * prezije restart: kdo nechal server bezet, ceka, ze po restartu zase
 * nabehne.
 * ------------------------------------------------------------------- */
#if CANOGRAF_MOD_WEB

static char web_addr_buf[20];

/* Adresa, na ktere je deska k nalezeni - bez "http://" a bez portu.
 * Je to to, co clovek opravdu napise do telefonu. */
static const char *web_addr(void)
{
    if (WiFi.getMode() & WIFI_MODE_AP) {
        snprintf(web_addr_buf, sizeof(web_addr_buf), "%s",
                 WiFi.softAPIP().toString().c_str());
        return web_addr_buf;
    }
    if (WiFi.status() != WL_CONNECTED) return "";
    snprintf(web_addr_buf, sizeof(web_addr_buf), "%s",
             WiFi.localIP().toString().c_str());
    return web_addr_buf;
}

/* Vsechno, co se server o siti dozvi. Heslo tu nema polozku, takze ho
 * nema jak poslat dal - je to stavba, ne opatrnost. */
static void web_net_info(report_net_t *out)
{
    bool ap = (WiFi.getMode() & WIFI_MODE_AP) != 0;

    out->ap         = ap;
    out->sta        = (WiFi.status() == WL_CONNECTED);
    out->can_normal = (can_mode() == CAN_MODE_NORMAL);
    out->uptime_s   = (uint32_t)(millis() / 1000u);
    out->clients    = host_wifi_ap_clients();
    snprintf(out->ssid, sizeof(out->ssid), "%s",
             ap ? host_wifi_ap_ssid()
                : (out->sta ? WiFi.SSID().c_str() : ""));
    snprintf(out->ip, sizeof(out->ip), "%s", web_addr());
}

/* Bezi? Pta se SLUZBY, ne nasi predstavy o ni. Kdyz se port nepodarilo
 * otevrit, tlacitko v nastaveni to rekne samo tim, ze zustane na VYP. */
static bool host_web_enabled(void) { return net_server_running(); }

static void web_start(void)
{
    net_server_cfg_t c = {};
    c.dp       = &g_diag;
    c.net_info = web_net_info;
    if (!net_server_start(&c)) Serial.println("web: nerozjelo se");
}

static void host_web_set(bool on)
{
    prefs.begin("talento", false);
    prefs.putBool("webon", on);
    prefs.end();
    if (on) web_start();
    else    net_server_stop();
}

/* Adresa stranky. Prazdno, kdyz server stoji nebo kdyz deska zadnou
 * adresu nema - "server bezi" bez adresy je polovina informace. */
static const char *host_web_url(void)
{
    return net_server_running() ? web_addr() : "";
}

#endif /* CANOGRAF_MOD_WEB */

/* Neni const: kdyz se PWM nerozjede, musi se bl_set_percent za behu
 * vynulovat, at UI stmivani nenabizi.
 *
 * Polozky jsou jmenovane zamerne. Tri dvojice maji shodny typ
 * (wifi_known/wifi_connect_known, heap_kb/psram_kb), takze prehozeni
 * dvou radku v ui.h by pozicni seznam prelozil bez jedineho varovani a
 * deska by zacala volat jine funkce. */
static ui_host_t UI_HOST = {
    .backlight          = host_backlight,
#if CANOGRAF_BL_PWM_PIN >= 0
    .bl_set_percent     = host_bl_percent,
#else
    .bl_set_percent     = nullptr,  /* bez dratku na plosce BL se neda stmivat */
#endif
    .save_cfg           = host_save_cfg,
    .wifi_scan_start    = host_wifi_scan_start,
    .wifi_scan_result   = host_wifi_scan_result,
    .wifi_scan_ssid     = host_wifi_scan_ssid,
    .wifi_connect       = host_wifi_connect,
    .wifi_connected     = host_wifi_connected,
    .wifi_ssid          = host_wifi_ssid,
    .wifi_ip            = host_wifi_ip,
    .wifi_rssi          = host_wifi_rssi,
    .wifi_ap            = host_wifi_ap,
    .wifi_known         = host_wifi_known,
    .wifi_connect_known = host_wifi_connect_known,
    .wifi_forget        = host_wifi_forget,
    .wifi_saved_count   = host_wifi_saved_count,
    .wifi_saved_ssid    = host_wifi_saved_ssid,
    .wifi_forget_all    = host_wifi_forget_all,
    .wifi_set_ap        = host_wifi_set_ap,
    .wifi_ap_ssid       = host_wifi_ap_ssid,
    .wifi_ap_pass       = host_wifi_ap_pass,
    .wifi_ap_clients    = host_wifi_ap_clients,
    .wifi_ap_newpass    = host_wifi_ap_newpass,
#if CANOGRAF_MOD_WEB
    .web_enabled        = host_web_enabled,
    .web_set            = host_web_set,
    .web_url            = host_web_url,
#else
    /* Bez modulu nezustane ani prepinac - UI server vubec nenabidne. */
    .web_enabled        = nullptr,
    .web_set            = nullptr,
    .web_url            = nullptr,
#endif
    .can_stats          = host_can_stats,
    .can_stats_reset    = host_can_stats_reset,
    .can_is_normal      = host_can_is_normal,
    .can_set_normal     = host_can_set_normal,
    .reboot             = host_reboot,
    .heap_kb            = host_heap_kb,
    .psram_kb           = host_psram_kb,
};

/* ---------------- obnovovani ---------------- */
static void tick_cb(lv_timer_t *t) { (void)t; ui_tick(); }

/* ---------------- start ---------------- */
void setup()
{
    Serial.begin(115200);
    delay(200);

#if CANOGRAF_SLEEP_TEST
    /* Mereni klidoveho odberu misto normalniho startu. Musi to byt uplne
     * prvni vec v setup(): panel, LVGL, CAN ani WiFi se tim padem vubec
     * nerozjedou, takze deska bere presne to, co se meri. Nevraci se -
     * posledni krok je hluboky spanek.
     *
     * Co se meri a cemu ta cisla NEodpovidaji (merime 5V vetev z USB,
     * ne proud z baterie): src/sleep_test.cpp. Zapina se v config/,
     * viz config/README.md. */
    sleep_test_run();
#endif

    Serial.println("\nTalento HMI v5 - start");

    prefs.begin("talento", true);
    ui_cfg_t cfg;
    cfg.mph = prefs.getBool("mph", false);
    cfg.fahrenheit = prefs.getBool("fahr", false);
    int def_lang = i18n_find(CANOGRAF_DEFAULT_LANG);
    if (def_lang < 0) def_lang = 0;
    cfg.lang = prefs.getInt("lang", def_lang);

    for (int i = 0; i < UI_PROFILES; i++) cfg.prof[i] = ui_profile_default(i);
    cfg.profile = prefs.getInt("prof", -1);
    if (cfg.profile < 0) {
        /* Prechod z verze s dvojici jasu den/noc. Stara denni uroven
         * patri profilu DEN, nocni profilu NOC a dilna zacina na
         * vychozich hodnotach - takovy profil driv nebyl. Zhasinani
         * bylo jen jedno spolecne, takze ho dostanou vsechny tri.
         * Stare klice smaze prvni ulozeni, viz host_save_cfg(). */
        cfg.profile = prefs.getBool("blnight_on", false) ? UI_PROF_NIGHT
                                                         : UI_PROF_DAY;
        cfg.prof[UI_PROF_DAY].bright   = prefs.getInt("blday", 100);
        cfg.prof[UI_PROF_NIGHT].bright = prefs.getInt("blnight", 35);
        uint32_t blank = prefs.getUInt("blank", 0);
        for (int i = 0; i < UI_PROFILES; i++) cfg.prof[i].dim_s = blank;
    } else {
        for (int i = 0; i < UI_PROFILES; i++) {
            char k[8];
            prof_key(k, sizeof(k), i, "bl");
            cfg.prof[i].bright = prefs.getInt(k, cfg.prof[i].bright);

            /* Doba do ztlumeni je tataz doba, po ktere displej driv
             * zhasl, takze stary klic "p<i>bk" slouzi jako vychozi
             * hodnota pro novy "p<i>dt". Kdo mel zhasinani po dvou
             * minutach, ztlumi se po dvou minutach.
             *
             * UROVEN ztlumeni z ceho vzit nejde - stare nastaveni
             * zadnou nemelo, displej zhasinal cely. Zustane tedy ta
             * vychozi z ui_profile_default(), tedy nejnizsi nabizena
             * (BL_MIN = 25 %): je to nejblizsi citelny protejsek tmy a
             * zaroven to sjednocuje desku po migraci s deskou po resetu
             * nastaveni, takze se obe chovaji stejne. */
            prof_key(k, sizeof(k), i, "bk");
            uint32_t blank = prefs.getUInt(k, cfg.prof[i].dim_s);
            prof_key(k, sizeof(k), i, "dt");
            cfg.prof[i].dim_s = prefs.getUInt(k, blank);

            prof_key(k, sizeof(k), i, "db");
            cfg.prof[i].dim_bright = prefs.getInt(k, cfg.prof[i].dim_bright);
            prof_key(k, sizeof(k), i, "pf");
            cfg.prof[i].perf = prefs.getBool(k, cfg.prof[i].perf);
        }
    }
    prefs.end();

    g_board = new Board();
    g_board->init();
    if (!g_board->begin()) { Serial.println("CHYBA: board->begin()"); return; }
    Serial.println("Panel OK");
    if (!lvgl_port_init(g_board->getLCD(), g_board->getTouch())) {
        Serial.println("CHYBA: lvgl_port_init"); return;
    }
    Serial.println("LVGL OK");

    /* Musi byt pred ui_build(): ta uz posila ulozenou uroven jasu. */
    if (!bl_pwm_init()) UI_HOST.bl_set_percent = nullptr;

    /* CAN se rozjizdi v listen-only: radic nevysila ani potvrzovaci bit,
     * takze ani spatna rychlost nebo prohozene piny nemuzou rozhodit
     * provozni sbernici vozu. Do normalniho rezimu se prepina az vedome
     * pres can_set_mode(). Kdyz profil desky nedosadil piny, can_start()
     * nic nenahodi a rekne to do logu - firmware tim nepadne, jen nema
     * CAN. */
    {
        can_cfg_t can;
        can_cfg_default(&can);
        Serial.printf("CAN: %s\n", can_start(&can) ? "listen-only" : "nejede");
    }

    /* Vrstva diagnostiky. Zacina odzbrojena - radic je v listen-only a
     * diag_pump() ji ozbroji az ve chvili, kdy uzivatel rezim prepne. */
    {
        diag_poller_cfg_t dc;
        diag_poller_cfg_default(&dc);
        dc.send = can_isotp_send;
        diag_poller_init(&g_diag, &dc);
    }

#if CANOGRAF_MOD_WEB
    /* Volba prezila restart. Port zustava zavreny, dokud ho nekdo
     * nezapnul - a kdo ho zapnul, ceka, ze po restartu zase nabehne.
     * Az za diag_poller_init(): bez vrstvy diagnostiky nema server co
     * ukazovat a net_server_start() by to odmitl. */
    prefs.begin("talento", true);
    bool want_web = prefs.getBool("webon", false);
    prefs.end();
#endif

    saved_load();

    /* Rezim WiFi prezil restart. Kdo nechal desku jako pristupovy bod,
     * stoji u ni s telefonem a ceka, ze se po restartu zase objevi -
     * navrat do rezimu stanice by ho odstrihl a on by nevedel proc. */
    prefs.begin("talento", true);
    bool want_ap = prefs.getBool("apon", false);
    prefs.end();

    if (want_ap) {
        ap_apply(true);
    } else {
        WiFi.mode(WIFI_STA);
        if (saved_n) {
            WiFi.begin(saved_ssid[0], saved_pass[0]);
            Serial.printf("WiFi: zkousim %s (ulozenych siti %d)\n",
                          saved_ssid[0], saved_n);
        }
    }
#if CANOGRAF_MOD_WEB
    if (want_web) web_start();
#endif

    ArduinoOTA.setHostname("talento-hmi");
    ArduinoOTA.begin();

    lvgl_port_lock(-1);
    ui_set_host(&UI_HOST);
    ui_set_diag(&g_diag);
    ui_set_cfg(&cfg);
    ui_build();
    lv_timer_create(tick_cb, 20, NULL);
    lvgl_port_unlock();

    Serial.printf("UI hotovo. Heap %u kB, PSRAM %u kB\n",
                  host_heap_kb(), host_psram_kb());
}

void loop()
{
    ArduinoOTA.handle();
    /* Jediny zapisovatel do kruhoveho zasobniku ramcu, zaroven jediny,
     * kdo dava cas vrstve diagnostiky. UI z zasobniku cte svym vlastnim
     * kurzorem, viz src/can/twai.h.
     *
     * Rozestup 20 ms je pod mezi ISO-TP (150 ms) s rezervou skoro
     * desetinasobnou a zaroven je to perioda lv_timeru, takze se obe
     * pravidelne veci v teto smycce toci stejne rychle. */
    diag_pump();
#if CANOGRAF_MOD_WEB
    /* Zaznam relace. Je to TATAZ smycka, ktera krmi diag_poller_poll(),
     * takze do kruhoveho zasobniku zapisuje jediny pisar; obsluha
     * stahovani z nej jen cte. Rozbor je u weblog_t v net/report.h.
     * Vlastni vyrizovani pozadavku bezi v uloze serveru, tady se nedeje. */
    net_server_tick(can_now_ms());
#endif
    delay(20);
}
