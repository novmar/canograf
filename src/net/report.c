/*
 * report.c - obsah odpovedi serveru
 *
 * Rozbor je v net/report.h. Tady je jen tabulka velicin, vykresleni
 * jedne hodnoty a tri skladace odpovedi (meta, live, CSV).
 *
 * Co se tu NEDELA: nic se neposila na sbernici, nic se nezapisuje do
 * vrstvy diagnostiky. Vsechny ukazatele na diag_poller_t jsou const,
 * takze to nehlida jen komentar, ale prekladac.
 */
#include "net/report.h"

#include "i18n.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Pojistka, ne komentar. Tahle vrstva nesmi mit po ruce nic, cim se da
 * na sbernici zapsat - can_send() a can_isotp_send() jsou prave v
 * can/twai.h. Kdyby se sem ten soubor dostal, treba pres novy #include
 * v necem vyse, preklad skonci TADY a ne az revizi. Stoji to za vsemi
 * includy zamerne: driv by to nic nehlidalo. */
#ifdef CAN_TWAI_H
#error "net/report: can/twai.h tu nema co delat - server do vozidla nezapisuje"
#endif

/* ---------------------------------------------------------------------
 * Co se ukazuje
 *
 * Je to TOTEZ, co ukazuje displej, a pokud mozno pod stejnym popiskem:
 * popisky se berou z tychz retezcu i18n, ktere kresli panel. Kde panel
 * zadny prelozeny nema - RAIL, MAF, VNT, EGR jsou zkratky stejne ve
 * vsech jazycich a i v ui.c stoji natvrdo - stoji literal tady.
 *
 * Zadane hodnoty (sp = true) dostanou pred popisek T(STR_DEMAND), aby
 * se dvojice skutecnost/zadano nedala splest. Na panelu je rozlisuje
 * barva, kterou web uzivatele nema jak zdedit.
 *
 * Jednorazova mereni (vyrovnani vstrikovacu) tu nejsou schvalne: do
 * zadneho kola se nedostanou, takze by na webu stala navzdy na
 * pomlckach a jen davala podezrivat server.
 * ------------------------------------------------------------------- */
