/*
 * elm.h - emulace ELM327 na TCP portu: cizi diagnosticky nastroj
 *         pouzije desku jako rozhrani k vozu
 *
 * Je v tom ironie, ktera patri na zacatek, protoze bez ni se tenhle
 * soubor cte spatne. Cely projekt vznikl proto, ze ELM327 je uzke
 * hrdlo: sedm datovych bajtu na ramec, viceramcovou zpravu si musi
 * klient skladat sam a seriova linka jede 38400 Bd. Vycitani pres nej
 * davalo 3,3 vzorku za vterinu na VSECHNY veliciny dohromady
 * (docs/findings.md). Emulovat ho proto, aby se pripojil CIZI nastroj,
 * je neco uplne jineho nez pres nej cist: tady jsme MY ten rychly
 * konec. Nastroj mluvi pomalym protokolem, ktery zna, a dostava od nas
 * neco lepsiho, nez cekal - slozene zpravy a ISO-TP, ktere skutecny
 * dongle neumi.
 *
 * ---------------------------------------------------------------
 * Cista logika, cas parametrem
 * ---------------------------------------------------------------
 * Stejny vzor jako can/isotp.h: zadne sokety, zadny hardware, zadne
 * hodiny. Dovnitr jdou bajty od klienta a ramce ze sbernice, ven jdou
 * bajty pro klienta; cas chodi v parametru. Diky tomu se cely stavovy
 * automat testuje na hostiteli bez desky a bez vozidla, viz
 * test/elm_test.c.
 *
 * Soket je az druha, mnohem tencejsi vrstva na konci tohoto souboru
 * (elm_net_*), prelozi se jen na desce a nedela nic jineho, nez ze
 * preleva bajty mezi soketem a timhle automatem.
 *
 *     elm_cfg_t c; elm_cfg_default(&c);
 *     c.tx_id = 0x7E0; c.rx_id = 0x7E8; c.send = can_isotp_send;
 *     elm_init(&e, &c);
 *     ... v cyklu:
 *     elm_rx(&e, buf, n, now);                      // co prislo od klienta
 *     elm_on_frame(&e, id, d, dlc, now);            // kazdy ramec ze sbernice
 *     elm_poll(&e, now);                            // casovace a dokonceni
 *     n = elm_take(&e, out, sizeof out);            // co poslat klientovi
 *
 * ---------------------------------------------------------------
 * JEN CTENI. Filtr je podle SLUZBY, ne podle toho, co prijde
 * ---------------------------------------------------------------
 * Pravidlo projektu pro vzdalene konce (docs/safety.md) plati i tady,
 * a je tu prisnejsi nez u serveru: server do vozidla zapsat NEMA cim,
 * kdezto emulace ELM327 z podstaty veci posila do jednotek to, co jim
 * rekne klient. Proto je mezi klientem a sbernici jedine misto, kterym
 * se da projit - elm_service_allowed() - a to propousti VYJMENOVANE
 * cteci sluzby:
 *
 *     01  OBD-II current data
 *     09  OBD-II vehicle information
 *     10  diagnostic session control, jen podfunkce 01 / 03 / C0
 *     19  read DTC information
 *     21  read data by local identifier (stary Renault)
 *     22  read data by identifier
 *     3E  tester present
 *
 * Vse ostatni se odmita, at uz to vypada jakkoliv: 2E a 3B (zapis
 * hodnoty), 14 (mazani pameti chyb), 31 (routine, tedy i regenerace),
 * 11 (reset jednotky), 27 (security access), 34/36/37 (nahravani
 * programu). Odmitnuti je SLOVO navic v odpovedi, ne ticho - viz
 * ELM_RSP_BLOCKED.
 *
 * Proc podle sluzby a ne podle obsahu: obsah je v rukou klienta a
 * prilezitosti ho prestrojit je nekonecne mnoho (jina hlavicka pres
 * ATSH, jiny vuz, jiny vyrobce, zapis schovany za neznamou sluzbou).
 * Seznam povolenych sluzeb je naopak kratky, uzavreny a da se precist
 * na jednu obrazovku. Kdyz nekdo prida sluzbu, kterou nezname, odpoved
 * je "ne" - bezpecny vychozi stav je odmitnuti, ne propusteni.
 *
 * Druha pojistka je rezim radice. V CAN_MODE_LISTEN_ONLY neodejde z
 * desky ani potvrzovaci bit, takze emulace nema jak cokoliv poslat.
 * Hlida se to DVAKRAT a nezavisle: cfg.tx_allowed() se ptame drive,
 * nez se pozadavek vubec zacne skladat, a can_send() ho v listen-only
 * odmitne jeste jednou. Tezko se to porucha obema zpusoby najednou.
 *
 * Treti pojistka je, ze tenhle soubor nezna can/twai.h: jedina cesta
 * na sbernici vede pres cfg.send, tedy pres ISO-TP, a tam uz je
 * pozadavek slozeny z bajtu, ktere proslo filtrem.
 *
 * ---------------------------------------------------------------
 * V cem se chovame JINAK nez skutecny ELM327 - a proc zamerne
 * ---------------------------------------------------------------
 * 1. VICERAMCOVOU ODPOVED SKLADAME MY. Skutecny v1.5 vypise ramce tak,
 *    jak prisly (`7E8 10 14 ...`, `7E8 21 ...`) a slozeni nechava na
 *    klientovi; proto po nem nastroje chteji `AT CAF0` a rucni
 *    ramcovani. My posleme jednu hotovou zpravu. Je to prave ta vec,
 *    kvuli ktere tenhle projekt vznikl, takze ji nebudeme simulovat
 *    zpatky. Klient dostane MIN prace, ne vic; nastroj, ktery umi
 *    precist odpoved ELM327, umi precist i tuhle.
 * 2. STROP CEKANI (AT ST) je ve vychozim stavu 1000 ms, ne 200 ms jako
 *    u ELM327. Dvesto milisekund je presne duvod, proc se po dongle
 *    musi volat `AT ST FF`: pomalejsi odpovedi tise mizi. Kdyz si
 *    nastroj o ST rekne, dostane presne to, co chtel.
 * 3. 7F xx 78 (responsePending) prodluzuje cekani podle ISO 15765,
 *    takze `19 02` s plnym seznamem chyb projde i bez AT ST FF.
 * 4. ATSH 7DF neni vychozi stav. Vychozi hlavicka je ta z konfigurace
 *    vozidla (CANOGRAF_ECU_TX_ID), tedy fyzicka adresa jednotky; klient
 *    si funkcni adresu muze nastavit.
 * 5. Napeti (ATRV) si NEVYMYSLIME. Deska zadny delic na prevodnik nema
 *    (docs/hardware), takze bud prijde cislo z jednotky pres
 *    cfg.volts(), nebo odpovime, ze ho nemame.
 * 6. Odmitnuta sluzba vraci ELM_RSP_BLOCKED misto toho, aby se poslala
 *    do vozu. Skutecny dongle posle cokoliv.
 * 7. Jen 11bitove identifikatory a jedna sbernice. ATSP6 ano, zbytek
 *    protokolu ne - deska ma jeden radic TWAI.
 * 8. ATFCSH se prijme a pamatuje, ale nase flow control odchazi vzdy na
 *    hlavicce pozadavku - spoj ISO-TP zna jednu adresu pro oba smery.
 *    Nastroje ho nastavuji na tutez hodnotu jako ATSH, takze se to
 *    projevi jen tomu, kdo si vymysli jinou.
 * 9. V ATCAF0 (nastroj si ramce sklada sam) propoustime JEN single
 *    frame. Rucne slozeny first frame by znamenal, ze si klient posle
 *    libovolne dlouhy obsah po kouscich a filtr sluzeb by videl jen
 *    jeho prvni kousek.
 *
 * Co se naopak drzi presne: echo (ATE), konce radku (ATL), mezery
 * (ATS), hlavicky (ATH), vyzva '>' po kazde odpovedi, prazdny radek
 * opakuje posledni prikaz, znak behem prenosu ho prerusi ("STOPPED").
 * Bez techhle drobnosti se nastroje nepripoji.
 */
