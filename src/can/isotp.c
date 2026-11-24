/*
 * isotp.c - implementace ISO 15765-2, viz isotp.h
 *
 * Zadne hodiny, zadne vlakno, zadny hardware. Cas chodi v parametru,
 * ramce odchazi callbackem. Cely soubor se preklada i na hostiteli.
 */
#include "isotp.h"

#include <string.h>

/* PCI - horni ctyri bity prvniho bajtu */
#define PCI_SF  0x0u
#define PCI_FF  0x1u
#define PCI_CF  0x2u
#define PCI_FC  0x3u

/* flow status v dolnich ctyrech bitech FC ramce */
#define FS_CTS  0x0u
#define FS_WAIT 0x1u
#define PCI_FC_OVFLW 0x2u

/* Porovnani casu odolne proti preteceni 32bitoveho citace milisekund.
 * Deska bezi na uptime v ms, ktery pretece po 49 dnech; prenos trva
 * desitky milisekund, takze rozdil se do int32_t vejde vzdy. */
static bool reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static void fail(isotp_link_t *l, isotp_err_t e)
{
    l->state = ISOTP_FAILED;
    l->err = e;
    l->awaiting = false;
    l->wait_fc = false;
    l->n_err++;
}

/* Odeslani jednoho ramce vcetne doplneni na 8 bajtu. Buffer musi mit
 * misto pro celych 8 bajtu, i kdyz se jich pouzije min. */
static bool emit(isotp_link_t *l, uint8_t *d, uint8_t used)
{
    uint8_t dlc = used;

    if (l->cfg.pad) {
        while (dlc < 8u) {
            d[dlc++] = l->cfg.pad_byte;
        }
    }
    if (l->cfg.send == NULL) {
        return false;
    }
    return l->cfg.send(l->cfg.user, l->cfg.tx_id, d, dlc);
}

/* Nas flow control smerem k jednotce. Posila se po first frame a pak po
 * kazdem bloku, kdyz mame block size nenulovy. */
static bool send_fc(isotp_link_t *l, uint8_t fs)
{
    uint8_t d[8];

    d[0] = (uint8_t)((PCI_FC << 4) | (fs & 0x0Fu));
    d[1] = l->cfg.rx_bs;
    d[2] = l->cfg.rx_st_min;
    return emit(l, d, 3u);
}

uint8_t isotp_st_min_ms(uint8_t raw)
{
    if (raw <= 0x7Fu) {
        return raw;
    }
    if (raw >= 0xF1u && raw <= 0xF9u) {
        return 1u;  /* 100-900 us, nase rozliseni je milisekunda */
    }
    return 127u;    /* rezervovane hodnoty se podle normy berou jako 0x7F */
}

void isotp_cfg_default(isotp_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    /* Nas flow control: posli vse bez cekani. Sbernici v aute to nezatizi
     * - odpovedi jsou kratke a jsme jediny tester na lince. */
    cfg->rx_bs = 0u;
    cfg->rx_st_min = 0u;
    /* Doplnovani na 8 bajtu nulami. Jednotky X82 to tak posilaji taky a
     * jednotky, ktere kratky ramec odmitaji, nejsou nijak vzacne. */
    cfg->pad = true;
    cfg->pad_byte = 0x00u;
}

void isotp_init(isotp_link_t *l, const isotp_cfg_t *cfg)
{
    memset(l, 0, sizeof(*l));
    l->cfg = *cfg;
    l->state = ISOTP_IDLE;
    l->err = ISOTP_OK;
}

void isotp_abort(isotp_link_t *l)
{
    l->state = ISOTP_IDLE;
    l->err = ISOTP_OK;
    l->awaiting = false;
    l->wait_fc = false;
    l->tx_len = 0u;
    l->tx_sent = 0u;
    l->rx_len = 0u;
    l->rx_got = 0u;
}

/* ------------------------------------------------------------------
 * Odesilani
 * ------------------------------------------------------------------ */
