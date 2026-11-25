/*
 * elm_test.c - testy emulace ELM327 na hostiteli
 *
 * Pokryva src/net/elm.c nad skutecnym src/can/isotp.c. Zadna deska,
 * zadny soket, zadne vozidlo: misto radice TWAI je fake_send(), ktery
 * ramce uklada do pole, misto jednotky funkce ecu_*() a cas se dosazuje
 * rucne. Diky tomu jde overit i vyprseni AT ST, na ktere by se jinak
 * cekalo vteriny.
 *
 * Nejdulezitejsi kontroly v tomhle souboru nejsou o prikazech, ale o
 * tom, co se NESMI stat:
 *
 *   - zapisova sluzba neprojde filtrem a na sbernici neodejde NIC
 *   - v listen-only neodejde ramec ani u povolene sluzby
 *   - ATRV nevymysli napeti, kdyz zadne nemame
 *
 * Preklad a spusteni:
 *
 *     cmake -S test -B test/build
 *     cmake --build test/build
 *     ./test/build/elm_test
 *
 * Navratovy kod 0 = vse proslo.
 */
#include "net/elm.h"

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
static bool g_send_ok;      /* false = radic ramec odmitl (listen-only) */
static bool g_tx_allowed;   /* false = rezim sbernice vysilat nedovoli */

/* Zaznam VSECH ramcu za cely beh, pres hranice testu. Slouzi k jedine
 * kontrole: ze z emulace nikdy neodejde nic jineho nez cteni. */
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

static bool fake_tx_allowed(void *user)
{
    (void)user;
    return g_tx_allowed;
}

/* Napeti z jednotky. g_volts_ok = prave ho mame. */
static bool g_volts_ok;
static float g_volts;

static bool fake_volts(void *user, float *out)
{
    (void)user;
    if (!g_volts_ok) {
        return false;
    }
    *out = g_volts;
    return true;
}

#define ECU_TX 0x7E0u   /* my -> motor */
#define ECU_RX 0x7E8u   /* motor -> my */
#define BCM_TX 0x745u   /* my -> karoserie, stary protokol Renault */
#define BCM_RX 0x765u

/* ------------------------------------------------------------------
 * Drobny testovaci stroj - stejny jako v isotp_test.c a uds_test.c
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
 * Emulace a klient
 * ------------------------------------------------------------------ */
static elm_t g_e;
static char  g_out[4096];

static void setup(void)
{
    elm_cfg_t cfg;

    g_ntx = 0;
    g_send_ok = true;
    g_tx_allowed = true;
    g_volts_ok = false;
    g_volts = 0.0f;
    memset(g_tx, 0, sizeof(g_tx));

    elm_cfg_default(&cfg);
    cfg.tx_id      = ECU_TX;
    cfg.rx_id      = ECU_RX;
    cfg.send       = fake_send;
    cfg.tx_allowed = fake_tx_allowed;
    cfg.volts      = fake_volts;
    elm_init(&g_e, &cfg);
}

/* Vyzvedne vsechno, co emulace poslala klientovi. */
static const char *out(void)
{
    uint16_t n = elm_take(&g_e, g_out, (uint16_t)(sizeof(g_out) - 1u));

    g_out[n] = '\0';
    return g_out;
}

static void drain(void)
{
    (void)out();
}

/* Jeden prikaz od klienta, vcetne CR, a hned k tomu odpoved. */
static const char *say_at(const char *cmd, uint32_t now)
{
    char buf[160];

    snprintf(buf, sizeof(buf), "%s\r", cmd);
    elm_rx(&g_e, buf, (uint16_t)strlen(buf), now);
    elm_poll(&g_e, now);
    return out();
}

static const char *say(const char *cmd)
{
    return say_at(cmd, 1000u);
}

