/*
 * uds.c - implementace klienta UDS, viz uds.h
 *
 * Zadne hodiny, zadne vlakno, zadny hardware. Cas chodi v parametru,
 * ramce odchazi pres isotp_link_t. Cely soubor se preklada i na
 * hostiteli.
 */
#include "uds.h"

#include <string.h>

/* Co prave bezi. Drzi se v uds_client_t.op; nula znamena "nic", aby
 * uds_busy() byl jen test na nulu. */
#define OP_NONE   0u
#define OP_SINGLE 1u   /* jeden pozadavek ulozeny v pend[] */
#define OP_DIDS   2u   /* davka DIDu v req[] */

/* Ktery z dvojice pozadavku je prave na ceste. Obnova session je
 * mezikrok uvnitr operace, ne samostatna operace - diky tomu se dotaz
 * po obnove zopakuje sam. */
#define STEP_NONE    0u
#define STEP_SESSION 1u
#define STEP_REQ     2u

/* ------------------------------------------------------------------
 * Drobnosti
 * ------------------------------------------------------------------ */

const char *uds_nrc_name(uint8_t nrc)
{
    switch (nrc) {
    case UDS_NRC_NONE:              return "none";
    case UDS_NRC_GENERAL_REJECT:    return "general-reject";
    case UDS_NRC_SERVICE_NOT_SUP:   return "service-not-supported";
    case UDS_NRC_SUBFUNC_NOT_SUP:   return "subfunction-not-supported";
    case UDS_NRC_BAD_LENGTH:        return "wrong-length";
    case UDS_NRC_RESPONSE_TOO_LONG: return "response-too-long";
    case UDS_NRC_BUSY:              return "ecu-busy";
    case UDS_NRC_CONDITIONS:        return "conditions-not-correct";
    case UDS_NRC_REQUEST_SEQUENCE:  return "request-sequence-error";
    case UDS_NRC_OUT_OF_RANGE:      return "request-out-of-range";
    case UDS_NRC_ACCESS_DENIED:     return "security-access-denied";
    case UDS_NRC_INVALID_KEY:       return "invalid-key";
    case UDS_NRC_PENDING:           return "response-pending";
    case UDS_NRC_NOT_IN_SESSION:    return "not-in-active-session";
    default:                        return "nrc-?";
    }
}

const char *uds_err_name(uds_err_t e)
{
    switch (e) {
    case UDS_OK:            return "ok";
    case UDS_ERR_BUSY:      return "busy";
    case UDS_ERR_BADARG:    return "bad-argument";
    case UDS_ERR_SEND:      return "send-failed";
    case UDS_ERR_TRANSPORT: return "transport";
    case UDS_ERR_TIMEOUT:   return "no-response";
    case UDS_ERR_NRC:       return "negative-response";
    case UDS_ERR_MISMATCH:  return "wrong-service";
    case UDS_ERR_PARSE:     return "unparseable";
    case UDS_ERR_SESSION:   return "session-lost";
    default:                return "?";
    }
}

/* ------------------------------------------------------------------
 * Vnitrni pomocnici
 * ------------------------------------------------------------------ */

static void finish_ok(uds_client_t *c)
{
    c->op = OP_NONE;
    c->step = STEP_NONE;
    c->internal = false;
    c->err = (uint8_t)UDS_OK;
    c->state = (uint8_t)UDS_DONE;
}

/* Konec operace chybou. Interni tester present verejnym stavem ani
 * chybou NEHYBE - uzivatel ceka na svuj vysledek a 3E 00 si o nej
 * rikat nesmi. */
static void fail_op(uds_client_t *c, uds_err_t e)
{
    c->op = OP_NONE;
    c->step = STEP_NONE;
    if (c->internal) {
        c->internal = false;
        return;
    }
    c->err = (uint8_t)e;
    c->state = (uint8_t)UDS_FAILED;
}

/* Odeslani pozadavku. Nesaha na verejny stav, duvod vraci - o tom, co
 * se s chybou stane, rozhoduje volajici. */
