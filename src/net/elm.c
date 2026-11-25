/*
 * elm.c - emulace ELM327: stavovy automat prikazu a prevod na ISO-TP
 *
 * Rozbor chovani, odchylek od skutecneho dongle a hlavne filtru sluzeb
 * je v elm.h. Tady je jen provedeni.
 *
 * Soubor ZNA can/isotp.h a NEZNA can/twai.h: jedina cesta na sbernici
 * vede pres cfg.send, tedy pres jeden ISO-TP spoj, do ktereho se
 * pozadavek dostane az potom, co prosel elm_service_allowed(). Pojistka
 * nize to drzi.
 */
#include "net/elm.h"

#include <stdio.h>
#include <string.h>

/* Pojistka, ne komentar. Emulace musi umet poslat pozadavek, takze
 * nemuze mit #error na can/twai.h jako net/server.c - zato nesmi
 * sahnout na can_send() mimo ISO-TP, protoze pak by se obesel filtr
 * sluzeb i listen-only. Jedine povolene odeslani je cfg.send. */
#ifdef CAN_TWAI_H
#error "net/elm: can/twai.h tu nema co delat - jedina cesta na sbernici je cfg.send"
#endif

/* ---------------------------------------------------------------------
 * Vystup ke klientovi
 * ------------------------------------------------------------------- */
static void out_ch(elm_t *e, char c)
{
    if (e->tx_len + 1u >= (uint16_t)sizeof(e->tx)) { e->tx_over = true; return; }
    e->tx[e->tx_len++] = c;
}

static void out_str(elm_t *e, const char *s)
{
    while (*s) out_ch(e, *s++);
}

static void out_eol(elm_t *e)
{
    out_ch(e, '\r');
    if (e->lf) out_ch(e, '\n');
}

static void out_line(elm_t *e, const char *s)
{
    out_str(e, s);
    out_eol(e);
}

/* Vyzva. Po ni uz prikaz neni rozdelany a nastroj smi poslat dalsi -
 * presne tohle znamena '>' na seriove lince ELM327. */
static void out_prompt(elm_t *e)
{
    /* Kdyz se odpoved nevesla, rekne se to a nemlci se. Misto na
     * hlasku se udela orezanim toho, co uz v bufferu lezi: pulka
     * odpovedi a BUFFER FULL je poctivejsi nez cela pulka odpovedi
     * tvarici se jako cela. */
    if (e->tx_over) {
        uint16_t keep = (uint16_t)(sizeof(e->tx) - (sizeof(ELM_RSP_FULL) + 8));
        if (e->tx_len > keep) e->tx_len = keep;
        e->tx_over = false;
        out_line(e, ELM_RSP_FULL);
    }
    out_eol(e);
    out_ch(e, '>');
}

static void out_hex2(elm_t *e, uint8_t b)
{
    static const char H[] = "0123456789ABCDEF";
    out_ch(e, H[b >> 4]);
    out_ch(e, H[b & 0x0F]);
}

static void out_id(elm_t *e, uint32_t id)
{
    static const char H[] = "0123456789ABCDEF";
    out_ch(e, H[(id >> 8) & 0x0F]);
    out_ch(e, H[(id >> 4) & 0x0F]);
    out_ch(e, H[id & 0x0F]);
}

static void out_bytes(elm_t *e, const uint8_t *d, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) {
        if (i && e->spaces) out_ch(e, ' ');
        out_hex2(e, d[i]);
    }
}

uint16_t elm_take(elm_t *e, char *out, uint16_t cap)
{
    uint16_t n = e->tx_len < cap ? e->tx_len : cap;

    if (!n) return 0;
    memcpy(out, e->tx, n);
    e->tx_len = (uint16_t)(e->tx_len - n);
    if (e->tx_len) memmove(e->tx, e->tx + n, e->tx_len);
    return n;
}

/* ---------------------------------------------------------------------
 * Filtr sluzeb - jedine misto mezi cizim nastrojem a jednotkami
 *
 * Bily seznam, ne cerny: co tu neni, neprojde. Kdyz nekdo zada sluzbu,
 * kterou nezname, odpoved je "ne".
 * ------------------------------------------------------------------- */