static bool has(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

/* Konci odpoved vyzvou? Bez ni nastroj ceka dal a relace stoji. */
static bool prompted(const char *s)
{
    size_t n = strlen(s);

    return n > 0 && s[n - 1] == '>';
}

/* Kolik radku odpovedi (bez vyzvy) v tom textu je. Slouzi ke kontrole,
 * ze se viceramcova zprava poslala jako JEDNA, a ne po ramcich. */
static int lines(const char *s)
{
    int n = 0;

    for (; *s; s++) {
        if (*s == '\r') {
            n++;
        }
    }
    return n - 1;       /* posledni konec radku patri vyzve */
}

/* ------------------------------------------------------------------
 * Pohled na odeslane ramce
 *
 * Mezi pozadavky lezi i nas vlastni flow control (PCI 3), ktery odchazi
 * pri prijmu viceramcove odpovedi. Kontroly se tykaji pozadavku, proto
 * se FC preskakuje.
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

static bool req_is(int k, uint32_t id, const uint8_t *want, uint8_t n)
{
    int i, j = 0;

    for (i = 0; i < g_ntx; i++) {
        if ((g_tx[i].data[0] >> 4) != 0x0u) {
            continue;
        }
        if (j++ != k) {
            continue;
        }
        return g_tx[i].id == id && g_tx[i].data[0] == n &&
               memcmp(&g_tx[i].data[1], want, n) == 0;
    }
    return false;
}

/* Odpoved jednotky vcetne ramcovani ISO-TP. Delsi nez sedm bajtu jde
 * jako first frame a consecutive frames; nas flow control si emulace
 * posle sama uvnitr elm_on_frame(). */
static void ecu_reply(uint32_t id, const uint8_t *msg, uint16_t len,
                      uint32_t now)
{
    uint8_t d[8];
    uint16_t sent;
    uint8_t sn = 1u;

    if (len <= 7u) {
        memset(d, 0, sizeof(d));
        d[0] = (uint8_t)len;
        memcpy(&d[1], msg, len);
        elm_on_frame(&g_e, id, d, 8u, now);
        return;
    }

    memset(d, 0, sizeof(d));
    d[0] = (uint8_t)(0x10u | ((len >> 8) & 0x0Fu));
    d[1] = (uint8_t)(len & 0xFFu);
    memcpy(&d[2], msg, 6u);
    elm_on_frame(&g_e, id, d, 8u, now);

    sent = 6u;
    while (sent < len) {
        uint16_t n = (uint16_t)(len - sent);

        if (n > 7u) {
            n = 7u;
        }
        memset(d, 0, sizeof(d));
        d[0] = (uint8_t)(0x20u | (sn & 0x0Fu));
        memcpy(&d[1], msg + sent, n);
        elm_on_frame(&g_e, id, d, 8u, now);
        sent = (uint16_t)(sent + n);
        sn++;
    }
}

/* Klient posle prikaz a necha si odpovedet pozdeji. Vraci se jen to, co
 * odeslo hned (echo), vlastni odpoved se vyzvedne az po ecu_reply(). */
static void ask(const char *cmd, uint32_t now)
{
    char buf[160];

    snprintf(buf, sizeof(buf), "%s\r", cmd);
    elm_rx(&g_e, buf, (uint16_t)strlen(buf), now);
    elm_poll(&g_e, now);
}

/* Vychozi stav pro cteni ze sbernice: bez echa a bez hlavicek, tedy to,
 * co posila vetsina nastroju po uvodni sekvenci. */
static void setup_quiet(void)
{
    setup();
    drain();
    (void)say("ATE0");
    drain();
}

/* ==================================================================
 * KROK 1 - prikazy bez site
 * ================================================================== */

/* Uvitani a vyzva. Nastroj cte az k '>' a bez nej ceka navzdy. */
static void test_hello(void)
{
    setup();
    elm_reset(&g_e, 0u);
    const char *s = out();

    CHECK(has(s, ELM_ID));
    CHECK(prompted(s));
}

static void test_echo(void)
{
    const char *s;

    setup();
    drain();

    /* Po zapnuti je echo zapnute, jako u skutecneho dongle. */
    s = say("ATI");
    CHECK(has(s, "ATI"));
    CHECK(has(s, ELM_ID));

    s = say("ATE0");
    CHECK(has(s, "ATE0"));          /* prikaz, kterym se echo vypina, se
                                       jeste echuje */
    CHECK(has(s, "OK"));

    s = say("ATI");
    CHECK(!has(s, "ATI"));          /* uz ne */
    CHECK(has(s, ELM_ID));

    s = say("ATE1");
    CHECK(has(s, "OK"));
    s = say("ATI");
    CHECK(has(s, "ATI"));
}

static void test_linefeed(void)
{
    const char *s;

    setup();
    drain();
    CHECK(has(say("ATE0"), "OK"));

    s = say("ATL0");
    CHECK(!has(s, "\n"));

    CHECK(has(say("ATL1"), "OK"));
    s = say("ATI");
    CHECK(has(s, "\r\n"));

    CHECK(has(say("ATL0"), "OK"));
    s = say("ATI");
    CHECK(!has(s, "\n"));
}

static void test_identity(void)
{
    setup();
    drain();
    CHECK(has(say("ATE0"), "OK"));

    /* ATI musi vratit retezec, ktery nastroje poznaji. */
    CHECK(has(say("ATI"), "ELM327 v1.5"));

    /* AT@1 je popis zarizeni - tam uz stoji pravda, protoze to zadny
     * nastroj nerozbije. */
    CHECK(has(say("AT@1"), "CANOGRAF"));
    CHECK(!has(say("AT@1"), "ELM327 v1.5"));
}

static void test_reset(void)
{
    setup();
    drain();
    CHECK(has(say("ATE0"), "OK"));
    CHECK(has(say("ATH1"), "OK"));

    const char *s = say("ATZ");
    CHECK(has(s, ELM_ID));
    CHECK(prompted(s));

    /* Po ATZ plati vychozi stav, tedy echo zpatky zapnute. */
    CHECK(has(say("ATI"), "ATI"));
}

static void test_protocol(void)
{
    const char *s;

    setup();
    drain();
    CHECK(has(say("ATE0"), "OK"));

    CHECK(has(say("ATSP6"), "OK"));
    s = say("ATDP");
    CHECK(has(s, "ISO 15765-4"));
    CHECK(has(s, "CAN 11/500"));
    CHECK(!has(s, "AUTO"));
    CHECK(has(say("ATDPN"), "6"));

    /* Automat: cislo protokolu dostane predponu A, jako u ELM327. */
    CHECK(has(say("ATSP0"), "OK"));
    CHECK(has(say("ATDP"), "AUTO"));
    CHECK(has(say("ATDPN"), "A6"));
    CHECK(has(say("ATSPA6"), "OK"));
    CHECK(has(say("ATDPN"), "A6"));

    /* Protokol, ktery deska nema, se odmita. Jeden radic TWAI, 11 bitu
     * a rychlost z konfigurace - OK na cokoliv jineho by byl slib
     * naprazdno. */
    CHECK(has(say("ATSP5"), "?"));
    CHECK(has(say("ATSP7"), "?"));
}

static void test_setup_commands(void)
{
    setup();
    drain();
    CHECK(has(say("ATE0"), "OK"));

    /* Uvodni sekvence, kterou posilaji nastroje. Kazdy radek musi
     * skoncit OK, jinak se nastroj nepripoji. */
    CHECK(has(say("ATL0"),    "OK"));
    CHECK(has(say("ATS0"),    "OK"));
    CHECK(has(say("ATS1"),    "OK"));
    CHECK(has(say("ATH0"),    "OK"));
    CHECK(has(say("ATH1"),    "OK"));
    CHECK(has(say("ATCAF1"),  "OK"));
    CHECK(has(say("ATCAF0"),  "OK"));
    CHECK(has(say("ATCAF1"),  "OK"));
    CHECK(has(say("ATAT0"),   "OK"));
    CHECK(has(say("ATAT1"),   "OK"));
    CHECK(has(say("ATAT2"),   "OK"));
    CHECK(has(say("ATST20"),  "OK"));
    CHECK(has(say("ATSTFF"),  "OK"));
    CHECK(has(say("ATSH7E0"), "OK"));
    CHECK(has(say("ATCRA7E8"),"OK"));
    CHECK(has(say("ATCRA"),   "OK"));
    CHECK(has(say("ATFCSH7E0"), "OK"));
    CHECK(has(say("ATFCSD300000"), "OK"));
    CHECK(has(say("ATFCSM1"), "OK"));
    CHECK(has(say("ATAL"),    "OK"));
    CHECK(has(say("ATPC"),    "OK"));
    CHECK(has(say("ATD"),     "OK"));

    /* Mezery a mala pismena nastroje posilaji taky. */
    setup();
    drain();
    CHECK(has(say("at e0"), "OK"));
    CHECK(has(say("at sp 6"), "OK"));
    CHECK(has(say("atsh 7e0"), "OK"));

    /* Co neumime, rekneme. Tiche OK by znamenalo slib naprazdno. */
    CHECK(has(say("ATXYZ"), "?"));
    CHECK(has(say("ATSHZZZ"), "?"));
    CHECK(has(say("ATFCSM9"), "?"));
    CHECK(has(say("ATST"), "?"));
    /* 29bitova hlavicka: tenhle radic jede na 11 bitech. */
    CHECK(has(say("ATSH18DAF110"), "?"));
}

/* Prazdny radek opakuje posledni prikaz - nastroje tak ctou jeden PID
 * dokola a setri bajty na lince. */
static void test_repeat(void)
{
    setup();
    drain();
    CHECK(has(say("ATE0"), "OK"));
    CHECK(has(say("ATI"), ELM_ID));
    CHECK(has(say(""), ELM_ID));
    CHECK(has(say(""), ELM_ID));

    /* Bez predchoziho prikazu neni co opakovat. */
    setup();
    drain();
    CHECK(has(say(""), "?"));
}

/* ------------------------------------------------------------------
 * ATRV - napeti, ktere deska zmerit neumi
 *
 * Z VIN, VBUS, 5V ani VBAT nevede zadny delic na pin s prevodnikem
 * (docs/findings.md). Bud cislo mame z jednotky, nebo se rekne, ze
 * zadne neni; vymyslene "12.6V" by byl udaj, ktery nikdo nezmeril.
 * ------------------------------------------------------------------ */
static bool has_digit(const char *s)
{
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            return true;
        }
    }
    return false;
}

