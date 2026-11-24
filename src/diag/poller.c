/*
 * poller.c - rozpis vycitani a profil vozidla
 *
 * Tabulka CHAN je jedine misto v celem projektu, ktere zna cisla DIDu a
 * jejich skalovani. Zdroj je docs/vehicles/x82-trafic3-talento.md -
 * vsechno v ni bylo precteno z opravdoveho vozu. UI zna jen kanaly,
 * takze vymena vozidla znamena vymenu teto tabulky a nic jineho.
 *
 * Rozhodnuti o periodach a o tom, proc je rozpocet omezeny, jsou v
 * poller.h; tady je jen jejich provedeni.
 */
#include "diag/poller.h"

#include <string.h>

/* ------------------------------------------------------------------
 * Profil vozidla
 *
 * Masky pohledu rikaji, na ktere strance je velicina opravdu videt.
 * Kanal, ktery nikdo nekresli, se nesmi objevit v zadne masce - kazdy
 * DID navic zdrzuje vsechny ostatni.
 * ------------------------------------------------------------------ */
#define V_MAIN   DIAG_VIEW_BIT(DIAG_VIEW_MAIN)
#define V_ENG    DIAG_VIEW_BIT(DIAG_VIEW_ENGINE)
#define V_BOOST  DIAG_VIEW_BIT(DIAG_VIEW_BOOST)
#define V_FAULT  DIAG_VIEW_BIT(DIAG_VIEW_FAULTS)
#define V_DPF    DIAG_VIEW_BIT(DIAG_VIEW_DPF)
#define V_ALL    DIAG_VIEW_ALL

typedef struct {
    uint16_t did;
    uint8_t  len;     /* delka dat, sloupec Bytes v profilu vozidla */
    uint8_t  tier;    /* diag_tier_t */
    uint16_t views;
    float    step;
    float    off;
} chan_t;

static const chan_t CHAN[DIAG_CH__COUNT] = {
    /* ---- rychla skupina: bez ni je velicina k nicemu ---- */
    [DIAG_CH_RPM]      = { 0x2002u, 2u, DIAG_TIER_FAST, V_MAIN,          0.25f,    0.0f },
    [DIAG_CH_SPEED]    = { 0x2003u, 2u, DIAG_TIER_FAST, V_MAIN,          0.01f,    0.0f },
    [DIAG_CH_BOOST]    = { 0x2401u, 2u, DIAG_TIER_FAST, V_ENG | V_BOOST, 1.0f,     0.0f },
    [DIAG_CH_BOOST_SP] = { 0x2402u, 2u, DIAG_TIER_FAST, V_ENG | V_BOOST, 1.0f,     0.0f },
    [DIAG_CH_RAIL]     = { 0x2801u, 2u, DIAG_TIER_FAST, V_ENG,           1.0f,     0.0f },
    [DIAG_CH_RAIL_SP]  = { 0x2802u, 2u, DIAG_TIER_FAST, V_ENG,           1.0f,     0.0f },
    [DIAG_CH_VNT]      = { 0x2527u, 2u, DIAG_TIER_FAST, V_ENG,           0.01f,    0.0f },

    /* ---- stredni skupina: hybe se v desitkach vterin ---- */
    [DIAG_CH_MAF]      = { 0x2412u, 2u, DIAG_TIER_MED,  V_ENG,           0.1f,     0.0f },
    [DIAG_CH_MAF_SP]   = { 0x2413u, 2u, DIAG_TIER_MED,  V_ENG,           0.1f,     0.0f },
    [DIAG_CH_EGR]      = { 0x2407u, 2u, DIAG_TIER_MED,  V_ENG,           0.0488f,  0.0f },
    [DIAG_CH_IAT]      = { 0x240Du, 2u, DIAG_TIER_MED,  V_ENG,           0.1f,  -273.0f },
    [DIAG_CH_COOLANT]  = { 0x2001u, 2u, DIAG_TIER_MED,  V_ALL,           0.1f,  -273.0f },
    [DIAG_CH_DPF_UP]   = { 0x2442u, 2u, DIAG_TIER_MED,  V_DPF,           0.1f,  -273.0f },
    [DIAG_CH_DPF_DN]   = { 0x2441u, 2u, DIAG_TIER_MED,  V_DPF,           0.1f,  -273.0f },
    [DIAG_CH_DPF_SP]   = { 0x2432u, 2u, DIAG_TIER_MED,  V_DPF,           0.1f,  -273.0f },
    /* Delka bezici regenerace je videt na kazde strance pres pravy
     * sloupec, a zacatek regenerace ma byt poznat do par vterin. */
    [DIAG_CH_RGN_DUR]  = { 0x2487u, 2u, DIAG_TIER_MED,  V_ALL,           1.0f,     0.0f },

    /* ---- pomala skupina: kumulativni veliciny ---- */
    [DIAG_CH_SOOT]     = { 0x242Cu, 2u, DIAG_TIER_SLOW, V_ALL,           0.01f,    0.0f },
    [DIAG_CH_CSF_LOAD] = { 0x24BAu, 2u, DIAG_TIER_SLOW, V_DPF,           0.1f,     0.0f },
    [DIAG_CH_RGN_END]  = { 0x2557u, 3u, DIAG_TIER_SLOW, V_DPF,           0.1f,     0.0f },
    /* Tachometr dava smysl rozsirenym zaznamum chyb, ktere jsou v km. */
    [DIAG_CH_ODO]      = { 0x2006u, 3u, DIAG_TIER_SLOW, V_FAULT,         1.0f,     0.0f },
};