#ifndef NET_ELM_H
#define NET_ELM_H

#include <stdbool.h>
#include <stdint.h>

#include "can/isotp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Port. 35000 je to, co maji sitove ELM327 adaptery (OBDLink, Vgate)
 * predvyplnene ve vsech nastrojich. */
#ifndef CANOGRAF_ELM_PORT
#define CANOGRAF_ELM_PORT 35000
#endif

/* Nejdelsi prikaz od klienta. Skutecny ELM327 ma vnitrni radek 48
 * znaku; osmdesat pobere i ramec v CAF0 zapsany s mezerami. Delsi radek
 * se zahodi a odpovi se '?', stejne jako na nesmysl. */
#define ELM_LINE_MAX 80

/* Odchozi text. Nejdelsi mozna odpoved je slozena zprava o ISOTP_BUF_SIZE
 * bajtech vypsana hexa s mezerami (3 znaky na bajt) plus hlavicka a
 * vyzva. Pri 512 bajtech je to 1536 znaku. */
#define ELM_TX_MAX (ISOTP_BUF_SIZE * 3 + 64)

/* Meze AT ST. Spodni je tam proto, aby si nastroj neumel nastavit
 * cekani kratsi, nez trva jedna obratka po sbernici; horni proto, aby
 * se port nedal zablokovat na minuty. */