static void test_volts(void)
{
    const char *s;

    setup();
    drain();
    CHECK(has(say("ATE0"), "OK"));

    /* Nemame. Odpoved nesmi obsahovat zadnou cifru - zadne napeti, ani
     * nula, ani "12.6V". */
    g_volts_ok = false;
    s = say("ATRV");
    CHECK(!has_digit(s));
    CHECK(has(s, "NO DATA"));
    CHECK(prompted(s));

    /* Mame z jednotky: posle se presne to. */
    g_volts_ok = true;
    g_volts = 13.9f;
    s = say("ATRV");
    CHECK(has(s, "13.9V"));

    g_volts = 12.1f;
    CHECK(has(say("ATRV"), "12.1V"));

    /* A kdyz zmizi, prestane se tvrdit i to posledni cislo. */
    g_volts_ok = false;
    s = say("ATRV");
    CHECK(!has_digit(s));
}

/* ------------------------------------------------------------------
 * Filtr sluzeb
 *
 * Bily seznam je to jedine, co stoji mezi cizim nastrojem a jednotkami
 * vozu, takze ma vlastni kontroly, nejen ty pres cely prikaz.
 * ------------------------------------------------------------------ */
static bool svc(uint8_t a, uint8_t b)
{
    uint8_t m[2];

    m[0] = a;
    m[1] = b;
    return elm_service_allowed(m, 2u);
}

