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

/* ---------------- podsviceni ---------------- */
static void bl_raw(bool on)
{
    if (!g_board) return;
    auto bl = g_board->getBacklight();
    if (bl) on ? bl->on() : bl->off();
}

static void host_backlight(bool on) { bl_raw(on); }

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
static uint32_t host_measure_toggle(void)
{
    uint32_t t0 = micros();
    for (int i = 0; i < 20; i++) { bl_raw(true); bl_raw(false); }
    bl_toggle_us = (micros() - t0) / 40;
    bl_raw(true);
    Serial.printf("prepnuti podsviceni: %u us\n", (unsigned)bl_toggle_us);
    return bl_toggle_us;
}

static void host_pwm_set(int duty, int period_ms)
{
    pwm_duty = duty;
    pwm_period_ms = period_ms;
    if (!pwm_task_h)
        xTaskCreatePinnedToCore(pwm_task, "blpwm", 2048, NULL, 1, &pwm_task_h, 0);
}

static void host_pwm_stop(void)
{
    if (pwm_task_h) { vTaskDelete(pwm_task_h); pwm_task_h = NULL; }
    pwm_duty = 10;
    bl_raw(true);
}

static bool host_pwm_running(void) { return pwm_task_h != NULL; }

/* ---------------- nastaveni v NVS ---------------- */
static void host_save_cfg(bool mph, bool fahrenheit, uint32_t blank_s, int lang)
{
    prefs.begin("talento", false);
    prefs.putBool("mph", mph);
    prefs.putBool("fahr", fahrenheit);
    prefs.putUInt("blank", blank_s);
    prefs.putInt("lang", lang);
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

static const ui_host_t UI_HOST = {
    host_backlight,
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
    host_measure_toggle,
    host_pwm_set,
    host_pwm_stop,
    host_pwm_running,
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
    bool mph = prefs.getBool("mph", false);
    bool fahr = prefs.getBool("fahr", false);
    uint32_t blank_s = prefs.getUInt("blank", 0);
    int def_lang = i18n_find(CANOGRAF_DEFAULT_LANG);
    if (def_lang < 0) def_lang = 0;
    int lang = prefs.getInt("lang", def_lang);
    prefs.end();

    g_board = new Board();
    g_board->init();
    if (!g_board->begin()) { Serial.println("CHYBA: board->begin()"); return; }
    Serial.println("Panel OK");
    if (!lvgl_port_init(g_board->getLCD(), g_board->getTouch())) {
        Serial.println("CHYBA: lvgl_port_init"); return;
    }
    Serial.println("LVGL OK");

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
    ui_set_cfg(mph, fahr, blank_s, lang);
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
