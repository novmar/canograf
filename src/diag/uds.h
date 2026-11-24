/*
 * uds.h - diagnosticky klient UDS (ISO 14229-1) nad ISO-TP
 *
 * Stejny vzor jako can/isotp.h: zadne hodiny, zadny hardware, zadne
 * vlakno. Cas chodi v parametru, ramce odchazi pres spoj isotp_link_t,
 * ktery vlastni volajici. Cely soubor se preklada i na hostiteli -
 * viz test/uds_test.c.
 *
 * Co to umi:
 *
 *   - 10 xx zahajeni session a jeji UDRZOVANI pres 3E 00 (viz nize)
 *   - 22 xxxx cteni jednoho i vice DIDu; davkovani po
 *     UDS_DIDS_PER_REQ a rozebrani odpovedi zpatky po jednotlivych DIDech
 *   - 19 02 / 19 04 / 19 06 pamet chyb, freeze frame, rozsirene zaznamy
 *   - rozklad zaporne odpovedi 7F sid nrc na konkretni duvod
 *
 * Co to NEUMI a umet nema: zapis. Zadne 2E, 3B, 31 ani 27 - zapisovy
 * kod patri do src/write/ za branu z docs/safety.md a v read-only
 * obrazu nesmi byt vubec prilinkovany.
 *
 * ---------------------------------------------------------------
 * Jak casto 3E 00 a proc
 * ---------------------------------------------------------------
 * Jednotka si na nediagnostickou session drzi casovac S3server; podle
 * ISO 14229 je vychozi 5 s a MD1CS006 se tak chova - po par vterinach
 * ticha spadne zpatky do default session a dalsi 22 uz odpovi zaporne.
 * KAZDY nas pozadavek S3 resetuje, takze tester present je potreba jen
 * v dobe, kdy se neptame.
 *
 * Posilame ho po UDS_TESTER_MS = 2000 ms ticha, tedy S3/2 se zalohou:
 *
 *   - Dve prilezitosti pred uzaverkou. Kdyz jeden 3E 00 propadne
 *     (plna fronta, prohrana arbitraz, hlavni smycka se zasekla na
 *     prekresleni), druhy ve 4000 ms session jeste zachrani.
 *   - Na sbernici to neni videt. Jeden ramec za dve vteriny je proti
 *     provozu X82 (tisice ramcu za vterinu) nula.
 *   - Kratsi perioda nic nepridava. S3 se resetuje i beznym ctenim, a
 *     kdyz se ctou zive hodnoty, tester present neodejde vubec.
 *
 * Posila se 3E 00, NE 3E 80 (suppressPosRspMsgIndicationBit). Potlaceni
 * odpovedi by uslo o jeden ramec levneji, ale pak nevime vubec nic:
 * 7E 00 je jediny dukaz, ze jednotka jeste zije, kdyz se zrovna na nic
 * neptame. Prave z nej zije treti stupen detekce v diag/link.h.
 *
 * Pokud se session ztrati (ticho delsi nez UDS_S3_MS, nebo jednotka
 * odpovi 7F xx 7F), klient ji pred dalsim pozadavkem SAM obnovi: posle
 * 10 xx znovu a teprve pak puvodni dotaz. Uprostred davky DIDu to tedy
 * neni videt jinak nez zdrzenim.
 *
 * ---------------------------------------------------------------
 * Pouziti
 * ---------------------------------------------------------------
 *     isotp_cfg_t cfg; isotp_cfg_default(&cfg);
 *     cfg.tx_id = 0x7E0; cfg.rx_id = 0x7E8; cfg.send = can_isotp_send;
 *     isotp_init(&link, &cfg);
 *     uds_init(&ecu, &link);
 *
 *     uds_session(&ecu, UDS_SESSION_EXTENDED, now);
 *     ... v cyklu:
 *     uds_poll(&ecu, now);                         // casovace, davka, 3E 00
 *     uds_on_frame(&ecu, id, d, dlc, now);         // kazdy prijaty ramec
 *
 *     static const uds_did_req_t want[] = {
 *         { 0x2002, 2 }, { 0x2001, 2 }, { 0x2006, 3 },
 *     };
 *     uds_read_dids(&ecu, want, 3, now);
 *     ... az uds_state() == UDS_DONE, vysledky cti pres uds_did_find().
 *
 * Klient si NIC neplanuje sam: kdo nevola uds_poll(), tomu prenos stoji
 * a session vyprsi.
 *
 * Tenhle soubor smi zaviset jen na stdbool/stdint a na can/isotp.h.
 */
