/*
 * legacy.c - implementace stareho protokolu Renault, viz legacy.h
 *
 * Zadne hodiny, zadne vlakno, zadny hardware. Zapis (3B) tu neni a
 * nebude - duvod je v legacy.h.
 */
#include "legacy.h"

#include <string.h>

#define OP_NONE   0u
#define OP_SINGLE 1u

#define STEP_NONE    0u
#define STEP_SESSION 1u
#define STEP_REQ     2u

static void finish_ok(legacy_t *l)
{
    l->op = OP_NONE;
    l->step = STEP_NONE;
    l->internal = false;
    l->err = (uint8_t)UDS_OK;
    l->state = (uint8_t)LEGACY_DONE;
}

static void fail_op(legacy_t *l, uds_err_t e)
{
    l->op = OP_NONE;
    l->step = STEP_NONE;
    if (l->internal) {
        l->internal = false;
        return;     /* tester present verejnym stavem nehybe */
    }
    l->err = (uint8_t)e;
    l->state = (uint8_t)LEGACY_FAILED;
}

static uds_err_t send_req(legacy_t *l, const uint8_t *msg, uint8_t len,
                          uint32_t now)
{
    if (l->link == NULL) {
        return UDS_ERR_BADARG;
    }
    if (!isotp_send(l->link, msg, len, now)) {
        return (isotp_error(l->link) == ISOTP_ERR_SEND) ? UDS_ERR_SEND
                                                        : UDS_ERR_TRANSPORT;
    }
    l->req_sid = msg[0];
    l->t_last_req = now;
    l->n_req_sent++;
    return UDS_OK;
}

static void advance(legacy_t *l, uint32_t now)
{
    uds_err_t e;
    uint8_t msg[2];

    if (!l->session_ok && l->pend[0] != LEGACY_SID_SESSION &&
        l->pend[0] != LEGACY_SID_TESTER) {
        msg[0] = LEGACY_SID_SESSION;
        msg[1] = LEGACY_SESSION;
        l->step = STEP_SESSION;
        e = send_req(l, msg, 2u, now);
        if (e != UDS_OK) {
            fail_op(l, e);
        }
        return;
    }
    l->step = STEP_REQ;
    e = send_req(l, l->pend, l->pend_len, now);
    if (e != UDS_OK) {
        fail_op(l, e);
    }
}

static void handle_nrc(legacy_t *l, uint8_t nrc, uint32_t now)
{
    l->nrc = nrc;
    l->n_nrc++;

    if (nrc == (uint8_t)UDS_NRC_NOT_IN_SESSION) {
        l->session_ok = false;
        l->n_session_lost++;
        if (l->step != STEP_SESSION && !l->resumed) {
            l->resumed = true;
            advance(l, now);
            return;
        }
        fail_op(l, UDS_ERR_SESSION);
        return;
    }
    fail_op(l, UDS_ERR_NRC);
}

static void handle_rsp(legacy_t *l, uint32_t now)
{
    uint8_t sid;

    if (l->rsp_len == 0u) {
        fail_op(l, UDS_ERR_PARSE);
        return;
    }
    sid = l->rsp[0];

    if (sid == UDS_SID_NEGATIVE) {
        if (l->rsp_len < 3u || l->rsp[1] != l->req_sid) {
            fail_op(l, UDS_ERR_MISMATCH);
            return;
        }
        handle_nrc(l, l->rsp[2], now);
        return;
    }
    if (sid != (uint8_t)(l->req_sid + 0x40u)) {
        fail_op(l, UDS_ERR_MISMATCH);
        return;
    }

    if (l->step == STEP_SESSION) {
        if (l->rsp_len < 2u || l->rsp[1] != LEGACY_SESSION) {
            fail_op(l, UDS_ERR_MISMATCH);
            return;
        }
        l->session_ok = true;
        advance(l, now);
        return;
    }

    if (l->pend[0] == LEGACY_SID_TESTER) {
        l->op = OP_NONE;
        l->step = STEP_NONE;
        l->internal = false;
        return;
    }
    if (l->pend[0] == LEGACY_SID_SESSION) {
        if (l->rsp_len < 2u || l->rsp[1] != LEGACY_SESSION) {
            fail_op(l, UDS_ERR_MISMATCH);
            return;
        }
        l->session_ok = true;
        finish_ok(l);
        return;
    }
    /* 61 <block> <data...>; blok se musi shodovat s tim, na co jsme se
     * ptali, jinak je to odpoved na predchozi otazku. */
    if (l->rsp_len < 2u || l->rsp[1] != l->block) {
        fail_op(l, UDS_ERR_MISMATCH);
        return;
    }
    finish_ok(l);
}

