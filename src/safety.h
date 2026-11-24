/*
 * safety.h - brana mezi ctenim a zapisem do ridicich jednotek
 *
 * Cteni z vozidla je zadarmo. Zapis muze vuz vyradit z provozu, a to
 * nevratne - `2E 2110` prepise servisni data, `3B 16` konfiguraci baterie
 * v EMM, `31 01 02 0D` rozpali vyfuk pres 600 C. Omylem stisknute tlacitko
 * na to stacit nesmi.
 *
 * Navrh, duvody a rozhodnuti jsou v docs/safety.md. Tahle hlavicka je jen
 * rozhrani - implementace prijde spolu s transportni vrstvou.
 *
 * Tri stupne:
 *
 *   1. preklad    CANOGRAF_ALLOW_WRITE=0 -> kod zapisu v obrazu neni.
 *                 CANOGRAF_WRITE_UNLOCKED=1 -> brana prelozena otevrena,
 *                 pro toho, kdo vi co dela a nechce byt otravovan.
 *   2. nastaveni  prepinac "Enable write functions", vychozi vypnuto
 *   3. sekvence   ctyri ikony, ctyri kroky, zopakovat po stroji
 *
 * Captchu (stupen 3) jde vypnout tremi zpusoby, od nejlepsiho:
 * CANOGRAF_ALLOW_WRITE=0 (zapis v obrazu neni), CANOGRAF_WRITE_UNLOCKED=1
 * (zapis je, brana ne) a prepinac v debug menu za behu - viz
 * safety_bypass_set() nize.
 *
 * Odemceni NEMA casovy limit a NIKAM se neuklada. Plati do restartu, pak
 * je zase zamceno. Jedna sekvence na jedno zapnuti pristroje.
 *
 * Brana plati pro VSECHNY kanaly stejne - panel, web server i emulace
 * ELM327. Vzdaleny klient si ji neotevre, otevrit ji muze jen clovek u
 * panelu. Dokud neni odemceno, zapis se odmita na urovni TRANSPORTU, ne
 * schovanim tlacitka: jedina verejna cesta na sbernici je
 * safety_send_guarded(), syrove odeslani je static uvnitr transportni
 * jednotky a chce safety_token_t, ktery umi vyrobit jen safety.c.
 *
 * Tenhle soubor smi zaviset jen na stdbool/stdint - preklada se i na
 * hostiteli (sim/) a nesmi tahnout LVGL ani Arduino.
 */
#ifndef SAFETY_H
#define SAFETY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * Stupen 1 - prekladove prepinace
 *
 * Obe makra maji bezpecnou vychozi hodnotu i kdyz je nikdo nenastavi.
 * Konfigurace (config/NAZEV.env -> build_config.py) je jen predefinuje pres
 * -D; kdyz skript nebezi, plati to, co je tady.
 *
 * CANOGRAF_ALLOW_WRITE=1 NEZNAMENA "zapis povolen". Znamena "zapis je v
 * obrazu a je chraneny". Povoleni dava az uzivatel sekvenci za behu.
 * ------------------------------------------------------------------ */
#ifndef CANOGRAF_ALLOW_WRITE
#define CANOGRAF_ALLOW_WRITE 0
#endif

#ifndef CANOGRAF_WRITE_UNLOCKED
#define CANOGRAF_WRITE_UNLOCKED 0
#endif

#if CANOGRAF_WRITE_UNLOCKED && !CANOGRAF_ALLOW_WRITE
#error "CANOGRAF_WRITE_UNLOCKED=1 nedava smysl bez CANOGRAF_ALLOW_WRITE=1"
#endif

/* ------------------------------------------------------------------
 * Kanaly
 *
 * Kazdy pozadavek na sbernici nekam patri. Prava jsou pro vsechny kanaly
 * stejna - kanal se nese kvuli auditu, hlaskam a kvuli tomu, aby se dal
 * jeden klient odpojit, aniz by se musel zamykat cely pristroj.
 * ------------------------------------------------------------------ */
typedef enum {
    SAFETY_CH_PANEL = 0,  /* dotykova obrazovka teto desky */
    SAFETY_CH_WEB,        /* vestaveny web server */
    SAFETY_CH_ELM,        /* emulace ELM327 na TCP portu */
    SAFETY_CH_INTERNAL,   /* vlastni periodicke cteni UI, 3E 00 apod. */
    SAFETY_CH__COUNT
} safety_channel_t;