static uds_err_t send_req(uds_client_t *c, const uint8_t *msg, uint8_t len,
                          uint32_t now)
{
    if (c->link == NULL) {
        return UDS_ERR_BADARG;
    }
    if (!isotp_send(c->link, msg, len, now)) {
        return (isotp_error(c->link) == ISOTP_ERR_SEND) ? UDS_ERR_SEND
                                                        : UDS_ERR_TRANSPORT;
    }
    c->req_sid = msg[0];
    c->t_last_req = now;
    c->n_req_sent++;
    return UDS_OK;
}

static void send_session(uds_client_t *c, uint32_t now)
{
    uint8_t msg[2];
    uds_err_t e;

    msg[0] = UDS_SID_SESSION;
    msg[1] = c->want_sub;
    c->step = STEP_SESSION;
    e = send_req(c, msg, 2u, now);
    if (e != UDS_OK) {
        fail_op(c, e);
    }
}

/* ---------------- davka DIDu ---------------- */

/* Kolik DIDu se posle v dalsim dotazu. Pri rozpadu davky (split) nebo u
 * DIDu s neznamou delkou vzdy jen jeden. */
static void plan_chunk(uds_client_t *c)
{
    uint8_t i = c->req_done;
    uint8_t n = 1u;

    c->chunk_first = i;
    if (c->split || c->req[i].len == 0u) {
        c->chunk_n = 1u;
        return;
    }
    while (n < (uint8_t)UDS_DIDS_PER_REQ && (uint8_t)(i + n) < c->n_req &&
           c->req[i + n].len != 0u) {
        n++;
    }
    c->chunk_n = n;
}

static void chunk_done(uds_client_t *c)
{
    c->req_done = (uint8_t)(c->chunk_first + c->chunk_n);
    if (c->split && c->req_done >= c->split_end) {
        c->split = false;
    }
}

static uds_err_t send_chunk(uds_client_t *c, uint32_t now)
{
    uint8_t msg[1u + 2u * UDS_DIDS_PER_REQ];
    uint8_t n = 0u;
    uint8_t i;

    msg[n++] = UDS_SID_READ_DID;
    for (i = 0u; i < c->chunk_n; i++) {
        uint16_t did = c->req[c->chunk_first + i].did;
        msg[n++] = (uint8_t)(did >> 8);
        msg[n++] = (uint8_t)(did & 0xFFu);
    }
    c->step = STEP_REQ;
    return send_req(c, msg, n, now);
}

/* Dalsi krok operace: obnovit session, poslat dalsi dotaz, nebo
 * skoncit. Jediny bod, ze ktereho odchazi uzivatelsky pozadavek. */
static void advance(uds_client_t *c, uint32_t now)
{
    uds_err_t e;

    if (c->op == OP_DIDS) {
        if (c->req_done >= c->n_req) {
            finish_ok(c);
            return;
        }
        if (!c->session_ok && c->want_sub != 0u) {
            send_session(c, now);
            return;
        }
        plan_chunk(c);
        e = send_chunk(c, now);
        if (e != UDS_OK) {
            fail_op(c, e);
        }
        return;
    }

    /* OP_SINGLE. Tester present a samo 10 xx session neobnovuji - prvni
     * ji drzi a druhe ji prave zrizuje. */
    if (!c->session_ok && c->want_sub != 0u &&
        c->pend[0] != UDS_SID_SESSION && c->pend[0] != UDS_SID_TESTER) {
        send_session(c, now);
        return;
    }
    c->step = STEP_REQ;
    e = send_req(c, c->pend, c->pend_len, now);
    if (e != UDS_OK) {
        fail_op(c, e);
    }
}

/* ------------------------------------------------------------------
 * Rozebrani odpovedi na 22
 * ------------------------------------------------------------------ */