bool isotp_send(isotp_link_t *l, const uint8_t *msg, uint16_t len,
                uint32_t now_ms)
{
    uint8_t d[8];

    if (isotp_busy(l)) {
        l->err = ISOTP_ERR_BUSY;
        return false;
    }
    if (len == 0u || len > (uint16_t)ISOTP_BUF_SIZE) {
        l->err = ISOTP_ERR_TOO_LONG;
        return false;
    }

    memcpy(l->tx_buf, msg, len);
    l->tx_len = len;
    l->tx_sent = 0u;
    l->err = ISOTP_OK;
    l->fc_waits = 0u;
    l->rx_len = 0u;
    l->rx_got = 0u;

    if (len <= 7u) {
        d[0] = (uint8_t)len;
        memcpy(&d[1], msg, len);
        if (!emit(l, d, (uint8_t)(len + 1u))) {
            fail(l, ISOTP_ERR_SEND);
            return false;
        }
        l->tx_sent = len;
        l->n_msg_tx++;
        l->awaiting = true;
        l->state = ISOTP_WAITING;
        l->t_rx_late = now_ms + ISOTP_P2_MS;
        return true;
    }

    /* first frame: 1LLL + sest bajtu dat */
    d[0] = (uint8_t)((PCI_FF << 4) | ((len >> 8) & 0x0Fu));
    d[1] = (uint8_t)(len & 0xFFu);
    memcpy(&d[2], msg, 6u);
    if (!emit(l, d, 8u)) {
        fail(l, ISOTP_ERR_SEND);
        return false;
    }
    l->tx_sent = 6u;
    l->tx_sn = 1u;
    l->tx_bs = 0u;
    l->tx_bs_left = 0u;
    l->tx_st_min = 0u;
    l->awaiting = true;
    l->wait_fc = true;
    l->state = ISOTP_SENDING;
    l->t_fc_late = now_ms + ISOTP_N_BS_MS;
    return true;
}

/* Jeden consecutive frame. Vola se jen ze stavu SENDING. */
static void send_cf(isotp_link_t *l, uint32_t now_ms)
{
    uint8_t d[8];
    uint16_t left = (uint16_t)(l->tx_len - l->tx_sent);
    uint8_t n = (left > 7u) ? 7u : (uint8_t)left;

    d[0] = (uint8_t)((PCI_CF << 4) | (l->tx_sn & 0x0Fu));
    memcpy(&d[1], &l->tx_buf[l->tx_sent], n);
    if (!emit(l, d, (uint8_t)(n + 1u))) {
        fail(l, ISOTP_ERR_SEND);
        return;
    }
    l->tx_sent = (uint16_t)(l->tx_sent + n);
    l->tx_sn = (uint8_t)((l->tx_sn + 1u) & 0x0Fu);

    if (l->tx_sent >= l->tx_len) {
        /* cela zprava venku, ted uz jen odpoved */
        l->n_msg_tx++;
        l->wait_fc = false;
        l->state = ISOTP_WAITING;
        l->t_rx_late = now_ms + ISOTP_P2_MS;
        return;
    }

    if (l->tx_bs != 0u) {
        l->tx_bs_left--;
        if (l->tx_bs_left == 0u) {
            /* blok dojel - jednotka musi poslat dalsi flow control.
             * Pri block size 1 (co dela X82) je to po kazdem ramci. */
            l->wait_fc = true;
            l->t_fc_late = now_ms + ISOTP_N_BS_MS;
            return;
        }
    }
    l->t_cf_ready = now_ms + l->tx_st_min;
    l->t_cf_late = now_ms + ISOTP_N_CS_MAX_MS;
}

/* ------------------------------------------------------------------
 * Prijem
 * ------------------------------------------------------------------ */

/* Zprava je kompletni. Zaporna odpoved 7F xx 78 ale neni odpoved -
 * jednotka jen rika "pracuji na tom", takze se ceka dal s delsim
 * limitem. Bez tohohle konci kazde 19 02 chybou. */
static void rx_complete(isotp_link_t *l, uint32_t now_ms)
{
    if (l->awaiting && l->rx_len >= 3u &&
        l->rx_buf[0] == 0x7Fu && l->rx_buf[2] == ISOTP_NRC_PENDING) {
        l->n_pending++;
        l->rx_len = 0u;
        l->rx_got = 0u;
        l->state = ISOTP_WAITING;
        l->t_rx_late = now_ms + ISOTP_P2_STAR_MS;
        return;
    }
    l->n_msg_rx++;
    l->awaiting = false;
    l->state = ISOTP_DONE;
}