bool elm_service_allowed(const uint8_t *msg, uint16_t len)
{
    if (!msg || len < 1) return false;

    switch (msg[0]) {
    case 0x01:  /* OBD-II current data */
    case 0x09:  /* OBD-II vehicle information */
    case 0x19:  /* read DTC information */
    case 0x21:  /* read data by local identifier, stary Renault */
    case 0x22:  /* read data by identifier */
    case 0x3E:  /* tester present */
        return true;

    /* Session control je jedina sluzba na seznamu, ktera neco PRESTAVUJE,
     * a proto se u ni hlida i podfunkce. 02 je programmingSession, tedy
     * brana k nahravani programu; ta se odmita i pres to, ze sama o sobe
     * jeste nic nezapise. Povolene jsou tri, ktere tenhle vuz potrebuje
     * ke cteni: 01 default, 03 extended a C0 stary Renault.
     *
     * C0 se porovnava CELE. Bit 0x80 je v UDS suppressPositiveResponse a
     * u 01/03 se odmaskuje, jenze predchudce UDS, kterym mluvi vsechno
     * krome motoru (docs/vehicles), ho jako priznak nepouziva - 0xC0 je
     * tam samo o sobe cislo session. Odmaskovat ho tady by z C0 udelalo
     * 0x40 a stary Renault by prestal fungovat. */
    case 0x10:
        if (len < 2) return false;
        if (msg[1] == 0xC0u) return true;
        switch (msg[1] & 0x7Fu) {
        case 0x01: case 0x03: return true;
        default: return false;
        }

    default:
        return false;
    }
}

/* ---------------------------------------------------------------------
 * Adresy
 * ------------------------------------------------------------------- */

/* Odkud ceka odpoved na pozadavek poslany na e->hdr.
 *
 * Poradi: ATCRA ma prednost, protoze si ji nastroj rekl sam. Pak
 * adresa z konfigurace vozidla, ale JEN dokud nastroj nezmenil
 * hlavicku - jinak by ATSH 745 cekalo odpoved porad na 7E8.
 *
 * Zbytek se odvozuje: 7E0-7E7 odpovida o osm vys (ISO 15765-4), 7DF je
 * funkcni adresa a prvni odpoved na ni chodi z 7E8, ostatni je stary
 * Renault a tam odpovida o 0x20 vys - 745/765, 74D/76D, 740/760
 * (docs/vehicles/x82-trafic3-talento.md). */
static uint32_t rsp_id(const elm_t *e)
{
    uint32_t tx = e->hdr;

    if (e->cra) return e->cra;
    if (tx == e->cfg.tx_id && e->cfg.rx_id) return e->cfg.rx_id;
    if (tx == 0x7DF) return 0x7E8;
    if (tx >= 0x7E0 && tx <= 0x7E7) return tx + 8u;
    return (tx + 0x20u) & 0x7FFu;
}

/* ---------------------------------------------------------------------
 * Vychozi stav
 * ------------------------------------------------------------------- */
void elm_cfg_default(elm_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->tx_id   = 0x7E0;
    cfg->rx_id   = 0x7E8;
    cfg->bitrate = 500000;
}

/* Vychozi nastaveni prikazu - tedy to, co dela ATZ, ATD i nove
 * spojeni. Echo, konce radku a mezery jsou zapnute jako po zapnuti
 * skutecneho ELM327; hlavicky vypnute. */
static void defaults(elm_t *e)
{
    e->echo       = true;
    e->lf         = false;
    e->spaces     = true;
    e->headers    = false;
    e->caf        = true;
    e->proto_auto = false;
    e->at_mode    = 1;
    e->st_ms      = ELM_ST_DEFAULT_MS;
    e->hdr        = e->cfg.tx_id;
    e->cra        = 0;          /* odvozuje se, viz rsp_id() */
    e->fc_sh      = 0;
    e->fc_len     = 0;
    e->fc_mode    = 0;
    e->line_len   = 0;
    e->line_over  = false;
    e->last[0]    = '\0';
    e->busy       = false;
    e->saw_frame  = false;
}

void elm_init(elm_t *e, const elm_cfg_t *cfg)
{
    memset(e, 0, sizeof(*e));
    e->cfg = *cfg;
    defaults(e);
}

/* Zahozeni rozdelaneho prenosu a vychozi stav prikazu. Vystup se
 * NEZAHAZUJE: pri ATZ v nem lezi echo prikazu a nastroj, ktery cte az
 * k vyzve, by o nej prisel uprostred radku. */
static void reset_state(elm_t *e)
{
    isotp_abort(&e->link);
    defaults(e);
}