static void test_service_filter(void)
{
    setup();

    /* Cteni projde. */
    CHECK(svc(0x01u, 0x00u));   /* OBD-II current data */
    CHECK(svc(0x09u, 0x02u));   /* VIN */
    CHECK(svc(0x19u, 0x02u));   /* seznam chyb */
    CHECK(svc(0x21u, 0x34u));   /* stary Renault, blok EMM */
    CHECK(svc(0x22u, 0xF1u));   /* read data by identifier */
    CHECK(svc(0x3Eu, 0x00u));   /* tester present */

    /* Zapis a vse ostatni ne. Tohle je ten seznam, kvuli kteremu cele
     * tohle rozhrani smi existovat. */
    CHECK(!svc(0x2Eu, 0xF1u));  /* write data by identifier */
    CHECK(!svc(0x3Bu, 0x34u));  /* stary Renault, zapis */
    CHECK(!svc(0x14u, 0xFFu));  /* smazani pameti chyb */
    CHECK(!svc(0x31u, 0x01u));  /* routine control, tedy i regenerace */
    CHECK(!svc(0x11u, 0x01u));  /* reset jednotky */
    CHECK(!svc(0x27u, 0x01u));  /* security access */
    CHECK(!svc(0x28u, 0x00u));  /* communication control */
    CHECK(!svc(0x2Fu, 0x00u));  /* input output control */
    CHECK(!svc(0x34u, 0x00u));  /* request download */
    CHECK(!svc(0x36u, 0x00u));  /* transfer data */
    CHECK(!svc(0x37u, 0x00u));  /* transfer exit */
    CHECK(!svc(0x85u, 0x02u));  /* control DTC setting */

    /* Session control se hlida i podle podfunkce: 02 je programovaci
     * session, tedy brana k nahravani programu. */
    CHECK(svc(0x10u, 0x01u));
    CHECK(svc(0x10u, 0x03u));
    CHECK(svc(0x10u, 0xC0u));
    CHECK(svc(0x10u, 0x81u));   /* 01 s potlacenou odpovedi */
    CHECK(!svc(0x10u, 0x02u));
    CHECK(!svc(0x10u, 0x82u));  /* ani s potlacenou odpovedi */
    CHECK(!svc(0x10u, 0x40u));

    /* Prazdne a useknute pozadavky nejsou "nic se nestalo", jsou to
     * pozadavky, o kterych nic nevime - tedy ne. */
    CHECK(!elm_service_allowed(NULL, 0u));
    {
        uint8_t one = 0x10u;
        CHECK(!elm_service_allowed(&one, 1u));  /* session bez podfunkce */
    }
    {
        uint8_t one = 0x22u;
        CHECK(elm_service_allowed(&one, 1u));   /* sluzba bez dat je cteni */
    }
}