static void maintain(legacy_t *l, uint32_t now)
{
    uint32_t idle;

    if (!l->keepalive || !l->session_ok || l->op != OP_NONE) {
        return;
    }
    idle = now - l->t_last_req;
    if (idle >= LEGACY_S3_MS) {
        l->session_ok = false;
        l->n_session_lost++;
        return;
    }
    if (idle < LEGACY_TESTER_MS) {
        return;
    }

    l->pend[0] = LEGACY_SID_TESTER;
    l->pend[1] = 0x00u;
    l->pend_len = 2u;
    l->op = OP_SINGLE;
    l->step = STEP_REQ;
    l->internal = true;
    if (send_req(l, l->pend, 2u, now) != UDS_OK) {
        l->op = OP_NONE;
        l->step = STEP_NONE;
        l->internal = false;
        l->session_ok = false;
        l->n_session_lost++;
    }
}

/* ------------------------------------------------------------------
 * Rozhrani
 * ------------------------------------------------------------------ */
void legacy_init(legacy_t *l, isotp_link_t *link)
{
    memset(l, 0, sizeof(*l));
    l->link = link;
    l->state = (uint8_t)LEGACY_IDLE;
}

void legacy_abort(legacy_t *l)
{
    if (l->link != NULL) {
        isotp_abort(l->link);
    }
    l->op = OP_NONE;
    l->step = STEP_NONE;
    l->internal = false;
    if (l->state == (uint8_t)LEGACY_BUSY) {
        l->state = (uint8_t)LEGACY_IDLE;
    }
}

static bool begin(legacy_t *l)
{
    if (l->link == NULL) {
        l->err = (uint8_t)UDS_ERR_BADARG;
        l->state = (uint8_t)LEGACY_FAILED;
        return false;
    }
    if (l->op != OP_NONE) {
        l->err = (uint8_t)UDS_ERR_BUSY;
        return false;
    }
    l->err = (uint8_t)UDS_OK;
    l->nrc = (uint8_t)UDS_NRC_NONE;
    l->resumed = false;
    l->rsp_len = 0u;
    l->state = (uint8_t)LEGACY_BUSY;
    return true;
}

bool legacy_session(legacy_t *l, uint32_t now_ms)
{
    if (!begin(l)) {
        return false;
    }
    l->session_ok = false;
    l->keepalive = true;
    l->pend[0] = LEGACY_SID_SESSION;
    l->pend[1] = LEGACY_SESSION;
    l->pend_len = 2u;
    l->op = OP_SINGLE;
    advance(l, now_ms);
    return l->state != (uint8_t)LEGACY_FAILED;
}

bool legacy_read(legacy_t *l, uint8_t block, uint32_t now_ms)
{
    if (!begin(l)) {
        return false;
    }
    l->block = block;
    l->pend[0] = LEGACY_SID_READ;
    l->pend[1] = block;
    l->pend_len = 2u;
    l->op = OP_SINGLE;
    advance(l, now_ms);
    return l->state != (uint8_t)LEGACY_FAILED;
}

legacy_state_t legacy_poll(legacy_t *l, uint32_t now_ms)
{
    isotp_state_t s;

    if (l->link == NULL) {
        return (legacy_state_t)l->state;
    }
    s = isotp_poll(l->link, now_ms);

    if (l->op != OP_NONE) {
        if (s == ISOTP_DONE) {
            l->rsp_len = isotp_take(l->link, l->rsp, (uint16_t)sizeof(l->rsp));
            l->n_rsp++;
            handle_rsp(l, now_ms);
        } else if (s == ISOTP_FAILED) {
            isotp_err_t e = isotp_error(l->link);

            isotp_abort(l->link);
            l->session_ok = false;
            if (e == ISOTP_ERR_SEND) {
                fail_op(l, UDS_ERR_SEND);
            } else if (e == ISOTP_ERR_TIMEOUT || e == ISOTP_ERR_PENDING_MAX ||
                       e == ISOTP_ERR_TXN) {
                fail_op(l, UDS_ERR_TIMEOUT);
            } else {
                fail_op(l, UDS_ERR_TRANSPORT);
            }
        }
    }

    maintain(l, now_ms);
    return (legacy_state_t)l->state;
}

void legacy_on_frame(legacy_t *l, uint32_t id, const uint8_t *data,
                     uint8_t dlc, uint32_t now_ms)
{
    if (l->link != NULL) {
        isotp_on_frame(l->link, id, data, dlc, now_ms);
    }
}

const uint8_t *legacy_data(const legacy_t *l)
{
    if (l->state != (uint8_t)LEGACY_DONE || l->rsp_len < 2u) {
        return NULL;
    }
    return (l->rsp_len > 2u) ? &l->rsp[2] : NULL;
}

uint16_t legacy_len(const legacy_t *l)
{
    if (l->state != (uint8_t)LEGACY_DONE || l->rsp_len < 2u) {
        return 0u;
    }
    return (uint16_t)(l->rsp_len - 2u);
}

uint8_t legacy_block(const legacy_t *l)
{
    return (l->rsp_len >= 2u) ? l->rsp[1] : 0u;
}

const uint8_t *legacy_rsp(const legacy_t *l)
{
    return l->rsp;
}

uint16_t legacy_rsp_len(const legacy_t *l)
{
    return l->rsp_len;
}