static bool chunk_find(const uds_client_t *c, uint16_t did, uint8_t *idx)
{
    uint8_t i;

    for (i = 0u; i < c->chunk_n; i++) {
        uint8_t k = (uint8_t)(c->chunk_first + i);
        if (c->req[k].did == did && c->res[k].status == (uint8_t)UDS_DID_PENDING) {
            *idx = k;
            return true;
        }
    }
    return false;
}

static void store_did(uds_client_t *c, uint8_t idx, const uint8_t *src,
                      uint16_t n)
{
    if (n > (uint16_t)UDS_DID_LEN_MAX ||
        (uint32_t)c->pool_used + n > (uint32_t)UDS_DATA_POOL) {
        /* Data se nevesla. Orez by znamenal ukazat cislo, ktere se
         * neprecetlo cele - radsi nic. Syrova odpoved zustava v rsp[]. */
        c->res[idx].status = (uint8_t)UDS_DID_NO_ROOM;
        c->res[idx].len = 0u;
        return;
    }
    memcpy(&c->pool[c->pool_used], src, n);
    c->res[idx].off = c->pool_used;
    c->res[idx].len = (uint8_t)n;
    c->res[idx].status = (uint8_t)UDS_DID_OK;
    c->pool_used = (uint16_t)(c->pool_used + n);
}

/* Odpoved na 22 je 62 <did> <data> <did> <data> ... bez jakehokoli
 * oddelovace a bez delek - rozebrat ji jde jen proti ocekavanym
 * delkam, ktere prisly v pozadavku. Poradi se nehlida, parujeme podle
 * identifikatoru; co v odpovedi nebylo, skonci jako UDS_DID_MISSING. */
static void parse_dids(uds_client_t *c, uint32_t now)
{
    uint16_t p = 1u;
    uint8_t  i;

    while ((uint16_t)(p + 2u) <= c->rsp_len) {
        uint16_t did = (uint16_t)(((uint16_t)c->rsp[p] << 8) | c->rsp[p + 1u]);
        uint16_t dlen;
        uint8_t  idx;

        p = (uint16_t)(p + 2u);
        if (!chunk_find(c, did, &idx)) {
            /* Jednotka vratila neco, na co jsme se neptali. Dal uz se
             * odpoved rozebrat neda - neznam delku, neznam hranice. */
            fail_op(c, UDS_ERR_PARSE);
            return;
        }
        dlen = (c->req[idx].len != 0u) ? c->req[idx].len
                                       : (uint16_t)(c->rsp_len - p);
        if ((uint32_t)p + dlen > c->rsp_len) {
            fail_op(c, UDS_ERR_PARSE);
            return;
        }
        store_did(c, idx, &c->rsp[p], dlen);
        p = (uint16_t)(p + dlen);
    }

    for (i = 0u; i < c->chunk_n; i++) {
        uds_did_res_t *r = &c->res[c->chunk_first + i];
        if (r->status == (uint8_t)UDS_DID_PENDING) {
            r->status = (uint8_t)UDS_DID_MISSING;
        }
    }
    chunk_done(c);
    advance(c, now);
}

/* ------------------------------------------------------------------
 * Zaporna odpoved
 * ------------------------------------------------------------------ */
static void refuse_chunk(uds_client_t *c, uint8_t nrc)
{
    uint8_t i;

    for (i = 0u; i < c->chunk_n; i++) {
        uds_did_res_t *r = &c->res[c->chunk_first + i];
        if (r->status == (uint8_t)UDS_DID_PENDING) {
            r->status = (uint8_t)UDS_DID_REFUSED;
            r->nrc = nrc;
        }
    }
    chunk_done(c);
}

