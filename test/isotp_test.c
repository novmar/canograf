/*
 * isotp_test.c - testy transportni vrstvy ISO-TP na hostiteli
 *
 * Zadna deska, zadne vozidlo, zadne hodiny. Misto radice TWAI je tu
 * funkce fake_send(), ktera ramce uklada do pole, a cas se do vrstvy
 * dosazuje rucne - diky tomu se da overit i to, co by se na stole
 * cekalo nekolik sekund (chybejici flow control, 7F xx 78).
 *
 * Preklad a spusteni:
 *
 *     cmake -S test -B test/build
 *     cmake --build test/build
 *     ./test/build/isotp_test
 *
 * Navratovy kod 0 = vse proslo.
 */
#include "isotp.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------
 * Nahrada sbernice
 * ------------------------------------------------------------------ */
#define TX_CAP 32

typedef struct {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  dlc;
} sent_frame_t;

static sent_frame_t g_tx[TX_CAP];
static int  g_ntx;
static bool g_send_ok;

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
    return true;
}

#define ECU_TX 0x7E0u   /* my -> jednotka */
#define ECU_RX 0x7E8u   /* jednotka -> my */

static isotp_link_t g_link;

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
}

/* Prijaty ramec z jednotky; zbytek do osmi bajtu doplni nulami, stejne
 * jako to dela skutecna jednotka. */
static void feed(uint32_t now, const uint8_t *bytes, uint8_t n)
{
    uint8_t d[8];

    memset(d, 0, sizeof(d));
    memcpy(d, bytes, n);
    isotp_on_frame(&g_link, ECU_RX, d, 8u, now);
}

/* ------------------------------------------------------------------
 * Drobny testovaci stroj
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
 * 1. Jednoramcova zprava tam i zpet
 * ------------------------------------------------------------------ */
static void test_single_frame(void)
{
    /* 22 F4 05 - precti otacky motoru */
    static const uint8_t req[] = { 0x22, 0xF4, 0x05 };
    static const uint8_t rsp[] = { 0x05, 0x62, 0xF4, 0x05, 0x01, 0x2C };
    uint8_t out[8];
    uint16_t n;

    CHECK(isotp_send(&g_link, req, sizeof(req), 0u));
    CHECK(g_ntx == 1);
    CHECK(g_tx[0].id == ECU_TX);
    CHECK(g_tx[0].dlc == 8);                 /* doplneno na osm bajtu */
    CHECK(g_tx[0].data[0] == 0x03);          /* single frame, tri bajty */
    CHECK(memcmp(&g_tx[0].data[1], req, 3) == 0);
    CHECK(g_tx[0].data[4] == 0x00);          /* vypln */
    CHECK(isotp_state(&g_link) == ISOTP_WAITING);

    feed(5u, rsp, sizeof(rsp));
    CHECK(isotp_state(&g_link) == ISOTP_DONE);

    n = isotp_take(&g_link, out, sizeof(out));
    CHECK(n == 5u);
    CHECK(memcmp(out, &rsp[1], 5) == 0);
    CHECK(isotp_state(&g_link) == ISOTP_IDLE);
    CHECK(g_link.n_msg_rx == 1u);
}

/* ------------------------------------------------------------------
 * 2. Viceramcovy prijem vcetne naseho flow control
 * ------------------------------------------------------------------ */
static void test_multi_receive(void)
{
    static const uint8_t req[] = { 0x22, 0x21, 0x46 };
    uint8_t ff[8], cf[8], out[32];
    uint8_t body[20];
    uint16_t n;
    int i;

    for (i = 0; i < 20; i++) {
        body[i] = (uint8_t)(0xA0 + i);
    }

    CHECK(isotp_send(&g_link, req, sizeof(req), 0u));

    /* first frame: 20 bajtu, z toho sest rovnou */
    ff[0] = 0x10; ff[1] = 20;
    memcpy(&ff[2], &body[0], 6);
    isotp_on_frame(&g_link, ECU_RX, ff, 8u, 5u);

    CHECK(isotp_state(&g_link) == ISOTP_RECEIVING);
    CHECK(g_ntx == 2);                       /* pozadavek + nas flow control */
    CHECK(g_tx[1].data[0] == 0x30);          /* clear to send */
    CHECK(g_tx[1].data[1] == 0x00);          /* block size 0 - posli vse */
    CHECK(g_tx[1].data[2] == 0x00);          /* bez separace */

    cf[0] = 0x21;
    memcpy(&cf[1], &body[6], 7);
    isotp_on_frame(&g_link, ECU_RX, cf, 8u, 10u);
    CHECK(isotp_state(&g_link) == ISOTP_RECEIVING);

    cf[0] = 0x22;
    memcpy(&cf[1], &body[13], 7);
    isotp_on_frame(&g_link, ECU_RX, cf, 8u, 15u);
    CHECK(isotp_state(&g_link) == ISOTP_DONE);

    n = isotp_take(&g_link, out, sizeof(out));
    CHECK(n == 20u);
    CHECK(memcmp(out, body, 20) == 0);
}