static const uint32_t TIER_MS[DIAG_TIER__COUNT] = {
    DIAG_FAST_MS, DIAG_MED_MS, DIAG_SLOW_MS
};

/* Rozsirene zaznamy 19 06: delka kazdeho cisla zaznamu. V odpovedi
 * delky nejsou, takze bez teto tabulky se rozebrat NEDAJI - a u
 * neznameho cisla se rozbor musi zastavit, ne hadat. */
typedef struct {
    uint8_t num;
    uint8_t len;
    uint8_t kind;
} rec_def_t;

static const rec_def_t REC_DEF[] = {
    { 0x80u, 3u, DIAG_REC_ODO_AT },
    { 0x81u, 1u, DIAG_REC_AGING },
    { 0x82u, 1u, DIAG_REC_COUNT },
    { 0x84u, 1u, DIAG_REC_COUNT },
    { 0x85u, 3u, DIAG_REC_ODO_HEALED },
    { 0x86u, 3u, DIAG_REC_DIST_WITH },
    { 0x87u, 3u, DIAG_REC_ODO_FIRST },
};
#define REC_DEFS ((uint8_t)(sizeof(REC_DEF) / sizeof(REC_DEF[0])))

/* ------------------------------------------------------------------
 * Co prave bezi
 * ------------------------------------------------------------------ */
enum {
    JOB_NONE = 0,
    JOB_SESSION,
    JOB_DIDS,
    JOB_DTC,
    JOB_EXT,
    JOB_FF
};

/* ------------------------------------------------------------------
 * Pomocne
 * ------------------------------------------------------------------ */
static int ch_by_did(uint16_t did)
{
    int i;

    for (i = 0; i < (int)DIAG_CH__COUNT; i++) {
        if (CHAN[i].did == did) {
            return i;
        }
    }
    return -1;
}

static uint32_t be_value(const uint8_t *b, uint8_t n)
{
    uint32_t v = 0u;
    uint8_t  i;

    for (i = 0u; i < n; i++) {
        v = (v << 8) | b[i];
    }
    return v;
}

static bool all_ff(const uint8_t *b, uint8_t n)
{
    uint8_t i;

    for (i = 0u; i < n; i++) {
        if (b[i] != 0xFFu) {
            return false;
        }
    }
    return true;
}

static uint32_t stale_ms(uint8_t tier)
{
    uint32_t lim = TIER_MS[tier] * DIAG_STALE_MULT;

    return (lim < DIAG_STALE_MIN_MS) ? DIAG_STALE_MIN_MS : lim;
}

/* ------------------------------------------------------------------
 * Nastaveni a zivotni cyklus
 * ------------------------------------------------------------------ */
void diag_poller_cfg_default(diag_poller_cfg_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->tx_id    = 0x7E0u;                  /* ECM na X82 */
    cfg->rx_id    = 0x7E8u;
    cfg->session  = UDS_SESSION_EXTENDED;    /* 10 03, tu MD1CS006 chce */
    cfg->dtc_mask = UDS_DTC_SHOW;            /* 0x2F, bez klidoveho 0x50 */
}

