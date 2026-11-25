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
 *
 * Sloupec F rika, co je na radku zvlastniho. F_PROBE je ze vsech
 * nejdulezitejsi: takovy radek je OPSANY Z POPISU JEDNOTKY a jeste
 * nikdo ho neprecetl ze skutecneho vozu. Cislo, ktere z nej vyleze,
 * je zatim tvrzeni souboru, ne mereni - a UI to musi rict nahlas,
 * dokud jednotka poprve neodpovi.
 * ------------------------------------------------------------------ */
#define V_MAIN   DIAG_VIEW_BIT(DIAG_VIEW_MAIN)
#define V_ENG    DIAG_VIEW_BIT(DIAG_VIEW_ENGINE)
#define V_BOOST  DIAG_VIEW_BIT(DIAG_VIEW_BOOST)
#define V_FAULT  DIAG_VIEW_BIT(DIAG_VIEW_FAULTS)
#define V_DPF    DIAG_VIEW_BIT(DIAG_VIEW_DPF)
#define V_BATT   DIAG_VIEW_BIT(DIAG_VIEW_BATT)
#define V_INJ    DIAG_VIEW_BIT(DIAG_VIEW_INJ)
#define V_ALL    DIAG_VIEW_ALL

#define F_SIGN  0x01u   /* dvojkovy doplnek; popis jednotky rika signed */
#define F_BIT   0x02u   /* jediny bit, jeho cislo je ve sloupci bit */
#define F_EMM   0x04u   /* EMM 74D, blok 21 34 - ne 22 <did> na ECM */
#define F_ONCE  0x08u   /* jen na vyzadani, do zadneho kola nepatri */
#define F_PROBE 0x10u   /* opsano z popisu jednotky, na voze neovereno */

typedef struct {
    uint16_t did;     /* 22 <did> na ECM; u EMM 0 */
    uint8_t  len;     /* delka dat, sloupec Bytes v profilu vozidla */
    uint8_t  tier;    /* diag_tier_t */
    uint16_t views;
    float    step;
    float    off;
    /* Dal uz jen to, co ma vetsina radku nulove. Vynechane polozky
     * vynuluje jazyk sam, takze puvodni radky zustaly beze zmeny. */
    uint8_t  flags;
    uint8_t  at;      /* EMM: index do dat bloku 21 34 */
    uint8_t  bit;     /* jen s F_BIT; cislovani jako v popisu jednotky,
                         tedy 0 = nejvyssi bit prvniho bajtu */
} chan_t;