static void handle_nrc(uds_client_t *c, uint8_t nrc, uint32_t now)
{
    c->nrc = nrc;
    c->n_nrc++;

    if (nrc == (uint8_t)UDS_NRC_NOT_IN_SESSION) {
        /* Session spadla pod rukama. Jednou ji obnovime a dotaz
         * zopakujeme; podruhe uz to vzdavame, aby se klient nezacyklil
         * na jednotce, ktera tu session proste nedava. */
        c->session_ok = false;
        c->n_session_lost++;
        if (c->step != STEP_SESSION && c->want_sub != 0u && !c->resumed) {
            c->resumed = true;
            advance(c, now);
            return;
        }
        fail_op(c, UDS_ERR_SESSION);
        return;
    }

    if (c->op == OP_DIDS && c->step == STEP_REQ) {
        /* Jednotka odmitne CELY dotaz, i kdyz jeden z DIDu zna. Ktery
         * byl spatne nerekne, takze se na davku zeptame po jednom -
         * jeden neznamy DID v seznamu nesmi shodit cele cteni. */
        if (c->chunk_n > 1u && (nrc == (uint8_t)UDS_NRC_OUT_OF_RANGE ||
                                nrc == (uint8_t)UDS_NRC_BAD_LENGTH)) {
            c->split = true;
            c->split_end = (uint8_t)(c->chunk_first + c->chunk_n);
            advance(c, now);
            return;
        }
        /* Jediny DID, nebo duvod, ktery se tyka vsech stejne
         * (0x22 podminky, 0x33 pristup) - oznac a pokracuj dal. */
        refuse_chunk(c, nrc);
        advance(c, now);
        return;
    }

    fail_op(c, UDS_ERR_NRC);
}

/* ------------------------------------------------------------------
 * Kladna odpoved
 * ------------------------------------------------------------------ */
static void handle_rsp(uds_client_t *c, uint32_t now)
{
    uint8_t sid;

    if (c->rsp_len == 0u) {
        fail_op(c, UDS_ERR_PARSE);
        return;
    }
    sid = c->rsp[0];

    if (sid == UDS_SID_NEGATIVE) {
        if (c->rsp_len < 3u || c->rsp[1] != c->req_sid) {
            fail_op(c, UDS_ERR_MISMATCH);
            return;
        }
        handle_nrc(c, c->rsp[2], now);
        return;
    }
    if (sid != (uint8_t)(c->req_sid + 0x40u)) {
        /* Opozdena odpoved na predchozi otazku nebo cizi provoz na nasi
         * adrese. Vydavat ji za svuj vysledek by byl vymysleny udaj. */
        fail_op(c, UDS_ERR_MISMATCH);
        return;
    }

    if (c->step == STEP_SESSION) {
        if (c->rsp_len < 2u || c->rsp[1] != c->want_sub) {
            fail_op(c, UDS_ERR_MISMATCH);
            return;
        }
        c->session_ok = true;
        advance(c, now);   /* a ted teprve vlastni dotaz */
        return;
    }

    if (c->op == OP_DIDS) {
        parse_dids(c, now);
        return;
    }

    /* OP_SINGLE */
    if (c->pend[0] == UDS_SID_TESTER) {
        c->op = OP_NONE;
        c->step = STEP_NONE;
        c->internal = false;
        return;                 /* verejny stav se nehne, o to tu jde */
    }
    if (c->pend[0] == UDS_SID_SESSION) {
        if (c->rsp_len < 2u || c->rsp[1] != c->want_sub) {
            fail_op(c, UDS_ERR_MISMATCH);
            return;
        }
        c->session_ok = true;
    }
    finish_ok(c);
}

/* ------------------------------------------------------------------
 * Udrzeni session
 * ------------------------------------------------------------------ */
static void maintain(uds_client_t *c, uint32_t now)
{
    uint32_t idle;

    if (!c->keepalive || !c->session_ok || c->op != OP_NONE) {
        return;   /* vlastni pozadavek resetuje S3 sam */
    }
    idle = now - c->t_last_req;
    if (idle >= UDS_S3_MS) {
        /* Hlavni smycka nas tak dlouho nepustila ke slovu, ze jednotka
         * session uz musela zahodit. Lhat si o tom nema smysl: dalsi
         * pozadavek ji zahaji znovu. */
        c->session_ok = false;
        c->n_session_lost++;
        return;
    }
    if (idle < UDS_TESTER_MS) {
        return;
    }

    c->pend[0] = UDS_SID_TESTER;
    c->pend[1] = 0x00u;
    c->pend_len = 2u;
    c->op = OP_SINGLE;
    c->step = STEP_REQ;
    c->internal = true;
    if (send_req(c, c->pend, 2u, now) != UDS_OK) {
        c->op = OP_NONE;
        c->step = STEP_NONE;
        c->internal = false;
        c->session_ok = false;
        c->n_session_lost++;
    }
}

