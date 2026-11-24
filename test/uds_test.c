/*
 * uds_test.c - testy diagnosticke vrstvy na hostiteli
 *
 * Pokryva src/diag/uds.c, src/diag/legacy.c a src/diag/link.c nad
 * skutecnym src/can/isotp.c. Zadna deska, zadne vozidlo, zadne hodiny:
 * misto radice TWAI je fake_send(), ktery ramce uklada do pole, a cas
 * se dosazuje rucne - diky tomu jde overit i vyprseni session nebo
 * ticho jednotky, na ktere by se jinak cekalo vteriny.
 *
 * Preklad a spusteni:
 *
 *     cmake -S test -B test/build
 *     cmake --build test/build
 *     ./test/build/uds_test
 *
 * Navratovy kod 0 = vse proslo.
 */
#include "diag/uds.h"
#include "diag/legacy.h"
#include "diag/link.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------
 * Nahrada sbernice
 * ------------------------------------------------------------------ */
#define TX_CAP  64
#define LOG_CAP 512

typedef struct {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  dlc;
} sent_frame_t;

static sent_frame_t g_tx[TX_CAP];
static int  g_ntx;
static bool g_send_ok;

/* Zaznam VSECH ramcu za cely beh, pres hranice testu. Slouzi k jedine
 * kontrole: ze z teto vrstvy nikdy neodejde zapis. */
static sent_frame_t g_log[LOG_CAP];
static int  g_nlog;

static bool fake_send(void *user, uint32_t id, const uint8_t *data, uint8_t dlc)
{
    (void)user;
    if (!g_send_ok) {
        return false;
    }
    if (g_ntx < TX_CAP) {
        g_tx[g_ntx].id = id;
        g_tx[g_ntx].dlc = dlc;
        memset(g_tx[g_ntx].data, 0, sizeof(g_tx[g_ntx].data));
        memcpy(g_tx[g_ntx].data, data, dlc);
        g_ntx++;
    }
    if (g_nlog < LOG_CAP) {
        g_log[g_nlog].id = id;
        g_log[g_nlog].dlc = dlc;
        memset(g_log[g_nlog].data, 0, sizeof(g_log[g_nlog].data));
        memcpy(g_log[g_nlog].data, data, dlc);
        g_nlog++;
    }
    return true;
}

#define ECU_TX 0x7E0u   /* my -> motor */
#define ECU_RX 0x7E8u   /* motor -> my */
#define BCM_TX 0x745u   /* my -> karoserie, stary protokol */
#define BCM_RX 0x765u

/* ------------------------------------------------------------------
 * Drobny testovaci stroj - stejny jako v isotp_test.c
 * ------------------------------------------------------------------ */
static int g_checks;
static int g_failed;
static const char *g_test;

