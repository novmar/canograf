/*
 * poller.h - co se z vozidla cte a jak casto
 *
 * Stejny vzor jako zbytek src/diag: zadne hodiny, zadny hardware, zadne
 * vlakno. Cas chodi v parametru, ramce odchazi pres callback, takze se
 * cela vrstva prelozi a odladi na hostiteli (sim/lv_shot.c).
 *
 * Vrstva drzi tri veci dohromady: spoj ISO-TP, klienta UDS a detekci
 * pripojeni. Volajici ji jen krmi ramci a casem; VSECHNO ostatni -
 * zahajeni session, davkovani, poradi dotazu, prepocet na fyzikalni
 * hodnoty a stari - je tady.
 *
 * ---------------------------------------------------------------
 * Proc rozpocet neni nekonecny
 * ---------------------------------------------------------------
 * Jeden dotaz 22 je cely round-trip pres ISO-TP: nas ramec, first frame
 * jednotky, nase flow control, consecutive frames. Do jednoho dotazu se
 * na MD1CS006 vejdou tri DIDy (UDS_DIDS_PER_REQ), ctvrty uz cely dotaz
 * odmitne. Pres ELM327 to pri jizde davalo asi 3,3 vzorku za vterinu;
 * pres TWAI odpada dongle i jeho ASCII protokol, ale zustava to, co
 * urcuje strop - P2 jednotky a doba na sbernici.
 *
 * Co se da spocitat dopredu (500 kbit/s, 11bitovy identifikator, 8
 * datovych bajtu = asi 128 bitovych dob vcetne vycpavky, tedy 0,26 ms
 * na ramec): dotaz na tri dvoubajtove DIDy je 5 ramcu (dotaz, FF, nase
 * FC, dve CF), tedy asi 1,3 ms provozu. Sbernice tedy neni mez ani
 * zdaleka - mez je doba, nez jednotka odpovi, a tu nezmeri nikdo jiny
 * nez vuz. Proto si vrstva meri skutecnou rychlost sama
 * (diag_poller_rate_x100) a UI ji ukazuje v diagnostice; cislo na
 * obrazovce je merene, ne odhadnute.
 *
 * ---------------------------------------------------------------
 * Tri rychlosti a proc zrovna takhle
 * ---------------------------------------------------------------
 * Ne vsechno ma smysl cist stejne casto. Rozpocet je jeden a kazdy DID
 * navic v rychle skupine zdrzuje vsechny ostatni.
 *
 *   DIAG_TIER_FAST (100 ms)   veliciny, ktere jsou k necemu jen zive:
 *                             otacky, rychlost, plnici tlak a jeho
 *                             zadana hodnota, rail, VNT. Plnici tlak pri
 *                             seslapnuti vyskoci a zase spadne behem
 *                             pul vteriny; pri 3,5 vzorku za vterinu uz
 *                             se dal najit 416 mbar schodek (viz
 *                             docs/vehicles), takze 10 Hz je pohodlna
 *                             rezerva, ne fantazie.
 *   DIAG_TIER_MED (1 s)       teploty, MAF, EGR, doba regenerace. Tyhle
 *                             se hybou v desitkach vterin, takze je
 *                             vterina porad desetkrat rychleji nez
 *                             velicina sama.
 *   DIAG_TIER_SLOW (10 s)     tachometr, saze, zatizeni filtru, cas od
 *                             posledni regenerace. Saze pribyvaji
 *                             0,043 g/km, tedy za deset vterin jizdy asi
 *                             0,004 g - hluboko pod rozlisenim, ktere
 *                             UI vubec ukazuje.
 *
 * Perioda je "ne drive nez", ne pevny takt. Kdyz jednotka nestiha,
 * rozestup se natahne a nic se nehromadi.
 *
 * ---------------------------------------------------------------
 * Stranky dostanou jen to, co prave potrebuji
 * ---------------------------------------------------------------
 * Kazdy kanal ma masku pohledu (diag_view_t). Pri stavbe davky se berou
 * jen kanaly, ktere prave videny pohled opravdu ukazuje - kdyz je videt
 * stranka DPF, nema smysl cist plnici tlak deset krat za vterinu.
 * Nekolik kanalu ma masku DIAG_VIEW_ALL, protoze jsou videt porad
 * (teplota v horni liste, saze v pravem sloupci).
 *
 * ---------------------------------------------------------------
 * Co se NIKDY nepredstira
 * ---------------------------------------------------------------
 * Kanal, ktery jeste nebyl precteny, vraci DIAG_SHOW_NONE a nema zadnou
 * hodnotu. Nula ani posledni znama hodnota bez oznaceni se nevraci -
 * UI tedy nema z ceho udelat cislo. Odmitnuty DID (7F 22 31) je vlastni
 * stav DIAG_SHOW_REFUSED i s duvodem, protoze "tahle verze softwaru ten
 * DID nema" je jina informace nez "jeste jsem se nezeptal".
 *
 * Odmitnuty kanal se z dalsich davek vyradi - ptat se porad dokola na
 * neco, co jednotka nezna, kradlo by misto v kazdem kole. Vyrazeni
 * zmizi az pri dalsim diag_poller_arm(p, true), tedy pri vedomem
 * prepnuti do normalniho rezimu.
 *
 * ---------------------------------------------------------------
 * Listen-only
 * ---------------------------------------------------------------
 * Dokud neni diag_poller_arm(p, true), NEODEJDE ANI JEDEN RAMEC. Neni
 * to jen spolehnuti na to, ze can_send() v listen-only vrati false:
 * prepnuti do normalniho rezimu je vedome rozhodnuti uzivatele a vrstva
 * se podle nej chova, ne podle toho, co by nahodou proslo.
 *
 * ---------------------------------------------------------------
 * Pouziti
 * ---------------------------------------------------------------
 *     diag_poller_cfg_t cfg;
 *     diag_poller_cfg_default(&cfg);
 *     cfg.send = can_isotp_send;
 *     diag_poller_init(&p, &cfg);
 *     ...
 *     can_poll();
 *     while (can_next(&cur, &f))
 *         diag_poller_on_frame(&p, f.id, f.data, f.dlc, f.tx, f.ts_ms);
 *     diag_poller_arm(&p, can_mode() == CAN_MODE_NORMAL);
 *     diag_poller_poll(&p, can_now_ms());
 *
 * Tenhle soubor smi zaviset jen na stdbool/stdint a na can/isotp.h,
 * diag/uds.h a diag/link.h.
 */