static void on_sf(isotp_link_t *l, const uint8_t *d, uint8_t dlc, uint32_t now)
{
    uint8_t len = (uint8_t)(d[0] & 0x0Fu);

    if (len == 0u || len > 7u || (uint8_t)(len + 1u) > dlc) {
        return;  /* poskozeny ramec, nic s nim nedelame */
    }
    if (l->state == ISOTP_SENDING) {
        return;  /* rozdelane odesilani ma prednost */
    }
    memcpy(l->rx_buf, &d[1], len);
    l->rx_len = len;
    l->rx_got = len;
    rx_complete(l, now);
}

static void on_ff(isotp_link_t *l, const uint8_t *d, uint8_t dlc, uint32_t now)
{
    uint16_t len;

    if (dlc < 8u || l->state == ISOTP_SENDING) {
        return;
    }
    len = (uint16_t)(((uint16_t)(d[0] & 0x0Fu) << 8) | d[1]);
    if (len <= 7u) {
        return;  /* kratka zprava patri do single frame */
    }
    if (len > (uint16_t)ISOTP_BUF_SIZE) {
        /* Rekneme to nahlas misto abychom pulku zpravy zahodili tise. */
        (void)send_fc(l, PCI_FC_OVFLW);
        fail(l, ISOTP_ERR_OVERFLOW);
        return;
    }
    memcpy(l->rx_buf, &d[2], 6u);
    l->rx_len = len;
    l->rx_got = 6u;
    l->rx_sn = 1u;
    l->rx_bs_left = l->cfg.rx_bs;
    l->state = ISOTP_RECEIVING;
    l->t_rx_late = now + ISOTP_N_CR_MS;
    if (!send_fc(l, FS_CTS)) {
        fail(l, ISOTP_ERR_SEND);
    }
}

static void on_cf(isotp_link_t *l, const uint8_t *d, uint8_t dlc, uint32_t now)
{
    uint16_t left;
    uint8_t n;

    if (l->state != ISOTP_RECEIVING) {
        return;
    }
    if ((uint8_t)(d[0] & 0x0Fu) != l->rx_sn) {
        fail(l, ISOTP_ERR_SEQ);
        return;
    }
    left = (uint16_t)(l->rx_len - l->rx_got);
    n = (left > 7u) ? 7u : (uint8_t)left;
    if ((uint8_t)(n + 1u) > dlc) {
        n = (uint8_t)(dlc - 1u);  /* kratsi ramec nez cekame, vezmi co je */
    }
    memcpy(&l->rx_buf[l->rx_got], &d[1], n);
    l->rx_got = (uint16_t)(l->rx_got + n);
    l->rx_sn = (uint8_t)((l->rx_sn + 1u) & 0x0Fu);

    if (l->rx_got >= l->rx_len) {
        rx_complete(l, now);
        return;
    }
    l->t_rx_late = now + ISOTP_N_CR_MS;
    if (l->cfg.rx_bs != 0u) {
        l->rx_bs_left--;
        if (l->rx_bs_left == 0u) {
            l->rx_bs_left = l->cfg.rx_bs;
            if (!send_fc(l, FS_CTS)) {
                fail(l, ISOTP_ERR_SEND);
            }
        }
    }
}

static void on_fc(isotp_link_t *l, const uint8_t *d, uint8_t dlc, uint32_t now)
{
    uint8_t fs;

    if (l->state != ISOTP_SENDING || !l->wait_fc || dlc < 3u) {
        return;
    }
    fs = (uint8_t)(d[0] & 0x0Fu);
    if (fs == FS_WAIT) {
        /* Jednotka si rekla o strpeni. Cekame znovu cely N_Bs, ale ne
         * donekonecna - nekolik WAIT za sebou znamena, ze se nedockame. */
        l->fc_waits++;
        if (l->fc_waits > (uint8_t)ISOTP_FC_WAIT_MAX) {
            fail(l, ISOTP_ERR_FC_WAIT);
            return;
        }
        l->t_fc_late = now + ISOTP_N_BS_MS;
        return;
    }
    if (fs == PCI_FC_OVFLW) {
        fail(l, ISOTP_ERR_FC_OVERFLOW);
        return;
    }
    if (fs != FS_CTS) {
        fail(l, ISOTP_ERR_FC_BAD);
        return;
    }

    l->fc_waits = 0u;
    l->tx_bs = d[1];
    l->tx_bs_left = d[1];
    l->tx_st_min = isotp_st_min_ms(d[2]);
    /* Separace delsi nez nas vlastni strop by znamenala, ze se shodime
     * sami. Norma takovou hodnotu nepredpoklada, ale jednotky posilaji
     * leccos - radsi posleme o chlup driv, nez abychom prenos zabili. */
    if (l->tx_st_min >= (uint8_t)ISOTP_N_CS_MAX_MS) {
        l->tx_st_min = (uint8_t)(ISOTP_N_CS_MAX_MS - 1u);
    }
    l->wait_fc = false;
    l->t_cf_ready = now + l->tx_st_min;
    l->t_cf_late = now + ISOTP_N_CS_MAX_MS;
}