/* ATZ a zaroven to, co se udela nad novym spojenim. Uvitaci retezec je
 * to, podle ceho nastroj pozna, ze na druhem konci nekdo je. */
void elm_reset(elm_t *e, uint32_t now_ms)
{
    (void)now_ms;
    reset_state(e);
    out_line(e, ELM_ID);
    out_prompt(e);
}

/* ---------------------------------------------------------------------
 * Pozadavek do vozu
 * ------------------------------------------------------------------- */
static void fail(elm_t *e, const char *msg)
{
    out_line(e, msg);
    out_prompt(e);
}

static void start_request(elm_t *e, const uint8_t *msg, uint16_t len,
                          uint32_t now_ms)
{
    isotp_cfg_t c;

    /* TADY se rozhoduje, jestli neco odejde do vozidla, a nikde jinde.
     * Poradi je zamerne: nejdriv sluzba, pak rezim sbernice. Odmitnuta
     * sluzba se nema pokouset ani na sbernici, kde zrovna vysilat
     * smime. */
    if (!elm_service_allowed(msg, len)) {
        e->n_blocked++;
        fail(e, ELM_RSP_BLOCKED);
        return;
    }
    if (e->cfg.tx_allowed && !e->cfg.tx_allowed(e->cfg.user)) {
        fail(e, ELM_RSP_NOCONN);
        return;
    }

    isotp_cfg_default(&c);
    c.tx_id = e->hdr;
    c.rx_id = rsp_id(e);
    c.send  = e->cfg.send;
    c.user  = e->cfg.user;
    /* Flow control, ktery POSILAME my pri prijmu viceramcove odpovedi.
     * ATFCSD je cely ramec "30 BS STmin", takze se berou bajty 1 a 2.
     * Bez ATFCSM zustava nase vychozi "posli vsechno bez cekani" - to
     * je pro desku, ktera staci cist, nejrychlejsi volba. */
    if (e->fc_mode >= 1 && e->fc_len >= 3) {
        c.rx_bs     = e->fc_data[1];
        c.rx_st_min = e->fc_data[2];
    }
    isotp_init(&e->link, &c);

    if (!isotp_send(&e->link, msg, len, now_ms)) {
        fail(e, ELM_RSP_NOCONN);
        return;
    }
    e->busy      = true;
    e->saw_frame = false;
    e->t_req     = now_ms;
    e->n_req++;
}

/* Hotova odpoved. CAF1: jedna slozena zprava na jednom radku, protoze
 * prave to je ta vec, kterou skutecny ELM327 neumi (elm.h). CAF0: ramce
 * uz odesly jeden po druhem z elm_on_frame(), tady se zprava jen
 * vyzvedne, aby spoj nezustal v DONE. */
static void emit_response(elm_t *e)
{
    uint8_t  buf[ISOTP_BUF_SIZE];
    uint16_t n = isotp_take(&e->link, buf, (uint16_t)sizeof(buf));

    e->n_rsp++;
    if (e->caf) {
        if (e->headers) {
            out_id(e, e->link.cfg.rx_id);
            if (e->spaces) out_ch(e, ' ');
        }
        out_bytes(e, buf, n);
        out_eol(e);
    }
    out_prompt(e);
}

/* Preklad chyby prenosu na hlasku, kterou nastroje znaji. */
static const char *err_text(isotp_err_t err)
{
    switch (err) {
    case ISOTP_ERR_SEND:      return ELM_RSP_NOCONN;
    case ISOTP_ERR_TIMEOUT:
    case ISOTP_ERR_PENDING_MAX:
    case ISOTP_ERR_TXN:       return ELM_RSP_NODATA;
    case ISOTP_ERR_OVERFLOW:  return ELM_RSP_FULL;
    default:                  return ELM_RSP_CANERR;
    }
}

void elm_poll(elm_t *e, uint32_t now_ms)
{
    if (!e->busy) return;

    switch (isotp_poll(&e->link, now_ms)) {
    case ISOTP_DONE:
        e->busy = false;
        emit_response(e);
        return;
    case ISOTP_FAILED:
        e->busy = false;
        fail(e, err_text(isotp_error(&e->link)));
        return;
    case ISOTP_WAITING:
        /* AT ST: strop cekani na PRVNI ramec odpovedi. Jakmile jednotka
         * zacala odpovidat (vcetne 7F xx 78), prebira casovani ISO-TP a
         * sem uz se nesaha - jinak by se dlouhe cteni utnulo uprostred,
         * presne jak to dela skutecny dongle s vychozim ST. */
        if (!e->saw_frame && now_ms - e->t_req >= e->st_ms) {
            isotp_abort(&e->link);
            e->busy = false;
            fail(e, ELM_RSP_NODATA);
        }
        return;
    default:
        return;
    }
}

