/*
 * legacy.h - stary diagnosticky protokol Renault nad ISO-TP
 *
 * Polovina vozu X82 nemluvi UDS. Karoserie (BCM 745/765), energetika
 * (EMM 74D/76D) a airbag (752/772) pouzivaji starsi transport z rodiny
 * KWP2000:
 *
 *   10 C0    zahajeni diagnosticke session (odpoved 50 C0)
 *   21 xx    cteni bloku podle jednobajtoveho identifikatoru (odpoved 61 xx)
 *   3B xx    zapis bloku - ZDE NENI, viz nize
 *   3E 00    tester present, stejne jako v UDS (odpoved 7E 00)
 *
 * Ramcovani je stejne ISO 15765-2 jako u UDS, takze pod tim lezi tentyz
 * isotp_link_t - lisi se cisla sluzeb a tvar odpovedi, ne transport.
 * Klient UDS se na tyhle jednotky pouzit neda: 22 xxxx neznaji a dvoubajtovy
 * identifikator u nich nic neznamena.
 *
 * Rozdily proti UDS, ktere stoji za pozornost:
 *
 *   - Identifikator bloku je JEDEN bajt a v jednom dotazu je vzdy jen
 *     jeden; davkovani jako u 22 neexistuje.
 *   - Delka odpovedi se predem nevi a v odpovedi neni. Bere se, co
 *     prislo (21 16 ma 36+ bajtu, 21 46 pres padesat) - proto
 *     legacy_data() + legacy_len() a zadne ocekavane delky.
 *
 * ---------------------------------------------------------------
 * 3B xx - zapis - ZAMERNE NEIMPLEMENTOVANO
 * ---------------------------------------------------------------
 * Tady by byl legacy_write(). Neni a v tomhle souboru nikdy nebude.
 *
 * Zapis do jednotky je podle docs/safety.md oddeleny na urovni SOUBORU:
 * vsechno, co sklada zapis (2E, 3B, 31 01), lezi v src/write/, a
 * build_config.py ten adresar pri CANOGRAF_ALLOW_WRITE=0 z prekladu
 * vyhazuje. Smysl toho stupne je, ze se zapisovy kod do read-only obrazu
 * vubec neprilinkuje - "nm firmware.elf" ho nenajde. Kdyby stavitel
 * zpravy 3B sedel tady, v souboru, ktery se preklada vzdy, byla by ta
 * ochrana jen na papire: staci jedno volani odkudkoli.
 *
 * Az na 3B 16 (konfigurace baterie, prvnich 36 bajtu z 21 16) a
 * 3B 46 (studeny startovaci proud, prvnich 53 bajtu z 21 46) dojde,
 * patri do src/write/legacy_write.c za safety_authorize() a za
 * potvrzovaci dialog, ktery ukazuje bajty pred a po. Cteni, ktere tomu
 * musi predchazet (CLAUDE.md: "zazalohuj kazdy blok, na ktery sahnes"),
 * uz tady je.
 *
 * Tenhle soubor smi zaviset jen na stdbool/stdint, can/isotp.h a
 * diag/uds.h (kvuli spolecnemu cislovani zapornych odpovedi - 7F sid nrc
 * vypada u obou protokolu stejne, druha tabulka tehoz by byla chyba).
 */
#ifndef DIAG_LEGACY_H
#define DIAG_LEGACY_H

#include <stdbool.h>
#include <stdint.h>

#include "can/isotp.h"
#include "diag/uds.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sluzby */
#define LEGACY_SID_SESSION 0x10u
#define LEGACY_SID_READ    0x21u
#define LEGACY_SID_WRITE   0x3Bu   /* jen pro rozpoznani, NEPOSILA SE, viz vyse */
#define LEGACY_SID_TESTER  0x3Eu

/* Diagnosticka session techto jednotek. Jine poddruhy se na X82
 * nepouzivaji. */
#define LEGACY_SESSION 0xC0u

/* Nejdelsi blok, ktery prevezmeme. Nejdelsi zmereny je 21 46 s 53
 * datovymi bajty; 256 necha misto i na nezname bloky. */