#ifndef DIAG_UDS_H
#define DIAG_UDS_H

#include <stdbool.h>
#include <stdint.h>

#include "can/isotp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * Meze
 * ------------------------------------------------------------------ */

/* Kolik DIDu se vejde do jednoho 22. Overeno na voze: MD1CS006 prijme
 * nejvyse tri, ctvrty uz cely dotaz odmitne 7F 22 31. Neni to mez
 * normy, je to mez teto jednotky - proto makro a ne konstanta v kodu. */
#ifndef UDS_DIDS_PER_REQ
#define UDS_DIDS_PER_REQ 3u
#endif

/* Kolik DIDu smi byt v jedne davce uds_read_dids(). Jedna obrazovka
 * zivych hodnot jich chce okolo desiti. */
#ifndef UDS_BATCH_MAX
#define UDS_BATCH_MAX 16u
#endif

/* Spolecne uloziste dat vsech DIDu v davce. Davka se sklada z vice
 * odpovedi a kazda dalsi prepise rsp[], takze hotove hodnoty musi mit
 * vlastni misto. */
#ifndef UDS_DATA_POOL
#define UDS_DATA_POOL 256u
#endif

/* Nejdelsi data jednoho DIDu. Nejdelsi zname bloky teto platformy maji
 * 53 bajtu, ale to je stary protokol Renault (diag/legacy.h); UDS DIDy
 * na X82 maji 1 az 3 bajty. */
#ifndef UDS_DID_LEN_MAX
#define UDS_DID_LEN_MAX 64u
#endif

/* Posledni odpoved v syrove podobe. 19 02 ff vrati pres sto zaznamu po
 * ctyrech bajtech, proto cely buffer ISO-TP. */
#define UDS_RSP_MAX ISOTP_BUF_SIZE

/* Casovani udrzeni session, duvod viz hlavicka souboru. */
#define UDS_S3_MS     5000u   /* po tak dlouhem tichu session povazujeme za ztracenou */
#define UDS_TESTER_MS 2000u   /* kdy odesilame 3E 00 */

/* ------------------------------------------------------------------
 * Sluzby a poddruhy
 * ------------------------------------------------------------------ */
#define UDS_SID_SESSION   0x10u
#define UDS_SID_READ_DTC  0x19u
#define UDS_SID_READ_DID  0x22u
#define UDS_SID_TESTER    0x3Eu
#define UDS_SID_NEGATIVE  0x7Fu

#define UDS_SESSION_DEFAULT  0x01u
#define UDS_SESSION_PROG     0x02u
#define UDS_SESSION_EXTENDED 0x03u

#define UDS_DTC_BY_STATUS 0x02u   /* 19 02 maska - seznam se stavovymi bajty */
#define UDS_DTC_SNAPSHOT  0x04u   /* 19 04 dtc zaznam - freeze frame */
#define UDS_DTC_EXTENDED  0x06u   /* 19 06 dtc zaznam - rozsirene zaznamy */

/* Stavovy bajt chyby, ISO 14229 priloha D. Klidovy stav skoro vsech
 * zaznamu na X82 je 0x50 (nikdy nebezelo od posledniho mazani) - to
 * NENI chyba. Co se ma ukazat uzivateli, filtruj pres UDS_DTC_SHOW. */
#define UDS_DTC_TEST_FAILED      0x01u
#define UDS_DTC_FAILED_CYCLE     0x02u
#define UDS_DTC_PENDING          0x04u
#define UDS_DTC_CONFIRMED        0x08u
#define UDS_DTC_NOT_DONE_CLEAR   0x10u
#define UDS_DTC_FAILED_CLEAR     0x20u
#define UDS_DTC_NOT_DONE_CYCLE   0x40u
#define UDS_DTC_WARNING          0x80u
#define UDS_DTC_SHOW             0x2Fu   /* doporuceny filtr, viz docs/vehicles */

/* ------------------------------------------------------------------
 * Zaporne odpovedi
 *
 * 0x78 (responsePending) se sem nedostane, ten resi uz isotp. Vsechno
 * ostatni musi dojit nahoru jako konkretni duvod - "nepovedlo se" je
 * pri hledani zavady k nicemu.
 * ------------------------------------------------------------------ */
