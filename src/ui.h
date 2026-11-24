/*
 * ui.h - rozhrani uzivatelskeho rozhrani Talento HMI
 *
 * Tenhle kod zna jen LVGL. Nesmi zaviset na Arduino.h, WiFi.h ani na
 * ESP32_Display_Panel, aby sel prelozit i na hostiteli (viz sim/).
 * Vse, co umi jen deska, se predava pres ui_host_t. Simulator necha
 * ukazatele NULL a ui.c to musi snest.
 *
 * Pozn.: vestaveny Montserrat nema ceskou diakritiku -> texty bez hacku.
 */
#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* podsviceni: tvrde zap/vyp */
    void (*backlight)(bool on);

    /* trvale ulozeni nastaveni (NVS) */
    void (*save_cfg)(bool mph, bool fahrenheit, uint32_t blank_s);

    /* WiFi */
    void        (*wifi_scan_start)(void);
    int         (*wifi_scan_result)(void);  /* <0 jeste bezi, jinak pocet siti */
    const char *(*wifi_scan_ssid)(int i);
    void        (*wifi_connect)(const char *ssid, const char *pass);
    bool        (*wifi_connected)(void);
    const char *(*wifi_ssid)(void);         /* "" kdyz nepripojeno */
    const char *(*wifi_ip)(void);           /* "" kdyz nepripojeno */

    /* diagnostika */
    unsigned (*heap_kb)(void);
    unsigned (*psram_kb)(void);

    /* pokus se softwarovym PWM podsviceni */
    uint32_t (*bl_measure_toggle_us)(void);
    void     (*bl_pwm_set)(int duty, int period_ms);  /* duty 0..10 */
    void     (*bl_pwm_stop)(void);
    bool     (*bl_pwm_running)(void);
} ui_host_t;

/* Ukazatel si UI jen pamatuje, struktura musi prezit cely beh. */
void ui_set_host(const ui_host_t *h);

/* Nastaveni nactene z NVS; volat pred ui_build(). */
void ui_set_cfg(bool mph, bool fahrenheit, uint32_t blank_s);

void        ui_build(void);       /* postavi celou UI do lv_scr_act() */
void        ui_tick(void);        /* jeden krok obnovy, volat ~50 Hz */
int         ui_page_count(void);
void        ui_show_page(int i);
const char *ui_page_name(int i);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