void elm_on_frame(elm_t *e, uint32_t id, const uint8_t *data, uint8_t dlc,
                  uint32_t now_ms)
{
    if (e->busy && id == e->link.cfg.rx_id) {
        e->saw_frame = true;
        /* CAF0: nastroj chce ramce tak, jak prisly, vcetne bajtu PCI.
         * Tohle je jedine misto, kde se chovame jako skutecny dongle -
         * kdo si o to vylozene rekne, dostane syrove ramce. */
        if (!e->caf) {
            if (e->headers) {
                out_id(e, id);
                if (e->spaces) out_ch(e, ' ');
            }
            out_bytes(e, data, dlc);
            out_eol(e);
        }
    }
    isotp_on_frame(&e->link, id, data, dlc, now_ms);
}

/* ---------------------------------------------------------------------
 * Prikazy AT
 * ------------------------------------------------------------------- */
static bool eq(const char *a, const char *b) { return strcmp(a, b) == 0; }

static bool pfx(const char *s, const char *p)
{
    size_t n = strlen(p);
    return strncmp(s, p, n) == 0;
}

static int hex1(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Hexa cislo pevne delky. -1 = neni to hexa nebo je to jinak dlouhe. */
static long hex_val(const char *s, unsigned want)
{
    long v = 0;

    if (strlen(s) != want) return -1;
    for (unsigned i = 0; i < want; i++) {
        int d = hex1(s[i]);
        if (d < 0) return -1;
        v = (v << 4) | d;
    }
    return v;
}

/* Cislo protokolu, ktere odpovida tomu, co deska opravdu ma. Jedna
 * sbernice, 11 bitu, rychlost z konfigurace vozidla. */
static int proto_num(const elm_t *e)
{
    return e->cfg.bitrate == 250000 ? 8 : 6;
}

/* true = prikaz jsem vyridil (vcetne vypsani odpovedi), false = nerozumim. */
static bool at_exec(elm_t *e, const char *c)
{
    char tmp[48];

    /* ---- reset a vychozi stav ----
     * Vyzvu dopisuje process(), stejne jako u kazdeho jineho prikazu. */
    if (eq(c, "Z") || eq(c, "WS")) {
        reset_state(e);
        out_line(e, ELM_ID);
        return true;
    }
    if (eq(c, "D")) { defaults(e); out_line(e, ELM_RSP_OK); return true; }

    /* ---- kdo jsem ---- */
    if (eq(c, "I"))  { out_line(e, ELM_ID);   return true; }
    if (eq(c, "@1")) { out_line(e, ELM_DESC); return true; }

    /* ---- podoba odpovedi ---- */
    if (eq(c, "E0") || eq(c, "E1")) { e->echo    = c[1] == '1'; goto ok; }
    if (eq(c, "L0") || eq(c, "L1")) { e->lf      = c[1] == '1'; goto ok; }
    if (eq(c, "S0") || eq(c, "S1")) { e->spaces  = c[1] == '1'; goto ok; }
    if (eq(c, "H0") || eq(c, "H1")) { e->headers = c[1] == '1'; goto ok; }
    if (eq(c, "CAF0") || eq(c, "CAF1")) { e->caf = c[3] == '1'; goto ok; }

    /* ---- casovani ---- */
    if (eq(c, "AT0") || eq(c, "AT1") || eq(c, "AT2")) {
        /* Adaptivni casovani si jen pamatujeme. Nase cekani urcuje
         * ISO-TP podle normy, takze neni co zrychlovat - a predstirat
         * zmenu by znamenalo tvrdit neco o rychlosti, co neplati. */
        e->at_mode = (uint8_t)(c[2] - '0');
        goto ok;
    }
    if (pfx(c, "ST")) {
        long v = hex_val(c + 2, 2);
        if (v < 0) return false;
        /* ELM327: nasobek 4 ms, 00 = vychozi hodnota. */
        uint32_t ms = v == 0 ? ELM_ST_DEFAULT_MS : (uint32_t)v * 4u;
        if (ms < ELM_ST_MIN_MS) ms = ELM_ST_MIN_MS;
        if (ms > ELM_ST_MAX_MS) ms = ELM_ST_MAX_MS;
        e->st_ms = (uint16_t)ms;
        goto ok;
    }

    /* ---- protokol ---- */
    if (pfx(c, "SP") || pfx(c, "TP")) {
        const char *p = c + 2;
        bool au = *p == 'A';
        if (au) p++;
        if (strlen(p) != 1) return false;
        /* Deska ma jeden radic TWAI, 11 bitu, rychlost z konfigurace.
         * Cokoliv jineho nez "nas" protokol a automat se odmita - OK na
         * protokol, ktery neumime, by byl slib naprazdno. */
        if (*p != '0' && hex1(*p) != proto_num(e)) return false;
        e->proto_auto = au || *p == '0';
        goto ok;
    }
    if (eq(c, "DP")) {
        snprintf(tmp, sizeof(tmp), "%sISO 15765-4 (CAN 11/%u)",
                 e->proto_auto ? "AUTO, " : "",
                 (unsigned)(e->cfg.bitrate / 1000u));
        out_line(e, tmp);
        return true;
    }
    if (eq(c, "DPN")) {
        snprintf(tmp, sizeof(tmp), "%s%d", e->proto_auto ? "A" : "",
                 proto_num(e));
        out_line(e, tmp);
        return true;
    }

    /* ---- adresy ---- */
    if (pfx(c, "SH")) {
        long v = hex_val(c + 2, 3);
        /* Sest cifer je 29bitova hlavicka. Tenhle radic jede na 11
         * bitech, takze se to odmita misto toho, aby se pak mlcelo. */
        if (v < 0) return false;
        e->hdr = (uint32_t)v;
        goto ok;
    }
    if (eq(c, "CRA")) { e->cra = 0; goto ok; }      /* zpet na odvozenou */
    if (pfx(c, "CRA")) {
        long v = hex_val(c + 3, 3);
        if (v < 0) return false;
        e->cra = (uint32_t)v;
        goto ok;
    }

    /* ---- flow control ----
     * FCSH se prijme a pamatuje, ale nase flow control odchazi vzdy na
     * hlavicce pozadavku: ISO-TP spoj zna jednu adresu pro oba smery.
     * Nastroje ho nastavuji na tutez hodnotu jako ATSH, takze se to
     * projevi jen tomu, kdo si vymysli jinou - a ten se to docte tady. */
    if (pfx(c, "FCSH")) {
        long v = hex_val(c + 4, 3);
        if (v < 0) return false;
        e->fc_sh = (uint32_t)v;
        goto ok;
    }
    if (pfx(c, "FCSD")) {
        const char *p = c + 4;
        size_t n = strlen(p);
        if (n == 0 || (n & 1) || n > sizeof(e->fc_data) * 2) return false;
        for (size_t i = 0; i < n; i += 2) {
            int hi = hex1(p[i]), lo = hex1(p[i + 1]);
            if (hi < 0 || lo < 0) return false;
            e->fc_data[i / 2] = (uint8_t)((hi << 4) | lo);
        }
        e->fc_len = (uint8_t)(n / 2);
        goto ok;
    }
    if (pfx(c, "FCSM")) {
        if (strlen(c + 4) != 1 || c[4] < '0' || c[4] > '2') return false;
        e->fc_mode = (uint8_t)(c[4] - '0');
        goto ok;
    }

    /* ---- napeti ----
     * Deska napeti zmerit NEUMI: z VIN, VBUS, 5V ani VBAT nevede zadny
     * delic na pin s prevodnikem (docs/findings.md, "Pins"). Cislo tedy
     * bud prijde z jednotky, nebo se poctive rekne, ze zadne nemame.
     * Vymyslet "12.6V" by znamenalo poslat nastroji udaj, ktery nikdo
     * nezmeril - presne ta jedina vec, kterou tahle aplikace delat
     * nesmi. */
    if (eq(c, "RV")) {
        float v = 0.0f;
        if (e->cfg.volts && e->cfg.volts(e->cfg.user, &v)) {
            snprintf(tmp, sizeof(tmp), "%.1fV", (double)v);
            out_line(e, tmp);
        } else {
            out_line(e, ELM_RSP_NODATA);
        }
        return true;
    }

    /* ---- nic nemenici potvrzeni ----
     * Nastroje je posilaji v uvodni sekvenci a odpoved '?' by nektere
     * zastavila. Nic z toho u nas nic neprestavuje: dlouhe zpravy
     * umime vzdy, pamet na nastaveni nemame a zavrit protokol znamena
     * jen zahodit rozdelany prenos. */
    if (eq(c, "AL") || eq(c, "NL") || eq(c, "M0") || eq(c, "M1")) goto ok;
    if (eq(c, "PC")) {
        isotp_abort(&e->link);
        e->busy = false;
        goto ok;
    }

    return false;

ok:
    out_line(e, ELM_RSP_OK);
    return true;
}

/* ---------------------------------------------------------------------
 * Prikaz s daty
 * ------------------------------------------------------------------- */

/* Nejdelsi pozadavek, ktery od nastroje prijmeme. Skutecny ELM327 umi
 * sedm bajtu, tedy jeden ramec; my slozime i delsi, protoze ISO-TP
 * umime - takze treba "22 F190 F191 F192" projde. Strop je delka
 * radku, ne vlastnost protokolu. */
#define ELM_REQ_MAX 32

static void data_exec(elm_t *e, const char *c, uint32_t now_ms)
{
    uint8_t  msg[ELM_REQ_MAX];
    size_t   n = strlen(c);
    uint16_t len = 0;

    /* Posledni licha cifra je u ELM327 pocet ocekavanych odpovedi
     * ("0100 1"), ne data. Zahodi se. */
    if (n & 1) n--;
    if (n < 2 || n / 2 > sizeof(msg)) { fail(e, ELM_RSP_HUH); return; }

    for (size_t i = 0; i < n; i += 2) {
        int hi = hex1(c[i]), lo = hex1(c[i + 1]);
        if (hi < 0 || lo < 0) { fail(e, ELM_RSP_HUH); return; }
        msg[len++] = (uint8_t)((hi << 4) | lo);
    }

    /* CAF0: nastroj si ramce sklada sam, takze prvni bajt je PCI.
     * Propousti se JEN single frame. Rucne slozeny first frame by
     * znamenal, ze si klient muze poslat libovolne dlouhy obsah po
     * kouscich, a filtr sluzeb by pak kontroloval jen jeho prvni
     * kousek - to je presne ta dira, kterou tu mit nechceme. */
    if (!e->caf) {
        uint8_t plen = (uint8_t)(msg[0] & 0x0Fu);
        if ((msg[0] & 0xF0u) != 0x00u || plen == 0 || plen > 7 ||
            plen + 1 > len) {
            fail(e, ELM_RSP_HUH);
            return;
        }
        start_request(e, msg + 1, plen, now_ms);
        return;
    }

    start_request(e, msg, len, now_ms);
}

/* ---------------------------------------------------------------------
 * Jeden radek od klienta
 * ------------------------------------------------------------------- */
static void process(elm_t *e, uint32_t now_ms)
{
    char cmd[ELM_LINE_MAX + 1];
    uint16_t k = 0;

    e->n_cmd++;

    if (e->echo) {
        out_str(e, e->line);
        out_eol(e);
    }
    if (e->line_over) {            /* delsi nez ELM_LINE_MAX, nedava smysl */
        e->line_over = false;
        fail(e, ELM_RSP_HUH);
        return;
    }

    /* Mezery pryc, mala pismena na velka - presne jako dongle, protoze
     * nastroje posilaji "AT SP 6" i "atsp6". */
    for (uint16_t i = 0; e->line[i]; i++) {
        char ch = e->line[i];
        if (ch == ' ' || ch == '\t') continue;
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        cmd[k++] = ch;
    }
    cmd[k] = '\0';

    /* Prazdny radek opakuje posledni prikaz. Nektere nastroje tak
     * ctou jeden PID dokola a usetri tim bajty na lince. */
    if (!k) {
        if (!e->last[0]) { fail(e, ELM_RSP_HUH); return; }
        snprintf(cmd, sizeof(cmd), "%s", e->last);
        k = (uint16_t)strlen(cmd);
    } else {
        snprintf(e->last, sizeof(e->last), "%s", cmd);
    }

    /* Vyzva patri ke KAZDE odpovedi na prikaz AT, at dopadl jakkoliv -
     * nastroj cte az k ni a bez ni by cekal navzdy. U prikazu s daty ji
     * dopise az odpoved z vozu, nebo chyba, protoze do te doby je
     * emulace zamestnana a dalsi prikaz prijmout nema. */
    if (k >= 2 && cmd[0] == 'A' && cmd[1] == 'T') {
        if (!at_exec(e, cmd + 2)) out_line(e, ELM_RSP_HUH);
        out_prompt(e);
        return;
    }
    data_exec(e, cmd, now_ms);
}

void elm_rx(elm_t *e, const char *in, uint16_t n, uint32_t now_ms)
{
    for (uint16_t i = 0; i < n; i++) {
        char ch = in[i];

        /* Znak behem prenosu ho prerusi, stejne jako u ELM327. Nastroj
         * to pouziva jako "uz to nechci"; znak se nezahazuje, takze
         * prerusit a hned poslat dalsi prikaz je jedna operace. */
        if (e->busy) {
            isotp_abort(&e->link);
            e->busy = false;
            fail(e, ELM_RSP_STOPPED);
        }

        if (ch == '\n' || ch == '\0') continue;   /* LF neni konec radku */
        if (ch == '\r') {
            e->line[e->line_len] = '\0';
            process(e, now_ms);
            e->line_len = 0;
            continue;
        }
        if (e->line_len >= ELM_LINE_MAX) { e->line_over = true; continue; }
        e->line[e->line_len++] = ch;
    }
}

/* =====================================================================
 * Soket
 *
 * Prelozi se jen na desce. Vsechno nad touto carou je cista logika a
 * preklada se i na hostiteli - viz test/elm_test.c.
 *
 * ZADNA VLASTNI ULOHA. Sokety jsou neblokujici a elm_net_tick() se vola
 * z hlavni smycky, tedy z TEHOZ vlakna jako can_poll() a
 * diag_poller_poll(). Ovladac TWAI i ISO-TP maji jednoho pisare
 * (can/twai.h) a vlastni uloha by k nim potrebovala zamek, ktery by se
 * zaviral pod LVGL - a displej ma prednost. Cena je zpozdeni jedne
 * otocky smycky, tedy 20 ms. Proti seriove lince skutecneho ELM327 na
 * 38400 Bd, kde samotna odpoved o deseti bajtech trva pres 8 ms, je to
 * nic.
 *
 * JEDEN KLIENT. Rozbor je v elm.h: emulovane zarizeni je jedna seriova
 * linka s jednim stavem a pod ni je sbernice, kde stejne bezi jedna
 * otazka po druhe. Druhe spojeni se prijme, dostane jeden radek
 * vysvetleni a zavre se - mlcici port by vypadal jako porucha site.
 * ===================================================================== */
#if CANOGRAF_MOD_ELM327

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>

#include "esp_log.h"

static const char *TAG = "canograf-elm";

/* Co dostane druhy zajemce. Konec radku je CR jako vsude jinde v tomhle
 * protokolu, at to nerozhodi nastroj, ktery cte po radcich. */
#define ELM_BUSY_MSG "BUSY - ONE CLIENT ONLY\r"

#define ELM_IN_MAX   128    /* kolik bajtu od klienta na jednu otocku */
#define ELM_OUT_MAX  256    /* davka pro send(), viz s_wr nize */

static int      s_listen = -1;
static int      s_client = -1;
static uint16_t s_port;
static elm_t    s_elm;

/* Rozeslana davka. send() na neblokujicim soketu smi vzit jen cast,
 * takze se text NEJDRIV vyzvedne sem a teprve odsud ubyva - jinak by
 * neodeslany zbytek propadl. */
static char     s_wr[ELM_OUT_MAX];
static uint16_t s_wr_len;
static uint16_t s_wr_off;

static bool set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);

    return fl >= 0 && fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0;
}