typedef enum {
    UDS_NRC_NONE             = 0x00u,
    UDS_NRC_GENERAL_REJECT   = 0x10u,
    UDS_NRC_SERVICE_NOT_SUP  = 0x11u,
    UDS_NRC_SUBFUNC_NOT_SUP  = 0x12u,
    UDS_NRC_BAD_LENGTH       = 0x13u,  /* spatna delka nebo pocet polozek */
    UDS_NRC_RESPONSE_TOO_LONG= 0x14u,
    UDS_NRC_BUSY             = 0x21u,
    UDS_NRC_CONDITIONS       = 0x22u,  /* podminky nesplneny (motor bezi, ...) */
    UDS_NRC_REQUEST_SEQUENCE = 0x24u,
    UDS_NRC_OUT_OF_RANGE     = 0x31u,  /* tenhle DID jednotka nezna */
    UDS_NRC_ACCESS_DENIED    = 0x33u,  /* chce security access */
    UDS_NRC_INVALID_KEY      = 0x35u,
    UDS_NRC_PENDING          = 0x78u,  /* resi isotp, sem nedojde */
    UDS_NRC_NOT_IN_SESSION   = 0x7Fu   /* session spadla, nebo je potreba jina */
} uds_nrc_t;

/* Kratky nazev duvodu pro log a pro vyvojare. Neni to text pro
 * uzivatele - ten patri do lang/ pres T(), az se tohle bude vesat na
 * UI. Stejna dohoda jako u isotp_err_name(). */
const char *uds_nrc_name(uint8_t nrc);

/* ------------------------------------------------------------------
 * Stavy a chyby klienta
 * ------------------------------------------------------------------ */
typedef enum {
    UDS_IDLE = 0,   /* nic se nedeje */
    UDS_BUSY,       /* pozadavek na ceste, pripadne i nekolik za sebou */
    UDS_DONE,       /* vysledek lezi v klientovi */
    UDS_FAILED      /* duvod rekne uds_error(), pripadne uds_nrc() */
} uds_state_t;

typedef enum {
    UDS_OK = 0,
    UDS_ERR_BUSY,       /* predchozi pozadavek jeste bezi */
    UDS_ERR_BADARG,     /* nesmyslny pozadavek, nic neodeslo */
    UDS_ERR_SEND,       /* transport ramec neodeslal (listen-only, fronta) */
    UDS_ERR_TRANSPORT,  /* chyba ISO-TP, detail rekne isotp_error() */
    UDS_ERR_TIMEOUT,    /* jednotka neodpovedela */
    UDS_ERR_NRC,        /* jednotka odmitla, duvod v uds_nrc() */
    UDS_ERR_MISMATCH,   /* odpoved patri k jine sluzbe nez nas dotaz */
    UDS_ERR_PARSE,      /* odpoved se neda rozebrat */
    UDS_ERR_SESSION     /* session se nepodarilo obnovit */
} uds_err_t;

const char *uds_err_name(uds_err_t e);

/* ------------------------------------------------------------------
 * Cteni DIDu
 *
 * Delka dat se pro kazdy DID uvadi predem a je to nutne: odpoved na
 * 22 slepi hodnoty za sebe bez jakehokoli oddelovace, takze bez znalosti
 * delek se vicenasobna odpoved rozebrat NEDA. Delky zna profil vozidla
 * (docs/vehicles, sloupec Bytes).
 *
 * len == 0 znamena "nevim, vezmi zbytek odpovedi". Takovy DID se vzdy
 * posila sam, protoze za nim uz by nebylo poznat, kde dalsi zacina.
 * ------------------------------------------------------------------ */
typedef struct {
    uint16_t did;
    uint8_t  len;   /* ocekavana delka dat; 0 = neznama, ptame se samostatne */
} uds_did_req_t;

typedef enum {
    UDS_DID_PENDING = 0,  /* jeste se na nej nedoslo */
    UDS_DID_OK,           /* data jsou k dispozici */
    UDS_DID_REFUSED,      /* jednotka odmitla, duvod v .nrc */
    UDS_DID_MISSING,      /* odpoved prisla, ale tenhle DID v ni nebyl */
    UDS_DID_NO_ROOM       /* data se nevesla do UDS_DATA_POOL / UDS_DID_LEN_MAX */
} uds_did_status_t;

typedef struct {
    uint16_t did;
    uint8_t  status;   /* uds_did_status_t */
    uint8_t  nrc;      /* platny jen pri UDS_DID_REFUSED */
    uint8_t  len;      /* delka dat, jen pri UDS_DID_OK */
    uint16_t off;      /* kam ukazuje do spolecneho uloziste */
} uds_did_res_t;