#ifndef DIAG_POLLER_H
#define DIAG_POLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "can/isotp.h"
#include "diag/link.h"
#include "diag/uds.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * Periody skupin. Duvod viz hlavicka souboru.
 * ------------------------------------------------------------------ */
#ifndef DIAG_FAST_MS
#define DIAG_FAST_MS 100u
#endif
#ifndef DIAG_MED_MS
#define DIAG_MED_MS 1000u
#endif
#ifndef DIAG_SLOW_MS
#define DIAG_SLOW_MS 10000u
#endif

/* Chybova pamet se sama od sebe nemeni, dokud se na ni clovek diva.
 * Cte se pri otevreni stranky a pak uz jen obcas; na vyzadani kdykoliv
 * pres diag_poller_dtc_refresh(). */
#ifndef DIAG_DTC_MS
#define DIAG_DTC_MS 30000u
#endif

/* Po neuspesne davce se chvili pocka. Na mrtve sbernici by se jinak
 * dotazy zkousely tak rychle, jak dovoli hlavni smycka, a citac
 * tx_fail by rostl o tisice za vterinu. */
#ifndef DIAG_RETRY_MS
#define DIAG_RETRY_MS 250u
#endif

/* Kolikanasobek periody skupiny jeste plati za zivou hodnotu, a jak
 * dlouho nejmene. Rychla skupina ma periodu 100 ms, ale blikat
 * "zastarale" pri kazdem vynechanem kole by bylo k nicemu - vterina je
 * dolni mez pro vsechny. */
#ifndef DIAG_STALE_MULT
#define DIAG_STALE_MULT 3u
#endif
#ifndef DIAG_STALE_MIN_MS
#define DIAG_STALE_MIN_MS 1000u
#endif