static void client_close(void)
{
    if (s_client >= 0) close(s_client);
    s_client = -1;
    s_wr_len = 0;
    s_wr_off = 0;
}

bool elm_net_start(const elm_net_cfg_t *cfg)
{
    struct sockaddr_in a;
    elm_cfg_t ec;
    int yes = 1;

    if (s_listen >= 0) return true;
    if (!cfg || !cfg->send) {
        ESP_LOGE(TAG, "bez transportu nema co posilat");
        return false;
    }

    elm_cfg_default(&ec);
    ec.tx_id      = cfg->tx_id ? cfg->tx_id : ec.tx_id;
    ec.rx_id      = cfg->rx_id ? cfg->rx_id : ec.rx_id;
    ec.bitrate    = cfg->bitrate ? cfg->bitrate : ec.bitrate;
    ec.send       = cfg->send;
    ec.user       = cfg->user;
    ec.tx_allowed = cfg->tx_allowed;
    ec.volts      = cfg->volts;
    elm_init(&s_elm, &ec);

    s_port = cfg->port ? cfg->port : (uint16_t)CANOGRAF_ELM_PORT;

    s_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_listen < 0) {
        ESP_LOGE(TAG, "socket: %d", errno);
        return false;
    }
    setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port        = htons(s_port);
    if (bind(s_listen, (struct sockaddr *)&a, sizeof(a)) != 0 ||
        listen(s_listen, 2) != 0 || !set_nonblock(s_listen)) {
        ESP_LOGE(TAG, "port %u se neotevrel: %d", (unsigned)s_port, errno);
        close(s_listen);
        s_listen = -1;
        s_port   = 0;
        return false;
    }
    ESP_LOGI(TAG, "ELM327 na portu %u, jen cteni", (unsigned)s_port);
    return true;
}