/* ==================================================================
 * KROK 2 - prenos dotazu na sbernici
 * ================================================================== */

/* Jednoramcovy dotaz a odpoved ve tvaru, ktery nastroje cekaji. */
static void test_single_frame(void)
{
    static const uint8_t req[] = { 0x01u, 0x00u };
    static const uint8_t rsp[] = { 0x41u, 0x00u, 0xBEu, 0x3Fu, 0xA8u, 0x13u };
    const char *s;

    setup_quiet();
    ask("0100", 1000u);
    CHECK(req_count() == 1);
    CHECK(req_is(0, ECU_TX, req, 2u));
    CHECK(elm_busy(&g_e));

    ecu_reply(ECU_RX, rsp, sizeof(rsp), 1020u);
    elm_poll(&g_e, 1020u);
    s = out();
    CHECK(has(s, "41 00 BE 3F A8 13"));
    CHECK(prompted(s));
    CHECK(!elm_busy(&g_e));

    /* ATH1: pred bajty stoji adresa, ze ktere odpoved prisla. */
    CHECK(has(say("ATH1"), "OK"));
    ask("0100", 2000u);
    ecu_reply(ECU_RX, rsp, sizeof(rsp), 2020u);
    elm_poll(&g_e, 2020u);
    CHECK(has(out(), "7E8 41 00 BE 3F A8 13"));

    /* ATS0: bez mezer, jako u skutecneho dongle. */
    CHECK(has(say("ATS0"), "OK"));
    ask("0100", 3000u);
    ecu_reply(ECU_RX, rsp, sizeof(rsp), 3020u);
    elm_poll(&g_e, 3020u);
    CHECK(has(out(), "7E84100BE3FA813"));

    /* Posledni licha cifra je u ELM327 pocet ocekavanych odpovedi
     * ("0100 1"), ne data - nesmi se dostat do pozadavku. */
    setup_quiet();
    ask("0100 1", 4000u);
    CHECK(req_count() == 1);
    CHECK(req_is(0, ECU_TX, req, 2u));
}

/* ------------------------------------------------------------------
 * Viceramcovou odpoved skladame MY
 *
 * Tohle je ta vec, kterou skutecny ELM327 v1.5 neumi a kvuli ktere
 * tenhle projekt vznikl (docs/findings.md). Nastroj proto dostane JEDEN
 * radek s celou zpravou, ne hromadu ramcu s bajty PCI.
 * ------------------------------------------------------------------ */