/* Kolik chyb z 19 02 si pamatujeme. Filtr 0x2F uz odfiltroval klidovy
 * stav 0x50, takze jich tolik nebyva; co se nevejde, rekne
 * diag_poller_dtc_dropped(). */
#ifndef DIAG_DTC_MAX
#define DIAG_DTC_MAX 24u
#endif

/* Kolik rozsirenych zaznamu (19 06) a polozek freeze framu (19 04)
 * drzime k jedne vybrane chybe. */
#ifndef DIAG_REC_MAX
#define DIAG_REC_MAX 8u
#endif
#ifndef DIAG_FF_MAX
#define DIAG_FF_MAX 10u
#endif

/* ------------------------------------------------------------------
 * Pohledy
 *
 * Neni to vycet stranek UI - je to vycet toho, CO je prave videt.
 * Mapovani stranka -> pohled dela UI, takze pridani stranky neznamena
 * zasah do profilu vozidla.
 * ------------------------------------------------------------------ */
typedef enum {
    DIAG_VIEW_MAIN = 0,   /* rychlost a otacky */
    DIAG_VIEW_ENGINE,     /* plnici tlak, rail, VNT, vzduch */
    DIAG_VIEW_BOOST,      /* graf zadano / skutecnost */
    DIAG_VIEW_FAULTS,     /* chybova pamet */
    DIAG_VIEW_DPF,        /* filtr pevnych castic */
    DIAG_VIEW_OTHER,      /* nastaveni a vse ostatni */
    DIAG_VIEW__COUNT
} diag_view_t;

#define DIAG_VIEW_BIT(v) ((uint16_t)(1u << (v)))
#define DIAG_VIEW_ALL    ((uint16_t)((1u << DIAG_VIEW__COUNT) - 1u))

/* ------------------------------------------------------------------
 * Skupiny podle potrebne rychlosti
 * ------------------------------------------------------------------ */
typedef enum {
    DIAG_TIER_FAST = 0,
    DIAG_TIER_MED,
    DIAG_TIER_SLOW,
    DIAG_TIER__COUNT
} diag_tier_t;

/* ------------------------------------------------------------------
 * Kanaly
 *
 * Fyzikalni veliciny, ne DIDy. Ktery DID a jake skalovani za nimi je,
 * vi jen poller.c - profil vozidla, viz docs/vehicles. UI zna jen
 * jmeno veliciny a jednotku, ve ktere ji kresli.
 * ------------------------------------------------------------------ */
typedef enum {
    DIAG_CH_RPM = 0,      /* 1/min */
    DIAG_CH_SPEED,        /* km/h */
    DIAG_CH_BOOST,        /* mbar, absolutne */
    DIAG_CH_BOOST_SP,     /* mbar, zadano */
    DIAG_CH_RAIL,         /* bar */
    DIAG_CH_RAIL_SP,      /* bar, zadano */
    DIAG_CH_VNT,          /* %, strida lopatek */
    DIAG_CH_MAF,          /* mg */
    DIAG_CH_MAF_SP,       /* mg, zadano */
    DIAG_CH_EGR,          /* %, poloha ventilu */
    DIAG_CH_IAT,          /* C, nasavany vzduch */
    DIAG_CH_COOLANT,      /* C, chladici kapalina */
    DIAG_CH_DPF_UP,       /* C, pred filtrem */
    DIAG_CH_DPF_DN,       /* C, za filtrem */
    DIAG_CH_DPF_SP,       /* C, cil pro regeneraci */
    DIAG_CH_SOOT,         /* g, odhad sazi */
    DIAG_CH_CSF_LOAD,     /* g, ekvivalentni zatizeni */
    DIAG_CH_RGN_DUR,      /* s, delka prave bezici regenerace */
    DIAG_CH_RGN_END,      /* s, od konce posledni regenerace */
    DIAG_CH_ODO,          /* km */
    DIAG_CH__COUNT
} diag_ch_t;

/* ------------------------------------------------------------------
 * Co smi UI ukazat
 *
 * Tohle je jedina odpoved, podle ktere se UI rozhoduje. DIAG_SHOW_NONE
 * znamena, ze hodnota NENI - ne ze je nula.
 * ------------------------------------------------------------------ */