/* ------------------------------------------------------------------
 * Klasifikace pozadavku
 *
 * READ      projde vzdy, i v zamcenem a v nezapisovem buildu
 * WRITE     jen pres branu
 * FORBIDDEN neprojde nikdy, ani odemcene - tyhle sluzby tenhle pristroj
 *           nedela (programovaci session, prenos dat do flash, umlceni
 *           komunikace, cokoli smerem k airbagu)
 *
 * Rozhoduje se podle sluzby a cilove adresy, ne podle toho, ktere
 * tlacitko to poslalo, a na SLOZENEM pozadavku, ne po jednotlivych
 * ramcich - do nasledujiciho ramce ISO-TP se tim padem zapis nepropasuje.
 * Neznama sluzba je WRITE, ne READ: seznam je povolovaci. Tabulka a
 * rozbor hranicnich pripadu (`10 03`, `31 03`, `27`) jsou v docs/safety.md.
 * ------------------------------------------------------------------ */
typedef enum {
    SAFETY_CLASS_READ = 0,
    SAFETY_CLASS_WRITE,
    SAFETY_CLASS_FORBIDDEN
} safety_class_t;

safety_class_t safety_classify(uint32_t tx_id, const uint8_t *req, uint16_t len);

/* ------------------------------------------------------------------
 * Vysledek posouzeni
 *
 * Duvod odmitnuti se neschovava. UI z nej dela hlasku (vzdy i s tim, kde
 * se zapis zapina), vzdaleny kanal z nej dela zapornou odpoved
 * 7F <sid> 33, audit ho zapise.
 * ------------------------------------------------------------------ */
typedef enum {
    SAFETY_OK = 0,
    SAFETY_DENY_BUILD,      /* CANOGRAF_ALLOW_WRITE=0 */
    SAFETY_DENY_LOCKED,     /* brana zamcena, sekvence neprobehla */
    SAFETY_DENY_FORBIDDEN,  /* SAFETY_CLASS_FORBIDDEN */
    SAFETY_DENY_PRECOND     /* motor bezi, vuz jede, nizke napeti, chybi zaloha */
} safety_verdict_t;

/* ------------------------------------------------------------------
 * Stupen 2 - stav brany
 *
 * Zadny casovy limit, zadne ukladani. Stav zije v RAM a po restartu je
 * zase zamceno. Odemyka se jednou za beh pristroje, viz docs/safety.md.
 * ------------------------------------------------------------------ */

/* Smi se zapisovat prave ted? Pro vsechny kanaly stejna odpoved. */
bool safety_write_allowed(void);

/* Totez s uvedenim kanalu - kvuli auditu a hlaskam. Vysledek je stejny;
 * kanal, ktery byl odpojen pres safety_channel_drop(), dostane false. */
bool safety_channel_write_allowed(safety_channel_t ch);

/* Otevre potvrzovaci dialog (stupen 3): nejdriv stranka s upozornenim,
 * po nem sekvence. Vola se z prepinace v nastavenich a z libovolneho
 * zapisoveho tlacitka stisknuteho v zamcenem stavu. Sama o sobe nic
 * neodemyka. V buildu bez zapisu jen ukaze STR_SAFETY_NO_WRITE_BUILD. */
void safety_request_unlock(void);

/* Okamzite zamceni: vypnuty prepinac, tlacitko STOP u vzdaleneho sezeni,
 * nouzovy stop odkudkoli. Idempotentni. Dalsi zapis chce znovu sekvenci. */
void safety_lock(void);

/* Odemkne branu po uspesne zopakovane sekvenci. Vola ji dialog v ui.c a
 * nikdo jiny. Ve vyvojovem rezimu (CANOGRAF_WRITE_UNLOCKED) je odemceno
 * uz od startu a tohle nic nemeni. */
void safety_unlock_grant(void);

/* Odpojeni jednoho kanalu - vzdaleny klient se odpojil nebo ho clovek u
 * panelu vyhodil. Branu to nezamyka, jen prestane platit pro ten kanal. */
void safety_channel_drop(safety_channel_t ch);

/* ------------------------------------------------------------------
 * Stupen 3 - potvrzovaci sekvence
 *
 * Logika je tady, protoze je to pravidlo, ne obrazek: generovani,
 * porovnavani, pocitani pokusu a cooldown se daji otestovat na hostiteli
 * bez LVGL. ui.c jen kresli ctyri ikony a hlasi doteky.
 *
 * Ctyri znaky jsou IKONY s vyrazne odlisnou siluetou (dum, blesk, ozubene
 * kolo, klic) plus cislice. Barva nenese zadnou informaci - rozsviceny
 * znak se pozna inverzi, ne odstinem - takze dialog funguje pro kazdou
 * poruchu barvocitu i na slunci. Viz docs/safety.md.
 * ------------------------------------------------------------------ */
