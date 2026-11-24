/*
 * ui.h - rozhrani uzivatelskeho rozhrani Talento HMI
 *
 * Tenhle kod zna jen LVGL. Nesmi zaviset na Arduino.h, WiFi.h ani na
 * ESP32_Display_Panel, aby sel prelozit i na hostiteli (viz sim/).
 * Vse, co umi jen deska, se predava pres ui_host_t. Simulator necha
 * ukazatele NULL a ui.c to musi snest.
 *
 * Pozn.: fonty UI maji ceskou i nemeckou diakritiku, viz src/font_ui.h.
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

    /* trvale ulozeni nastaveni (NVS); lang je index jazyka z i18n.c */
    void (*save_cfg)(bool mph, bool fahrenheit, uint32_t blank_s, int lang);

    /* WiFi */
    void        (*wifi_scan_start)(void);
    int         (*wifi_scan_result)(void);  /* <0 jeste bezi, jinak pocet siti */
    const char *(*wifi_scan_ssid)(int i);
    void        (*wifi_connect)(const char *ssid, const char *pass);
    bool        (*wifi_connected)(void);
    const char *(*wifi_ssid)(void);         /* "" kdyz nepripojeno */
    const char *(*wifi_ip)(void);           /* "" kdyz nepripojeno */

    /* Zapamatovane site. Hesla drzi deska, UI je nikdy nevidi - jen se
     * pta, jestli uz sit zna, a rekne, kterou pouzit nebo zapomenout.
     * wifi_connect() ulozeni obstara sam, takze dalsi funkce na to neni. */
    bool (*wifi_known)(const char *ssid);          /* uz ulozena? */
    bool (*wifi_connect_known)(const char *ssid);  /* pripojit ulozenym heslem */
    void (*wifi_forget)(const char *ssid);         /* smazat ze seznamu */

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

/* Nastaveni nactene z NVS; volat pred ui_build(). Neplatny lang spadne
 * na vychozi jazyk (0 = English). */
void ui_set_cfg(bool mph, bool fahrenheit, uint32_t blank_s, int lang);

void        ui_build(void);       /* postavi celou UI do lv_scr_act() */
void        ui_tick(void);        /* jeden krok obnovy, volat ~50 Hz */
int         ui_page_count(void);
void        ui_show_page(int i);
const char *ui_page_name(int i);

/* ---------------------------------------------------------------------
 * Stav regenerace DPF.
 *
 * Kresli se na pravy sloupec, ktery je videt na kazde strance. Dokud
 * neni CAN, prepina stav rucne stranka DPF; az bude, zavola tohle
 * diagnostika podle odpovedi jednotky (31 03 02 0D status, DID 2487).
 * percent < 0 znamena, ze jednotka postup nehlasi - UI si ho nevymysli
 * a ubyva po hrubych krocich, aby bylo poznat, ze je to odhad.
 * ------------------------------------------------------------------- */
typedef enum {
    UI_REGEN_OFF = 0,       /* nic nebezi */
    UI_REGEN_REQUESTED,     /* odeslano, jednotka jeste nepotvrdila */
    UI_REGEN_ACTIVE,        /* regenerace bezi */
} ui_regen_t;

void ui_set_regen_state(ui_regen_t state, int percent);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