void diag_poller_init(diag_poller_t *p, const diag_poller_cfg_t *cfg)
{
    isotp_cfg_t ic;

    if (p == NULL || cfg == NULL) {
        return;
    }
    memset(p, 0, sizeof(*p));
    p->cfg = *cfg;

    isotp_cfg_default(&ic);
    ic.tx_id = cfg->tx_id;
    ic.rx_id = cfg->rx_id;
    ic.send  = cfg->send;
    ic.user  = cfg->user;
    isotp_init(&p->link, &ic);
    uds_init(&p->uds, &p->link);
    diag_link_init(&p->dl);

    p->view    = (uint8_t)DIAG_VIEW_MAIN;
    p->dtc_sel = -1;
}

void diag_poller_arm(diag_poller_t *p, bool on)
{
    int i;

    if (p == NULL || p->armed == on) {
        return;
    }
    p->armed = on;
    uds_abort(&p->uds);
    p->job = JOB_NONE;

    if (!on) {
        return;
    }
    /* Vedome prepnuti do normalniho rezimu je zaroven "zkus to znovu":
     * DIDy vyrazene po odmitnuti dostanou dalsi sanci a session se
     * zaklada od zacatku. */
    for (i = 0; i < (int)DIAG_CH__COUNT; i++) {
        if (p->val[i].off) {
            p->val[i].off  = false;
            p->val[i].show = (uint8_t)DIAG_SHOW_NONE;
            p->val[i].nrc  = 0u;
        }
    }
    p->session_req = true;
    p->t_retry     = p->now;
    for (i = 0; i < (int)DIAG_TIER__COUNT; i++) {
        p->t_due[i] = p->now;
    }
    p->win_start = p->now;
    p->win_req   = p->uds.n_req_sent;
}

void diag_poller_set_view(diag_poller_t *p, diag_view_t v)
{
    int i;

    if (p == NULL || v < 0 || v >= DIAG_VIEW__COUNT || p->view == (uint8_t)v) {
        return;
    }
    p->view = (uint8_t)v;
    /* Nova stranka chce jine veliciny hned, ne az dobehne perioda te
     * stare. */
    for (i = 0; i < (int)DIAG_TIER__COUNT; i++) {
        p->t_due[i] = p->now;
    }
}

void diag_poller_on_frame(diag_poller_t *p, uint32_t id, const uint8_t *data,
                          uint8_t dlc, bool from_us, uint32_t now_ms)
{
    if (p == NULL) {
        return;
    }
    /* Provoz na sbernici dokazuje pritomnost vozu i v listen-only, kdy
     * se nikdo na nic neptat nesmi - proto je tohle PRED branou. */
    diag_link_on_frame(&p->dl, id, from_us, now_ms);
    if (p->armed) {
        uds_on_frame(&p->uds, id, data, dlc, now_ms);
    }
}

/* ------------------------------------------------------------------
 * Sber vysledku
 * ------------------------------------------------------------------ */
static void harvest_dids(diag_poller_t *p)
{
    uint8_t i;
    uint8_t n = uds_did_count(&p->uds);

    for (i = 0u; i < n; i++) {
        const uds_did_res_t *r = uds_did_at(&p->uds, i);
        int ch;
        diag_val_t *v;

        if (r == NULL) {
            continue;
        }
        ch = ch_by_did(r->did);
        if (ch < 0) {
            continue;
        }
        v = &p->val[ch];

        if (r->status == (uint8_t)UDS_DID_OK) {
            const uint8_t *b = uds_did_bytes(&p->uds, r);

            if (b == NULL || r->len != CHAN[ch].len) {
                continue;   /* jina delka nez profil cekal - nerozebirat */
            }
            v->val  = (float)be_value(b, r->len) * CHAN[ch].step + CHAN[ch].off;
            v->t_ms = p->now;
            v->show = (uint8_t)DIAG_SHOW_LIVE;
            v->nrc  = 0u;
            p->n_sample++;
        } else if (r->status == (uint8_t)UDS_DID_REFUSED) {
            v->show = (uint8_t)DIAG_SHOW_REFUSED;
            v->nrc  = r->nrc;
            v->t_ms = p->now;
            /* "Tenhle DID neznam" je trvale - ptat se na nej v kazdem
             * kole by kradlo misto. Jine duvody (podminky nesplneny,
             * potreba security access) trvale nejsou, ty se zkousi dal. */
            if (r->nrc == (uint8_t)UDS_NRC_OUT_OF_RANGE ||
                r->nrc == (uint8_t)UDS_NRC_BAD_LENGTH) {
                v->off = true;
            }
        }
        /* UDS_DID_PENDING a UDS_DID_MISSING necham byt: stara hodnota
         * zustava, ale uz se neobnovila, takze zestarne sama. */
    }
}