void elm_net_stop(void)
{
    client_close();
    if (s_listen >= 0) close(s_listen);
    s_listen = -1;
    s_port   = 0;
}

bool elm_net_running(void) { return s_listen >= 0; }
bool elm_net_client(void)  { return s_client >= 0; }
uint16_t elm_net_port(void) { return s_listen >= 0 ? s_port : 0u; }
uint32_t elm_net_requests(void) { return s_elm.n_req; }
uint32_t elm_net_blocked(void)  { return s_elm.n_blocked; }

/* Prijeti spojeni. Druhemu zajemci se rekne proc, az potom se zavre. */
static void accept_one(uint32_t now_ms)
{
    int fd = accept(s_listen, NULL, NULL);

    if (fd < 0) return;
    if (s_client >= 0) {
        send(fd, ELM_BUSY_MSG, sizeof(ELM_BUSY_MSG) - 1u, 0);
        close(fd);
        return;
    }
    if (!set_nonblock(fd)) {
        close(fd);
        return;
    }
    s_client = fd;
    s_wr_len = 0;
    s_wr_off = 0;
    /* Nove spojeni je nove zarizeni: vychozi stav prikazu a uvitani,
     * podle ktereho nastroj pozna, ze na druhem konci nekdo je. */
    elm_reset(&s_elm, now_ms);
}