typedef enum {
    DIAG_SHOW_NONE = 0,   /* jeste neprecteno; zadne cislo se ukazat nesmi */
    DIAG_SHOW_LIVE,       /* cerstve precteno */
    DIAG_SHOW_STALE,      /* precteno, ale uz davno - musi byt poznat */
    DIAG_SHOW_REFUSED     /* jednotka DID odmitla, duvod v diag_poller_nrc() */
} diag_show_t;

/* ------------------------------------------------------------------
 * Chybova pamet
 * ------------------------------------------------------------------ */
typedef struct {
    uint32_t dtc;      /* tri bajty kodu */
    uint8_t  status;   /* stavovy bajt, bity UDS_DTC_* z uds.h */
} diag_dtc_t;

/* Rozsirene zaznamy 19 06. Cisla zaznamu jsou z popisu jednotky a
 * overena na voze (docs/vehicles). Delky zaznamu nejsou v odpovedi -
 * musi se vedet dopredu, takze u NEZNAMEHO cisla zaznamu rozbor skonci
 * a dal se nic nepredstira. */
typedef enum {
    DIAG_REC_UNKNOWN = 0,
    DIAG_REC_ODO_AT,      /* 0x80 tachometr pri vyskytu, km */
    DIAG_REC_AGING,       /* 0x81 citac starnuti */
    DIAG_REC_COUNT,       /* 0x82 a 0x84 pocet vyskytu */
    DIAG_REC_ODO_HEALED,  /* 0x85 tachometr, kdyz chyba zmizela, km */
    DIAG_REC_DIST_WITH,   /* 0x86 ujeto se zavadou, km */
    DIAG_REC_ODO_FIRST    /* 0x87 tachometr pri prvnim vyskytu, km */
} diag_rec_kind_t;

typedef struct {
    uint8_t  num;    /* cislo zaznamu, jak ho posila jednotka */
    uint8_t  kind;   /* diag_rec_kind_t */
    uint8_t  len;    /* delka v bajtech */
    bool     unset;  /* same 0xFF - jednotka hodnotu nema, neukazovat */
    uint32_t val;
} diag_rec_t;

/* Freeze frame 19 04. Delky dat se bere z profilu vozidla stejne jako u
 * zivych hodnot, takze se rozebrat daji jen ZNAME DIDy; na prvnim
 * neznamem rozbor skonci. */
typedef struct {
    uint16_t did;
    uint8_t  ch;    /* diag_ch_t */
    float    val;
} diag_ff_t;

/* ------------------------------------------------------------------
 * Nastaveni
 * ------------------------------------------------------------------ */
typedef struct {
    uint32_t      tx_id;    /* 0x7E0 - ECM na X82 */
    uint32_t      rx_id;    /* 0x7E8 */
    uint8_t       session;  /* 0x03 extended, tu MD1CS006 chce */
    uint8_t       dtc_mask; /* maska pro 19 02; 0x2F = co ma smysl ukazat */
    isotp_send_fn send;     /* povinne, jinak se nic nepta */
    void         *user;
} diag_poller_cfg_t;

void diag_poller_cfg_default(diag_poller_cfg_t *cfg);

/* ------------------------------------------------------------------
 * Vrstva
 *
 * Struktura je verejna kvuli statickemu umisteni; polozky patri
 * implementaci, cti je pres funkce nize.
 * ------------------------------------------------------------------ */
typedef struct {
    uint8_t  show;     /* diag_show_t bez rozliseni LIVE/STALE */
    uint8_t  nrc;      /* platny pri DIAG_SHOW_REFUSED */
    bool     off;      /* odmitnuty DID, uz se na nej neptame */
    uint32_t t_ms;     /* kdy byla hodnota prectena */
    float    val;
} diag_val_t;