void isotp_on_frame(isotp_link_t *l, uint32_t id, const uint8_t *data,
                    uint8_t dlc, uint32_t now_ms)
{
    if (id != l->cfg.rx_id || dlc == 0u || data == NULL) {
        return;
    }
    switch ((uint8_t)(data[0] >> 4)) {
    case PCI_SF: on_sf(l, data, dlc, now_ms); break;
    case PCI_FF: on_ff(l, data, dlc, now_ms); break;
    case PCI_CF: on_cf(l, data, dlc, now_ms); break;
    case PCI_FC: on_fc(l, data, dlc, now_ms); break;
    default: break;  /* neznamy PCI - neni nase vec */
    }
}

/* ------------------------------------------------------------------
 * Hodiny
 * ------------------------------------------------------------------ */
isotp_state_t isotp_poll(isotp_link_t *l, uint32_t now_ms)
{
    switch (l->state) {
    case ISOTP_SENDING:
        if (l->wait_fc) {
            if (reached(now_ms, l->t_fc_late)) {
                fail(l, ISOTP_ERR_NO_FC);
            }
            break;
        }
        if (reached(now_ms, l->t_cf_late)) {
            /* Nepustili nas ke slovu vcas. Jednotka uz prenos nejspis
             * zahodila, takze nema smysl posilat zbytek do prazdna. */
            fail(l, ISOTP_ERR_N_CS);
            break;
        }
        if (reached(now_ms, l->t_cf_ready)) {
            send_cf(l, now_ms);
        }
        break;

    case ISOTP_WAITING:
        if (reached(now_ms, l->t_rx_late)) {
            fail(l, ISOTP_ERR_TIMEOUT);
        }
        break;

    case ISOTP_RECEIVING:
        if (reached(now_ms, l->t_rx_late)) {
            fail(l, ISOTP_ERR_NO_CF);
        }
        break;

    default:
        break;
    }
    return l->state;
}

uint16_t isotp_take(isotp_link_t *l, uint8_t *out, uint16_t cap)
{
    uint16_t len;

    if (l->state != ISOTP_DONE) {
        return 0u;
    }
    len = l->rx_len;
    if (out != NULL && cap > 0u) {
        memcpy(out, l->rx_buf, (len < cap) ? len : cap);
    }
    l->rx_len = 0u;
    l->rx_got = 0u;
    l->state = ISOTP_IDLE;
    return len;
}

const char *isotp_err_name(isotp_err_t e)
{
    switch (e) {
    case ISOTP_OK:              return "ok";
    case ISOTP_ERR_BUSY:        return "busy";
    case ISOTP_ERR_TOO_LONG:    return "too-long";
    case ISOTP_ERR_SEND:        return "send-failed";
    case ISOTP_ERR_NO_FC:       return "no-flow-control";
    case ISOTP_ERR_FC_OVERFLOW: return "fc-overflow";
    case ISOTP_ERR_FC_WAIT:     return "fc-wait-forever";
    case ISOTP_ERR_FC_BAD:      return "fc-invalid";
    case ISOTP_ERR_N_CS:        return "n-cs-exceeded";
    case ISOTP_ERR_NO_CF:       return "no-consecutive-frame";
    case ISOTP_ERR_SEQ:         return "bad-sequence";
    case ISOTP_ERR_TIMEOUT:     return "no-response";
    case ISOTP_ERR_OVERFLOW:    return "message-too-long";
    default:                    return "?";
    }
}