static const report_ch_t CH[REPORT_CH_N] = {
    /* ---- hlavni ---- */
    { DIAG_CH_RPM,      REPORT_GRP_MAIN,   0, false, "rpm",      NULL,
      STR_UNIT_RPM,          ""     },
    { DIAG_CH_SPEED,    REPORT_GRP_MAIN,   0, false, "speed",    NULL,
      STR_SET_SPEED,         "km/h" },
    { DIAG_CH_COOLANT,  REPORT_GRP_MAIN,   0, false, "coolant",  NULL,
      STR_TOP_TEMP,          "C"    },
    { DIAG_CH_ODO,      REPORT_GRP_MAIN,   0, false, "odo",      NULL,
      STR_ODOMETER,          ""     },

    /* ---- motor ---- */
    { DIAG_CH_BOOST,    REPORT_GRP_ENGINE, 0, false, "boost",    NULL,
      STR_BOOST_PRESSURE,    "mbar" },
    { DIAG_CH_BOOST_SP, REPORT_GRP_ENGINE, 0, true,  "boost_sp", NULL,
      STR_BOOST_PRESSURE,    "mbar" },
    { DIAG_CH_RAIL,     REPORT_GRP_ENGINE, 0, false, "rail",     "RAIL bar",
      -1,                    ""     },
    { DIAG_CH_RAIL_SP,  REPORT_GRP_ENGINE, 0, true,  "rail_sp",  "RAIL bar",
      -1,                    ""     },
    { DIAG_CH_VNT,      REPORT_GRP_ENGINE, 0, false, "vnt",      "VNT %",
      -1,                    ""     },
    { DIAG_CH_MAF,      REPORT_GRP_ENGINE, 0, false, "maf",      "MAF mg",
      -1,                    ""     },
    { DIAG_CH_MAF_SP,   REPORT_GRP_ENGINE, 0, true,  "maf_sp",   "MAF mg",
      -1,                    ""     },
    { DIAG_CH_EGR,      REPORT_GRP_ENGINE, 0, false, "egr",      "EGR %",
      -1,                    ""     },
    { DIAG_CH_IAT,      REPORT_GRP_ENGINE, 0, false, "iat",      NULL,
      STR_INTAKE_AIR,        ""     },

    /* ---- filtr pevnych castic ---- */
    { DIAG_CH_DPF_UP,   REPORT_GRP_DPF,    0, false, "dpf_up",   NULL,
      STR_DPF_TEMP_IN,       ""     },
    { DIAG_CH_DPF_DN,   REPORT_GRP_DPF,    0, false, "dpf_dn",   NULL,
      STR_DPF_TEMP_OUT,      ""     },
    { DIAG_CH_DPF_SP,   REPORT_GRP_DPF,    0, false, "dpf_sp",   NULL,
      STR_DPF_TEMP_TARGET,   ""     },
    { DIAG_CH_SOOT,     REPORT_GRP_DPF,    2, false, "soot",     NULL,
      STR_DPF_SOOT,          ""     },
    { DIAG_CH_CSF_LOAD, REPORT_GRP_DPF,    1, false, "csf_load", NULL,
      STR_DPF_LOAD,          ""     },
    { DIAG_CH_RGN_DUR,  REPORT_GRP_DPF,    0, false, "regen_s",  NULL,
      STR_REGEN_STATE,       "s"    },
    { DIAG_CH_RGN_END,  REPORT_GRP_DPF,    0, false, "since_s",  NULL,
      STR_DPF_SINCE,         "s"    },
    { DIAG_CH_EXH_P,    REPORT_GRP_DPF,    0, false, "exh_p",    NULL,
      STR_DPF_EXH_P,         ""     },
    { DIAG_CH_DPF_DP,   REPORT_GRP_DPF,    0, false, "dpf_dp",   NULL,
      STR_DPF_DP,            ""     },
    { DIAG_CH_DPF_DP_U, REPORT_GRP_DPF,    2, false, "dpf_dp_u", NULL,
      STR_DPF_DP_U,          ""     },
    { DIAG_CH_DPF_FROZEN, REPORT_GRP_DPF,  0, false, "frozen",   NULL,
      STR_DPF_FROZEN,        ""     },

    /* ---- baterie a dobijeni ---- */
    { DIAG_CH_BATT_I,   REPORT_GRP_BATT,   2, false, "batt_i",   NULL,
      STR_BATT_I,            ""     },
    { DIAG_CH_BATT_SOC, REPORT_GRP_BATT,   1, false, "batt_soc", NULL,
      STR_BATT_SOC,          ""     },
    { DIAG_CH_ALT_LOAD, REPORT_GRP_BATT,   0, false, "alt_load", NULL,
      STR_BATT_ALT,          ""     },
};

static const int GRP_STR[REPORT_GRP__COUNT] = {
    STR_PAGE_MAIN, STR_PAGE_ENGINE, STR_PAGE_DPF, STR_NAV_BATT,
};

int report_ch_count(void) { return REPORT_CH_N; }

const report_ch_t *report_ch(int i)
{
    return (i >= 0 && i < REPORT_CH_N) ? &CH[i] : NULL;
}

const char *report_grp_name(int g)
{
    return (g >= 0 && g < REPORT_GRP__COUNT) ? T(GRP_STR[g]) : "?";
}

void report_caption(char *out, int n, int i)
{
    const report_ch_t *c = report_ch(i);

    if (!out || n <= 0) return;
    out[0] = '\0';
    if (!c) return;

    const char *base = (c->name >= 0) ? T(c->name) : (c->lit ? c->lit : "");
    if (c->sp)
        snprintf(out, (size_t)n, "%s %s%s%s", T(STR_DEMAND), base,
                 c->unit[0] ? " " : "", c->unit);
    else
        snprintf(out, (size_t)n, "%s%s%s", base, c->unit[0] ? " " : "",
                 c->unit);
}

/* ---------------------------------------------------------------------
 * Jedna hodnota
 *
 * Jedine misto, kde na webu vznika text hodnoty, a jedine misto, kde by
 * se dalo tise zacit predstirat. Vetev DIAG_SHOW_NONE proto nema
 * pristup k zadnemu cislu - txt zustane pomlckou, se kterou doslo.
 *
 * Mutacni zkouska: do vetve DIAG_SHOW_NONE dopsat vykresleni
 * diag_poller_value(). Kontrola "neprectena hodnota neni na webu cislo"
 * v sim/lv_shot.c MUSI selhat.
 * ------------------------------------------------------------------- */