typedef struct {
    diag_poller_cfg_t cfg;

    isotp_link_t link;
    uds_client_t uds;
    diag_link_t  dl;

    bool     armed;        /* smime vysilat? */
    bool     session_req;  /* 10 03 jeste neodeslano */
    uint8_t  view;         /* diag_view_t */
    uint8_t  job;          /* co prave bezi, interni */
    uint8_t  job_tier;     /* u davky DIDu ktera skupina */
    uint32_t now;          /* posledni cas z diag_poller_poll */
    uint32_t t_due[DIAG_TIER__COUNT];
    uint32_t t_dtc;        /* kdy naposled dobehlo 19 02 */
    bool     dtc_force;    /* uzivatel si rekl o nove cteni */
    bool     dtc_done;     /* uz nejaka odpoved 19 02 dorazila */
    uint32_t t_retry;      /* driv se novy dotaz nezacne */
    uint32_t seen_rsp;     /* kolik odpovedi uz bylo zapocitano do link */
    bool     seen_timeout;

    diag_val_t val[DIAG_CH__COUNT];

    diag_dtc_t dtc[DIAG_DTC_MAX];
    uint16_t   n_dtc;
    uint16_t   dtc_dropped;
    int16_t    dtc_sel;    /* index vybrane chyby, -1 = zadna */
    uint32_t   dtc_sel_code;
    bool       want_ff;
    bool       want_ext;

    diag_rec_t rec[DIAG_REC_MAX];
    uint8_t    n_rec;
    bool       rec_done;
    diag_ff_t  ff[DIAG_FF_MAX];
    uint8_t    n_ff;
    bool       ff_done;

    /* Mereni skutecne rychlosti vycitani. Zadne cislo tady neni
     * odhad - vsechno se pocita z toho, co opravdu odeslo a prislo. */
    uint32_t n_req;        /* odeslanych pozadavku celkem, vcetne 3E 00 */
    uint32_t n_sample;     /* uspesne prectenych hodnot DIDu celkem */
    uint32_t n_fail;       /* davek, ktere skoncily chybou prenosu */
    uint32_t rtt_start;    /* zacatek prave bezici davky DIDu */
    uint32_t rtt_ms;       /* jak dlouho trvala ta posledni */
    uint32_t rate_x100;    /* pozadavku za vterinu, nasobeno stem */
    uint32_t win_start;    /* okno mereni */
    uint32_t win_req;
} diag_poller_t;

/* ------------------------------------------------------------------
 * Rozhrani
 * ------------------------------------------------------------------ */

/* Vynuluje vrstvu a prevezme nastaveni. cfg.send musi byt vyplneno. */
void diag_poller_init(diag_poller_t *p, const diag_poller_cfg_t *cfg);

/* Smime vysilat? false zastavi uplne vsechno - ani 3E 00 neodejde.
 * Prechod na true zahodi vyrazeni odmitnutych DIDu a zacne znovu
 * session, protoze uzivatel prave vedome rekl "ptej se". */
void diag_poller_arm(diag_poller_t *p, bool on);

static inline bool diag_poller_armed(const diag_poller_t *p)
{
    return p->armed;
}

/* Co je prave videt. Zmena pohledu zneplatni rozpis a dalsi kolo se
 * zepta na to, co potrebuje nova stranka. */
void diag_poller_set_view(diag_poller_t *p, diag_view_t v);

static inline diag_view_t diag_poller_view(const diag_poller_t *p)
{
    return (diag_view_t)p->view;
}

/* Jeden ramec ze sbernice. from_us = poslali jsme ho my. */
void diag_poller_on_frame(diag_poller_t *p, uint32_t id, const uint8_t *data,
                          uint8_t dlc, bool from_us, uint32_t now_ms);

/* Casovace, dokonceni davky a zacatek dalsi. Volat z hlavni smycky tak
 * casto, aby mezi dvema volanimi neuplynulo 150 ms (mez ISO-TP). */
void diag_poller_poll(diag_poller_t *p, uint32_t now_ms);

/* ---------------- hodnoty ---------------- */
diag_show_t diag_poller_show(const diag_poller_t *p, int ch);

/* Hodnota v jednotce kanalu. Plati JEN pri DIAG_SHOW_LIVE a
 * DIAG_SHOW_STALE; jindy vraci 0 a to cislo nic neznamena. */
float diag_poller_value(const diag_poller_t *p, int ch);

uint8_t  diag_poller_nrc(const diag_poller_t *p, int ch);