#define ELM_ST_MIN_MS      50u
#define ELM_ST_MAX_MS   10000u
#define ELM_ST_DEFAULT_MS 1000u

/* Odpovedi, ktere nejsou daty z vozu. Vsechny jsou ASCII velkymi
 * pismeny jako u ELM327, takze je nastroj pozna jako stavovou hlasku. */
#define ELM_RSP_OK        "OK"
#define ELM_RSP_HUH       "?"            /* nerozumim */
#define ELM_RSP_NODATA    "NO DATA"      /* jednotka neodpovedela */
#define ELM_RSP_NOCONN    "UNABLE TO CONNECT" /* nesmime vysilat */
#define ELM_RSP_CANERR    "CAN ERROR"    /* prenos se rozpadl */
#define ELM_RSP_FULL      "BUFFER FULL"
#define ELM_RSP_STOPPED   "STOPPED"      /* znak behem prenosu */
#define ELM_RSP_BLOCKED   "WRITE PROTECTED" /* sluzba neprosla filtrem */

/* Co o sobe rekne. ATI musi vratit retezec, ktery nastroje poznaji,
 * jinak se nepripoji - takze tam ELM327 stoji. AT@1 je popis zarizeni
 * a tam uz stoji pravda, protoze to zadny nastroj nerozbije. */
#define ELM_ID   "ELM327 v1.5"
#define ELM_DESC "CANOGRAF ELM327 EMULATION"

typedef struct {
    uint32_t tx_id;      /* vychozi hlavicka pozadavku (CANOGRAF_ECU_TX_ID) */
    uint32_t rx_id;      /* odkud se ceka odpoved */
    uint32_t bitrate;    /* kvuli ATDP, at nelze o sbernici */

    /* Jedina cesta na sbernici: ramec ISO-TP. Povinne. */
    isotp_send_fn send;
    void         *user;

    /* Smime vubec vysilat? false = listen-only nebo necinna
     * diagnostika; pozadavek se pak ani nezacne skladat. NULL znamena
     * "smime" a je to tak jen pro testy transportu - firmware tohle
     * vyplnuje vzdy. */
    bool (*tx_allowed)(void *user);

    /* Napeti palubni site, kdyz ho prave mame z jednotky. false =
     * nemame, a pak to ATRV takhle i rekne. NULL = totez. */
    bool (*volts)(void *user, float *out);
} elm_cfg_t;

typedef struct {
    elm_cfg_t    cfg;
    isotp_link_t link;

    /* nastaveni, ktera meni AT prikazy */
    bool     echo;       /* ATE */
    bool     lf;         /* ATL */
    bool     spaces;     /* ATS */
    bool     headers;    /* ATH */
    bool     caf;        /* ATCAF, true = my skladame */
    bool     proto_auto; /* ATSP0 / ATSPA6 */
    uint8_t  at_mode;    /* ATAT, jen se pamatuje */
    uint16_t st_ms;      /* ATST */
    uint32_t hdr;        /* ATSH */
    uint32_t cra;        /* ATCRA; 0 = odvodit z hdr */
    uint32_t fc_sh;      /* ATFCSH */
    uint8_t  fc_data[8]; /* ATFCSD */
    uint8_t  fc_len;
    uint8_t  fc_mode;    /* ATFCSM */

    /* radek od klienta */
    char     line[ELM_LINE_MAX + 1];
    uint16_t line_len;
    bool     line_over;  /* radek pretekl, odpoved bude '?' */
    char     last[ELM_LINE_MAX + 1];  /* pro prazdny radek = opakuj */

    /* bezici pozadavek */
    bool     busy;
    uint32_t t_req;      /* kdy byl odeslan */
    bool     saw_frame;  /* uz prisel aspon jeden ramec odpovedi */

    /* odchozi text */
    char     tx[ELM_TX_MAX];
    uint16_t tx_len;
    bool     tx_over;

    /* mereni pro UI a pro log */
    uint32_t n_cmd;      /* prikazu celkem */
    uint32_t n_req;      /* pozadavku odeslanych do vozu */
    uint32_t n_blocked;  /* pozadavku zastavenych filtrem */
    uint32_t n_rsp;      /* odpovedi z vozu */
} elm_t;

/* Vychozi nastaveni: 7E0/7E8, 500 kbit/s, zadny transport. */
void elm_cfg_default(elm_cfg_t *cfg);

/* Vynuluje automat a prevezme nastaveni. */
void elm_init(elm_t *e, const elm_cfg_t *cfg);

/* ATZ, a totez se dela nad novym spojenim: vychozi stav prikazu,
 * zahozeni rozdelaneho prenosu a uvitaci retezec s vyzvou. */