void report_value(report_val_t *out, const diag_poller_t *p, int i)
{
    const report_ch_t *c = report_ch(i);

    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->show   = DIAG_SHOW_NONE;
    out->age_ms = UINT32_MAX;
    snprintf(out->txt, sizeof(out->txt), "%s", REPORT_NONE_TXT);
    if (!c || !p) return;

    diag_show_t s = diag_poller_show(p, (int)c->ch);
    out->show = (uint8_t)s;
    switch (s) {
    case DIAG_SHOW_LIVE:
    case DIAG_SHOW_STALE:
        out->age_ms = diag_poller_age_ms(p, (int)c->ch);
        snprintf(out->txt, sizeof(out->txt), "%.*f", (int)c->dec,
                 (double)diag_poller_value(p, (int)c->ch));
        break;
    case DIAG_SHOW_REFUSED:
        out->nrc = diag_poller_nrc(p, (int)c->ch);
        snprintf(out->txt, sizeof(out->txt), "7F %02X", out->nrc);
        break;
    case DIAG_SHOW_NONE:
    default:
        break;
    }
}

/* ---------------------------------------------------------------------
 * Stav pripojeni k vozidlu
 *
 * Ta sama funkce, kterou pouziva horni lista displeje (link_state_paint
 * v ui.c), aby web a panel nemohly rict kazdy neco jineho.
 * ------------------------------------------------------------------- */
int report_link_str(const diag_poller_t *p)
{
    diag_link_state_t ls = p ? diag_link_state(diag_poller_link(p))
                             : DIAG_LINK_DOWN;

    if (ls == DIAG_LINK_ECU)  return STR_LINK_ECU;
    if (ls == DIAG_LINK_DOWN) return STR_LINK_NO_BUS;
    if (p && diag_poller_armed(p)) return STR_LINK_NO_ECU;
    return STR_LINK_LISTEN;
}

bool report_no_vehicle(const diag_poller_t *p)
{
    return report_link_str(p) != STR_LINK_ECU;
}

/* ---------------------------------------------------------------------
 * Zaznam relace
 * ------------------------------------------------------------------- */
void weblog_init(weblog_t *w, weblog_row_t *store, uint32_t cap,
                 uint32_t period_ms)
{
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->row       = store;
    w->cap       = store ? cap : 0u;
    w->period_ms = period_ms ? period_ms : 1u;
}

void weblog_sample(weblog_t *w, const diag_poller_t *p, uint32_t now_ms)
{
    if (!w || !w->row || !w->cap || !p) return;

    if (!w->started) {
        w->started = true;
        w->t0_ms   = now_ms;
        w->t_next  = now_ms;
    }
    if ((int32_t)(now_ms - w->t_next) < 0) return;
    w->t_next = now_ms + w->period_ms;

    weblog_row_t *r = &w->row[w->head % w->cap];
    r->t_ms = now_ms - w->t0_ms;
    for (int i = 0; i < REPORT_CH_N; i++) {
        diag_show_t s = diag_poller_show(p, (int)CH[i].ch);
        r->show[i] = (uint8_t)s;
        /* Hodnota plati jen u zivych a starych; u ostatnich se do pole
         * NEPISE nic, co by slo precist jako cislo. */
        r->val[i] = (s == DIAG_SHOW_LIVE || s == DIAG_SHOW_STALE)
                    ? diag_poller_value(p, (int)CH[i].ch) : 0.0f;
    }
    w->head++;
    if (w->head > w->cap) w->dropped = w->head - w->cap;
}

uint32_t weblog_rows(const weblog_t *w)
{
    if (!w || !w->cap) return 0u;
    return (w->head < w->cap) ? w->head : w->cap;
}

uint32_t weblog_first(const weblog_t *w)
{
    if (!w || !w->cap) return 0u;
    return (w->head < w->cap) ? 0u : w->head - w->cap;
}

const weblog_row_t *weblog_at(const weblog_t *w, uint32_t seq)
{
    if (!w || !w->row || !w->cap) return NULL;
    if (seq < weblog_first(w) || seq >= w->head) return NULL;
    return &w->row[seq % w->cap];
}