static void test_multiframe(void)
{
    static const uint8_t req[] = { 0x22u, 0xF1u, 0x90u };
    static const uint8_t rsp[] = {
        0x62u, 0xF1u, 0x90u,
        'V', 'F', '1', 'J', 'L', '0', '0', '0', '0', '6', '5', '4', '3', '2',
        '1', '0', '9'
    };
    const char *s;

    setup_quiet();
    ask("22F190", 1000u);
    CHECK(req_count() == 1);
    CHECK(req_is(0, ECU_TX, req, 3u));

    ecu_reply(ECU_RX, rsp, sizeof(rsp), 1020u);
    elm_poll(&g_e, 1020u);
    s = out();

    /* Cela zprava, od sluzby po posledni bajt, na JEDNOM radku. */
    CHECK(has(s, "62 F1 90 56 46 31 4A 4C 30 30 30 30 36 35 34 33 32 31 30 39"));
    CHECK(lines(s) == 1);
    /* Bajty PCI (10 14 / 21 / 22) do vystupu nepatri - ty uz jsme
     * rozebrali my. */
    CHECK(!has(s, "10 14"));
    CHECK(prompted(s));

    /* Flow control jsme museli poslat, jinak by jednotka nepokracovala. */
    {
        int i, fc = 0;
        for (i = 0; i < g_ntx; i++) {
            if ((g_tx[i].data[0] >> 4) == 0x3u) {
                fc++;
                CHECK(g_tx[i].id == ECU_TX);
            }
        }
        CHECK(fc == 1);
    }
}

/* ------------------------------------------------------------------
 * Zapis neprojde
 *
 * Emulace je cteci zarizeni. Filtr je podle SLUZBY, takze nezalezi na
 * tom, jak se pozadavek tvari ani na jakou hlavicku jde.
 * ------------------------------------------------------------------ */
static void test_write_blocked(void)
{
    const char *s;

    setup_quiet();

    /* 2E F190: zapis VIN. Odmitnuti je slovo, ne ticho. */
    ask("2EF190313233", 1000u);
    s = out();
    CHECK(has(s, "WRITE PROTECTED"));
    CHECK(prompted(s));
    CHECK(g_ntx == 0);              /* na sbernici NEODESLO NIC */
    CHECK(!elm_busy(&g_e));

    /* Ani s vlastni hlavickou, ani v jine session, ani jako routine. */
    CHECK(has(say("ATSH745"), "OK"));
    ask("3B3401", 2000u);
    CHECK(has(out(), "WRITE PROTECTED"));
    ask("31010210", 3000u);         /* spusteni regenerace DPF */
    CHECK(has(out(), "WRITE PROTECTED"));
    ask("14FFFFFF", 4000u);         /* smazani pameti chyb */
    CHECK(has(out(), "WRITE PROTECTED"));
    ask("1101", 5000u);             /* reset jednotky */
    CHECK(has(out(), "WRITE PROTECTED"));
    ask("1002", 6000u);             /* programovaci session */
    CHECK(has(out(), "WRITE PROTECTED"));
    ask("2701", 7000u);             /* security access */
    CHECK(has(out(), "WRITE PROTECTED"));
    CHECK(g_ntx == 0);

    /* A cteni na teze hlavicce projde - filtr je o sluzbe, ne o adrese. */
    ask("2134", 8000u);
    CHECK(req_count() == 1);
    CHECK(g_tx[0].id == BCM_TX);
}

/* ------------------------------------------------------------------
 * V listen-only neodejde nic
 *
 * Stejne jako u polleru: rezim radice je ochrana, ne doporuceni.
 * Hlida se to dvakrat - cfg.tx_allowed() drive, nez se pozadavek zacne
 * skladat, a can_send() jeste jednou pri odeslani ramce.
 * ------------------------------------------------------------------ */
static void test_listen_only(void)
{
    const char *s;

    setup_quiet();
    g_tx_allowed = false;

    ask("0100", 1000u);
    s = out();
    CHECK(has(s, "UNABLE TO CONNECT"));
    CHECK(prompted(s));
    CHECK(g_ntx == 0);
    CHECK(!elm_busy(&g_e));

    ask("22F190", 2000u);
    CHECK(has(out(), "UNABLE TO CONNECT"));
    ask("3E00", 3000u);
    CHECK(has(out(), "UNABLE TO CONNECT"));
    CHECK(g_ntx == 0);

    /* Druha pojistka sama o sobe: rezim bychom "povolili", ale radic
     * ramec stejne odmitne. Ani tak neodejde nic a nastroj se to dozvi. */
    setup_quiet();
    g_tx_allowed = true;
    g_send_ok = false;
    ask("0100", 4000u);
    CHECK(has(out(), "UNABLE TO CONNECT"));
    CHECK(g_ntx == 0);
    CHECK(!elm_busy(&g_e));

    /* A jakmile se smi, projde to. */
    g_send_ok = true;
    ask("0100", 5000u);
    CHECK(req_count() == 1);
}