#define SAFETY_SEQ_LEN        4    /* kroku v sekvenci */
#define SAFETY_SEQ_TOKENS     4    /* kolik ruznych znaku */
#define SAFETY_SEQ_ATTEMPTS   3    /* pokusu, pak cooldown */
#define SAFETY_SEQ_ON_MS      700  /* jak dlouho sviti jeden znak */
#define SAFETY_SEQ_GAP_MS     300  /* tma mezi znaky - nese opakovani */
#define SAFETY_SEQ_LEAD_MS    500  /* pauza po "WATCH", nez se rozsviti prvni */
#define SAFETY_SEQ_TAP_MS   10000  /* nejdelsi pauza mezi doteky uzivatele */
#define SAFETY_SEQ_IDLE_MS  60000  /* necinnost, po ktere se dialog zavre */
#define SAFETY_SEQ_COOLDOWN_MS (60u * 1000u) /* po vycerpani pokusu */

/* Zamerne to nejsou konfiguracni klice. Ochrana, kterou jde zeslabit
 * radkem v config/NAZEV.env, neni ochrana - kdo ji nechce, prelozi si
 * CANOGRAF_WRITE_UNLOCKED=1 a vi o tom. */

typedef struct {
    uint8_t  step[SAFETY_SEQ_LEN];  /* 0..SAFETY_SEQ_TOKENS-1 */
    uint8_t  pos;                   /* kolik uzivatel zatim trefil */
    uint8_t  attempt;               /* 1..SAFETY_SEQ_ATTEMPTS */
    bool     playing;               /* stroj prehrava - doteky se ignoruji */
} safety_seq_t;

typedef enum {
    SAFETY_SEQ_PROGRESS = 0,  /* spravny znak, pokracuj */
    SAFETY_SEQ_WRONG,         /* spatny znak - nova sekvence, pokus +1 */
    SAFETY_SEQ_DONE,          /* cela sekvence sedi - brana se otevrela */
    SAFETY_SEQ_BLOCKED        /* pokusy vycerpany, dialog se zavira */
} safety_seq_result_t;

/* Nova nahodna sekvence. Pri opakovanem pokusu se generuje VZDY znovu:
 * se stejnou sekvenci a okamzitou zpetnou vazbou by sla lustit po jednom
 * kroku misto 256 moznosti. Nahodnost bere z host->random32(); dva bity
 * na znak, zadne modulo, zadna zaujatost. Opakovani stejneho znaku se
 * nevylucuje - zakaz by zmensil prostor z 256 na 108. */
void safety_seq_begin(safety_seq_t *s);

/* Jeden dotek uzivatele. */
safety_seq_result_t safety_seq_input(safety_seq_t *s, int token);

/* Znak na dane pozici - pro prehravani v UI. Mimo rozsah vraci 0. */
int safety_seq_token(const safety_seq_t *s, int i);

/* Kolik sekund jeste trva cooldown po vycerpani pokusu; 0 = lze zkusit. */
uint32_t safety_seq_cooldown_s(void);

/* ------------------------------------------------------------------
 * Debug menu - treti zpusob, jak captchu vypnout
 *
 * Prepinac v debug panelu (dlazdice ve vysuvne mrizce) preskoci sekvenci:
 * safety_request_unlock() odemkne rovnou. Nepersistuje - po restartu je
 * zase vypnuty, a nesmi se dostat ani do profilu v NVS, jinak by to byla
 * trvale vypnuta ochrana, kterou na pristroji nikdo nepozna. Duvody a
 * text, ktery u prepinace stoji na obrazovce, jsou v docs/safety.md.
 *
 * Co NEobchazi: SAFETY_CLASS_FORBIDDEN, predpoklady (motor, rychlost,
 * napeti, zaloha bloku) ani potvrzeni u jednotlivych zapisu. Vypina jen
 * stupen 3. Dokud je zapnuty, UI o tom drzi odznak v horni liste a kazde
 * takhle povolene odemceni jde do auditu.
 *
 * V buildu bez zapisu (CANOGRAF_ALLOW_WRITE=0) je prepinac videt, ale
 * neaktivni - neni co odemykat.
 * ------------------------------------------------------------------ */
bool safety_bypass_get(void);
void safety_bypass_set(bool on);

/* "TRY CAPTCHA" - zkouska sekvence nanecisto. Ukaze stejny dialog,
 * ohlasi, jestli ji clovek zvladl, a NIC neodemkne ani neposle. Slouzi
 * k vyzkouseni mechanismu bez vozidla a bez chuti skutecne zapisovat.
 *
 * Zamerne samostatna funkce, ne parametr safety_request_unlock():
 * odemykat smi jen jedna cesta v celem programu. Priznak "tohle je jen
 * zkouska" prochazejici odemykaci funkci by stacilo prohodit a brana by
 * se otevrela potichu; takhle je safety_unlock_grant() volana z jednoho
 * mista a da se to overit grepem. Sekvencni logiku obe cesty sdileji.
 *
 * Zkouska bezi v kazdem buildu, i tam, kde se zapis vubec neprelozil, a
 * jeji neuspechy se nezapocitavaji do cooldownu ostreho odemykani -
 * nacvik nesmi zablokovat praci. */