/* Jak dlouho je hodnota stara. UINT32_MAX = jeste nikdy neprisla. */
uint32_t diag_poller_age_ms(const diag_poller_t *p, int ch);

/* Jak stary je NEJMLADSI udaj z jednotky. UINT32_MAX = jeste nic.
 * Tohle je to cislo, ktere patri do horni listy. */
uint32_t diag_poller_fresh_ms(const diag_poller_t *p);

/* Rychlost kanalu, podle ktere se pozna, kdy uz je hodnota stara. */
diag_tier_t diag_poller_tier(int ch);

/* ---------------- stav spoje ---------------- */
static inline const diag_link_t *diag_poller_link(const diag_poller_t *p)
{
    return &p->dl;
}

/* ---------------- mereni ---------------- */

/* Pozadavku odeslanych za vterinu, nasobeno stem. Merene cislo, ne
 * odhad: pocita se v jednovterinovem okne z toho, co opravdu odeslo.
 * Pocita se i tester present - na sbernici je to tentyz provoz. */
static inline uint32_t diag_poller_rate_x100(const diag_poller_t *p)
{
    return p->rate_x100;
}

/* Jak dlouho trvala posledni davka DIDu, od odeslani prvniho dotazu po
 * rozebrani posledni odpovedi. Tohle je ta doba, kterou rozpocet
 * opravdu stoji. */
static inline uint32_t diag_poller_rtt_ms(const diag_poller_t *p)
{
    return p->rtt_ms;
}
static inline uint32_t diag_poller_requests(const diag_poller_t *p)
{
    return p->n_req;
}

/* Kolik hodnot uz jednotka opravdu vydala. */
static inline uint32_t diag_poller_samples(const diag_poller_t *p)
{
    return p->n_sample;
}
static inline uint32_t diag_poller_failures(const diag_poller_t *p)
{
    return p->n_fail;
}

/* ---------------- chybova pamet ---------------- */

/* Dorazila uz nejaka odpoved na 19 02? Dokud ne, UI nesmi tvrdit, ze
 * vuz zadnou chybu nema - prazdny seznam a neprectena pamet vypadaji
 * stejne jen tomu, kdo se neptal. */
static inline bool diag_poller_dtc_known(const diag_poller_t *p)
{
    return p->dtc_done;
}
static inline uint16_t diag_poller_dtc_count(const diag_poller_t *p)
{
    return p->n_dtc;
}
static inline uint16_t diag_poller_dtc_dropped(const diag_poller_t *p)
{
    return p->dtc_dropped;
}
const diag_dtc_t *diag_poller_dtc_at(const diag_poller_t *p, uint16_t i);

/* Vyzadani noveho cteni 19 02. */
void diag_poller_dtc_refresh(diag_poller_t *p);

/* Vyber chyby, ke ktere se maji docist 19 04 a 19 06. -1 = zadna. */
void diag_poller_dtc_select(diag_poller_t *p, int i);
static inline int diag_poller_dtc_selected(const diag_poller_t *p)
{
    return p->dtc_sel;
}

/* Rozsirene zaznamy vybrane chyby. rec_known() rika, jestli odpoved uz
 * dorazila - prazdny seznam jinak nic neznamena. */
static inline bool diag_poller_rec_known(const diag_poller_t *p)
{
    return p->rec_done;
}
static inline uint8_t diag_poller_rec_count(const diag_poller_t *p)
{
    return p->n_rec;
}
const diag_rec_t *diag_poller_rec_at(const diag_poller_t *p, uint8_t i);

static inline bool diag_poller_ff_known(const diag_poller_t *p)
{
    return p->ff_done;
}
static inline uint8_t diag_poller_ff_count(const diag_poller_t *p)
{
    return p->n_ff;
}
const diag_ff_t *diag_poller_ff_at(const diag_poller_t *p, uint8_t i);

/* Kod chyby slovem, treba "P2263-22". Buffer staci 12 bajtu. */
void diag_dtc_text(uint32_t dtc, char *out, unsigned n);

#ifdef __cplusplus
}
#endif

#endif /* DIAG_POLLER_H */