static void harvest_dtc(diag_poller_t *p)
{
    uint16_t n = uds_dtc_count(&p->uds);
    uint16_t i;

    p->n_dtc       = 0u;
    p->dtc_dropped = 0u;
    for (i = 0u; i < n; i++) {
        uds_dtc_t d;

        if (!uds_dtc_at(&p->uds, i, &d)) {
            break;
        }
        if (p->n_dtc >= (uint16_t)DIAG_DTC_MAX) {
            p->dtc_dropped++;
            continue;
        }
        p->dtc[p->n_dtc].dtc    = d.dtc;
        p->dtc[p->n_dtc].status = d.status;
        p->n_dtc++;
    }
    p->dtc_done = true;
    p->t_dtc    = p->now;

    /* Vybrana chyba uz v pameti byt nemusi (vylecila se, nebo ji nekdo
     * smazal). Vyber se tim rusi, aby detail neukazoval neco, co uz
     * nikde neni. */
    if (p->dtc_sel >= 0) {
        bool found = false;

        for (i = 0u; i < p->n_dtc; i++) {
            if (p->dtc[i].dtc == p->dtc_sel_code) {
                p->dtc_sel = (int16_t)i;
                found = true;
                break;
            }
        }
        if (!found) {
            diag_poller_dtc_select(p, -1);
        }
    }
}

/* 59 06 <dtc 3B> <stav> ( <cislo zaznamu> <data> )* */
static void harvest_ext(diag_poller_t *p)
{
    const uint8_t *r = uds_rsp(&p->uds);
    uint16_t len = uds_rsp_len(&p->uds);
    uint16_t at  = 6u;

    p->n_rec    = 0u;
    p->rec_done = true;
    if (len < 6u || r[0] != 0x59u || r[1] != UDS_DTC_EXTENDED) {
        return;
    }
    while (at < len && p->n_rec < (uint8_t)DIAG_REC_MAX) {
        uint8_t num = r[at];
        uint8_t k;
        const rec_def_t *def = NULL;

        for (k = 0u; k < REC_DEFS; k++) {
            if (REC_DEF[k].num == num) {
                def = &REC_DEF[k];
                break;
            }
        }
        if (def == NULL || (uint16_t)(at + 1u + def->len) > len) {
            break;   /* neznama delka - dal uz by to bylo hadani */
        }
        p->rec[p->n_rec].num   = num;
        p->rec[p->n_rec].kind  = def->kind;
        p->rec[p->n_rec].len   = def->len;
        p->rec[p->n_rec].unset = all_ff(&r[at + 1u], def->len);
        p->rec[p->n_rec].val   = be_value(&r[at + 1u], def->len);
        p->n_rec++;
        at = (uint16_t)(at + 1u + def->len);
    }
}

/* 59 04 <dtc 3B> <stav> ( <cislo zaznamu> <pocet DIDu> ( <did 2B> <data> )* )* */
static void harvest_ff(diag_poller_t *p)
{
    const uint8_t *r = uds_rsp(&p->uds);
    uint16_t len = uds_rsp_len(&p->uds);
    uint16_t at  = 6u;

    p->n_ff    = 0u;
    p->ff_done = true;
    if (len < 8u || r[0] != 0x59u || r[1] != UDS_DTC_SNAPSHOT) {
        return;
    }
    while ((uint16_t)(at + 2u) <= len && p->n_ff < (uint8_t)DIAG_FF_MAX) {
        uint8_t ids = r[at + 1u];
        uint8_t i;

        at = (uint16_t)(at + 2u);          /* cislo zaznamu a pocet DIDu */
        for (i = 0u; i < ids && p->n_ff < (uint8_t)DIAG_FF_MAX; i++) {
            uint16_t did;
            int ch;

            if ((uint16_t)(at + 2u) > len) {
                return;
            }
            did = (uint16_t)(((uint16_t)r[at] << 8) | r[at + 1u]);
            ch  = ch_by_did(did);
            /* Delka dat DIDu je v profilu vozidla, ne v odpovedi. Na
             * neznamem DIDu uz nevime, kde zacina ten dalsi. */
            if (ch < 0 || (uint16_t)(at + 2u + CHAN[ch].len) > len) {
                return;
            }
            p->ff[p->n_ff].did = did;
            p->ff[p->n_ff].ch  = (uint8_t)ch;
            p->ff[p->n_ff].val = (float)be_value(&r[at + 2u], CHAN[ch].len) *
                                 CHAN[ch].step + CHAN[ch].off;
            p->n_ff++;
            at = (uint16_t)(at + 2u + CHAN[ch].len);
        }
    }
}