void safety_demo_sequence(void);

/* ------------------------------------------------------------------
 * Vzdalene kanaly
 *
 * Zadne vlastni pravo, zadna vlastni brana. Vzdaleny klient smi zapsat
 * prave tehdy, kdyz clovek u panelu prosel sekvenci. Tyhle dve funkce
 * existuji jen proto, aby panel mohl ukazat, kdo je pripojen, a aby slo
 * jedno sezeni utnout bez zamceni celeho pristroje.
 * ------------------------------------------------------------------ */
void        safety_channel_attach(safety_channel_t ch, const char *who);
const char *safety_channel_peer(safety_channel_t ch);  /* "" kdyz nikdo */

/* ------------------------------------------------------------------
 * Transport - jedina cesta na sbernici
 *
 * Token je jednorazovy a vazany na konkretni ramec: kanal, adresu, delku
 * a otisk dat, plus kratka platnost. Nejde si ho schovat na pozdeji ani
 * jim poslat neco jineho, nez co bylo posouzeno.
 *
 * Transportni jednotka ma syrove odeslani privatni (static) a verejne
 * nabizi jen funkci, ktera chce `const safety_token_t *`. Kdo token nema,
 * nic neodesle. Tim je ochrana v transportu, ne v UI, a neobejde ji ani
 * budouci web server, ani emulace ELM327, ani skript.
 * ------------------------------------------------------------------ */
typedef struct {
    uint32_t magic;     /* platny token ma SAFETY_TOKEN_MAGIC */
    uint32_t serial;    /* poradove cislo, po pouziti se zneplatni */
    uint32_t fp;        /* otisk (kanal, adresa, delka, data) */
    uint64_t valid_to;  /* ms uptime, po kterych token propada */
} safety_token_t;

#define SAFETY_TOKEN_MAGIC  0x5AFE7A1Bu
#define SAFETY_TOKEN_MS     200u   /* token plati dve desetiny sekundy */

/* Posoudi pozadavek a pri SAFETY_OK vyda token. Primo ji potrebuje jen
 * transport, ktery sklada ISO-TP prenos z vice ramcu. */
safety_verdict_t safety_authorize(safety_channel_t ch, uint32_t tx_id,
                                  const uint8_t *req, uint16_t len,
                                  safety_token_t *out);

/* Overi token proti prave odesilanemu ramci a spotrebuje ho. Vola
 * transport tesne pred zapisem do TWAI. */
bool safety_token_consume(safety_token_t *tok, safety_channel_t ch,
                          uint32_t tx_id, const uint8_t *req, uint16_t len);

/* JEDINA verejna cesta na sbernici pro vsechny kanaly. Klasifikuje,
 * posoudi, vyda token, posle a zapise radek do auditu. Pri odmitnuti
 * neposle nic a vrati duvod. */
safety_verdict_t safety_send_guarded(safety_channel_t ch, uint32_t tx_id,
                                     const uint8_t *req, uint16_t len);

/* Obal pro mista, kde se jen posila. Pohodli, ne ochrana - ochrana je
 * uvnitr safety_send_guarded(). */
#define SAFETY_SEND(ch, id, buf, len) \
    safety_send_guarded((ch), (uint32_t)(id), (const uint8_t *)(buf), (uint16_t)(len))

/* ------------------------------------------------------------------
 * Deska pod tim
 *
 * safety.c nesmi znat Arduino ani ESP-IDF, stejne jako ui.c. Vse, co umi
 * jen deska, chodi sem. Simulator necha ukazatele NULL a implementace to
 * musi snest - vzdy smerem k zavreno: bez random32() se dialog neotevre
 * vubec (predvidatelna sekvence je horsi nez zadny dialog).
 * ------------------------------------------------------------------ */
typedef struct {
    uint32_t (*random32)(void);       /* hardwarovy generator desky */
    uint64_t (*uptime_ms)(void);      /* monotonni cas od startu */
    bool     (*engine_running)(void); /* otacky > 0 */
    bool     (*vehicle_moving)(void); /* rychlost > 0 */
    uint32_t (*batt_mv)(void);        /* napeti site, 0 = nezname */
    void     (*open_unlock_ui)(void); /* ui.c: postav dialog se sekvenci */
    void     (*audit)(const char *line); /* radek do logu na TF kartu */
} safety_host_t;

void safety_set_host(const safety_host_t *h);

/* Text pro cloveka k danemu verdiktu - uz prelozeny pres T(). U
 * SAFETY_DENY_LOCKED vzdy rekne i KDE se zapis zapina a ze jde vypnout
 * pri prekladu; uzivatel nema hadat, proc se nic nestalo. */
const char *safety_verdict_text(safety_verdict_t v);

#ifdef __cplusplus
}
#endif

#endif /* SAFETY_H */