typedef struct {
    uint32_t dtc;      /* tri bajty kodu, 0x00P0234 ... */
    uint8_t  status;   /* stavovy bajt, bity vyse */
} uds_dtc_t;

/* ------------------------------------------------------------------
 * Klient
 *
 * Struktura je verejna kvuli statickemu umisteni (zadny malloc),
 * polozky ale patri implementaci - cti je pres funkce nize.
 * ------------------------------------------------------------------ */
typedef struct {
    isotp_link_t *link;      /* spoj vlastni volajici, klient ho jen rizeni */

    uint8_t  want_sub;       /* session, kterou drzime; 0 = zadnou */
    bool     session_ok;
    bool     keepalive;      /* posilat 3E 00 (jen u nedefaultni session) */

    uint8_t  op;             /* co prave bezi, interni */
    uint8_t  step;           /* obnova session vs. vlastni dotaz, interni */
    uint8_t  state;          /* uds_state_t */
    uint8_t  err;            /* uds_err_t */
    uint8_t  nrc;            /* posledni zaporna odpoved */
    uint8_t  req_sid;        /* sluzba posledniho odeslaneho pozadavku */
    bool     internal;       /* bezi 3E 00 - verejny stav se nesmi hnout */
    bool     resumed;        /* v teto operaci uz jsme session obnovovali */

    uint32_t t_last_req;     /* kdy odesel posledni pozadavek, pro S3 */

    uint8_t  pend[8];        /* jednorazovy pozadavek, aby sel po obnove zopakovat */
    uint8_t  pend_len;

    uds_did_req_t req[UDS_BATCH_MAX];
    uds_did_res_t res[UDS_BATCH_MAX];
    uint8_t  n_req;
    uint8_t  req_done;       /* prvni jeste nevyrizeny DID */
    uint8_t  chunk_first;
    uint8_t  chunk_n;
    bool     split;          /* davku odmitli, ptame se po jednom */
    uint8_t  split_end;      /* dokud sem req_done nedojde, zustava split */

    uint8_t  pool[UDS_DATA_POOL];
    uint16_t pool_used;

    uint8_t  rsp[UDS_RSP_MAX];
    uint16_t rsp_len;

    uint32_t n_req_sent;     /* citace pro diagnostiku a pro diag/link.h */
    uint32_t n_rsp;
    uint32_t n_nrc;
    uint32_t n_session_lost;
} uds_client_t;

/* ------------------------------------------------------------------
 * Rozhrani
 * ------------------------------------------------------------------ */

/* Vynuluje klienta a prevezme spoj. Spoj musi byt uz nastaveny
 * (isotp_init) a nesmi ho soubezne pouzivat nikdo jiny. */
void uds_init(uds_client_t *c, isotp_link_t *link);

/* Zahodi rozdelanou operaci i rozdelany prenos. Session se tim
 * nezahazuje - jednotka o nasem abortu nevi. */
void uds_abort(uds_client_t *c);

/* 10 sub. Po uspechu si klient session UDRZUJE sam pres 3E 00; u
 * UDS_SESSION_DEFAULT se neudrzuje nic, default session nevyprsi.
 * false = nezacalo se, duvod rekne uds_error(). */
bool uds_session(uds_client_t *c, uint8_t sub, uint32_t now_ms);

/* 22 - jeden az UDS_BATCH_MAX DIDu. Davkuje se samo po UDS_DIDS_PER_REQ
 * a odpovedi se rozeberou zpatky po DIDech.
 *
 * Kdyz jednotka celou davku odmitne 7F 22 31 nebo 7F 22 13 (jeden z DIDu
 * je ji cizi, ale ktery nerekne), klient se na DIDy te davky zepta po
 * jednom. Ostatni hodnoty se tim zachrani - jeden neznamy DID v seznamu
 * nesmi shodit cele cteni.
 *
 * ODMITNUTY DID NENI CHYBA OPERACE. Operace skonci v UDS_DONE a
 * odmitnuty DID ma status UDS_DID_REFUSED a svuj vlastni nrc. Do
 * UDS_FAILED spadne az selhani prenosu nebo ztracena session - tedy
 * neco, co se tyka vsech DIDu v davce. */
bool uds_read_dids(uds_client_t *c, const uds_did_req_t *dids, uint8_t n,
                   uint32_t now_ms);

/* Jeden DID; len == 0 znamena "vezmi zbytek odpovedi". */
bool uds_read_did(uds_client_t *c, uint16_t did, uint8_t len, uint32_t now_ms);