/* ---------------------------------------------------------------------
 * Skladani odpovedi
 *
 * Jednoduchy pripojovac, ktery si pamatuje preteceni. Oriznuta odpoved
 * se neposila vubec - polovicni JSON prohlizec bud nerozebere, nebo si
 * necha stara cisla a tvari se, ze jsou nova.
 * ------------------------------------------------------------------- */
typedef struct {
    char *b;
    int   n;
    int   at;
    bool  ovf;
} buf_t;

static void bput(buf_t *o, const char *s)
{
    if (o->ovf) return;
    while (*s) {
        if (o->at + 1 >= o->n) { o->ovf = true; return; }
        o->b[o->at++] = *s++;
    }
    o->b[o->at] = '\0';
}

static void bch(buf_t *o, char c)
{
    if (o->ovf) return;
    if (o->at + 1 >= o->n) { o->ovf = true; return; }
    o->b[o->at++] = c;
    o->b[o->at] = '\0';
}

static void bfmt(buf_t *o, const char *fmt, ...)
{
    char tmp[128];
    va_list ap;

    if (o->ovf) return;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    bput(o, tmp);
}

/* Retezec do JSON i s uvozovkami. Prelozene texty nesou apostrofy,
 * uvozovky i zalomeni radku a jmeno site muze nest cokoliv - neoseknuty
 * retezec by rozbil rozbor na strane prohlizece. Bajty nad 0x7F se
 * nechavaji byt: odpoved je UTF-8 a diakritika v nem platny JSON je. */
static void bjson(buf_t *o, const char *s)
{
    bch(o, '"');
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  bput(o, "\\\""); break;
        case '\\': bput(o, "\\\\"); break;
        case '\n': bput(o, "\\n");  break;
        case '\r': bput(o, "\\r");  break;
        case '\t': bput(o, "\\t");  break;
        default:
            if (c < 0x20) bfmt(o, "\\u%04X", (unsigned)c);
            else          bch(o, (char)c);
            break;
        }
    }
    bch(o, '"');
}

static int bdone(const buf_t *o) { return o->ovf ? -1 : o->at; }

int report_meta_json(char *out, int n)
{
    buf_t o = { out, n, 0, false };

    if (!out || n <= 0) return -1;
    out[0] = '\0';

    bput(&o, "{\"title\":");   bjson(&o, T(STR_WEB_TITLE));
    bput(&o, ",\"ro\":");      bjson(&o, T(STR_WEB_READONLY));
    bput(&o, ",\"note\":");    bjson(&o, T(STR_WEB_VIEW_NOTE));
    bput(&o, ",\"csv\":");     bjson(&o, T(STR_WEB_CSV));
    bput(&o, ",\"nolink\":");  bjson(&o, T(STR_WEB_NOLINK));
    bput(&o, ",\"nosrv\":");   bjson(&o, T(STR_WEB_NOSRV));
    bput(&o, ",\"rows\":");    bjson(&o, T(STR_WEB_LOG));
    bput(&o, ",\"drop\":");    bjson(&o, T(STR_WEB_DROP));
    bput(&o, ",\"nolog\":");   bjson(&o, T(STR_WEB_NOLOG));
    bput(&o, ",\"leg\":[");
    bjson(&o, T(STR_WEB_MISSING)); bch(&o, ',');
    bjson(&o, T(STR_WEB_LIVE));    bch(&o, ',');
    bjson(&o, T(STR_WEB_STALE));   bch(&o, ',');
    bjson(&o, T(STR_WEB_REFUSED));
    bput(&o, "],\"grp\":[");
    for (int g = 0; g < REPORT_GRP__COUNT; g++) {
        if (g) bch(&o, ',');
        bjson(&o, report_grp_name(g));
    }
    bput(&o, "],\"ch\":[");
    for (int i = 0; i < REPORT_CH_N; i++) {
        char cap[80];
        report_caption(cap, sizeof(cap), i);
        if (i) bch(&o, ',');
        bput(&o, "{\"n\":");
        bjson(&o, cap);
        bfmt(&o, ",\"g\":%u}", (unsigned)CH[i].grp);
    }
    bput(&o, "]}");
    return bdone(&o);
}