/* Odesle, co jde. Vraci false, kdyz spojeni padlo. */
static bool flush_out(void)
{
    while (1) {
        int n;

        if (s_wr_off >= s_wr_len) {
            s_wr_off = 0;
            s_wr_len = elm_take(&s_elm, s_wr, (uint16_t)sizeof(s_wr));
            if (!s_wr_len) return true;
        }
        n = (int)send(s_client, s_wr + s_wr_off,
                      (size_t)(s_wr_len - s_wr_off), 0);
        if (n > 0) {
            s_wr_off = (uint16_t)(s_wr_off + n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return true;
        return false;       /* spojeni je pryc */
    }
}

void elm_net_tick(uint32_t now_ms)
{
    char in[ELM_IN_MAX];
    int n;

    if (s_listen < 0) return;

    accept_one(now_ms);
    if (s_client < 0) return;

    if (!flush_out()) { client_close(); return; }

    /* Cte se, az kdyz je odchozi text pryc. Je to protitlak: nastroj,
     * ktery se pta rychleji, nez stihame odpovidat, nam tim nenafoukne
     * frontu - prestane se od nej cist a TCP si to vyridi samo. */
    if (s_wr_off < s_wr_len || elm_pending(&s_elm)) {
        elm_poll(&s_elm, now_ms);
        return;
    }

    n = (int)recv(s_client, in, sizeof(in), 0);
    if (n > 0) {
        elm_rx(&s_elm, in, (uint16_t)n, now_ms);
    } else if (n == 0) {
        client_close();         /* klient zavesil */
        return;
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        client_close();
        return;
    }

    elm_poll(&s_elm, now_ms);
    if (!flush_out()) client_close();
}

/* Ramce ze sbernice. Vlastni kurzor si drzi volajici (src/main.cpp) -
 * can_next() ho ma pro kazdeho ctenare zvlast, takze odposlech v UI ani
 * poller o ramec neprijdou. */
void elm_net_on_frame(uint32_t id, const uint8_t *data, uint8_t dlc,
                      uint32_t now_ms)
{
    if (s_listen < 0 || s_client < 0) return;
    elm_on_frame(&s_elm, id, data, dlc, now_ms);
}

#endif /* CANOGRAF_MOD_ELM327 */
