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

/* ---------------------------------------------------------------------
 * Nastaveni, ktere prezije restart. Deska ho nacte z NVS a preda sem
 * pred ui_build(); kdykoliv ho uzivatel zmeni, posle ho UI zpatky pres
 * ui_host_t.save_cfg.
 *
 * Jas je ulozeny dvakrat, zvlast pro den a zvlast pro noc, protoze mezi
 * nimi se prepina za jizdy a hledat pokazde spravnou uroven by bylo
 * horsi nez oslneni samotne. Automaticke prepinani podle casu tu zamerne
 * neni - RTC sice na desce je, ale cas v nem nikdo nenastavil.
 * ------------------------------------------------------------------- */
typedef struct {
    bool     mph;           /* false = km/h */
    bool     fahrenheit;    /* false = C */
    uint32_t blank_s;       /* zhasnout po tolika vterinach; 0 = nikdy */
    int      lang;          /* index jazyka z i18n.c, 0 = English */
    int      bright_day;    /* jas 0..100 % ve dne */
    int      bright_night;  /* jas 0..100 % v noci */
    bool     night;         /* prave plati nocni uroven */
    /* Rychly prepinac den/noc v horni liste. Volitelny, protoze lista je
     * uzka; zapina se v dialogu jasu. Vychozi je vypnuty. */
    bool     topbar_dn;
} ui_cfg_t;

typedef struct {
    /* Podsviceni: tvrde zap/vyp pres EXIO2. Vypina i samotny panel,
     * takze je to hlavni vypinac a jedina cesta k uplne tme. */
    void (*backlight)(bool on);

    /* Plynule stmivani, 0..100 % jasu. NULL znamena, ze deska stmivat
     * neumi - UI pak stmivani vubec nenabidne a netvari se, ze jde.
     * Panel tim nezhasina, od toho je backlight(). */
    void (*bl_set_percent)(int percent);

    /* trvale ulozeni nastaveni (NVS) */
    void (*save_cfg)(const ui_cfg_t *c);

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
} ui_host_t;

/* Ukazatel si UI jen pamatuje, struktura musi prezit cely beh. */
void ui_set_host(const ui_host_t *h);

/* Nastaveni nactene z NVS; volat pred ui_build(). Neplatny lang spadne
 * na vychozi jazyk (0 = English), jas mimo rozsah se oreze. */
void ui_set_cfg(const ui_cfg_t *c);

/* ---------------------------------------------------------------------
 * Prevod jasu v procentech na stridu PWM pro plosku BL.
 *
 * Dve veci, ktere se tu daji tise rozbit, a proto maji vlastni funkci
 * a kontrolu v sim/lv_shot.c:
 *
 *   1. POLARITA JE OBRACENA. Napeti na plosce BL ubira proud LEDkam:
 *      0 V = plny jas, 3,3 V = tma. Vetsi jas tedy znamena MENSI stridu.
 *   2. Prepocet neni linearni, viz komentar u implementace v ui.c.
 *
 * percent se oreze na 0..100, vraci 0..duty_max.
 * ------------------------------------------------------------------- */
uint32_t ui_bl_duty(int percent, uint32_t duty_max);

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