static const chan_t CHAN[DIAG_CH__COUNT] = {
    /* ---- rychla skupina: bez ni je velicina k nicemu ---- */
    [DIAG_CH_RPM]      = { 0x2002u, 2u, DIAG_TIER_FAST, V_MAIN | V_INJ,  0.25f,    0.0f },
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

    /* ---- protitlak vyfuku a tlakovy rozdil na filtru ----
     * Skalovani je opsane z MD1CS006_7A00_5P0: 2448 "Pressure before
     * turbine" (mbar, bez prepoctu), 2443 "CSF differential pressure"
     * (mbar, offset -32768) a 2444 "CSF differential pressure sensor
     * voltage" (0,01 V, offset -327,68). 252D je jediny bit na pozici
     * 7 prvniho datoveho bajtu a jednotka jim sama rika, ze signal
     * snimace prestal chodit; dokud sviti, je rozdil tlaku bezcenny.
     * Stredni skupina: obe tlakova cisla se pri jizde hybou s plynem,
     * priznak zamrznuti se nehybe skoro nikdy. */
    [DIAG_CH_EXH_P]      = { 0x2448u, 2u, DIAG_TIER_MED,  V_DPF, 1.0f,       0.0f, F_PROBE },
    [DIAG_CH_DPF_DP]     = { 0x2443u, 2u, DIAG_TIER_MED,  V_DPF, 1.0f,  -32768.0f, F_PROBE },
    [DIAG_CH_DPF_DP_U]   = { 0x2444u, 2u, DIAG_TIER_MED,  V_DPF, 0.01f,   -327.68f, F_PROBE },
    [DIAG_CH_DPF_FROZEN] = { 0x252Du, 1u, DIAG_TIER_SLOW, V_DPF, 1.0f,       0.0f,
                             F_PROBE | F_BIT, 0u, 7u },

    /* ---- stav baterie: EMM 74D, jediny blok 21 34 ----
     * Indexy jsou sloupec firstbyte z EMM_EDISON_DDT2000_SW15_3
     * zmenseny o tri: popis cisluje od jednicky a pres hlavicku
     * 61 34, kdezto legacy_data() uz hlavicku nema.
     *
     * V_I_IbattSensor_flt ma offset -200 A, takze ZNAMENKO JE V
     * SKALOVANI a nikde se neodstranuje: syrova nula je -200 A a
     * rozdil mezi nabijenim a odberem je prave to, kvuli cemu se
     * tenhle blok cte. */
    [DIAG_CH_BATT_I]       = { 0u, 2u, DIAG_TIER_MED, V_BATT, 0.01f,   -200.0f,
                               F_PROBE | F_EMM, 23u },
    [DIAG_CH_BATT_SOC]     = { 0u, 3u, DIAG_TIER_MED, V_BATT, 0.001f,     0.0f,
                               F_PROBE | F_EMM, 25u },
    [DIAG_CH_ALT_LOAD]     = { 0u, 2u, DIAG_TIER_MED, V_BATT, 0.39063f,   0.0f,
                               F_PROBE | F_EMM, 4u },
    [DIAG_CH_BATT_U_CRANK] = { 0u, 2u, DIAG_TIER_MED, V_BATT, 0.001f,     0.0f,
                               F_PROBE | F_EMM, 30u },
    [DIAG_CH_BATT_U_SLEEP] = { 0u, 2u, DIAG_TIER_MED, V_BATT, 0.001f,    11.5f,
                               F_PROBE | F_EMM, 28u },

    /* ---- vyrovnani vstrikovacu: FBC_qDvtCylPhys[0..3] ----
     * Dvoubajtove se znamenkem, deleno stem. Prazdna maska pohledu je
     * zamer - do zadneho kola to nepatri, cte se jen na vyzadani a jen
     * na teplem volnobehu, kde regulator vubec bezi. */
    [DIAG_CH_FBC1] = { 0xFD38u, 2u, DIAG_TIER_SLOW, 0u, 0.01f, 0.0f, F_PROBE | F_SIGN | F_ONCE },
    [DIAG_CH_FBC2] = { 0xFD39u, 2u, DIAG_TIER_SLOW, 0u, 0.01f, 0.0f, F_PROBE | F_SIGN | F_ONCE },
    [DIAG_CH_FBC3] = { 0xFD3Au, 2u, DIAG_TIER_SLOW, 0u, 0.01f, 0.0f, F_PROBE | F_SIGN | F_ONCE },
    [DIAG_CH_FBC4] = { 0xFD3Bu, 2u, DIAG_TIER_SLOW, 0u, 0.01f, 0.0f, F_PROBE | F_SIGN | F_ONCE },
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
    JOB_FF,
    JOB_EMM,    /* 21 34 na energetice */
    JOB_FBC     /* jednorazove FD38..FD3B */
};

/* ------------------------------------------------------------------
 * Pomocne
 * ------------------------------------------------------------------ */