/* ------------------------------------------------------------------
 * Vyber dalsi prace
 * ------------------------------------------------------------------ */
static bool tier_wanted(const diag_poller_t *p, uint8_t tier)
{
    uint16_t bit = DIAG_VIEW_BIT(p->view);
    int i;

    for (i = 0; i < (int)DIAG_CH__COUNT; i++) {
        if (CHAN[i].tier == tier && (CHAN[i].views & bit) != 0u &&
            !p->val[i].off) {
            return true;
        }
    }
    return false;
}

/* Nejvic zpozdena skupina vyhrava. Pri shode (hned po startu jsou
 * zpozdene vsechny o nulu) zustava poradi tabulky, tedy rychla prvni;
 * stredni a pomala se dostanou ke slovu v okamziku, kdy jejich
 * zpozdeni prekroci zpozdeni rychle - a to nastane vzdycky, protoze
 * rychla se obslouzi kazde kolo. */
static int pick_tier(const diag_poller_t *p)
{
    int best = -1;
    uint32_t best_over = 0u;
    int t;

    for (t = 0; t < (int)DIAG_TIER__COUNT; t++) {
        uint32_t over;

        if (!tier_wanted(p, (uint8_t)t)) {
            continue;
        }
        if ((int32_t)(p->now - p->t_due[t]) < 0) {
            continue;
        }
        over = p->now - p->t_due[t];
        if (best < 0 || over > best_over) {
            best      = t;
            best_over = over;
        }
    }
    return best;
}

static uint8_t build_batch(const diag_poller_t *p, uint8_t tier,
                           uds_did_req_t *out)
{
    uint16_t bit = DIAG_VIEW_BIT(p->view);
    uint8_t  n = 0u;
    int i;

    for (i = 0; i < (int)DIAG_CH__COUNT && n < (uint8_t)UDS_BATCH_MAX; i++) {
        if (CHAN[i].tier != tier || (CHAN[i].views & bit) == 0u ||
            p->val[i].off) {
            continue;
        }
        out[n].did = CHAN[i].did;
        out[n].len = CHAN[i].len;
        n++;
    }
    return n;
}

static bool dtc_due(const diag_poller_t *p)
{
    if (p->dtc_force) {
        return true;
    }
    if (p->view != (uint8_t)DIAG_VIEW_FAULTS) {
        return false;
    }
    if (!p->dtc_done) {
        return true;
    }
    return (p->now - p->t_dtc) >= DIAG_DTC_MS;
}

static void start_next(diag_poller_t *p)
{
    uds_did_req_t want[UDS_BATCH_MAX];
    uint8_t n;
    int t;

    if (p->session_req) {
        if (uds_session(&p->uds, p->cfg.session, p->now)) {
            p->job         = JOB_SESSION;
            p->session_req = false;
        } else {
            p->t_retry = p->now + DIAG_RETRY_MS;
        }
        return;
    }

    /* Rozsirene zaznamy driv nez freeze frame: prave ty zmizi, jakmile
     * se chyba sama vyleci nebo ji nekdo smaze. */
    if (p->want_ext && p->dtc_sel >= 0) {
        if (uds_read_extended(&p->uds, p->dtc_sel_code, 0xFFu, p->now)) {
            p->job      = JOB_EXT;
            p->want_ext = false;
        } else {
            p->t_retry = p->now + DIAG_RETRY_MS;
        }
        return;
    }
    if (p->want_ff && p->dtc_sel >= 0) {
        if (uds_read_freeze(&p->uds, p->dtc_sel_code, 0xFFu, p->now)) {
            p->job     = JOB_FF;
            p->want_ff = false;
        } else {
            p->t_retry = p->now + DIAG_RETRY_MS;
        }
        return;
    }
    if (dtc_due(p)) {
        if (uds_read_dtcs(&p->uds, p->cfg.dtc_mask, p->now)) {
            p->job       = JOB_DTC;
            p->dtc_force = false;
        } else {
            p->t_retry = p->now + DIAG_RETRY_MS;
        }
        return;
    }

    t = pick_tier(p);
    if (t < 0) {
        return;
    }
    n = build_batch(p, (uint8_t)t, want);
    if (n == 0u) {
        p->t_due[t] = p->now + TIER_MS[t];
        return;
    }
    if (uds_read_dids(&p->uds, want, n, p->now)) {
        p->job      = JOB_DIDS;
        p->job_tier = (uint8_t)t;
    } else {
        p->t_due[t] = p->now + TIER_MS[t];
        p->t_retry  = p->now + DIAG_RETRY_MS;
    }
    p->rtt_start = p->now;
}