/* ------------------------------------------------------------------
 * Cekani: AT ST, a proc ho 7F xx 78 neprerusi
 * ------------------------------------------------------------------ */
static void test_timeout(void)
{
    const char *s;

    setup_quiet();

    /* Nastroj si rekne o kratke cekani a dostane presne to. */
    CHECK(has(say("ATST0A"), "OK"));        /* 10 x 4 ms, dolni mez 50 ms */
    ask("0100", 1000u);
    elm_poll(&g_e, 1030u);
    CHECK(strlen(out()) == 0);             /* jeste se ceka */
    CHECK(elm_busy(&g_e));
    elm_poll(&g_e, 1060u);
    s = out();
    CHECK(has(s, "NO DATA"));
    CHECK(prompted(s));
    CHECK(!elm_busy(&g_e));
}

static void test_response_pending(void)
{
    static const uint8_t pend[] = { 0x7Fu, 0x19u, 0x78u };
    static const uint8_t rsp[]  = { 0x59u, 0x02u, 0xFFu, 0xC1u, 0x02u,
                                    0x63u, 0xE5u };
    const char *s;

    setup_quiet();

    /* 19 02 s plnym seznamem chyb odpovida 7F xx 78 opakovane
     * (docs/findings.md). Vychozi strop cekani je 1000 ms, takze bez
     * prodlouzeni by odpoved ve 2 s propadla - a presne proto se po
     * skutecnem dongle musi volat AT ST FF. */
    ask("1902FF", 1000u);
    CHECK(req_count() == 1);
    ecu_reply(ECU_RX, pend, sizeof(pend), 1050u);
    elm_poll(&g_e, 1050u);

    elm_poll(&g_e, 2500u);                 /* uz za hranici AT ST */
    CHECK(strlen(out()) == 0);
    CHECK(elm_busy(&g_e));

    ecu_reply(ECU_RX, rsp, sizeof(rsp), 2600u);
    elm_poll(&g_e, 2600u);
    s = out();
    CHECK(has(s, "59 02 FF C1 02 63 E5"));
    CHECK(prompted(s));
}

/* Znak behem prenosu ho prerusi - nastroj to pouziva jako "uz to
 * nechci" a bez toho by cekal na odpoved, kterou uz nechce. */
static void test_stopped(void)
{
    const char *s;

    setup_quiet();
    ask("0100", 1000u);
    CHECK(elm_busy(&g_e));

    elm_rx(&g_e, "ATI\r", 4u, 1010u);
    s = out();
    CHECK(has(s, "STOPPED"));
    CHECK(has(s, ELM_ID));                 /* a prikaz se nezahodil */
    CHECK(!elm_busy(&g_e));
}

/* Vlastni hlavicka: ATSH meni, komu se posila, a odpoved se ceka z
 * adresy odvozene podle vozidla (745 -> 765). */
static void test_addressing(void)
{
    static const uint8_t req[] = { 0x21u, 0x34u };
    static const uint8_t rsp[] = { 0x61u, 0x34u, 0x0Cu, 0x7Au, 0x00u };

    setup_quiet();
    CHECK(has(say("ATSH745"), "OK"));
    ask("2134", 1000u);
    CHECK(req_is(0, BCM_TX, req, 2u));

    /* Z cizi adresy nic neprijmeme... */
    ecu_reply(0x7E8u, rsp, sizeof(rsp), 1010u);
    elm_poll(&g_e, 1010u);
    CHECK(strlen(out()) == 0);
    CHECK(elm_busy(&g_e));

    /* ...z te spravne ano. */
    ecu_reply(BCM_RX, rsp, sizeof(rsp), 1020u);
    elm_poll(&g_e, 1020u);
    CHECK(has(out(), "61 34 0C 7A 00"));

    /* ATCRA prebije odvozeni, protoze si ji nastroj rekl sam. */
    setup_quiet();
    CHECK(has(say("ATSH745"), "OK"));
    CHECK(has(say("ATCRA7AB"), "OK"));
    ask("2134", 2000u);
    ecu_reply(0x7ABu, rsp, sizeof(rsp), 2010u);
    elm_poll(&g_e, 2010u);
    CHECK(has(out(), "61 34 0C 7A 00"));
}

