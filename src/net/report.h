/*
 * report.h - co server rika o vozidle, bez jedineho slibu navic
 *
 * Tohle je OBSAH odpovedi serveru. Vlastni HTTP, WiFi a hardware jsou
 * vedle v net/server.h; tady je jen cisty C nad diag/poller.h, takze se
 * to prelozi i na hostiteli a da se na tom merit, co server opravdu
 * posila (sim/lv_shot.c, faze phase_web).
 *
 * To rozdeleni neni kosmetika. Dve veci, ktere musi platit, se jinak
 * nedaji zkontrolovat nikde jinde nez na desce, a tu nemame:
 *
 *   1. CTYRI STAVY HODNOTY PLATI I NA WEBU. Chybejici hodnota je
 *      pomlcka, ne nula - uplne stejne jako na displeji. Vykresleni
 *      textu proto dela TAHLE vrstva, ne prohlizec: v odpovedi uz je
 *      hotovy retezec a stav k nemu. Prohlizec nema z ceho udelat
 *      cislo, protoze zadne nedostal.
 *   2. HESLO K SITI SE DO ODPOVEDI NEDOSTANE. report_net_t zadnou
 *      polozku na heslo NEMA a mit nebude; server dostava jen tuhle
 *      strukturu, takze heslo nema odkud vzit. Je to stavba, ne
 *      opatrnost - opatrnost se da zapomenout.
 *
 * CTENI, NIC JINEHO. Tahle vrstva nezna can/twai.h a nesmi ho poznat
 * ani pres nekoho jineho; kdyby se to stalo, zastavi preklad #error
 * nize. Zadna funkce odsud nic nevysila a vysilat nema cim.
 *
 * Zavislosti: stdbool, stdint, stdio, string, diag/poller.h, i18n.h.
 */
#ifndef NET_REPORT_H
#define NET_REPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "diag/poller.h"

/* Pojistka, ne komentar: net/report.c nesmi mit po ruce nic, cim se da
 * na sbernici zapsat - can_send() a can_isotp_send() jsou prave v
 * can/twai.h. Hlida to prekladac, ne revize, a stoji to v report.c za
 * includy. Nemuze to stat TADY: tenhle soubor cte i hostitelsky build
 * (sim/lv_shot.c) a deska (src/main.cpp), a ti oba can/twai.h znat
 * SMI - jsou to prave oni, kdo sbernici obsluhuje. Jde o to, ze se k
 * nemu nedostane vrstva, ktera stavi odpovedi serveru. */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * Skupiny
 *
 * Stejne deleni, jake ma displej, protoze z nej plyne i to, co vrstva
 * diagnostiky prave cte: pohled (diag_view_t) je vazany na stranku,
 * ktera je videt. Na webu jsou videt vsechny skupiny najednou, ale
 * ziva je ta, kterou ma deska prave na obrazovce - ostatni zestarnou.
 * Neni to vada prenosu, je to pravda o tom, na co se deska pta, a
 * stranka to rika slovem (STR_WEB_VIEW_NOTE).
 * ------------------------------------------------------------------ */
typedef enum {
    REPORT_GRP_MAIN = 0,
    REPORT_GRP_ENGINE,
    REPORT_GRP_DPF,
    REPORT_GRP_BATT,
    REPORT_GRP__COUNT
} report_grp_t;

/* ------------------------------------------------------------------
 * Jeden radek tabulky velicin
 *
 * Popisky se berou z i18n, a to pokud mozno TY STEJNE, ktere ukazuje
 * displej - web pak nemuze rict neco jineho nez panel. Kde displej
 * zadny prelozeny popisek nema (RAIL, MAF, VNT, EGR jsou zkratky
 * stejne ve vsech jazycich a v ui.c jsou take natvrdo), stoji tu
 * literal v lit a name je -1.
 * ------------------------------------------------------------------ */
typedef struct {
    uint8_t     ch;      /* diag_ch_t */
    uint8_t     grp;     /* report_grp_t */
    uint8_t     dec;     /* desetinnych mist */
    bool        sp;      /* zadana hodnota - popisek dostane T(STR_DEMAND) */
    const char *key;     /* ASCII: klic v JSON a nazev sloupce v CSV */
    const char *lit;     /* popisek natvrdo, plati pri name < 0 */
    int16_t     name;    /* STR_* z i18n.h, nebo -1 */
    const char *unit;    /* ASCII; "" kdyz uz jednotka v popisku je */
} report_ch_t;

#define REPORT_CH_N 27

int                report_ch_count(void);
const report_ch_t *report_ch(int i);            /* NULL mimo rozsah */

/* Prelozeny popisek veliciny i s jednotkou, tak jak se ukaze na webu. */
void report_caption(char *out, int n, int i);

/* Prelozeny nazev skupiny. Mimo rozsah vraci "?". */
const char *report_grp_name(int g);

/* ------------------------------------------------------------------
 * Jedna hodnota, uz vykreslena
 *
 * txt je to, co se ma ukazat, a plati pro nej uplne stejne pravidlo
 * jako na displeji:
 *
 *   DIAG_SHOW_NONE     "--"       zadne cislo, protoze zadne neni
 *   DIAG_SHOW_REFUSED  "7F 31"    jednotka ten udaj odmitla
 *   DIAG_SHOW_LIVE     "1832"     cerstve precteno
 *   DIAG_SHOW_STALE    "1832"     tataz hodnota, ale uz stara - pozna
 *                                 se podle show a age_ms, ne podle textu
 * ------------------------------------------------------------------ */