#ifndef LEGACY_RSP_MAX
#define LEGACY_RSP_MAX 256u
#endif

/* Udrzeni session je stejne jako v UDS a ze stejnych duvodu - viz
 * rozbor v uds.h. Konstanty se zamerne nekopiruji. */
#define LEGACY_S3_MS     UDS_S3_MS
#define LEGACY_TESTER_MS UDS_TESTER_MS

typedef enum {
    LEGACY_IDLE = 0,
    LEGACY_BUSY,
    LEGACY_DONE,
    LEGACY_FAILED
} legacy_state_t;

/* Chyby maji stejny vyznam jako u UDS, tak at maji i stejna jmena -
 * uds_err_name() je prelozi pro obe vrstvy. */
typedef uds_err_t legacy_err_t;

typedef struct {
    isotp_link_t *link;

    bool     session_ok;
    bool     keepalive;    /* 10 C0 session vyprsi, drzi se pres 3E 00 */

    uint8_t  op;           /* 0 = nic, 1 = pozadavek na ceste */
    uint8_t  step;
    uint8_t  state;        /* legacy_state_t */
    uint8_t  err;          /* legacy_err_t */
    uint8_t  nrc;
    uint8_t  req_sid;
    uint8_t  block;        /* identifikator prave ctenneho bloku */
    bool     internal;     /* bezi 3E 00 */
    bool     resumed;

    uint32_t t_last_req;

    uint8_t  pend[4];
    uint8_t  pend_len;

    uint8_t  rsp[LEGACY_RSP_MAX];
    uint16_t rsp_len;

    uint32_t n_req_sent;
    uint32_t n_rsp;
    uint32_t n_nrc;
    uint32_t n_session_lost;
} legacy_t;

/* ------------------------------------------------------------------
 * Rozhrani - stejny tvar jako uds.h, aby se to nemuselo ucit dvakrat
 * ------------------------------------------------------------------ */
void legacy_init(legacy_t *l, isotp_link_t *link);
void legacy_abort(legacy_t *l);

/* 10 C0. Po uspechu si klient session udrzuje sam. */
bool legacy_session(legacy_t *l, uint32_t now_ms);

/* 21 <block>. Kdyz session mezitim spadla, klient ji pred dotazem sam
 * obnovi. Delka odpovedi se predem nevi - bere se, co prislo. */
bool legacy_read(legacy_t *l, uint8_t block, uint32_t now_ms);

legacy_state_t legacy_poll(legacy_t *l, uint32_t now_ms);
void legacy_on_frame(legacy_t *l, uint32_t id, const uint8_t *data,
                     uint8_t dlc, uint32_t now_ms);

/* Data bloku bez hlavicky 61 xx, platna od LEGACY_DONE do dalsiho
 * pozadavku. NULL / 0, kdyz se nic neprecetlo. */
const uint8_t *legacy_data(const legacy_t *l);
uint16_t legacy_len(const legacy_t *l);

/* Identifikator bloku, ktery v odpovedi opravdu prisel. */
uint8_t legacy_block(const legacy_t *l);

/* Syrova odpoved vcetne 61 xx - pro odposlech. */
const uint8_t *legacy_rsp(const legacy_t *l);
uint16_t legacy_rsp_len(const legacy_t *l);

static inline legacy_state_t legacy_state(const legacy_t *l) { return (legacy_state_t)l->state; }
static inline legacy_err_t   legacy_error(const legacy_t *l) { return (legacy_err_t)l->err; }
static inline uint8_t        legacy_nrc(const legacy_t *l)   { return l->nrc; }
static inline bool legacy_session_ok(const legacy_t *l) { return l->session_ok; }
static inline bool legacy_busy(const legacy_t *l) { return l->op != 0u; }
static inline uint32_t legacy_rsp_count(const legacy_t *l) { return l->n_rsp; }

#ifdef __cplusplus
}
#endif

#endif /* DIAG_LEGACY_H */
