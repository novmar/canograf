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

/* ---------------- WiFi ---------------- */
/* LVGL dostava jen const char*, takze retezce z Arduino String musi prezit
 * navrat z funkce - proto staticke kopie. */
static char scan_ssid_buf[33];
static char conn_ssid_buf[33];
static char ip_buf[20];

static void host_wifi_scan_start(void)
{
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
}

static int host_wifi_scan_result(void) { return WiFi.scanComplete(); }

static const char *host_wifi_scan_ssid(int i)
{
    snprintf(scan_ssid_buf, sizeof(scan_ssid_buf), "%s", WiFi.SSID(i).c_str());
    return scan_ssid_buf;
}

static void host_wifi_connect(const char *ssid, const char *pass)
{
    prefs.begin("talento", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
    WiFi.begin(ssid, pass);
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
    delay(20);
}