/* Prijem s nasim block size 2 - flow control musi odejit po kazdych
 * dvou ramcich, ne jen jednou na zacatku. */
static void test_multi_receive_blocks(void)
{
    static const uint8_t req[] = { 0x19, 0x02, 0xAF };
    uint8_t ff[8], cf[8], out[48];
    uint8_t body[34];
    isotp_cfg_t cfg;
    int i;

    isotp_cfg_default(&cfg);
    cfg.tx_id = ECU_TX;
    cfg.rx_id = ECU_RX;
    cfg.send = fake_send;
    cfg.rx_bs = 2u;
    isotp_init(&g_link, &cfg);

    for (i = 0; i < 34; i++) {
        body[i] = (uint8_t)i;
    }
    CHECK(isotp_send(&g_link, req, sizeof(req), 0u));

    ff[0] = 0x10; ff[1] = 34;
    memcpy(&ff[2], &body[0], 6);
    isotp_on_frame(&g_link, ECU_RX, ff, 8u, 1u);
    CHECK(g_ntx == 2);                        /* prvni flow control */

    for (i = 0; i < 4; i++) {
        uint8_t off = (uint8_t)(6 + i * 7);
        uint8_t left = (uint8_t)(34 - off);
        cf[0] = (uint8_t)(0x21 + i);
        memcpy(&cf[1], &body[off], (left > 7u) ? 7u : left);
        isotp_on_frame(&g_link, ECU_RX, cf, 8u, (uint32_t)(2 + i));
    }
    CHECK(g_ntx == 3);                        /* druhy flow control po dvou CF */
    CHECK(g_tx[2].data[0] == 0x30);
    CHECK(g_tx[2].data[1] == 0x02);
    CHECK(isotp_state(&g_link) == ISOTP_DONE);
    CHECK(isotp_take(&g_link, out, sizeof(out)) == 34u);
    CHECK(memcmp(out, body, 34) == 0);
}

/* ------------------------------------------------------------------
 * 3. Viceramcove odeslani s block size 1
 *
 * Presne to, co posila X82: 30 01 05. Po KAZDEM consecutive frame se
 * musi cekat na dalsi flow control.
 * ------------------------------------------------------------------ */
static void test_multi_send_bs1(void)
{
    static const uint8_t fc[] = { 0x30, 0x01, 0x05 };
    uint8_t msg[20];
    int i;

    for (i = 0; i < 20; i++) {
        msg[i] = (uint8_t)(0x10 + i);
    }

    CHECK(isotp_send(&g_link, msg, sizeof(msg), 0u));
    CHECK(isotp_state(&g_link) == ISOTP_SENDING);
    CHECK(g_ntx == 1);
    CHECK(g_tx[0].data[0] == 0x10);           /* first frame, delka 20 */
    CHECK(g_tx[0].data[1] == 20);
    CHECK(memcmp(&g_tx[0].data[2], &msg[0], 6) == 0);

    /* bez flow control se nesmi hnout */
    isotp_poll(&g_link, 1u);
    CHECK(g_ntx == 1);

    feed(2u, fc, sizeof(fc));
    isotp_poll(&g_link, 3u);
    CHECK(g_ntx == 1);                        /* separace 5 ms jeste neuplynula */

    isotp_poll(&g_link, 7u);
    CHECK(g_ntx == 2);
    CHECK(g_tx[1].data[0] == 0x21);
    CHECK(memcmp(&g_tx[1].data[1], &msg[6], 7) == 0);

    /* block size 1 - dalsi ramec az po dalsim flow control */
    isotp_poll(&g_link, 20u);
    CHECK(g_ntx == 2);
    CHECK(isotp_state(&g_link) == ISOTP_SENDING);

    feed(20u, fc, sizeof(fc));
    isotp_poll(&g_link, 25u);
    CHECK(g_ntx == 3);
    CHECK(g_tx[2].data[0] == 0x22);
    CHECK(memcmp(&g_tx[2].data[1], &msg[13], 7) == 0);

    /* cela zprava venku, ted se ceka na odpoved */
    CHECK(isotp_state(&g_link) == ISOTP_WAITING);
    CHECK(g_link.n_msg_tx == 1u);
}

/* ------------------------------------------------------------------
 * 4. Chybejici flow control
 * ------------------------------------------------------------------ */