static int ch_by_did(uint16_t did)
{
    int i;

    /* Nula neni DID - tu maji radky, ktere na ECM vubec nepatri. */
    if (did == 0u) {
        return -1;
    }
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

/* Syrove bajty na fyzikalni hodnotu podle jednoho radku profilu.
 * Jedine misto, kde se skalovani provadi - at uz data prisla z 22 na
 * ECM nebo z bloku 21 34 na energetice. */
static float chan_scale(const chan_t *c, const uint8_t *b)
{
    uint32_t raw = be_value(b, c->len);
    int32_t  s;
    unsigned n;

    if ((c->flags & F_BIT) != 0u) {
        /* Popis jednotky cisluje bity od nejvyssiho: bit 0 je nejvyssi
         * bit prvniho bajtu, takze se zprava posouva o zbytek. */
        n = (unsigned)c->len * 8u;
        raw = (raw >> (n - 1u - (unsigned)c->bit)) & 1u;
        return (float)raw * c->step + c->off;
    }

    s = (int32_t)raw;
    if ((c->flags & F_SIGN) != 0u) {
        n = (unsigned)c->len * 8u;
        if (n < 32u && (raw & (1u << (n - 1u))) != 0u) {
            s = (int32_t)(raw | (0xFFFFFFFFu << n));
        }
    }
    return (float)s * c->step + c->off;
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
    cfg->emm_tx_id = 0x74Du;                 /* energetika EMM */
    cfg->emm_rx_id = 0x76Du;
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

    /* Druhy spoj: tataz sbernice, jine identifikatory, jiny protokol. */
    ic.tx_id = cfg->emm_tx_id;
    ic.rx_id = cfg->emm_rx_id;
    isotp_init(&p->emm_link, &ic);
    legacy_init(&p->emm, &p->emm_link);
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
    legacy_abort(&p->emm);
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
    /* Totez pro energetiku a pro jednorazove mereni: po vedomem
     * prepnuti uz nic neplati a nic se nepamatuje jako vysledek. */
    p->emm_off  = false;
    p->t_emm    = p->now;
    p->fbc_req  = false;
    p->fbc_done = false;
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
        legacy_on_frame(&p->emm, id, data, dlc, now_ms);
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
            v->val  = chan_scale(&CHAN[ch], b);
            v->t_ms = p->now;
            v->show = (uint8_t)DIAG_SHOW_LIVE;
            v->nrc  = 0u;
            v->seen = true;
            p->n_sample++;
        } else if (r->status == (uint8_t)UDS_DID_REFUSED) {
            v->show = (uint8_t)DIAG_SHOW_REFUSED;
            v->nrc  = r->nrc;
            v->t_ms = p->now;
            /* "Tenhle DID neznam" je trvale - ptat se na nej v kazdem
             * kole by kradlo misto. Jine duvody (podminky nesplneny,
             * potreba security access) trvale nejsou, ty se zkousi dal. */
            if ((CHAN[ch].flags & F_ONCE) == 0u &&
                (r->nrc == (uint8_t)UDS_NRC_OUT_OF_RANGE ||
                 r->nrc == (uint8_t)UDS_NRC_BAD_LENGTH)) {
                v->off = true;
            }
        }
        /* UDS_DID_PENDING a UDS_DID_MISSING necham byt: stara hodnota
         * zustava, ale uz se neobnovila, takze zestarne sama. */
    }
}

/* ------------------------------------------------------------------
 * Energetika
 *
 * Jedna odpoved 61 34 nese cely blok a z nej se vybiraji jednotlive
 * polozky podle sloupce at. Kratsi odpoved se NEDOPLNUJE: polozka,
 * ktera se do prislych dat nevesla, zustava, jak byla, a zestarne
 * sama - vymyslet za jednotku bajty, ktere neposlala, je presne to,
 * co se tady delat nesmi.
 * ------------------------------------------------------------------ */
static void harvest_emm(diag_poller_t *p)
{
    const uint8_t *d = legacy_data(&p->emm);
    uint16_t       n = legacy_len(&p->emm);
    int i;

    if (d == NULL || legacy_block(&p->emm) != DIAG_EMM_BLOCK) {
        return;
    }
    for (i = 0; i < (int)DIAG_CH__COUNT; i++) {
        const chan_t *c = &CHAN[i];
        diag_val_t   *v = &p->val[i];

        if ((c->flags & F_EMM) == 0u) {
            continue;
        }
        if ((uint32_t)c->at + c->len > (uint32_t)n) {
            continue;
        }
        v->val  = chan_scale(c, d + c->at);
        v->t_ms = p->now;
        v->show = (uint8_t)DIAG_SHOW_LIVE;
        v->nrc  = 0u;
        v->seen = true;
        p->n_sample++;
    }
}