/* ------------------------------------------------------------------
 * Rozhrani
 * ------------------------------------------------------------------ */
void uds_init(uds_client_t *c, isotp_link_t *link)
{
    memset(c, 0, sizeof(*c));
    c->link = link;
    c->state = (uint8_t)UDS_IDLE;
}

void uds_abort(uds_client_t *c)
{
    if (c->link != NULL) {
        isotp_abort(c->link);
    }
    c->op = OP_NONE;
    c->step = STEP_NONE;
    c->internal = false;
    if (c->state == (uint8_t)UDS_BUSY) {
        c->state = (uint8_t)UDS_IDLE;
    }
}

/* Spolecny zacatek kazdeho uzivatelskeho pozadavku. */
static bool begin(uds_client_t *c)
{
    if (c->link == NULL) {
        c->err = (uint8_t)UDS_ERR_BADARG;
        c->state = (uint8_t)UDS_FAILED;
        return false;
    }
    if (c->op != OP_NONE) {
        /* Muze to byt i interni 3E 00 - ten je jeden ramec tam a zpet,
         * takze staci zkusit znovu po dalsim uds_poll(). Prerusit ho a
         * poslat dotaz hned by znamenalo, ze nam pozdni 7E 00 spadne do
         * cekani na odpoved. */
        c->err = (uint8_t)UDS_ERR_BUSY;
        return false;
    }
    c->err = (uint8_t)UDS_OK;
    c->nrc = (uint8_t)UDS_NRC_NONE;
    c->resumed = false;
    c->rsp_len = 0u;
    c->n_req = 0u;
    c->state = (uint8_t)UDS_BUSY;
    return true;
}

bool uds_session(uds_client_t *c, uint8_t sub, uint32_t now_ms)
{
    if (!begin(c)) {
        return false;
    }
    c->want_sub = sub;
    c->session_ok = false;
    /* Default session nevyprsi, takze se neudrzuje. Posilat do ni
     * 3E 00 by byl provoz bez uzitku. */
    c->keepalive = (sub != UDS_SESSION_DEFAULT);
    c->pend[0] = UDS_SID_SESSION;
    c->pend[1] = sub;
    c->pend_len = 2u;
    c->op = OP_SINGLE;
    advance(c, now_ms);
    return c->state != (uint8_t)UDS_FAILED;
}

bool uds_read_dids(uds_client_t *c, const uds_did_req_t *dids, uint8_t n,
                   uint32_t now_ms)
{
    uint8_t i;
    uint32_t need = 0u;

    if (dids == NULL || n == 0u || n > (uint8_t)UDS_BATCH_MAX) {
        c->err = (uint8_t)UDS_ERR_BADARG;
        return false;
    }
    for (i = 0u; i < n; i++) {
        if (dids[i].len > (uint8_t)UDS_DID_LEN_MAX) {
            c->err = (uint8_t)UDS_ERR_BADARG;
            return false;
        }
        need += dids[i].len;
    }
    if (need > (uint32_t)UDS_DATA_POOL) {
        c->err = (uint8_t)UDS_ERR_BADARG;
        return false;
    }
    if (!begin(c)) {
        return false;
    }

    for (i = 0u; i < n; i++) {
        c->req[i] = dids[i];
        memset(&c->res[i], 0, sizeof(c->res[i]));
        c->res[i].did = dids[i].did;
        c->res[i].status = (uint8_t)UDS_DID_PENDING;
    }
    c->n_req = n;
    c->req_done = 0u;
    c->chunk_first = 0u;
    c->chunk_n = 0u;
    c->split = false;
    c->split_end = 0u;
    c->pool_used = 0u;
    c->op = OP_DIDS;
    advance(c, now_ms);
    return c->state != (uint8_t)UDS_FAILED;
}