/* ------------------------------------------------------------------
 * Hlavni krok
 * ------------------------------------------------------------------ */
void diag_poller_poll(diag_poller_t *p, uint32_t now_ms)
{
    uint32_t rc;

    if (p == NULL) {
        return;
    }
    p->now = now_ms;

    /* Uroven spoje se pocita i v listen-only: provoz na sbernici chodi
     * bez ohledu na to, jestli se smime ptat. */
    if (!p->armed) {
        diag_link_poll(&p->dl, now_ms);
        return;
    }

    uds_poll(&p->uds, now_ms);

    rc = uds_rsp_count(&p->uds);
    if (rc != p->seen_rsp) {
        p->seen_rsp = rc;
        diag_link_on_reply(&p->dl, p->cfg.rx_id, now_ms);
    }

    if (p->job != JOB_NONE && !uds_busy(&p->uds)) {
        uds_state_t s = uds_state(&p->uds);

        if (s == UDS_DONE || s == UDS_FAILED) {
            if (s == UDS_FAILED) {
                p->n_fail++;
                if (uds_error(&p->uds) == UDS_ERR_TIMEOUT) {
                    diag_link_on_silence(&p->dl, now_ms);
                }
                p->t_retry = now_ms + DIAG_RETRY_MS;
            }
            switch (p->job) {
            case JOB_DIDS:
                harvest_dids(p);
                p->t_due[p->job_tier] = now_ms + TIER_MS[p->job_tier];
                p->rtt_ms = now_ms - p->rtt_start;
                break;
            case JOB_DTC:
                if (s == UDS_DONE) {
                    harvest_dtc(p);
                }
                break;
            case JOB_EXT:
                if (s == UDS_DONE) {
                    harvest_ext(p);
                }
                break;
            case JOB_FF:
                if (s == UDS_DONE) {
                    harvest_ff(p);
                }
                break;
            default:
                break;
            }
            p->job = JOB_NONE;
        }
    }

    diag_link_poll(&p->dl, now_ms);

    /* Merene cislo, ne odhad: kolik pozadavku opravdu odeslo za
     * posledni vterinu. Pocita se i tester present a 10 xx - na
     * sbernici je to stejny provoz jako dotaz na DIDy. */
    p->n_req = p->uds.n_req_sent;
    if ((uint32_t)(now_ms - p->win_start) >= 1000u) {
        uint32_t el = now_ms - p->win_start;
        uint32_t d  = p->n_req - p->win_req;

        p->rate_x100 = (d * 100000u) / el;
        p->win_start = now_ms;
        p->win_req   = p->n_req;
    }

    if (p->job != JOB_NONE || uds_busy(&p->uds)) {
        return;
    }
    if ((int32_t)(now_ms - p->t_retry) < 0) {
        return;
    }
    start_next(p);
}

/* ------------------------------------------------------------------
 * Hodnoty
 * ------------------------------------------------------------------ */
diag_show_t diag_poller_show(const diag_poller_t *p, int ch)
{
    const diag_val_t *v;

    if (p == NULL || ch < 0 || ch >= (int)DIAG_CH__COUNT) {
        return DIAG_SHOW_NONE;
    }
    v = &p->val[ch];
    if (v->show != (uint8_t)DIAG_SHOW_LIVE) {
        return (diag_show_t)v->show;
    }
    return ((p->now - v->t_ms) > stale_ms(CHAN[ch].tier)) ? DIAG_SHOW_STALE
                                                          : DIAG_SHOW_LIVE;
}

float diag_poller_value(const diag_poller_t *p, int ch)
{
    if (p == NULL || ch < 0 || ch >= (int)DIAG_CH__COUNT) {
        return 0.0f;
    }
    return p->val[ch].val;
}