static void test_no_flow_control(void)
{
    uint8_t msg[20];

    memset(msg, 0x5A, sizeof(msg));
    CHECK(isotp_send(&g_link, msg, sizeof(msg), 0u));

    CHECK(isotp_poll(&g_link, 999u) == ISOTP_SENDING);
    CHECK(isotp_poll(&g_link, 1000u) == ISOTP_FAILED);
    CHECK(isotp_error(&g_link) == ISOTP_ERR_NO_FC);
    CHECK(g_ntx == 1);                        /* zbytek uz nikam neodesel */
}

/* Jednotka si rekne o strpeni (FS=1) a pak povoli. */
static void test_flow_control_wait(void)
{
    static const uint8_t fc_wait[] = { 0x31, 0x00, 0x00 };
    static const uint8_t fc_go[]   = { 0x30, 0x01, 0x05 };
    uint8_t msg[20];

    memset(msg, 0x33, sizeof(msg));
    CHECK(isotp_send(&g_link, msg, sizeof(msg), 0u));

    feed(900u, fc_wait, sizeof(fc_wait));
    CHECK(isotp_poll(&g_link, 1500u) == ISOTP_SENDING);   /* limit se posunul */

    feed(1600u, fc_go, sizeof(fc_go));
    isotp_poll(&g_link, 1605u);
    CHECK(g_ntx == 2);
    CHECK(g_tx[1].data[0] == 0x21);
}

/* Jednotce se zprava nevejde (FS=2). */
static void test_flow_control_overflow(void)
{
    static const uint8_t fc_ovf[] = { 0x32, 0x00, 0x00 };
    uint8_t msg[20];

    memset(msg, 0x44, sizeof(msg));
    CHECK(isotp_send(&g_link, msg, sizeof(msg), 0u));
    feed(5u, fc_ovf, sizeof(fc_ovf));
    CHECK(isotp_state(&g_link) == ISOTP_FAILED);
    CHECK(isotp_error(&g_link) == ISOTP_ERR_FC_OVERFLOW);
}

/* ------------------------------------------------------------------
 * 5. responsePending 0x78
 * ------------------------------------------------------------------ */
static void test_response_pending(void)
{
    static const uint8_t req[]     = { 0x19, 0x02, 0xAF };
    static const uint8_t pending[] = { 0x03, 0x7F, 0x19, 0x78 };
    static const uint8_t rsp[]     = { 0x04, 0x59, 0x02, 0xFF, 0x01 };
    uint8_t out[8];

    CHECK(isotp_send(&g_link, req, sizeof(req), 0u));

    feed(50u, pending, sizeof(pending));
    CHECK(isotp_state(&g_link) == ISOTP_WAITING);   /* chyba to neni */
    CHECK(g_link.n_pending == 1u);

    /* P2 by uz davno vyprselo, P2* ne */
    CHECK(isotp_poll(&g_link, 1100u) == ISOTP_WAITING);

    feed(2000u, rsp, sizeof(rsp));
    CHECK(isotp_state(&g_link) == ISOTP_DONE);
    CHECK(isotp_take(&g_link, out, sizeof(out)) == 4u);
    CHECK(out[0] == 0x59);

    /* a kdyz ani po P2* nic nedojde, je to chyba */
    setup();
    CHECK(isotp_send(&g_link, req, sizeof(req), 0u));
    feed(50u, pending, sizeof(pending));
    CHECK(isotp_poll(&g_link, 5051u) == ISOTP_FAILED);
    CHECK(isotp_error(&g_link) == ISOTP_ERR_TIMEOUT);
}

/* ------------------------------------------------------------------
 * 6. Nase vlastni pojistka N_Cs
 *
 * Kdyz hlavni smycka nepusti transport ke slovu do 150 ms, jednotka uz
 * prenos stejne zahodila. Lepsi to rict nez posilat do prazdna.
 * ------------------------------------------------------------------ */
static void test_n_cs_guard(void)
{
    static const uint8_t fc[] = { 0x30, 0x01, 0x05 };
    uint8_t msg[20];

    memset(msg, 0x77, sizeof(msg));
    CHECK(isotp_send(&g_link, msg, sizeof(msg), 0u));
    feed(2u, fc, sizeof(fc));

    CHECK(isotp_poll(&g_link, 200u) == ISOTP_FAILED);
    CHECK(isotp_error(&g_link) == ISOTP_ERR_N_CS);
    CHECK(g_ntx == 1);
}

/* ------------------------------------------------------------------
 * 7. Drobnosti, ktere se snadno rozbiji
 * ------------------------------------------------------------------ */