#define REPORT_NONE_TXT "--"

typedef struct {
    uint8_t  show;      /* diag_show_t */
    uint8_t  nrc;       /* plati pri DIAG_SHOW_REFUSED */
    uint32_t age_ms;    /* UINT32_MAX = jeste nikdy neprislo */
    char     txt[16];
} report_val_t;

void report_value(report_val_t *out, const diag_poller_t *p, int i);

/* ------------------------------------------------------------------
 * Stav, ktery server o sobe rekne
 *
 * HESLO TU NENI A NEBUDE. Server nema zadnou jinou cestu k udajum o
 * siti nez tuhle strukturu, takze se heslo do odpovedi nema jak
 * dostat. Kdo sem polozku na heslo prida, rozbije kontrolu
 * "heslo k siti neni v zadne odpovedi serveru" v sim/lv_shot.c.
 * ------------------------------------------------------------------ */
typedef struct {
    bool     ap;           /* deska je pristupovy bod */
    bool     sta;          /* deska je pripojena k cizi siti */
    char     ssid[33];     /* jmeno site, ktera prave plati */
    char     ip[20];
    bool     can_normal;   /* radic vysila, tedy se vubec smime ptat */
    uint32_t uptime_s;
    int      clients;      /* pripojenych zarizeni v rezimu AP; -1 = nevim */
} report_net_t;

/* Ctyri urovne pripojeni k vozidlu, jako STR_LINK_*. Je to ta sama
 * jedina funkce, kterou pouziva i horni lista displeje (ui.c,
 * link_state_paint), takze se web a panel nemuzou rozejit. */
int report_link_str(const diag_poller_t *p);

/* Rika horni lista webu "vuz pripojeny neni"? Prazdna pole bez tehle
 * vety vypadaji jako rozbity server. */
bool report_no_vehicle(const diag_poller_t *p);

/* ------------------------------------------------------------------
 * Zaznam relace
 *
 * Kruhovy zasobnik vzorku. Pamet dodava volajici - na desce je to
 * PSRAM, na hostiteli obycejne pole - protoze tahle vrstva nesmi vedet
 * nic o tom, jaka pamet na desce je.
 *
 * Zapisuje JEDINY volajici (hlavni smycka, ta sama, ktera krmi
 * diag_poller_poll), cte ho obsluha stahovani v uloze serveru. Jine
 * jisteni tu neni a nepotrebuje: pri periode 500 ms a kapacite na
 * hodinu je mezi hlavou a patou zasobniku 7200 radku, kdezto stazeni
 * trva vteriny. Kolik radku uz zasobnik prepsal, rika dropped - tise
 * zmizely zacatek jizdy by byl presne ta lez, kterou tahle aplikace
 * delat nesmi, takze to CSV nahore rekne.
 * ------------------------------------------------------------------ */
typedef struct {
    uint32_t t_ms;
    float    val[REPORT_CH_N];
    uint8_t  show[REPORT_CH_N];
} weblog_row_t;

typedef struct {
    weblog_row_t *row;
    uint32_t      cap;
    uint32_t      head;        /* kolik radku uz vzniklo celkem */
    uint32_t      dropped;     /* kolik jich zasobnik prepsal */
    uint32_t      period_ms;
    uint32_t      t_next;
    uint32_t      t0_ms;       /* cas prvniho vzorku, od nej se pocita t_s */
    bool          started;
} weblog_t;

void weblog_init(weblog_t *w, weblog_row_t *store, uint32_t cap,
                 uint32_t period_ms);

/* Jeden krok. Vzorek vznikne, jen kdyz uz uplynula perioda. */
void weblog_sample(weblog_t *w, const diag_poller_t *p, uint32_t now_ms);

/* Kolik radku jde prave ted precist a jake je nejnizsi platne poradi. */
uint32_t weblog_rows(const weblog_t *w);
uint32_t weblog_first(const weblog_t *w);

/* Radek podle ABSOLUTNIHO poradi (weblog_first .. head-1). NULL, kdyz
 * uz v zasobniku neni. */
const weblog_row_t *weblog_at(const weblog_t *w, uint32_t seq);

/* ------------------------------------------------------------------
 * Odpovedi
 *
 * Vraci pocet zapsanych bajtu, nebo -1, kdyz se odpoved do bufferu
 * nevesla. Oriznuta odpoved se neposila: polovicni JSON je horsi nez
 * zadny, protoze prohlizec ukaze stara cisla a tvari se, ze jsou nova.
 * ------------------------------------------------------------------ */

/* Popisky, jednotky a skupiny. Stahuje se jednou pri otevreni stranky,
 * takze se prelozene texty neposilaji dvakrat za vterinu. */
int report_meta_json(char *out, int n);

/* Hodnoty. Tohle chodi periodicky a je zamerne male. */
int report_live_json(char *out, int n, const diag_poller_t *p,
                     const report_net_t *net, const weblog_t *log);

/* CSV: hlavicka souboru (dva radky komentare a radek nazvu sloupcu) a
 * jeden radek dat. Kazda velicina ma dva sloupce - hodnotu a stav -
 * takze se ctyri stavy prenesou i do souboru. Chybejici hodnota ma
 * sloupec PRAZDNY; nula by v tabulce lhala. */
int report_csv_head(char *out, int n, const weblog_t *w);
int report_csv_row(char *out, int n, const weblog_t *w, uint32_t seq);

#ifdef __cplusplus
}
#endif

#endif /* NET_REPORT_H */