uint8_t diag_poller_nrc(const diag_poller_t *p, int ch)
{
    if (p == NULL || ch < 0 || ch >= (int)DIAG_CH__COUNT) {
        return 0u;
    }
    return p->val[ch].nrc;
}

uint32_t diag_poller_age_ms(const diag_poller_t *p, int ch)
{
    if (p == NULL || ch < 0 || ch >= (int)DIAG_CH__COUNT) {
        return UINT32_MAX;
    }
    if (p->val[ch].show != (uint8_t)DIAG_SHOW_LIVE) {
        return UINT32_MAX;
    }
    return p->now - p->val[ch].t_ms;
}

uint32_t diag_poller_fresh_ms(const diag_poller_t *p)
{
    uint32_t best = UINT32_MAX;
    int i;

    if (p == NULL) {
        return UINT32_MAX;
    }
    for (i = 0; i < (int)DIAG_CH__COUNT; i++) {
        uint32_t age;

        if (p->val[i].show != (uint8_t)DIAG_SHOW_LIVE) {
            continue;
        }
        age = p->now - p->val[i].t_ms;
        if (age < best) {
            best = age;
        }
    }
    return best;
}

diag_tier_t diag_poller_tier(int ch)
{
    if (ch < 0 || ch >= (int)DIAG_CH__COUNT) {
        return DIAG_TIER_SLOW;
    }
    return (diag_tier_t)CHAN[ch].tier;
}

/* ------------------------------------------------------------------
 * Chybova pamet
 * ------------------------------------------------------------------ */
const diag_dtc_t *diag_poller_dtc_at(const diag_poller_t *p, uint16_t i)
{
    if (p == NULL || i >= p->n_dtc) {
        return NULL;
    }
    return &p->dtc[i];
}

void diag_poller_dtc_refresh(diag_poller_t *p)
{
    if (p != NULL) {
        p->dtc_force = true;
    }
}

void diag_poller_dtc_select(diag_poller_t *p, int i)
{
    if (p == NULL) {
        return;
    }
    p->n_rec    = 0u;
    p->n_ff     = 0u;
    p->rec_done = false;
    p->ff_done  = false;

    if (i < 0 || i >= (int)p->n_dtc) {
        p->dtc_sel      = -1;
        p->dtc_sel_code = 0u;
        p->want_ext     = false;
        p->want_ff      = false;
        return;
    }
    p->dtc_sel      = (int16_t)i;
    p->dtc_sel_code = p->dtc[i].dtc;
    p->want_ext     = true;
    p->want_ff      = true;
}

const diag_rec_t *diag_poller_rec_at(const diag_poller_t *p, uint8_t i)
{
    if (p == NULL || i >= p->n_rec) {
        return NULL;
    }
    return &p->rec[i];
}

const diag_ff_t *diag_poller_ff_at(const diag_poller_t *p, uint8_t i)
{
    if (p == NULL || i >= p->n_ff) {
        return NULL;
    }
    return &p->ff[i];
}

/* Kodovani podle ISO 15031-6: dva nejvyssi bity urcuji skupinu, zbytek
 * jsou hexadecimalni cislice. Pripona za pomlckou je bajt typu zavady,
 * ktery UDS pridava navic oproti klasickemu OBD kodu. */
void diag_dtc_text(uint32_t dtc, char *out, unsigned n)
{
    static const char GROUP[4] = { 'P', 'C', 'B', 'U' };
    static const char HEX[17]  = "0123456789ABCDEF";
    uint8_t a = (uint8_t)((dtc >> 16) & 0xFFu);
    uint8_t b = (uint8_t)((dtc >> 8) & 0xFFu);
    uint8_t c = (uint8_t)(dtc & 0xFFu);
    char buf[9];

    if (out == NULL || n == 0u) {
        return;
    }
    buf[0] = GROUP[(a >> 6) & 3u];
    buf[1] = HEX[(a >> 4) & 3u];
    buf[2] = HEX[a & 0x0Fu];
    buf[3] = HEX[(b >> 4) & 0x0Fu];
    buf[4] = HEX[b & 0x0Fu];
    buf[5] = '-';
    buf[6] = HEX[(c >> 4) & 0x0Fu];
    buf[7] = HEX[c & 0x0Fu];
    buf[8] = '\0';

    {
        unsigned i = 0u;

        while (i + 1u < n && buf[i] != '\0') {
            out[i] = buf[i];
            i++;
        }
        out[i] = '\0';
    }
}