static void test_bad_sequence(void)
{
    static const uint8_t req[] = { 0x22, 0x21, 0x46 };
    uint8_t ff[8], cf[8];

    CHECK(isotp_send(&g_link, req, sizeof(req), 0u));
    ff[0] = 0x10; ff[1] = 20;
    memset(&ff[2], 0x11, 6);
    isotp_on_frame(&g_link, ECU_RX, ff, 8u, 1u);

    cf[0] = 0x22;                 /* ma prijit 0x21 */
    memset(&cf[1], 0x22, 7);
    isotp_on_frame(&g_link, ECU_RX, cf, 8u, 2u);
    CHECK(isotp_state(&g_link) == ISOTP_FAILED);
    CHECK(isotp_error(&g_link) == ISOTP_ERR_SEQ);
}

static void test_no_response(void)
{
    static const uint8_t req[] = { 0x22, 0xF4, 0x05 };

    CHECK(isotp_send(&g_link, req, sizeof(req), 0u));
    CHECK(isotp_poll(&g_link, 999u) == ISOTP_WAITING);
    CHECK(isotp_poll(&g_link, 1000u) == ISOTP_FAILED);
    CHECK(isotp_error(&g_link) == ISOTP_ERR_TIMEOUT);
}

static void test_foreign_frames(void)
{
    static const uint8_t req[] = { 0x22, 0xF4, 0x05 };
    uint8_t d[8];

    CHECK(isotp_send(&g_link, req, sizeof(req), 0u));

    /* provoz jine jednotky na stejne sbernici nam nesmi nic udelat */
    memset(d, 0, sizeof(d));
    d[0] = 0x03; d[1] = 0x62;
    isotp_on_frame(&g_link, 0x765u, d, 8u, 10u);
    CHECK(isotp_state(&g_link) == ISOTP_WAITING);
}

static void test_send_failure(void)
{
    static const uint8_t req[] = { 0x22, 0xF4, 0x05 };

    g_send_ok = false;    /* listen-only nebo plna fronta */
    CHECK(!isotp_send(&g_link, req, sizeof(req), 0u));
    CHECK(isotp_state(&g_link) == ISOTP_FAILED);
    CHECK(isotp_error(&g_link) == ISOTP_ERR_SEND);
}

static void test_too_long(void)
{
    static uint8_t big[ISOTP_BUF_SIZE + 1];

    CHECK(!isotp_send(&g_link, big, (uint16_t)sizeof(big), 0u));
    CHECK(isotp_error(&g_link) == ISOTP_ERR_TOO_LONG);
}

static void test_st_min_decode(void)
{
    CHECK(isotp_st_min_ms(0x00u) == 0u);
    CHECK(isotp_st_min_ms(0x05u) == 5u);      /* to posila X82 */
    CHECK(isotp_st_min_ms(0x7Fu) == 127u);
    CHECK(isotp_st_min_ms(0xF1u) == 1u);      /* 100 us */
    CHECK(isotp_st_min_ms(0xF9u) == 1u);      /* 900 us */
    CHECK(isotp_st_min_ms(0x80u) == 127u);    /* rezervovana hodnota */
}

/* ------------------------------------------------------------------ */
typedef void (*test_fn)(void);

static const struct {
    const char *name;
    test_fn     fn;
} TESTS[] = {
    { "jednoramcova zprava tam i zpet",     test_single_frame },
    { "viceramcovy prijem",                 test_multi_receive },
    { "viceramcovy prijem po blocich",      test_multi_receive_blocks },
    { "viceramcove odeslani, block size 1", test_multi_send_bs1 },
    { "chybejici flow control",             test_no_flow_control },
    { "flow control WAIT",                  test_flow_control_wait },
    { "flow control OVERFLOW",              test_flow_control_overflow },
    { "responsePending 0x78",               test_response_pending },
    { "pojistka N_Cs 150 ms",               test_n_cs_guard },
    { "spatne poradove cislo",              test_bad_sequence },
    { "zadna odpoved",                      test_no_response },
    { "cizi ramce na sbernici",             test_foreign_frames },
    { "odeslani ramce selhalo",             test_send_failure },
    { "prilis dlouha zprava",               test_too_long },
    { "prevod STmin",                       test_st_min_decode },
};

int main(void)
{
    unsigned i;
    int bad = 0;

    for (i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); i++) {
        int before = g_failed;

        g_test = TESTS[i].name;
        setup();
        TESTS[i].fn();
        if (g_failed == before) {
            printf("ok    %s\n", TESTS[i].name);
        } else {
            printf("CHYBA %s\n", TESTS[i].name);
            bad++;
        }
    }

    printf("\n%d testu, %d kontrol, %d chyb\n",
           (int)(sizeof(TESTS) / sizeof(TESTS[0])), g_checks, g_failed);
    return (bad == 0) ? 0 : 1;
}