/* 19 02 maska - seznam chyb se stavovymi bajty. Maska 0xFF vrati
 * vsechno, co jednotka zna (na X82 pres sto zaznamu, skoro vsechny v
 * klidovem 0x50), maska 0x01 jen to, co pravi hori. */
bool uds_read_dtcs(uds_client_t *c, uint8_t status_mask, uint32_t now_ms);

/* 19 04 dtc zaznam - freeze frame. record 0xFF = vsechny zaznamy. */
bool uds_read_freeze(uds_client_t *c, uint32_t dtc, uint8_t record,
                     uint32_t now_ms);

/* 19 06 dtc zaznam - rozsirene zaznamy (pocet vyskytu, km pri prvnim
 * vyskytu, ujeta vzdalenost se zavadou). Cti je DRIV, nez chybu nekdo
 * smaze nebo nez se sama vyleci - pak uz jsou pryc. */
bool uds_read_extended(uds_client_t *c, uint32_t dtc, uint8_t record,
                       uint32_t now_ms);

/* Casovace, pokracovani davky a udrzeni session. Volat z hlavni smycky
 * tak casto, aby mezi dvema volanimi neuplynulo 150 ms (mez ISO-TP). */
uds_state_t uds_poll(uds_client_t *c, uint32_t now_ms);

/* Jeden prijaty CAN ramec; cizi identifikatory si odfiltruje spoj sam,
 * takze se sem da sypat cela sbernice. */
void uds_on_frame(uds_client_t *c, uint32_t id, const uint8_t *data,
                  uint8_t dlc, uint32_t now_ms);

/* ---------------- vysledky ----------------
 * Plati od UDS_DONE (a u davky i v UDS_FAILED - co se stihlo precist,
 * zustava citelne) az do zacatku dalsiho pozadavku. */

/* Kolik DIDu bylo v posledni davce a jak dopadly. */
uint8_t uds_did_count(const uds_client_t *c);
const uds_did_res_t *uds_did_at(const uds_client_t *c, uint8_t i);
const uds_did_res_t *uds_did_find(const uds_client_t *c, uint16_t did);

/* Data DIDu, nebo NULL kdyz status neni UDS_DID_OK. */
const uint8_t *uds_did_bytes(const uds_client_t *c, const uds_did_res_t *r);

/* Hodnota DIDu jako cislo, bajty v poradi od nejvyssiho (tak je posila
 * jednotka). false = nebylo precteno nebo je delsi nez ctyri bajty; v
 * tom pripade se *out NESAHA. Nic se nepredstira. */
bool uds_did_u32(const uds_client_t *c, uint16_t did, uint32_t *out);

/* Syrova posledni odpoved vcetne sluzby na prvnim bajtu - pro 19 04,
 * 19 06 a pro odposlech. */
const uint8_t *uds_rsp(const uds_client_t *c);
uint16_t uds_rsp_len(const uds_client_t *c);

/* Rozebrani odpovedi na 19 02. Vraci 0, kdyz posledni odpoved neni
 * 59 02. */
uint16_t uds_dtc_count(const uds_client_t *c);
bool uds_dtc_at(const uds_client_t *c, uint16_t i, uds_dtc_t *out);

/* ---------------- stav ---------------- */
static inline uds_state_t uds_state(const uds_client_t *c) { return (uds_state_t)c->state; }
static inline uds_err_t   uds_error(const uds_client_t *c) { return (uds_err_t)c->err; }
static inline uint8_t     uds_nrc(const uds_client_t *c)   { return c->nrc; }
static inline bool uds_session_ok(const uds_client_t *c)   { return c->session_ok; }

/* true = na sbernici je nas pozadavek, vcetne interniho 3E 00. Novy
 * pozadavek se v te dobe odmitne s UDS_ERR_BUSY; tester present je
 * jeden ramec tam a zpet, takze se o nej ceka nejvyse par milisekund. */
static inline bool uds_busy(const uds_client_t *c) { return c->op != 0u; }

/* Kolik odpovedi od jednotky uz prislo, vcetne zapornych a vcetne
 * odpovedi na tester present. Kdyz se tohle cislo zvysilo, jednotka
 * prokazatelne zije - presne tim se krmi diag_link_on_reply(). */
static inline uint32_t uds_rsp_count(const uds_client_t *c) { return c->n_rsp; }

#ifdef __cplusplus
}
#endif

#endif /* DIAG_UDS_H */