/* ------------------------------------------------------------------
 * ATCAF0 - kdo si rekne o syrove ramce, dostane je
 *
 * Rucne slozeny first frame se ale odmita: tim by si klient mohl
 * poslat libovolne dlouhy obsah po kouscich a filtr sluzeb by videl jen
 * jeho prvni kousek.
 * ------------------------------------------------------------------ */
static void test_caf_off(void)
{
    static const uint8_t req[] = { 0x01u, 0x00u };
    static const uint8_t rsp[] = { 0x41u, 0x00u, 0xBEu, 0x3Fu, 0xA8u, 0x13u };
    const char *s;

    setup_quiet();
    CHECK(has(say("ATCAF0"), "OK"));
    CHECK(has(say("ATH1"), "OK"));

    ask("0201000000000000", 1000u);
    CHECK(req_is(0, ECU_TX, req, 2u));

    ecu_reply(ECU_RX, rsp, sizeof(rsp), 1010u);
    elm_poll(&g_e, 1010u);
    s = out();
    /* Ramec tak, jak prisel, vcetne bajtu PCI a vypne. */
    CHECK(has(s, "7E8 06 41 00 BE 3F A8 13"));
    CHECK(prompted(s));

    /* Rucni first frame ne. */
    setup_quiet();
    CHECK(has(say("ATCAF0"), "OK"));
    ask("1014622E0102030405", 2000u);
    CHECK(has(out(), "?"));
    CHECK(g_ntx == 0);

    /* A zapis se neschova ani do single frame. */
    ask("042EF19001", 3000u);
    CHECK(has(out(), "WRITE PROTECTED"));
    CHECK(g_ntx == 0);
}

/* Chyba prenosu se nastroji rekne, misto aby se mlcelo. */
static void test_bus_error(void)
{
    static const uint8_t rsp[] = {
        0x62u, 0xF1u, 0x90u, 'V', 'F', '1', 'J', 'L', '0', '0'
    };
    uint8_t d[8];

    setup_quiet();
    ask("22F190", 1000u);

    /* Jednotka zacne viceramcovou odpoved a dalsi ramec uz neposle. */
    memset(d, 0, sizeof(d));
    d[0] = 0x10u;
    d[1] = (uint8_t)sizeof(rsp);
    memcpy(&d[2], rsp, 6u);
    elm_on_frame(&g_e, ECU_RX, d, 8u, 1010u);

    elm_poll(&g_e, 1010u + 1100u);
    CHECK(has(out(), "CAN ERROR"));
    CHECK(!elm_busy(&g_e));
}

/* ------------------------------------------------------------------
 * Seznam testu
 * ------------------------------------------------------------------ */
typedef struct {
    const char *name;
    void (*fn)(void);
} test_t;

static const test_t g_tests[] = {
    { "uvitani a vyzva",        test_hello },
    { "echo ATE",               test_echo },
    { "konce radku ATL",        test_linefeed },
    { "ATI a AT@1",             test_identity },
    { "ATZ",                    test_reset },
    { "protokol ATSP/ATDP",     test_protocol },
    { "uvodni sekvence",        test_setup_commands },
    { "prazdny radek opakuje",  test_repeat },
    { "ATRV nevymysli napeti",  test_volts },
    { "filtr sluzeb",           test_service_filter },
    { "jeden ramec tam i zpet", test_single_frame },
    { "viceramcova odpoved",    test_multiframe },
    { "zapis neprojde",         test_write_blocked },
    { "listen-only mlci",       test_listen_only },
    { "AT ST a NO DATA",        test_timeout },
    { "7F xx 78 ceka dal",      test_response_pending },
    { "znak prerusi prenos",    test_stopped },
    { "ATSH a ATCRA",           test_addressing },
    { "ATCAF0 syrove ramce",    test_caf_off },
    { "chyba prenosu",          test_bus_error },
};

int main(void)
{
    size_t i;
    int before;

    printf("emulace ELM327 - testy na hostiteli\n\n");
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
