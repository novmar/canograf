/*
 * Talento HMI - testovaci firmware v3
 * Waveshare ESP32-S3-Touch-LCD-4.3B, 800x480, GT911 (kapacitni), CH422G
 *
 * JESTE NECTE VOZIDLO - hodnoty jsou simulovane.
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

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
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

/* ---------------- nastaveni v NVS ---------------- */
static void host_save_cfg(const ui_cfg_t *c)
{
    prefs.begin("talento", false);
    prefs.putBool("mph", c->mph);
    prefs.putBool("fahr", c->fahrenheit);
    prefs.putUInt("blank", c->blank_s);
    prefs.putInt("lang", c->lang);
    prefs.putInt("blday", c->bright_day);
    prefs.putInt("blnight", c->bright_night);
    prefs.putBool("blnight_on", c->night);
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

static void host_wifi_scan_start(void)
{
    /* Sken se nerozjede, kdyz radio neni v rezimu STA nebo kdyz prave
     * bezi nedokonceny pokus o pripojeni - esp_wifi_scan_start v tom
     * stavu vrati chybu a scanComplete() pak hlasi -2 az do smrti.
     * Fungujici spojeni se nerusi, pri nem sken na ESP32 projde. */
    WiFi.mode(WIFI_STA);
    if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false, false);
    WiFi.scanDelete();
    int16_t r = WiFi.scanNetworks(true);
    if (r == WIFI_SCAN_FAILED) Serial.println("WiFi: sken se nerozjel");
}

/* -1 jeste bezi, -2 nerozjelo se, jinak pocet siti */
static int host_wifi_scan_result(void) { return WiFi.scanComplete(); }

static const char *host_wifi_scan_ssid(int i)
{
    snprintf(scan_ssid_buf, sizeof(scan_ssid_buf), "%s", WiFi.SSID(i).c_str());
    return scan_ssid_buf;
}

static void host_wifi_connect(const char *ssid, const char *pass)
{
    saved_put(ssid, pass);
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

/* ---------------- diagnostika ---------------- */
static unsigned host_heap_kb(void)  { return (unsigned)(ESP.getFreeHeap() / 1024); }
static unsigned host_psram_kb(void) { return (unsigned)(ESP.getFreePsram() / 1024); }

/* Neni const: kdyz se PWM nerozjede, musi se bl_set_percent za behu
 * vynulovat, at UI stmivani nenabizi. */
static ui_host_t UI_HOST = {
    host_backlight,
#if CANOGRAF_BL_PWM_PIN >= 0
    host_bl_percent,
#else
    nullptr,                /* bez dratku na plosce BL se stmivat neda */
#endif
    host_save_cfg,
    host_wifi_scan_start,
    host_wifi_scan_result,
    host_wifi_scan_ssid,
    host_wifi_connect,
    host_wifi_connected,
    host_wifi_ssid,
    host_wifi_ip,
    host_wifi_known,
    host_wifi_connect_known,
    host_wifi_forget,
    host_heap_kb,
    host_psram_kb,
};

/* ---------------- obnovovani ---------------- */
static void tick_cb(lv_timer_t *t) { (void)t; ui_tick(); }

/* ---------------- start ---------------- */
void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("\nTalento HMI v5 - start");

    prefs.begin("talento", true);
    ui_cfg_t cfg;
    cfg.mph = prefs.getBool("mph", false);
    cfg.fahrenheit = prefs.getBool("fahr", false);
    cfg.blank_s = prefs.getUInt("blank", 0);
    int def_lang = i18n_find(CANOGRAF_DEFAULT_LANG);
    if (def_lang < 0) def_lang = 0;
    cfg.lang = prefs.getInt("lang", def_lang);
    /* Po prvnim startu: ve dne naplno, v noci na tretinu. Nocni rezim
     * zacina vypnuty - zapnout ho musi clovek, hodiny nikdo nenastavil. */
    cfg.bright_day = prefs.getInt("blday", 100);
    cfg.bright_night = prefs.getInt("blnight", 35);
    cfg.night = prefs.getBool("blnight_on", false);
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

    WiFi.mode(WIFI_STA);
    saved_load();
    if (saved_n) {
        WiFi.begin(saved_ssid[0], saved_pass[0]);
        Serial.printf("WiFi: zkousim %s (ulozenych siti %d)\n",
                      saved_ssid[0], saved_n);
    }
    ArduinoOTA.setHostname("talento-hmi");
    ArduinoOTA.begin();

    lvgl_port_lock(-1);
    ui_set_host(&UI_HOST);
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
    /* Jediny zapisovatel do kruhoveho zasobniku ramcu. UI z nej cte
     * svym vlastnim kurzorem, viz src/can/twai.h. */
    can_poll();
    delay(20);
}