int report_live_json(char *out, int n, const diag_poller_t *p,
                     const report_net_t *net, const weblog_t *log)
{
    buf_t o = { out, n, 0, false };

    if (!out || n <= 0) return -1;
    out[0] = '\0';

    bfmt(&o, "{\"up\":%u,\"nov\":%u,\"can\":%u,\"link\":",
         net ? (unsigned)net->uptime_s : 0u,
         report_no_vehicle(p) ? 1u : 0u,
         (net && net->can_normal) ? 1u : 0u);
    bjson(&o, T(report_link_str(p)));

    bput(&o, ",\"net\":{");
    bfmt(&o, "\"ap\":%u,\"sta\":%u,\"cl\":%d,\"ssid\":",
         (net && net->ap) ? 1u : 0u,
         (net && net->sta) ? 1u : 0u,
         net ? net->clients : -1);
    bjson(&o, net ? net->ssid : "");
    bput(&o, ",\"ip\":");
    bjson(&o, net ? net->ip : "");
    bput(&o, "}");

    bfmt(&o, ",\"log\":%u,\"rows\":%u,\"drop\":%u",
         (log && log->cap) ? 1u : 0u,
         (unsigned)weblog_rows(log), log ? (unsigned)log->dropped : 0u);

    bput(&o, ",\"v\":[");
    for (int i = 0; i < REPORT_CH_N; i++) {
        report_val_t v;
        report_value(&v, p, i);
        if (i) bch(&o, ',');
        bfmt(&o, "{\"s\":%u,\"t\":", (unsigned)v.show);
        bjson(&o, v.txt);
        if (v.show == DIAG_SHOW_STALE && v.age_ms != UINT32_MAX)
            bfmt(&o, ",\"a\":%u", (unsigned)(v.age_ms / 1000u));
        bch(&o, '}');
    }
    bput(&o, "]}");
    return bdone(&o);
}

/* ---------------------------------------------------------------------
 * CSV
 *
 * Dva sloupce na velicinu: hodnota a stav. Bez toho druheho by stara
 * hodnota v tabulce vypadala stejne jako cerstva a soubor by tvrdil
 * vic, nez se zmerilo. Chybejici hodnota ma sloupec PRAZDNY - nula je
 * cislo a zadne cislo tam neprislo.
 *
 * Slova stavu jsou ASCII a neprekladaji se: cte je tabulkovy procesor,
 * ne clovek, a sloupec, ktery se jmenuje jinak podle toho, jaky jazyk
 * mela deska nastaveny, by se nedal zpracovat davkou.
 * ------------------------------------------------------------------- */
static const char *csv_state(uint8_t show)
{
    switch (show) {
    case DIAG_SHOW_LIVE:    return "live";
    case DIAG_SHOW_STALE:   return "old";
    case DIAG_SHOW_REFUSED: return "ref";
    default:                return "none";
    }
}

int report_csv_head(char *out, int n, const weblog_t *w)
{
    buf_t o = { out, n, 0, false };

    if (!out || n <= 0) return -1;
    out[0] = '\0';

    bput(&o, "# canograf session log\r\n");
    bfmt(&o, "# rows %u, dropped %u, period %u ms\r\n",
         (unsigned)weblog_rows(w),
         w ? (unsigned)w->dropped : 0u,
         w ? (unsigned)w->period_ms : 0u);
    bput(&o, "# value empty = the ECU never gave that one; "
             "state column: live / old / none / ref\r\n");
    bput(&o, "t_s");
    for (int i = 0; i < REPORT_CH_N; i++)
        bfmt(&o, ",%s,%s.s", CH[i].key, CH[i].key);
    bput(&o, "\r\n");
    return bdone(&o);
}

int report_csv_row(char *out, int n, const weblog_t *w, uint32_t seq)
{
    const weblog_row_t *r = weblog_at(w, seq);
    buf_t o = { out, n, 0, false };

    if (!out || n <= 0) return -1;
    out[0] = '\0';
    if (!r) return -1;

    bfmt(&o, "%u.%03u", (unsigned)(r->t_ms / 1000u),
         (unsigned)(r->t_ms % 1000u));
    for (int i = 0; i < REPORT_CH_N; i++) {
        bch(&o, ',');
        if (r->show[i] == DIAG_SHOW_LIVE || r->show[i] == DIAG_SHOW_STALE)
            bfmt(&o, "%.*f", (int)CH[i].dec, (double)r->val[i]);
        bch(&o, ',');
        bput(&o, csv_state(r->show[i]));
    }
    bput(&o, "\r\n");
    return bdone(&o);
}