bool uds_read_did(uds_client_t *c, uint16_t did, uint8_t len, uint32_t now_ms)
{
    uds_did_req_t one;

    one.did = did;
    one.len = len;
    return uds_read_dids(c, &one, 1u, now_ms);
}

/* Spolecny zacatek pro 19 xx. */
static bool start_single(uds_client_t *c, const uint8_t *msg, uint8_t len,
                         uint32_t now_ms)
{
    if (!begin(c)) {
        return false;
    }
    memcpy(c->pend, msg, len);
    c->pend_len = len;
    c->op = OP_SINGLE;
    advance(c, now_ms);
    return c->state != (uint8_t)UDS_FAILED;
}

bool uds_read_dtcs(uds_client_t *c, uint8_t status_mask, uint32_t now_ms)
{
    uint8_t msg[3];

    msg[0] = UDS_SID_READ_DTC;
    msg[1] = UDS_DTC_BY_STATUS;
    msg[2] = status_mask;
    return start_single(c, msg, 3u, now_ms);
}

static bool read_dtc_record(uds_client_t *c, uint8_t sub, uint32_t dtc,
                            uint8_t record, uint32_t now_ms)
{
    uint8_t msg[6];

    msg[0] = UDS_SID_READ_DTC;
    msg[1] = sub;
    msg[2] = (uint8_t)((dtc >> 16) & 0xFFu);
    msg[3] = (uint8_t)((dtc >> 8) & 0xFFu);
    msg[4] = (uint8_t)(dtc & 0xFFu);
    msg[5] = record;
    return start_single(c, msg, 6u, now_ms);
}

bool uds_read_freeze(uds_client_t *c, uint32_t dtc, uint8_t record,
                     uint32_t now_ms)
{
    return read_dtc_record(c, UDS_DTC_SNAPSHOT, dtc, record, now_ms);
}

bool uds_read_extended(uds_client_t *c, uint32_t dtc, uint8_t record,
                       uint32_t now_ms)
{
    return read_dtc_record(c, UDS_DTC_EXTENDED, dtc, record, now_ms);
}

uds_state_t uds_poll(uds_client_t *c, uint32_t now_ms)
{
    isotp_state_t s;

    if (c->link == NULL) {
        return (uds_state_t)c->state;
    }
    s = isotp_poll(c->link, now_ms);

    if (c->op != OP_NONE) {
        if (s == ISOTP_DONE) {
            if (c->internal) {
                /* Udrzovaci 3E 00 NESMI prepsat odpoved, kterou si drzi
                 * volajici. Stranka chyb cte c->rsp jeste dlouho po tom,
                 * co 19 02 dobehlo, a 7E 00 o delce dvou bajtu by z nej
                 * udelalo "zadna chyba" - tedy tiche tvrzeni, ze je vuz
                 * v poradku. Odpoved proto odebereme stranou a zahodime.
                 *
                 * Zaporna odpoved tady znamena jedine: session uz neplati.
                 * Nehlasi se jako chyba pozadavku, protoze o zadny
                 * pozadavek uzivatele neslo - jen se priste zahaji znovu. */
                uint8_t ignored[8];
                uint16_t n;

                n = isotp_take(c->link, ignored, (uint16_t)sizeof(ignored));
                c->n_rsp++;
                if (n >= 1u && ignored[0] == UDS_SID_NEGATIVE) {
                    c->session_ok = false;
                    c->n_session_lost++;
                }
                c->op = OP_NONE;
                c->step = STEP_NONE;
                c->internal = false;
            } else {
                c->rsp_len =
                    isotp_take(c->link, c->rsp, (uint16_t)sizeof(c->rsp));
                c->n_rsp++;
                handle_rsp(c, now_ms);
            }
        } else if (s == ISOTP_FAILED) {
            isotp_err_t e = isotp_error(c->link);

            isotp_abort(c->link);
            /* Po tichu nevime, jestli session jeste plati. Dalsi
             * pozadavek ji proto zahaji znovu - jeden ramec navic je
             * levnejsi nez cela davka odmitnuta 7F xx 7F. */
            c->session_ok = false;
            if (e == ISOTP_ERR_SEND) {
                fail_op(c, UDS_ERR_SEND);
            } else if (e == ISOTP_ERR_TIMEOUT || e == ISOTP_ERR_PENDING_MAX ||
                       e == ISOTP_ERR_TXN) {
                fail_op(c, UDS_ERR_TIMEOUT);
            } else {
                fail_op(c, UDS_ERR_TRANSPORT);
            }
        }
    }

    maintain(c, now_ms);
    return (uds_state_t)c->state;
}