static void check(bool ok, const char *what, int line)
{
    g_checks++;
    if (!ok) {
        g_failed++;
        printf("   %s:%d  %s\n", g_test, line, what);
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/* ------------------------------------------------------------------
 * Pohled na odeslane ramce
 *
 * Mezi nasimi pozadavky lezi i nas vlastni flow control (PCI 3), ktery
 * odchazi pri prijmu viceramcove odpovedi. Kontroly se tykaji
 * pozadavku, proto se FC preskakuje.
 * ------------------------------------------------------------------ */
static int req_count(void)
{
    int i, n = 0;

    for (i = 0; i < g_ntx; i++) {
        if ((g_tx[i].data[0] >> 4) == 0x0u) {
            n++;
        }
    }
    return n;
}

static const sent_frame_t *req_at(int k)
{
    int i, n = 0;

    for (i = 0; i < g_ntx; i++) {
        if ((g_tx[i].data[0] >> 4) == 0x0u) {
            if (n == k) {
                return &g_tx[i];
            }
            n++;
        }
    }
    return NULL;
}

/* Je k-ty pozadavek presne tenhle? */
static bool req_is(int k, const uint8_t *want, uint8_t n)
{
    const sent_frame_t *f = req_at(k);

    if (f == NULL || f->data[0] != n) {
        return false;
    }
    return memcmp(&f->data[1], want, n) == 0;
}

/* ------------------------------------------------------------------
 * Jednotka
 * ------------------------------------------------------------------ */
static isotp_link_t g_link;
static uds_client_t g_uds;

static void setup(void)
{
    isotp_cfg_t cfg;

    g_ntx = 0;
    g_send_ok = true;
    memset(g_tx, 0, sizeof(g_tx));

    isotp_cfg_default(&cfg);
    cfg.tx_id = ECU_TX;
    cfg.rx_id = ECU_RX;
    cfg.send = fake_send;
    isotp_init(&g_link, &cfg);
    uds_init(&g_uds, &g_link);
}

/* Odpoved jednotky vcetne ISO-TP ramcovani. Delsi nez sedm bajtu jde
 * jako first frame a consecutive frames; nas flow control si klient
 * posle sam uvnitr uds_on_frame(). */
static void ecu_send(uint32_t now, const uint8_t *msg, uint16_t len)
{
    uint8_t d[8];
    uint16_t sent;
    uint8_t sn = 1u;

    if (len <= 7u) {
        memset(d, 0, sizeof(d));
        d[0] = (uint8_t)len;
        memcpy(&d[1], msg, len);
        uds_on_frame(&g_uds, ECU_RX, d, 8u, now);
        return;
    }

    memset(d, 0, sizeof(d));
    d[0] = (uint8_t)(0x10u | ((len >> 8) & 0x0Fu));
    d[1] = (uint8_t)(len & 0xFFu);
    memcpy(&d[2], msg, 6u);
    uds_on_frame(&g_uds, ECU_RX, d, 8u, now);

    sent = 6u;
    while (sent < len) {
        uint16_t left = (uint16_t)(len - sent);
        uint8_t  n = (uint8_t)((left > 7u) ? 7u : left);

        memset(d, 0, sizeof(d));
        d[0] = (uint8_t)(0x20u | (sn & 0x0Fu));
        memcpy(&d[1], &msg[sent], n);
        uds_on_frame(&g_uds, ECU_RX, d, 8u, now);
        sent = (uint16_t)(sent + n);
        sn = (uint8_t)((sn + 1u) & 0x0Fu);
    }
}

/* Odpoved a hned i jeden pruchod hlavni smyckou. */
static void answer(uint32_t now, const uint8_t *msg, uint16_t len)
{
    ecu_send(now, msg, len);
    uds_poll(&g_uds, now);
}

/* ------------------------------------------------------------------
 * 1. Session a jeji udrzeni pres 3E 00
 * ------------------------------------------------------------------ */
static void test_session_keepalive(void)
{
    static const uint8_t req_sess[] = { 0x10, 0x03 };
    static const uint8_t rsp_sess[] = { 0x50, 0x03, 0x00, 0x32, 0x01, 0xF4 };
    static const uint8_t req_tp[]   = { 0x3E, 0x00 };
    static const uint8_t rsp_tp[]   = { 0x7E, 0x00 };

    /* Dve vteriny jsou rozhodnuti popsane v uds.h (S3/2 se zalohou),
     * ne volny parametr - proto to cislo stoji i tady. */
    CHECK(UDS_TESTER_MS == 2000u);
    CHECK(UDS_S3_MS == 5000u);
    CHECK(UDS_DIDS_PER_REQ == 3u);

    setup();

    CHECK(uds_session(&g_uds, UDS_SESSION_EXTENDED, 0u));
    CHECK(req_count() == 1);
    CHECK(req_is(0, req_sess, 2u));
    CHECK(uds_state(&g_uds) == UDS_BUSY);
    CHECK(!uds_session_ok(&g_uds));

    answer(5u, rsp_sess, sizeof(rsp_sess));
    CHECK(uds_state(&g_uds) == UDS_DONE);
    CHECK(uds_session_ok(&g_uds));

    /* Tesne pred uzaverkou se jeste nic neposila. */
    uds_poll(&g_uds, 1999u);
    CHECK(req_count() == 1);

    /* UDS_TESTER_MS od POSLEDNIHO pozadavku, ne od odpovedi. */
    uds_poll(&g_uds, 2000u);
    CHECK(req_count() == 2);
    CHECK(req_is(1, req_tp, 2u));
    CHECK(uds_busy(&g_uds));
    /* Verejny stav se interni udrzbou hnout nesmi. */
    CHECK(uds_state(&g_uds) == UDS_DONE);

    /* Dokud 3E 00 lita, uzivatelsky pozadavek pocka. */
    CHECK(!uds_read_did(&g_uds, 0x2002u, 2u, 2001u));
    CHECK(uds_error(&g_uds) == UDS_ERR_BUSY);

    answer(2005u, rsp_tp, sizeof(rsp_tp));
    CHECK(!uds_busy(&g_uds));
    CHECK(uds_state(&g_uds) == UDS_DONE);
    CHECK(uds_session_ok(&g_uds));

    /* A zase za dve vteriny, porad dokola. */
    uds_poll(&g_uds, 3999u);
    CHECK(req_count() == 2);
    uds_poll(&g_uds, 4000u);
    CHECK(req_count() == 3);
    CHECK(req_is(2, req_tp, 2u));

    /* Default session nevyprsi, takze se neudrzuje nicim. */
    setup();
    CHECK(uds_session(&g_uds, UDS_SESSION_DEFAULT, 0u));
    answer(5u, (const uint8_t[]){ 0x50, 0x01 }, 2u);
    CHECK(uds_session_ok(&g_uds));
    uds_poll(&g_uds, 4000u);
    CHECK(req_count() == 1);

    /* Udrzba nesmi prepsat ani VYSLEDEK, ani CHYBU, na kterou se
     * uzivatel jeste nepodival. Jednotka odmitla 19 02, pak proleti
     * tester present - a odmitnuti musi zustat videt. */
    setup();
    CHECK(uds_session(&g_uds, UDS_SESSION_EXTENDED, 0u));
    answer(5u, rsp_sess, sizeof(rsp_sess));
    CHECK(uds_read_dtcs(&g_uds, 0xFFu, 10u));
    answer(20u, (const uint8_t[]){ 0x7F, 0x19, 0x22 }, 3u);
    CHECK(uds_state(&g_uds) == UDS_FAILED);
    CHECK(uds_error(&g_uds) == UDS_ERR_NRC);

    uds_poll(&g_uds, 2010u);
    CHECK(req_count() == 3);
    CHECK(req_is(2, req_tp, 2u));
    answer(2015u, rsp_tp, sizeof(rsp_tp));
    CHECK(!uds_busy(&g_uds));
    CHECK(uds_state(&g_uds) == UDS_FAILED);
    CHECK(uds_error(&g_uds) == UDS_ERR_NRC);
    CHECK(uds_nrc(&g_uds) == 0x22u);
}

/* ------------------------------------------------------------------
 * 2. Ticho delsi nez S3: session je pryc, dalsi dotaz ji obnovi
 * ------------------------------------------------------------------ */
static void test_session_s3_expiry(void)
{
    static const uint8_t rsp_sess[] = { 0x50, 0x03 };
    static const uint8_t req_sess[] = { 0x10, 0x03 };
    static const uint8_t req_did[]  = { 0x22, 0x20, 0x02 };

    setup();
    CHECK(uds_session(&g_uds, UDS_SESSION_EXTENDED, 0u));
    answer(5u, rsp_sess, sizeof(rsp_sess));
    CHECK(uds_session_ok(&g_uds));

    /* Hlavni smycka nas tak dlouho nepustila ke slovu, ze uz je po
     * session. Nic se neposila, jen se prizna pravda. */
    uds_poll(&g_uds, 5000u);
    CHECK(!uds_session_ok(&g_uds));
    CHECK(g_uds.n_session_lost == 1u);
    CHECK(req_count() == 1);

    /* Dalsi cteni si session obnovi samo, jeste pred dotazem. */
    CHECK(uds_read_did(&g_uds, 0x2002u, 2u, 5100u));
    CHECK(req_count() == 2);
    CHECK(req_is(1, req_sess, 2u));

    answer(5105u, rsp_sess, sizeof(rsp_sess));
    CHECK(uds_session_ok(&g_uds));
    CHECK(req_count() == 3);
    CHECK(req_is(2, req_did, 3u));
    CHECK(uds_state(&g_uds) == UDS_BUSY);

    answer(5110u, (const uint8_t[]){ 0x62, 0x20, 0x02, 0x0B, 0xB8 }, 5u);
    CHECK(uds_state(&g_uds) == UDS_DONE);
}

/* ------------------------------------------------------------------
 * 3. Davkovani vic DIDu a rozebrani odpovedi
 * ------------------------------------------------------------------ */
static void test_batch_split_and_parse(void)
{
    static const uds_did_req_t want[] = {
        { 0x2002u, 2u },   /* otacky */
        { 0x2001u, 2u },   /* teplota chladici kapaliny */
        { 0x2006u, 3u },   /* stav tachometru */
        { 0x2005u, 2u },   /* napeti */
        { 0x2009u, 2u },   /* tlak vzduchu */
        { 0x241Eu, 1u },   /* rezim plneni */
    };
    static const uint8_t req1[] = { 0x22, 0x20, 0x02, 0x20, 0x01, 0x20, 0x06 };
    static const uint8_t req2[] = { 0x22, 0x20, 0x05, 0x20, 0x09, 0x24, 0x1E };
    static const uint8_t rsp1[] = {
        0x62, 0x20, 0x02, 0x0B, 0xB8, 0x20, 0x01, 0x0F, 0x00,
        0x20, 0x06, 0x01, 0xE2, 0x40
    };
    static const uint8_t rsp2[] = {
        0x62, 0x20, 0x05, 0x04, 0xF8, 0x20, 0x09, 0x03, 0xE8, 0x24, 0x1E, 0x00
    };
    const uds_did_res_t *r;
    uint32_t v;

    setup();
    CHECK(uds_read_dids(&g_uds, want, 6u, 0u));

    /* Tri DIDy na dotaz - vic MD1CS006 neprijme. */
    CHECK(req_count() == 1);
    CHECK(req_is(0, req1, 7u));

    answer(10u, rsp1, sizeof(rsp1));
    /* Druha davka odchazi hned po rozebrani prvni odpovedi. */
    CHECK(req_count() == 2);
    CHECK(req_is(1, req2, 7u));
    CHECK(uds_state(&g_uds) == UDS_BUSY);

    answer(20u, rsp2, sizeof(rsp2));
    CHECK(uds_state(&g_uds) == UDS_DONE);
    CHECK(req_count() == 2);
    CHECK(uds_did_count(&g_uds) == 6u);

    r = uds_did_find(&g_uds, 0x2002u);
    CHECK(r != NULL && r->status == UDS_DID_OK && r->len == 2u);
    CHECK(uds_did_u32(&g_uds, 0x2002u, &v) && v == 0x0BB8u);
    CHECK(uds_did_u32(&g_uds, 0x2001u, &v) && v == 0x0F00u);
    /* Tribajtova hodnota uprostred odpovedi: kdyby se delky posunuly,
     * sedet nebude ani tahle, ani nic za ni. */
    CHECK(uds_did_u32(&g_uds, 0x2006u, &v) && v == 0x01E240u);
    CHECK(uds_did_u32(&g_uds, 0x2005u, &v) && v == 0x04F8u);
    CHECK(uds_did_u32(&g_uds, 0x2009u, &v) && v == 0x03E8u);
    CHECK(uds_did_u32(&g_uds, 0x241Eu, &v) && v == 0x00u);

    r = uds_did_find(&g_uds, 0x241Eu);
    CHECK(r != NULL && r->len == 1u);
    r = uds_did_at(&g_uds, 2u);
    CHECK(r != NULL && r->did == 0x2006u);   /* poradi odpovida pozadavku */
    CHECK(uds_did_at(&g_uds, 6u) == NULL);

    /* Na co se neptalo, to se netvari jako precteny udaj. */
    CHECK(uds_did_find(&g_uds, 0x2412u) == NULL);
    CHECK(!uds_did_u32(&g_uds, 0x2412u, &v));
}

/* ------------------------------------------------------------------
 * 4. DID, ktery jednotka odmitne uprostred davky
 *
 * Jednotka odmitne CELY dotaz a nerekne, ktery z DIDu za to muze.
 * Klient se proto na tu davku zepta po jednom a zbytek seznamu pak zase
 * davkuje.
 * ------------------------------------------------------------------ */
static void test_refused_did_in_batch(void)
{
    static const uds_did_req_t want[] = {
        { 0x2002u, 2u },
        { 0x247Bu, 2u },   /* na teto verzi softwaru neexistuje */
        { 0x2001u, 2u },
        { 0x2005u, 2u },
        { 0x2009u, 2u },
        { 0x241Eu, 1u },
    };
    static const uint8_t req_all[]  = { 0x22, 0x20, 0x02, 0x24, 0x7B, 0x20, 0x01 };
    static const uint8_t req_one1[] = { 0x22, 0x20, 0x02 };
    static const uint8_t req_one2[] = { 0x22, 0x24, 0x7B };
    static const uint8_t req_one3[] = { 0x22, 0x20, 0x01 };
    static const uint8_t req_rest[] = { 0x22, 0x20, 0x05, 0x20, 0x09, 0x24, 0x1E };
    static const uint8_t nrc31[]    = { 0x7F, 0x22, 0x31 };
    const uds_did_res_t *r;

    setup();
    CHECK(uds_read_dids(&g_uds, want, 6u, 0u));
    CHECK(req_is(0, req_all, 7u));

    answer(10u, nrc31, sizeof(nrc31));
    /* Rozpad davky na jednotlivce, cteni pokracuje. */
    CHECK(uds_state(&g_uds) == UDS_BUSY);
    CHECK(req_count() == 2);
    CHECK(req_is(1, req_one1, 3u));

    answer(20u, (const uint8_t[]){ 0x62, 0x20, 0x02, 0x0B, 0xB8 }, 5u);
    CHECK(req_count() == 3);
    CHECK(req_is(2, req_one2, 3u));

    answer(30u, nrc31, sizeof(nrc31));
    CHECK(req_count() == 4);
    CHECK(req_is(3, req_one3, 3u));

    answer(40u, (const uint8_t[]){ 0x62, 0x20, 0x01, 0x0F, 0x00 }, 5u);
    /* Davka dojela, zbytek seznamu se zase posila po trech. */
    CHECK(req_count() == 5);
    CHECK(req_is(4, req_rest, 7u));

    answer(50u, (const uint8_t[]){ 0x62, 0x20, 0x05, 0x04, 0xF8,
                                   0x20, 0x09, 0x03, 0xE8,
                                   0x24, 0x1E, 0x02 }, 12u);

    /* Odmitnuty DID NENI chyba cteni - ostatni hodnoty plati. */
    CHECK(uds_state(&g_uds) == UDS_DONE);
    CHECK(uds_error(&g_uds) == UDS_OK);
    CHECK(req_count() == 5);

    r = uds_did_find(&g_uds, 0x247Bu);
    CHECK(r != NULL && r->status == UDS_DID_REFUSED);
    CHECK(r != NULL && r->nrc == 0x31u);
    CHECK(uds_did_bytes(&g_uds, r) == NULL);   /* zadna vymyslena data */

    r = uds_did_find(&g_uds, 0x2002u);
    CHECK(r != NULL && r->status == UDS_DID_OK);
    r = uds_did_find(&g_uds, 0x241Eu);
    CHECK(r != NULL && r->status == UDS_DID_OK && r->len == 1u);
}

/* ------------------------------------------------------------------
 * 5. Zaporne odpovedi - kazdy kod jako konkretni duvod
 * ------------------------------------------------------------------ */
static void nrc_case(uint8_t nrc)
{
    uint8_t rsp[3];
    const uds_did_res_t *r;

    rsp[0] = 0x7F;
    rsp[1] = 0x22;
    rsp[2] = nrc;

    setup();
    CHECK(uds_read_did(&g_uds, 0x2002u, 2u, 0u));
    answer(10u, rsp, 3u);

    CHECK(uds_state(&g_uds) == UDS_DONE);     /* jen tenhle DID je mimo */
    CHECK(uds_nrc(&g_uds) == nrc);
    r = uds_did_find(&g_uds, 0x2002u);
    CHECK(r != NULL && r->status == UDS_DID_REFUSED);
    CHECK(r != NULL && r->nrc == nrc);
    CHECK(uds_did_bytes(&g_uds, r) == NULL);
}

static void test_negative_codes(void)
{
    static const uint8_t nrc22[] = { 0x7F, 0x19, 0x22 };
    const char *n31, *n22, *n33, *n13;

    /* Ctyri kody, ktere na X82 opravdu chodi. */
    nrc_case(0x31u);   /* DID mimo rozsah */
    nrc_case(0x22u);   /* podminky nesplneny */
    nrc_case(0x33u);   /* pristup odepren */
    nrc_case(0x13u);   /* spatna delka */

    /* Kazdy ma vlastni ctitelny duvod, zadny nespadne na "nrc-?". */
    n31 = uds_nrc_name(0x31u);
    n22 = uds_nrc_name(0x22u);
    n33 = uds_nrc_name(0x33u);
    n13 = uds_nrc_name(0x13u);
    CHECK(strcmp(n31, "request-out-of-range") == 0);
    CHECK(strcmp(n22, "conditions-not-correct") == 0);
    CHECK(strcmp(n33, "security-access-denied") == 0);
    CHECK(strcmp(n13, "wrong-length") == 0);
    CHECK(strcmp(uds_nrc_name(0x5Au), "nrc-?") == 0);
    CHECK(strcmp(n31, n22) != 0 && strcmp(n22, n33) != 0 && strcmp(n33, n13) != 0);

    /* U jednorazove sluzby (19 xx) je zaporna odpoved chybou operace a
     * duvod se da precist stejne. */
    setup();
    CHECK(uds_read_dtcs(&g_uds, 0xFFu, 0u));
    answer(10u, nrc22, sizeof(nrc22));
    CHECK(uds_state(&g_uds) == UDS_FAILED);
    CHECK(uds_error(&g_uds) == UDS_ERR_NRC);
    CHECK(uds_nrc(&g_uds) == 0x22u);
    CHECK(strcmp(uds_err_name(uds_error(&g_uds)), "negative-response") == 0);

    /* 0x13 u davky o trech DIDech znamena totez co 0x31: zepta se po
     * jednom, misto aby se cele cteni vzdalo. */
    {
        static const uds_did_req_t want[] = {
            { 0x2002u, 2u }, { 0x2001u, 2u }, { 0x2006u, 3u }
        };
        static const uint8_t nrc13[] = { 0x7F, 0x22, 0x13 };
        static const uint8_t one[] = { 0x22, 0x20, 0x02 };

        setup();
        CHECK(uds_read_dids(&g_uds, want, 3u, 0u));
        answer(10u, nrc13, sizeof(nrc13));
        CHECK(req_count() == 2);
        CHECK(req_is(1, one, 3u));
    }
}

/* ------------------------------------------------------------------
 * 6. Rozebrani 19 02 se stavovymi bajty
 * ------------------------------------------------------------------ */
static void test_dtc_list(void)
{
    static const uint8_t req[] = { 0x19, 0x02, 0xFF };
    static const uint8_t rsp[] = {
        0x59, 0x02, 0xFF,
        0xC0, 0x12, 0x34, 0x2F,   /* hori ted */
        0x00, 0x56, 0x78, 0x50,   /* klidovy stav, neni to zavada */
        0x90, 0x12, 0x34, 0xE5    /* potvrzena, rozsvicena kontrolka */
    };
    uds_dtc_t d;
    uint16_t i, shown = 0u;

    setup();
    CHECK(uds_read_dtcs(&g_uds, 0xFFu, 0u));
    CHECK(req_is(0, req, 3u));

    answer(10u, rsp, sizeof(rsp));
    CHECK(uds_state(&g_uds) == UDS_DONE);
    CHECK(uds_dtc_count(&g_uds) == 3u);

    CHECK(uds_dtc_at(&g_uds, 0u, &d));
    CHECK(d.dtc == 0xC01234u);
    CHECK(d.status == 0x2Fu);
    CHECK((d.status & UDS_DTC_TEST_FAILED) != 0u);
    CHECK((d.status & UDS_DTC_CONFIRMED) != 0u);

    CHECK(uds_dtc_at(&g_uds, 1u, &d));
    CHECK(d.dtc == 0x005678u);
    CHECK(d.status == 0x50u);
    CHECK((d.status & UDS_DTC_SHOW) == 0u);   /* nikdy nebezelo od mazani */

    CHECK(uds_dtc_at(&g_uds, 2u, &d));
    CHECK(d.dtc == 0x901234u);
    CHECK(d.status == 0xE5u);
    CHECK((d.status & UDS_DTC_WARNING) != 0u);

    CHECK(!uds_dtc_at(&g_uds, 3u, &d));

    for (i = 0u; i < uds_dtc_count(&g_uds); i++) {
        if (uds_dtc_at(&g_uds, i, &d) && (d.status & UDS_DTC_SHOW) != 0u) {
            shown++;
        }
    }
    CHECK(shown == 2u);

    /* Syrova odpoved je k dispozici i pro 19 04 / 19 06. */
    CHECK(uds_rsp_len(&g_uds) == sizeof(rsp));
    CHECK(uds_rsp(&g_uds)[0] == 0x59u);

    /* Z jine odpovedi se chyby netvari. */
    setup();
    CHECK(uds_read_did(&g_uds, 0x2002u, 2u, 0u));
    answer(10u, (const uint8_t[]){ 0x62, 0x20, 0x02, 0x0B, 0xB8 }, 5u);
    CHECK(uds_dtc_count(&g_uds) == 0u);
}

/* ------------------------------------------------------------------
 * 7. Freeze frame a rozsirene zaznamy
 * ------------------------------------------------------------------ */
static void test_dtc_records(void)
{
    static const uint8_t req_ff[] = { 0x19, 0x04, 0xC0, 0x12, 0x34, 0xFF };
    static const uint8_t req_ex[] = { 0x19, 0x06, 0xC0, 0x12, 0x34, 0xFF };
    static const uint8_t rsp_ex[] = {
        0x59, 0x06, 0xC0, 0x12, 0x34, 0x2F,
        0x86, 0x01, 0x00, 0x2A      /* 0x86 = ujeto se zavadou, 42 km */
    };

    setup();
    CHECK(uds_read_freeze(&g_uds, 0xC01234u, 0xFFu, 0u));
    CHECK(req_is(0, req_ff, 6u));
    answer(10u, (const uint8_t[]){ 0x59, 0x04, 0xC0, 0x12, 0x34, 0x2F, 0x01 }, 7u);
    CHECK(uds_state(&g_uds) == UDS_DONE);
    CHECK(uds_rsp_len(&g_uds) == 7u);

    setup();
    CHECK(uds_read_extended(&g_uds, 0xC01234u, 0xFFu, 0u));
    CHECK(req_is(0, req_ex, 6u));
    answer(10u, rsp_ex, sizeof(rsp_ex));
    CHECK(uds_state(&g_uds) == UDS_DONE);
    CHECK(uds_rsp_len(&g_uds) == sizeof(rsp_ex));
    CHECK(uds_rsp(&g_uds)[6] == 0x86u);
}

/* ------------------------------------------------------------------
 * 8. Session vyprsi uprostred cteni
 *
 * Jednotka na dotaz odpovi 7F 22 7F (uz nejsme v te session). Klient ji
 * obnovi a dotaz zopakuje - navenek je to jen zdrzeni.
 * ------------------------------------------------------------------ */
static void test_session_lost_mid_read(void)
{
    static const uds_did_req_t want[] = {
        { 0x2002u, 2u }, { 0x2001u, 2u }, { 0x2006u, 3u },
        { 0x2005u, 2u }, { 0x2009u, 2u }, { 0x241Eu, 1u },
    };
    static const uint8_t req2[]   = { 0x22, 0x20, 0x05, 0x20, 0x09, 0x24, 0x1E };
    static const uint8_t sess[]   = { 0x10, 0x03 };
    static const uint8_t nrc7f[]  = { 0x7F, 0x22, 0x7F };
    static const uint8_t rsp1[]   = {
        0x62, 0x20, 0x02, 0x0B, 0xB8, 0x20, 0x01, 0x0F, 0x00,
        0x20, 0x06, 0x01, 0xE2, 0x40
    };
    static const uint8_t rsp2[]   = {
        0x62, 0x20, 0x05, 0x04, 0xF8, 0x20, 0x09, 0x03, 0xE8, 0x24, 0x1E, 0x00
    };
    const uds_did_res_t *r;

    setup();
    CHECK(uds_session(&g_uds, UDS_SESSION_EXTENDED, 0u));
    answer(5u, (const uint8_t[]){ 0x50, 0x03 }, 2u);
    CHECK(uds_session_ok(&g_uds));

    CHECK(uds_read_dids(&g_uds, want, 6u, 10u));
    answer(20u, rsp1, sizeof(rsp1));
    CHECK(req_count() == 3);          /* 10 03, prvni davka, druha davka */
    CHECK(req_is(2, req2, 7u));

    /* A ted jednotka session zahodila. */
    answer(30u, nrc7f, sizeof(nrc7f));
    CHECK(!uds_session_ok(&g_uds));
    CHECK(g_uds.n_session_lost == 1u);
    CHECK(uds_state(&g_uds) == UDS_BUSY);      /* cteni jeste nekonci */
    CHECK(req_count() == 4);
    CHECK(req_is(3, sess, 2u));                /* nejdriv zpatky do session */

    answer(40u, (const uint8_t[]){ 0x50, 0x03 }, 2u);
    CHECK(uds_session_ok(&g_uds));
    CHECK(req_count() == 5);
    CHECK(req_is(4, req2, 7u));                /* a pak tytez DIDy znovu */

    answer(50u, rsp2, sizeof(rsp2));
    CHECK(uds_state(&g_uds) == UDS_DONE);
    CHECK(uds_did_count(&g_uds) == 6u);
    r = uds_did_find(&g_uds, 0x2005u);
    CHECK(r != NULL && r->status == UDS_DID_OK);
    /* Hodnoty z prvni davky prezily obnovu session. */
    r = uds_did_find(&g_uds, 0x2002u);
    CHECK(r != NULL && r->status == UDS_DID_OK);

    /* Podruhe uz se klient nezacykli. */
    setup();
    CHECK(uds_session(&g_uds, UDS_SESSION_EXTENDED, 0u));
    answer(5u, (const uint8_t[]){ 0x50, 0x03 }, 2u);
    CHECK(uds_read_did(&g_uds, 0x2002u, 2u, 10u));
    answer(20u, nrc7f, sizeof(nrc7f));
    CHECK(req_count() == 3);                   /* 10 03 znovu */
    answer(30u, (const uint8_t[]){ 0x50, 0x03 }, 2u);
    CHECK(req_count() == 4);
    answer(40u, nrc7f, sizeof(nrc7f));
    CHECK(uds_state(&g_uds) == UDS_FAILED);
    CHECK(uds_error(&g_uds) == UDS_ERR_SESSION);
    CHECK(req_count() == 4);                   /* a uz se nic neposila */
}

/* ------------------------------------------------------------------
 * 9. Chyby prenosu, listen-only a odpoved na jinou sluzbu
 * ------------------------------------------------------------------ */
static void test_transport_errors(void)
{
    const uds_did_res_t *r;

    /* Listen-only: radic neodesle nic a vrstva to musi rict rovnou. */
    setup();
    g_send_ok = false;
    CHECK(!uds_read_did(&g_uds, 0x2002u, 2u, 0u));
    CHECK(uds_state(&g_uds) == UDS_FAILED);
    CHECK(uds_error(&g_uds) == UDS_ERR_SEND);
    CHECK(g_ntx == 0);

    /* Jednotka mlci: P2 vyprsi a session uz neplati. */
    setup();
    CHECK(uds_session(&g_uds, UDS_SESSION_EXTENDED, 0u));
    answer(5u, (const uint8_t[]){ 0x50, 0x03 }, 2u);
    CHECK(uds_read_did(&g_uds, 0x2002u, 2u, 10u));
    uds_poll(&g_uds, 500u);
    CHECK(uds_state(&g_uds) == UDS_BUSY);
    uds_poll(&g_uds, 1200u);
    CHECK(uds_state(&g_uds) == UDS_FAILED);
    CHECK(uds_error(&g_uds) == UDS_ERR_TIMEOUT);
    CHECK(!uds_session_ok(&g_uds));

    /* Odpoved na jinou sluzbu nesmi projit jako vysledek. */
    setup();
    CHECK(uds_read_did(&g_uds, 0x2002u, 2u, 0u));
    answer(10u, (const uint8_t[]){ 0x59, 0x02, 0xFF, 0x00 }, 4u);
    CHECK(uds_state(&g_uds) == UDS_FAILED);
    CHECK(uds_error(&g_uds) == UDS_ERR_MISMATCH);

    /* Jednotka vratila DID, na ktery se nikdo neptal: dal uz nejde
     * poznat, kde ktera hodnota konci. */
    setup();
    CHECK(uds_read_did(&g_uds, 0x2002u, 2u, 0u));
    answer(10u, (const uint8_t[]){ 0x62, 0x24, 0x12, 0x0B, 0xB8 }, 5u);
    CHECK(uds_state(&g_uds) == UDS_FAILED);
    CHECK(uds_error(&g_uds) == UDS_ERR_PARSE);

    /* Useknuta hodnota se nedopocitava. */
    setup();
    CHECK(uds_read_did(&g_uds, 0x2006u, 3u, 0u));
    answer(10u, (const uint8_t[]){ 0x62, 0x20, 0x06, 0x01, 0xE2 }, 5u);
    CHECK(uds_state(&g_uds) == UDS_FAILED);
    CHECK(uds_error(&g_uds) == UDS_ERR_PARSE);

    /* DID, ktery v odpovedi vubec nebyl. */
    {
        static const uds_did_req_t want[] = { { 0x2002u, 2u }, { 0x2001u, 2u } };
        uint32_t v;

        setup();
        CHECK(uds_read_dids(&g_uds, want, 2u, 0u));
        answer(10u, (const uint8_t[]){ 0x62, 0x20, 0x02, 0x0B, 0xB8 }, 5u);
        CHECK(uds_state(&g_uds) == UDS_DONE);
        r = uds_did_find(&g_uds, 0x2001u);
        CHECK(r != NULL && r->status == UDS_DID_MISSING);
        CHECK(!uds_did_u32(&g_uds, 0x2001u, &v));
        r = uds_did_find(&g_uds, 0x2002u);
        CHECK(r != NULL && r->status == UDS_DID_OK);
    }

    /* Nesmyslny pozadavek se neodesila vubec. */
    setup();
    CHECK(!uds_read_dids(&g_uds, NULL, 3u, 0u));
    CHECK(uds_error(&g_uds) == UDS_ERR_BADARG);
    CHECK(!uds_read_did(&g_uds, 0x2002u, (uint8_t)(UDS_DID_LEN_MAX + 1u), 0u));
    CHECK(uds_error(&g_uds) == UDS_ERR_BADARG);
    CHECK(g_ntx == 0);
}

/* ------------------------------------------------------------------
 * 10. DID s neznamou delkou se vzdy pta sam
 * ------------------------------------------------------------------ */
static void test_unknown_length(void)
{
    static const uds_did_req_t want[] = { { 0x2002u, 2u }, { 0x2110u, 0u } };
    static const uint8_t one1[] = { 0x22, 0x20, 0x02 };
    static const uint8_t one2[] = { 0x22, 0x21, 0x10 };
    const uds_did_res_t *r;

    setup();
    CHECK(uds_read_dids(&g_uds, want, 2u, 0u));
    CHECK(req_count() == 1);
    CHECK(req_is(0, one1, 3u));      /* davka se o neznamou delku zastavi */

    answer(10u, (const uint8_t[]){ 0x62, 0x20, 0x02, 0x0B, 0xB8 }, 5u);
    CHECK(req_count() == 2);
    CHECK(req_is(1, one2, 3u));

    answer(20u, (const uint8_t[]){ 0x62, 0x21, 0x10, 0x01, 0x02, 0x03,
                                   0x04, 0x05 }, 8u);
    CHECK(uds_state(&g_uds) == UDS_DONE);
    r = uds_did_find(&g_uds, 0x2110u);
    CHECK(r != NULL && r->status == UDS_DID_OK && r->len == 5u);
    CHECK(r != NULL && uds_did_bytes(&g_uds, r)[4] == 0x05u);

    /* A stejne tak, kdyz je neznama delka na zacatku seznamu: za
     * hodnotou nezname delky uz nejde poznat, kde zacina dalsi DID,
     * takze se nesmi pribalit ani ten, ktery ma delku znamou. */
    {
        static const uds_did_req_t rev[] = { { 0x2110u, 0u }, { 0x2002u, 2u } };

        setup();
        CHECK(uds_read_dids(&g_uds, rev, 2u, 0u));
        CHECK(req_count() == 1);
        CHECK(req_is(0, one2, 3u));
        answer(10u, (const uint8_t[]){ 0x62, 0x21, 0x10, 0x01, 0x02 }, 5u);
        CHECK(req_count() == 2);
        CHECK(req_is(1, one1, 3u));
        answer(20u, (const uint8_t[]){ 0x62, 0x20, 0x02, 0x0B, 0xB8 }, 5u);
        CHECK(uds_state(&g_uds) == UDS_DONE);
        r = uds_did_find(&g_uds, 0x2110u);
        CHECK(r != NULL && r->status == UDS_DID_OK && r->len == 2u);
        r = uds_did_find(&g_uds, 0x2002u);
        CHECK(r != NULL && r->status == UDS_DID_OK && r->len == 2u);
    }
}

/* ------------------------------------------------------------------
 * 11. Stary protokol Renault
 * ------------------------------------------------------------------ */
static isotp_link_t g_blink;
static legacy_t     g_leg;

static void legacy_setup(void)
{
    isotp_cfg_t cfg;

    g_ntx = 0;
    g_send_ok = true;
    memset(g_tx, 0, sizeof(g_tx));

    isotp_cfg_default(&cfg);
    cfg.tx_id = BCM_TX;
    cfg.rx_id = BCM_RX;
    cfg.send = fake_send;
    isotp_init(&g_blink, &cfg);
    legacy_init(&g_leg, &g_blink);
}

static void bcm_answer(uint32_t now, const uint8_t *msg, uint16_t len)
{
    uint8_t d[8];
    uint16_t sent;
    uint8_t sn = 1u;

    if (len <= 7u) {
        memset(d, 0, sizeof(d));
        d[0] = (uint8_t)len;
        memcpy(&d[1], msg, len);
        legacy_on_frame(&g_leg, BCM_RX, d, 8u, now);
    } else {
        memset(d, 0, sizeof(d));
        d[0] = (uint8_t)(0x10u | ((len >> 8) & 0x0Fu));
        d[1] = (uint8_t)(len & 0xFFu);
        memcpy(&d[2], msg, 6u);
        legacy_on_frame(&g_leg, BCM_RX, d, 8u, now);
        sent = 6u;
        while (sent < len) {
            uint16_t left = (uint16_t)(len - sent);
            uint8_t  n = (uint8_t)((left > 7u) ? 7u : left);

            memset(d, 0, sizeof(d));
            d[0] = (uint8_t)(0x20u | (sn & 0x0Fu));
            memcpy(&d[1], &msg[sent], n);
            legacy_on_frame(&g_leg, BCM_RX, d, 8u, now);
            sent = (uint16_t)(sent + n);
            sn = (uint8_t)((sn + 1u) & 0x0Fu);
        }
    }
    legacy_poll(&g_leg, now);
}

static void test_legacy(void)
{
    static const uint8_t req_sess[] = { 0x10, 0xC0 };
    static const uint8_t req_read[] = { 0x21, 0x16 };
    static const uint8_t req_tp[]   = { 0x3E, 0x00 };
    uint8_t blk[1 + 1 + 36];
    const uint8_t *d;
    int i;

    /* 10 C0, ne 10 03: tyhle jednotky UDS neznaji. */
    legacy_setup();
    CHECK(legacy_session(&g_leg, 0u));
    CHECK(req_is(0, req_sess, 2u));
    CHECK(legacy_state(&g_leg) == LEGACY_BUSY);

    bcm_answer(5u, (const uint8_t[]){ 0x50, 0xC0 }, 2u);
    CHECK(legacy_state(&g_leg) == LEGACY_DONE);
    CHECK(legacy_session_ok(&g_leg));

    /* 21 xx, jednobajtovy identifikator, delka odpovedi se predem nevi. */
    blk[0] = 0x61;
    blk[1] = 0x16;
    for (i = 0; i < 36; i++) {
        blk[2 + i] = (uint8_t)(0xA0 + i);
    }
    CHECK(legacy_read(&g_leg, 0x16u, 10u));
    CHECK(req_is(1, req_read, 2u));
    bcm_answer(20u, blk, sizeof(blk));

    CHECK(legacy_state(&g_leg) == LEGACY_DONE);
    CHECK(legacy_block(&g_leg) == 0x16u);
    CHECK(legacy_len(&g_leg) == 36u);
    d = legacy_data(&g_leg);
    CHECK(d != NULL && d[0] == 0xA0u);
    CHECK(d != NULL && d[35] == (uint8_t)(0xA0 + 35));
    CHECK(legacy_rsp_len(&g_leg) == 38u);

    /* Odpoved na jiny blok je odpoved na jinou otazku. */
    legacy_setup();
    g_leg.session_ok = true;
    g_leg.keepalive = true;
    CHECK(legacy_read(&g_leg, 0x46u, 0u));
    bcm_answer(10u, (const uint8_t[]){ 0x61, 0x16, 0x01 }, 3u);
    CHECK(legacy_state(&g_leg) == LEGACY_FAILED);
    CHECK(legacy_error(&g_leg) == UDS_ERR_MISMATCH);

    /* Zaporna odpoved ma stejny tvar jako v UDS a stejne se cte. */
    legacy_setup();
    g_leg.session_ok = true;
    CHECK(legacy_read(&g_leg, 0x46u, 0u));
    bcm_answer(10u, (const uint8_t[]){ 0x7F, 0x21, 0x12 }, 3u);
    CHECK(legacy_state(&g_leg) == LEGACY_FAILED);
    CHECK(legacy_error(&g_leg) == UDS_ERR_NRC);
    CHECK(legacy_nrc(&g_leg) == 0x12u);
    CHECK(strcmp(uds_nrc_name(legacy_nrc(&g_leg)),
                 "subfunction-not-supported") == 0);

    /* Session se udrzuje stejne jako v UDS a po jejim ztracenim se
     * obnovi pred dalsim ctenim. */
    legacy_setup();
    CHECK(legacy_session(&g_leg, 0u));
    bcm_answer(5u, (const uint8_t[]){ 0x50, 0xC0 }, 2u);
    legacy_poll(&g_leg, 1999u);
    CHECK(req_count() == 1);
    legacy_poll(&g_leg, 2000u);
    CHECK(req_count() == 2);
    CHECK(req_is(1, req_tp, 2u));
    bcm_answer(2005u, (const uint8_t[]){ 0x7E, 0x00 }, 2u);
    CHECK(legacy_state(&g_leg) == LEGACY_DONE);   /* udrzba nehne stavem */

    legacy_poll(&g_leg, 8000u);
    CHECK(!legacy_session_ok(&g_leg));
    CHECK(legacy_read(&g_leg, 0x46u, 8100u));
    CHECK(req_is(2, req_sess, 2u));               /* nejdriv zpatky 10 C0 */
    bcm_answer(8105u, (const uint8_t[]){ 0x50, 0xC0 }, 2u);
    CHECK(req_is(3, (const uint8_t[]){ 0x21, 0x46 }, 2u));

    /* Listen-only. */
    legacy_setup();
    g_send_ok = false;
    CHECK(!legacy_session(&g_leg, 0u));
    CHECK(legacy_error(&g_leg) == UDS_ERR_SEND);
    CHECK(g_ntx == 0);
}

/* ------------------------------------------------------------------
 * 12. Detekce pripojeni k vozidlu
 * ------------------------------------------------------------------ */
static void test_link_detect(void)
{
    diag_link_t dl;
    uint32_t t;
    int i;

    /* Uroven 0: deska na stole. */
    diag_link_init(&dl);
    CHECK(diag_link_state(&dl) == DIAG_LINK_DOWN);
    CHECK(diag_link_poll(&dl, 0u) == DIAG_LINK_DOWN);
    CHECK(diag_link_quiet_ms(&dl, 1000u) == 0xFFFFFFFFu);

    /* Vlastni vysilani nic nedokazuje - a v listen-only zadne neni. */
    for (i = 0; i < 10; i++) {
        diag_link_on_frame(&dl, ECU_TX, true, (uint32_t)(i * 10));
    }
    CHECK(diag_link_state(&dl) == DIAG_LINK_DOWN);
    CHECK(dl.n_frames == 0u);

    /* Uroven 1: po sbernici chodi cizi ramce. Jeden ramec je nahoda,
     * ctyri uz ne. Pocty jsou tu schvalne cislem a ne makrem: ta ctyrka
     * je rozhodnuti popsane v diag/link.h, ne volny parametr. */
    CHECK(DIAG_BUS_MIN_FRAMES == 4u);
    for (i = 0; i < 3; i++) {
        diag_link_on_frame(&dl, 0x18Du, false, (uint32_t)(100 + i * 10));
        CHECK(diag_link_state(&dl) == DIAG_LINK_DOWN);
    }
    diag_link_on_frame(&dl, 0x18Du, false, 200u);
    CHECK(diag_link_state(&dl) == DIAG_LINK_BUS);
    CHECK(dl.n_frames == 4u);
    CHECK(diag_link_quiet_ms(&dl, 250u) == 50u);

    /* V listen-only se dal nez sem dostat nejde a je to v poradku. */
    CHECK(diag_link_poll(&dl, 300u) == DIAG_LINK_BUS);

    /* Konektor ven: po vterine ticha uroven padne. */
    CHECK(DIAG_BUS_IDLE_MS == 1000u);
    CHECK(diag_link_poll(&dl, 1199u) == DIAG_LINK_BUS);
    CHECK(diag_link_poll(&dl, 1200u) == DIAG_LINK_DOWN);

    /* A po tichu se ramce pocitaji od zacatku, jeden navrat neplati. */
    diag_link_on_frame(&dl, 0x18Du, false, 2000u);
    CHECK(diag_link_state(&dl) == DIAG_LINK_DOWN);
    for (i = 1; i < 4; i++) {
        diag_link_on_frame(&dl, 0x18Du, false, (uint32_t)(2000 + i * 10));
    }
    CHECK(diag_link_state(&dl) == DIAG_LINK_BUS);

    /* Uroven 2: jednotka odpovedela. */
    diag_link_on_reply(&dl, ECU_RX, 2100u);
    CHECK(diag_link_state(&dl) == DIAG_LINK_ECU);
    CHECK(dl.n_replies == 1u);
    CHECK(dl.last_rx_id == ECU_RX);

    /* Mezi dotazy muze byt pauza - sbernice uz muze byt "ticha", ale
     * dokud jednotka odpovidala nedavno, uroven drzi. */
    CHECK(DIAG_ECU_IDLE_MS == 10000u);
    CHECK(diag_link_poll(&dl, 12099u) == DIAG_LINK_ECU);
    CHECK(diag_link_poll(&dl, 12100u) == DIAG_LINK_DOWN);

    /* Tri dotazy bez odpovedi urovni ubere drive, nez dobehne cas. */
    CHECK(DIAG_ECU_MISS_MAX == 3u);
    diag_link_init(&dl);
    t = 0u;
    for (i = 0; i < 4; i++) {
        diag_link_on_frame(&dl, 0x18Du, false, t);
        t += 10u;
    }
    diag_link_on_reply(&dl, ECU_RX, t);
    CHECK(diag_link_state(&dl) == DIAG_LINK_ECU);
    for (i = 0; i < 2; i++) {
        diag_link_on_silence(&dl, t);
        CHECK(diag_link_state(&dl) == DIAG_LINK_ECU);
    }
    diag_link_on_silence(&dl, t);
    CHECK(diag_link_state(&dl) == DIAG_LINK_BUS);   /* sbernice porad zije */
    CHECK(dl.n_silence == 3u);

    /* Dalsi odpoved to zase zvedne. */
    diag_link_on_reply(&dl, ECU_RX, t);
    CHECK(diag_link_state(&dl) == DIAG_LINK_ECU);
    CHECK(dl.misses == 0u);

    /* Odpoved sama staci i bez odposlechu ramcu (filtr radice). */
    diag_link_init(&dl);
    diag_link_on_reply(&dl, ECU_RX, 500u);
    CHECK(diag_link_state(&dl) == DIAG_LINK_ECU);
    CHECK(diag_link_poll(&dl, 600u) == DIAG_LINK_ECU);

    CHECK(strcmp(diag_link_state_name(DIAG_LINK_DOWN), "down") == 0);
    CHECK(strcmp(diag_link_state_name(DIAG_LINK_BUS), "bus-alive") == 0);
    CHECK(strcmp(diag_link_state_name(DIAG_LINK_ECU), "ecu-answering") == 0);
}

/* ------------------------------------------------------------------
 * 13. Detekce zapojena na skutecny klient
 *
 * Totez, ale udalosti chodi z uds_client_t presne tak, jak to bude
 * delat hlavni smycka (recept je v diag/link.h).
 * ------------------------------------------------------------------ */
static void test_link_with_client(void)
{
    diag_link_t dl;
    uint32_t seen;
    int i;

    setup();
    diag_link_init(&dl);
    seen = uds_rsp_count(&g_uds);

    /* Listen-only: ramce chodi, poslat nejde nic. Nejvyssi dosazitelna
     * uroven je "sbernice zije". */
    g_send_ok = false;
    for (i = 0; i < 8; i++) {
        diag_link_on_frame(&dl, 0x18Du, false, (uint32_t)(i * 10));
    }
    CHECK(diag_link_poll(&dl, 80u) == DIAG_LINK_BUS);
    CHECK(!uds_read_did(&g_uds, 0x2002u, 2u, 80u));
    CHECK(uds_rsp_count(&g_uds) == seen);
    CHECK(diag_link_poll(&dl, 90u) == DIAG_LINK_BUS);

    /* Normalni rezim: jednotka odpovi a uroven vyskoci. */
    g_send_ok = true;
    CHECK(uds_read_did(&g_uds, 0x2002u, 2u, 100u));
    answer(110u, (const uint8_t[]){ 0x62, 0x20, 0x02, 0x0B, 0xB8 }, 5u);
    CHECK(uds_rsp_count(&g_uds) != seen);
    diag_link_on_reply(&dl, ECU_RX, 110u);
    CHECK(diag_link_poll(&dl, 110u) == DIAG_LINK_ECU);

    /* I zaporna odpoved je dukaz, ze tam nekdo je. */
    seen = uds_rsp_count(&g_uds);
    CHECK(uds_read_did(&g_uds, 0x247Bu, 2u, 200u));
    answer(210u, (const uint8_t[]){ 0x7F, 0x22, 0x31 }, 3u);
    CHECK(uds_rsp_count(&g_uds) == seen + 1u);
    diag_link_on_reply(&dl, ECU_RX, 210u);
    CHECK(diag_link_poll(&dl, 210u) == DIAG_LINK_ECU);

    /* Zapalovani ven: dotazy se ztraci a uroven klesa. */
    for (i = 0; i < 3; i++) {
        uint32_t t = (uint32_t)(300 + i * 2000);

        CHECK(uds_read_did(&g_uds, 0x2002u, 2u, t));
        uds_poll(&g_uds, t + 1200u);
        CHECK(uds_error(&g_uds) == UDS_ERR_TIMEOUT);
        diag_link_on_silence(&dl, t + 1200u);
    }
    CHECK(diag_link_poll(&dl, 6000u) == DIAG_LINK_DOWN);
}

/* ------------------------------------------------------------------
 * 14. Z teto vrstvy nikdy neodejde zapis
 *
 * Kontrola pres vsechny ramce, ktere za cely beh odesly: zadny nenese
 * sluzbu 3B ani 2E. Zapis patri do src/write/ za branu z docs/safety.md
 * - viz komentar v diag/legacy.h.
 * ------------------------------------------------------------------ */
static void test_no_write_service(void)
{
    int i;
    int writes = 0;
    int reads = 0;

    CHECK(g_nlog > 20);        /* at je co prohledavat */
    for (i = 0; i < g_nlog; i++) {
        uint8_t pci = (uint8_t)(g_log[i].data[0] >> 4);
        uint8_t sid;

        if (pci != 0x0u) {
            continue;          /* flow control, ne pozadavek */
        }
        sid = g_log[i].data[1];
        if (sid == 0x3Bu || sid == 0x2Eu || sid == 0x31u || sid == 0x27u ||
            sid == 0x34u || sid == 0x14u) {
            writes++;
        }
        if (sid == 0x22u || sid == 0x21u || sid == 0x19u) {
            reads++;
        }
    }
    CHECK(writes == 0);
    CHECK(reads > 0);
}

/* ------------------------------------------------------------------ */
typedef struct {
    const char *name;
    void (*fn)(void);
} test_t;

static const test_t g_tests[] = {
    { "session + tester present",   test_session_keepalive },
    { "session vyprsi tichem",      test_session_s3_expiry },
    { "davka DIDu a rozebrani",     test_batch_split_and_parse },
    { "odmitnuty DID v davce",      test_refused_did_in_batch },
    { "zaporne odpovedi",           test_negative_codes },
    { "19 02 se stavovymi bajty",   test_dtc_list },
    { "19 04 a 19 06",              test_dtc_records },
    { "session vyprsi pri cteni",   test_session_lost_mid_read },
    { "chyby prenosu",              test_transport_errors },
    { "DID s neznamou delkou",      test_unknown_length },
    { "stary protokol Renault",     test_legacy },
    { "detekce pripojeni",          test_link_detect },
    { "detekce nad klientem",       test_link_with_client },
    { "zadny zapis",                test_no_write_service },
};

int main(void)
{
    size_t i;
    int before;

    printf("diagnosticka vrstva - testy na hostiteli\n\n");
    for (i = 0; i < sizeof(g_tests) / sizeof(g_tests[0]); i++) {
        g_test = g_tests[i].name;
        before = g_failed;
        g_tests[i].fn();
        printf("  %-28s %s\n", g_tests[i].name,
               (g_failed == before) ? "ok" : "CHYBA");
    }

    printf("\n%d kontrol, %d chyb\n", g_checks, g_failed);
    return (g_failed == 0) ? 0 : 1;
}