/* Blok odmitnuty jednotkou. Odmitnuti plati pro vsechny polozky z nej
 * najednou - prisla jedna odpoved na jeden dotaz, ne pet. */
static void harvest_emm_nrc(diag_poller_t *p, uint8_t nrc)
{
    int i;

    for (i = 0; i < (int)DIAG_CH__COUNT; i++) {
        if ((CHAN[i].flags & F_EMM) == 0u) {
            continue;
        }
        p->val[i].show = (uint8_t)DIAG_SHOW_REFUSED;
        p->val[i].nrc  = nrc;
        p->val[i].t_ms = p->now;
    }
    /* "Tuhle sluzbu ani blok neznam" je trvale; podminky a session se
     * zkousi dal, stejne jako u 22 na motoru. */
    if (nrc == (uint8_t)UDS_NRC_OUT_OF_RANGE ||
        nrc == (uint8_t)UDS_NRC_SERVICE_NOT_SUP ||
        nrc == (uint8_t)UDS_NRC_SUBFUNC_NOT_SUP) {
        p->emm_off = true;
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

/* Patri tenhle radek do bezneho kola dotazu 22 na motor? Radky z
 * energetiky maji vlastni transport a jednorazove mereni se spousti
 * tlacitkem, takze ani jeden do davky nepatri. */
static bool chan_cyclic(int i)
{
    return (CHAN[i].flags & (F_EMM | F_ONCE)) == 0u;
}

static bool tier_wanted(const diag_poller_t *p, uint8_t tier)
{
    uint16_t bit = DIAG_VIEW_BIT(p->view);
    int i;

    for (i = 0; i < (int)DIAG_CH__COUNT; i++) {
        if (chan_cyclic(i) && CHAN[i].tier == tier &&
            (CHAN[i].views & bit) != 0u && !p->val[i].off) {
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
        if (!chan_cyclic(i) || CHAN[i].tier != tier ||
            (CHAN[i].views & bit) == 0u || p->val[i].off) {
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

/* Energetika se cte jen tam, kde je opravdu videt. Je to jeden dotaz
 * navic na sbernici a na kterekoliv jine strance by nebyl komu k
 * uzitku. */
static bool emm_due(const diag_poller_t *p)
{
    if (p->emm_off || p->view != (uint8_t)DIAG_VIEW_BATT) {
        return false;
    }
    return (int32_t)(p->now - p->t_emm) >= 0;
}

/* Kolik radku ma jednorazove mereni a jake DIDy. */
static uint8_t build_fbc(uds_did_req_t *out)
{
    uint8_t n = 0u;
    int i;

    for (i = 0; i < (int)DIAG_CH__COUNT && n < (uint8_t)UDS_BATCH_MAX; i++) {
        if ((CHAN[i].flags & F_ONCE) == 0u) {
            continue;
        }
        out[n].did = CHAN[i].did;
        out[n].len = CHAN[i].len;
        n++;
    }
    return n;
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

    /* Jednorazove mereni jde pred vsechno ostatni: uzivatel si o nej
     * prave rekl a ma smysl jen v tom stavu motoru, ve kterem na
     * tlacitko sahl. */
    if (p->fbc_req) {
        n = build_fbc(want);
        if (n != 0u && uds_read_dids(&p->uds, want, n, p->now)) {
            p->job     = JOB_FBC;
            p->fbc_req = false;
        } else {
            p->t_retry = p->now + DIAG_RETRY_MS;
        }
        p->rtt_start = p->now;
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
    if (emm_due(p)) {
        /* legacy_read() si session 10 C0 zalozi sam, kdyz zadna
         * neplati, takze tady zadny vlastni krok navic neni. */
        if (legacy_read(&p->emm, DIAG_EMM_BLOCK, p->now)) {
            p->job = JOB_EMM;
        } else {
            p->t_retry = p->now + DIAG_RETRY_MS;
        }
        p->t_emm = p->now + DIAG_EMM_MS;
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
    legacy_poll(&p->emm, now_ms);

    rc = uds_rsp_count(&p->uds);
    if (rc != p->seen_rsp) {
        p->seen_rsp = rc;
        diag_link_on_reply(&p->dl, p->cfg.rx_id, now_ms);
    }

    /* Energetika ma vlastniho klienta, takze se jeji dobehnuti musi
     * hlidat zvlast - uds_busy() o ni nic nevi a bez tohohle by se
     * dotaz na 21 34 uzavrel uz pri prvnim dalsim kole, jeste nez
     * odpoved vubec dorazila. */
    if (p->job == JOB_EMM) {
        legacy_state_t ls = legacy_state(&p->emm);

        if (!legacy_busy(&p->emm) &&
            (ls == LEGACY_DONE || ls == LEGACY_FAILED)) {
            if (ls == LEGACY_DONE) {
                harvest_emm(p);
                diag_link_on_reply(&p->dl, p->cfg.emm_rx_id, now_ms);
            } else {
                p->n_fail++;
                if (legacy_error(&p->emm) == UDS_ERR_NRC) {
                    harvest_emm_nrc(p, legacy_nrc(&p->emm));
                } else if (legacy_error(&p->emm) == UDS_ERR_TIMEOUT) {
                    diag_link_on_silence(&p->dl, now_ms);
                }
                p->t_retry = now_ms + DIAG_RETRY_MS;
            }
            p->job = JOB_NONE;
        }
    } else if (p->job != JOB_NONE && !uds_busy(&p->uds)) {
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
            case JOB_FBC:
                harvest_dids(p);
                p->rtt_ms   = now_ms - p->rtt_start;
                /* Mereni probehlo at tak nebo tak; co z nej vyslo,
                 * rika stav jednotlivych kanalu. */
                p->fbc_done = true;
                p->fbc_t_ms = now_ms;
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
    p->n_req = p->uds.n_req_sent + p->emm.n_req_sent;
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

bool diag_poller_unverified(int ch)
{
    if (ch < 0 || ch >= (int)DIAG_CH__COUNT) {
        return false;
    }
    return (CHAN[ch].flags & F_PROBE) != 0u;
}

bool diag_poller_proven(const diag_poller_t *p, int ch)
{
    if (p == NULL || ch < 0 || ch >= (int)DIAG_CH__COUNT) {
        return false;
    }
    return p->val[ch].seen;
}

/* ------------------------------------------------------------------
 * Jednorazove mereni vyrovnani vstrikovacu
 * ------------------------------------------------------------------ */
void diag_poller_fbc_start(diag_poller_t *p)
{
    int i;

    if (p == NULL || !p->armed) {
        return;
    }
    /* Nove mereni zacina od nuly, ne od toho, co zbylo po minulem -
     * jinak by pri odpovedi jen na cast valcu zustala stara cisla
     * vedle novych a vypadala by jako jedno mereni. */
    for (i = 0; i < (int)DIAG_CH__COUNT; i++) {
        if ((CHAN[i].flags & F_ONCE) == 0u) {
            continue;
        }
        p->val[i].show = (uint8_t)DIAG_SHOW_NONE;
        p->val[i].nrc  = 0u;
        p->val[i].off  = false;
    }
    p->fbc_done = false;
    p->fbc_req  = true;
    p->t_retry  = p->now;
}

bool diag_poller_fbc_busy(const diag_poller_t *p)
{
    if (p == NULL) {
        return false;
    }
    return p->fbc_req || p->job == JOB_FBC;
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