void uds_on_frame(uds_client_t *c, uint32_t id, const uint8_t *data,
                  uint8_t dlc, uint32_t now_ms)
{
    if (c->link != NULL) {
        isotp_on_frame(c->link, id, data, dlc, now_ms);
    }
}

/* ------------------------------------------------------------------
 * Vysledky
 * ------------------------------------------------------------------ */
uint8_t uds_did_count(const uds_client_t *c)
{
    return c->n_req;
}

const uds_did_res_t *uds_did_at(const uds_client_t *c, uint8_t i)
{
    return (i < c->n_req) ? &c->res[i] : NULL;
}

const uds_did_res_t *uds_did_find(const uds_client_t *c, uint16_t did)
{
    uint8_t i;

    for (i = 0u; i < c->n_req; i++) {
        if (c->res[i].did == did) {
            return &c->res[i];
        }
    }
    return NULL;
}

const uint8_t *uds_did_bytes(const uds_client_t *c, const uds_did_res_t *r)
{
    if (r == NULL || r->status != (uint8_t)UDS_DID_OK || r->len == 0u) {
        return NULL;
    }
    return &c->pool[r->off];
}

bool uds_did_u32(const uds_client_t *c, uint16_t did, uint32_t *out)
{
    const uds_did_res_t *r = uds_did_find(c, did);
    const uint8_t *b = uds_did_bytes(c, r);
    uint32_t v = 0u;
    uint8_t i;

    if (b == NULL || out == NULL || r->len > 4u) {
        return false;
    }
    for (i = 0u; i < r->len; i++) {
        v = (v << 8) | b[i];
    }
    *out = v;
    return true;
}

const uint8_t *uds_rsp(const uds_client_t *c)
{
    return c->rsp;
}

uint16_t uds_rsp_len(const uds_client_t *c)
{
    return c->rsp_len;
}

/* Odpoved na 19 02 je 59 02 <dostupne masky> a pak ctverice
 * <dtc hi> <dtc mid> <dtc lo> <stav>. */
uint16_t uds_dtc_count(const uds_client_t *c)
{
    if (c->rsp_len < 3u || c->rsp[0] != (uint8_t)(UDS_SID_READ_DTC + 0x40u) ||
        c->rsp[1] != UDS_DTC_BY_STATUS) {
        return 0u;
    }
    return (uint16_t)((c->rsp_len - 3u) / 4u);
}

bool uds_dtc_at(const uds_client_t *c, uint16_t i, uds_dtc_t *out)
{
    uint16_t p;

    if (out == NULL || i >= uds_dtc_count(c)) {
        return false;
    }
    p = (uint16_t)(3u + i * 4u);
    out->dtc = ((uint32_t)c->rsp[p] << 16) | ((uint32_t)c->rsp[p + 1u] << 8) |
               (uint32_t)c->rsp[p + 2u];
    out->status = c->rsp[p + 3u];
    return true;
}