void elm_reset(elm_t *e, uint32_t now_ms);

/* Bajty od klienta. Prikaz se vyridi, jakmile dojde CR. */
void elm_rx(elm_t *e, const char *in, uint16_t n, uint32_t now_ms);

/* Jeden ramec ze sbernice - vsechny, filtrovat netreba. */
void elm_on_frame(elm_t *e, uint32_t id, const uint8_t *data, uint8_t dlc,
                  uint32_t now_ms);

/* Casovace a dokonceni pozadavku. Volat z hlavni smycky tak casto, aby
 * mezi dvema volanimi neuplynulo 150 ms (mez ISO-TP). */
void elm_poll(elm_t *e, uint32_t now_ms);

/* Kolik znaku ceka na odeslani klientovi. */
static inline uint16_t elm_pending(const elm_t *e) { return e->tx_len; }

/* Vyzvedne az cap znaku. Vraci kolik jich opravdu vzal. */
uint16_t elm_take(elm_t *e, char *out, uint16_t cap);

/* Bezi prave pozadavek do vozu? */
static inline bool elm_busy(const elm_t *e) { return e->busy; }

/* ------------------------------------------------------------------
 * Filtr sluzeb
 *
 * Verejny schvalne: je to to jedine, co stoji mezi cizim nastrojem a
 * jednotkami vozu, takze to ma mit vlastni testy a ma se na to dat
 * ukazat prstem. true = jen cteni, smi se odeslat.
 * ------------------------------------------------------------------ */
bool elm_service_allowed(const uint8_t *msg, uint16_t len);

/* ------------------------------------------------------------------
 * Soket
 *
 * Prelozi se jen na desce (CANOGRAF_MOD_ELM327=1). Neni to vlakno:
 * sokety jsou neblokujici a elm_net_tick() se vola z hlavni smycky,
 * ze stejneho vlakna jako can_poll(). Je to zamerne - ISO-TP i ovladac
 * TWAI maji jednoho pisare (can/twai.h) a vlastni uloha by k nim
 * potrebovala zamek, ktery by se ted zaviral pod LVGL.
 *
 * KOLIK KLIENTU: JEDEN. Neni to nahoda ani lenost. Emulovane zarizeni
 * je JEDNA seriova linka s JEDNIM stavem - echo, hlavicky, filtr,
 * rozdelany pozadavek - a dva nastroje by si navzajem prepisovaly
 * nastaveni a cetly cizi odpovedi, aniz by o sobe vedely. Pod tim je
 * jeste sbernice, kde stejne bezi jedna otazka po druhe. Druhe spojeni
 * se proto prijme, dostane jednoradkove vysvetleni a hned se zavre -
 * nabidnout port, ktery jen mlci, by vypadalo jako porucha site.
 * ------------------------------------------------------------------ */
#if CANOGRAF_MOD_ELM327

typedef struct {
    isotp_send_fn send;
    void         *user;
    bool (*tx_allowed)(void *user);
    bool (*volts)(void *user, float *out);
    uint32_t tx_id;
    uint32_t rx_id;
    uint32_t bitrate;
    uint16_t port;
} elm_net_cfg_t;

/* Otevre port. false = nepodarilo se, duvod jde do logu. */
bool elm_net_start(const elm_net_cfg_t *cfg);
void elm_net_stop(void);
bool elm_net_running(void);

/* Jeden krok: prijmout spojeni, precist bajty, pretocit automat,
 * odeslat odpoved. Vola se z hlavni smycky vedle can_poll(). */
void elm_net_tick(uint32_t now_ms);

/* Jeden ramec ze sbernice. Volajici si drzi VLASTNI kurzor do
 * kruhoveho zasobniku (can_next), takze emulace nebere ramce ani
 * odposlechu v UI, ani vrstve diagnostiky. Dokud nikdo nevisi na
 * portu, nedela to nic. */
void elm_net_on_frame(uint32_t id, const uint8_t *data, uint8_t dlc,
                      uint32_t now_ms);

/* Je prave nekdo pripojeny? Pro UI - "port otevreny" a "nekdo na nem
 * visi" jsou dve ruzne veci a clovek ma videt obe. */
bool elm_net_client(void);

/* Port, na kterem to posloucha; 0 kdyz nic nebezi. */
uint16_t elm_net_port(void);

/* Kolik pozadavku uz proslo a kolik jich zastavil filtr sluzeb. */
uint32_t elm_net_requests(void);
uint32_t elm_net_blocked(void);

#endif /* CANOGRAF_MOD_ELM327 */

#ifdef __cplusplus
}
#endif

#endif /* NET_ELM_H */
