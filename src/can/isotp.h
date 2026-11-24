/*
 * isotp.h - skladani a rozkladani diagnostickych zprav, ISO 15765-2
 *
 * Cista stavova logika. Nezna TWAI, nezna ESP-IDF, nezna hodiny: jeden
 * ramec odesila pres callback a aktualni cas dostava v parametru. Diky
 * tomu se cela vrstva da otestovat na hostiteli bez desky a bez vozidla
 * - viz test/isotp_test.c a test/README.md.
 *
 * Co to umi:
 *
 *   - single frame tam i zpet
 *   - first frame + consecutive frames pri odesilani, vcetne cekani na
 *     flow control po KAZDEM ramci, kdyz si jednotka rekne o block size 1
 *   - prijem viceramcove odpovedi vcetne odeslani naseho flow control
 *   - 7F xx 78 (responsePending) neni chyba: ceka se dal s delsim limitem
 *
 * Pouziti (polovicni duplex, jak to dela diagnostika):
 *
 *     isotp_cfg_t cfg; isotp_cfg_default(&cfg);
 *     cfg.tx_id = 0x7E0; cfg.rx_id = 0x7E8;
 *     cfg.send  = can_isotp_send;     // z src/can/twai.h
 *     isotp_init(&link, &cfg);
 *
 *     isotp_send(&link, req, req_len, now_ms());
 *     ... v cyklu:
 *     isotp_poll(&link, now_ms());                 // casovace a dalsi CF
 *     isotp_on_frame(&link, id, d, dlc, now_ms()); // kazdy prijaty ramec
 *     if (isotp_state(&link) == ISOTP_DONE) {
 *         uint16_t n = isotp_take(&link, buf, sizeof buf);
 *     }
 *
 * Spoj si NIC neplanuje sam: kdo nevola isotp_poll(), tomu prenos stoji.
 * Je to zamerne - bez vlastniho vlakna a bez vlastniho casovace se to
 * chova stejne na desce i v testu.
 *
 * Tenhle soubor smi zaviset jen na stdbool/stdint/stddef.
 */
#ifndef CAN_ISOTP_H
#define CAN_ISOTP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Nejdelsi zprava, kterou spoj slozi nebo odesle. ISO 15765-2 dovoluje
 * 4095 bajtu, tolik ale nepotrebujeme: nejdelsi zname bloky teto
 * platformy maji 53 bajtu (21 46) a seznam chyb z 19 02 par stovek.
 * Kazdy spoj drzi dva takove buffery, takze to neni zadarmo. */
#ifndef ISOTP_BUF_SIZE
#define ISOTP_BUF_SIZE 512
#endif

/* ------------------------------------------------------------------
 * Casovani
 *
 * Hodnoty podle ISO 15765-2, krome N_Cs, ktera je nase vlastni pojistka.
 *
 * N_Cs je doba od prijeti flow control (nebo od odeslani minuleho
 * consecutive frame) do odeslani toho dalsiho. Jednotka ceka N_Cr, coz
 * je typicky 1 s, ale v praxi to vzdalo uz pod 200 ms - pri praci s
 * ELM327 to stalo cas, protoze dongle s "AT ST FF" ceka po kazdem ramci
 * skoro sekundu a prenos tise umiral. Proto tu je tvrdy strop 150 ms:
 * kdyz nas hlavni smycka tak dlouho nepusti ke slovu, prenos je stejne
 * po smrti a je lepsi to rict nahlas nez cekat na timeout.
 * ------------------------------------------------------------------ */
#define ISOTP_N_BS_MS       1000u   /* cekani na flow control od jednotky */
#define ISOTP_N_CR_MS       1000u   /* cekani na dalsi consecutive frame */
#define ISOTP_N_CS_MAX_MS    150u   /* nejdelsi nase pauza mezi ramci */
#define ISOTP_P2_MS         1000u   /* cekani na prvni ramec odpovedi */
#define ISOTP_P2_STAR_MS    5000u   /* totez po 7F xx 78 */
#define ISOTP_FC_WAIT_MAX      8u   /* kolik FS=WAIT za sebou jeste snesem */

/* Dve nezavisle pojistky proti prenosu, ktery nikdy neskonci.
 *
 * Jednotka smi odpovidat 7F xx 78 a tim si rikat o dalsich P2*. Kdyz to
 * dela porad dokola, bez stropu by spoj zustal navzdy v ISOTP_WAITING:
 * UI by neuvidelo ani odpoved, ani chybu, jen ticho. Na X82 to neni
 * teorie - plny seznam chyb z 19 02 i routine control pro regeneraci
 * DPF posilaji 0x78 opakovane.
 *
 * Strop poctu je na opakovane 0x78, celkovy cas je na VSECHNO ostatni
 * (pomalu kapajici pendingy, stridani prijmu a odesilani). Jsou schvalne
 * oddelene: kazda chyti jiny zpusob, jak prenos uvazne. */
#define ISOTP_PENDING_MAX     16u   /* kolik 7F xx 78 za jeden pozadavek */
#define ISOTP_TXN_MAX_MS   30000u   /* nejdelsi cely prenos, od isotp_send */

/* Zaporna odpoved "requestCorrectlyReceived-ResponsePending" */
#define ISOTP_NRC_PENDING   0x78u

/* ------------------------------------------------------------------
 * Stavy a chyby
 * ------------------------------------------------------------------ */
typedef enum {
    ISOTP_IDLE = 0,    /* nic se nedeje */
    ISOTP_SENDING,     /* odesilame, vcetne cekani na flow control */
    ISOTP_WAITING,     /* odeslano, cekame na odpoved */
    ISOTP_RECEIVING,   /* skladame viceramcovou zpravu */
    ISOTP_DONE,        /* zprava lezi v bufferu, vyzvedni isotp_take() */
    ISOTP_FAILED       /* duvod rekne isotp_error() */
} isotp_state_t;

typedef enum {
    ISOTP_OK = 0,
    ISOTP_ERR_BUSY,         /* predchozi prenos jeste bezi */
    ISOTP_ERR_TOO_LONG,     /* zprava se nevejde do ISOTP_BUF_SIZE */
    ISOTP_ERR_SEND,         /* callback odmitl ramec (fronta, listen-only) */
    ISOTP_ERR_NO_FC,        /* flow control nedosel do N_Bs */
    ISOTP_ERR_FC_OVERFLOW,  /* jednotka hlasi FS=2, zprava se ji nevejde */
    ISOTP_ERR_FC_WAIT,      /* prilis mnoho FS=1 za sebou */
    ISOTP_ERR_FC_BAD,       /* neznamy flow status */
    ISOTP_ERR_N_CS,         /* nestihli jsme odeslat CF do 150 ms */
    ISOTP_ERR_NO_CF,        /* dalsi consecutive frame nedosel do N_Cr */
    ISOTP_ERR_SEQ,          /* spatne poradove cislo CF */
    ISOTP_ERR_CF_SHORT,     /* CF kratsi nez sedm bajtu, a neni posledni */
    ISOTP_ERR_TIMEOUT,      /* jednotka neodpovedela do P2 / P2* */
    ISOTP_ERR_OVERFLOW,     /* odpoved se nevejde do naseho bufferu */
    ISOTP_ERR_PENDING_MAX,  /* 7F xx 78 porad dokola, viz ISOTP_PENDING_MAX */
    ISOTP_ERR_TXN           /* cely prenos prekrocil ISOTP_TXN_MAX_MS */
} isotp_err_t;

/* ------------------------------------------------------------------
 * Odeslani jednoho ramce
 *
 * Tohle je jediny bod, kterym se vrstva dotyka hardwaru. V firmwaru sem
 * patri can_isotp_send() z src/can/twai.h, v testu nahrada, ktera ramce
 * jen uklada do pole. Vraci false, kdyz ramec neodesel (plna fronta,
 * listen-only rezim) - prenos tim skonci chybou ISOTP_ERR_SEND.
 * ------------------------------------------------------------------ */
typedef bool (*isotp_send_fn)(void *user, uint32_t id,
                              const uint8_t *data, uint8_t dlc);

typedef struct {
    uint32_t      tx_id;      /* adresa, na kterou posilame (napr. 0x7E0) */
    uint32_t      rx_id;      /* adresa, ze ktere posloucham (0x7E8) */
    isotp_send_fn send;       /* odeslani ramce, povinne */
    void         *user;       /* prvni parametr callbacku */
    uint8_t       rx_bs;      /* nas flow control: block size, 0 = posli vse */
    uint8_t       rx_st_min;  /* nas flow control: separace, kodovani ISO */
    bool          pad;        /* doplnovat ramce na 8 bajtu */
    uint8_t       pad_byte;   /* cim doplnovat */
} isotp_cfg_t;

/* ------------------------------------------------------------------
 * Spoj
 *
 * Struktura je verejna kvuli statickemu umisteni (zadny malloc), ale
 * polozky patri implementaci - cti je pres funkce nize.
 * ------------------------------------------------------------------ */
typedef struct {
    isotp_cfg_t   cfg;
    isotp_state_t state;
    isotp_err_t   err;
    bool          wait_fc;       /* ve stavu SENDING: stojime na FC */
    uint16_t      pending;       /* 7F xx 78 v TOMTO prenosu */
    uint32_t      t_txn_late;    /* dokdy musi cely prenos skoncit */

    uint8_t  tx_buf[ISOTP_BUF_SIZE];
    uint16_t tx_len;
    uint16_t tx_sent;
    uint8_t  tx_sn;              /* poradove cislo dalsiho CF, 0..15 */
    uint8_t  tx_bs;              /* block size z FC, 0 = bez omezeni */
    uint8_t  tx_bs_left;         /* kolik CF jeste smime poslat bez FC */
    uint8_t  tx_st_min;          /* separace v ms, uz prelozena */
    uint8_t  fc_waits;           /* kolik FS=WAIT prislo za sebou */
    uint32_t t_cf_ready;         /* nejdriv smime poslat dalsi CF */
    uint32_t t_cf_late;          /* nejpozdeji, jinak ISOTP_ERR_N_CS */
    uint32_t t_fc_late;          /* dokdy musi dojit flow control */

    uint8_t  rx_buf[ISOTP_BUF_SIZE];
    uint16_t rx_len;             /* ohlasena delka zpravy */
    uint16_t rx_got;             /* kolik uz mame */
    uint8_t  rx_sn;              /* ocekavane poradove cislo */
    uint8_t  rx_bs_left;         /* do dalsiho naseho FC */
    uint32_t t_rx_late;          /* P2, P2* nebo N_Cr */

    uint32_t n_msg_tx;           /* citace pro UI a diagnostiku */
    uint32_t n_msg_rx;
    uint32_t n_pending;          /* kolikrat prislo 7F xx 78 */
    uint32_t n_err;
} isotp_link_t;

/* ------------------------------------------------------------------
 * Rozhrani
 * ------------------------------------------------------------------ */

/* Rozumne vychozi nastaveni: zadne adresy, FC "posli vse bez cekani",
 * doplnovani ramcu na 8 bajtu nulami. */
void isotp_cfg_default(isotp_cfg_t *cfg);

/* Vynuluje spoj a prevezme nastaveni. Volat jednou pred pouzitim. */
void isotp_init(isotp_link_t *l, const isotp_cfg_t *cfg);

/* Zahodi rozdelany prenos a vrati spoj do ISOTP_IDLE. Citace zustavaji. */
void isotp_abort(isotp_link_t *l);

/* Zacne odesilat zpravu. Delsi nez 7 bajtu odejde jako FF + CF, zbytek
 * se dosila v isotp_poll(). Po uspesnem odeslani spoj ceka na odpoved.
 * false = nezacalo se, duvod rekne isotp_error().
 *
 * Ze stavu ISOTP_DONE se da poslat rovnou, ale nevyzvednuta odpoved tim
 * propadne - vyzvedni ji drive pres isotp_take(). Ze stavu ISOTP_FAILED
 * se posilat smi, novy pozadavek je prave ta cesta, jak se z chyby
 * dostat. */
bool isotp_send(isotp_link_t *l, const uint8_t *msg, uint16_t len,
                uint32_t now_ms);

/* Jeden prijaty CAN ramec. Ramce s jinym identifikatorem nez cfg.rx_id
 * se tise ignoruji, takze se sem da sypat cela sbernice.
 *
 * Spoj je klient: zacatek zpravy (SF nebo FF) prijima JEN ve stavu
 * ISOTP_WAITING, tedy kdyz na odpoved opravdu ceka. Opozdena odpoved na
 * predchozi otazku, provoz jine jednotky na stejne adrese ani SF
 * uprostred skladani viceramcove odpovedi tim nemuzou podvrhnout
 * vysledek - a to je dulezitejsi, nez umet odposlouchavat cizi relaci.
 * Kdo chce videt vsechno, cte kruhovy zasobnik v twai.h. */
void isotp_on_frame(isotp_link_t *l, uint32_t id, const uint8_t *data,
                    uint8_t dlc, uint32_t now_ms);

/* Casovace a odesilani dalsich consecutive frames. Volat z hlavni
 * smycky tak casto, aby mezi dvema volanimi neuplynulo 150 ms. */
isotp_state_t isotp_poll(isotp_link_t *l, uint32_t now_ms);

/* Delka slozene zpravy, dokud je spoj v ISOTP_DONE; jinak 0. Slouzi k
 * tomu, aby si volajici mohl rict o dost velky buffer DRIV, nez zpravu
 * vyzvedne. */
uint16_t isotp_msg_len(const isotp_link_t *l);

/* Vyzvedne slozenou zpravu a vrati spoj do ISOTP_IDLE.
 *
 * Vraci POCET SKUTECNE ZKOPIROVANYCH bajtu, nikdy vic nez cap - takze
 * `n = isotp_take(l, buf, sizeof buf); for (i = 0; i < n; i++)` je vzdy
 * v mezich bufferu. Kdyz se zprava nevesla, zkopiruje se jeji zacatek a
 * isotp_error() vrati ISOTP_ERR_OVERFLOW; cela delka se da zjistit
 * predem pres isotp_msg_len().
 *
 * Zahazovat spravne prijatou odpoved kvuli malemu bufferu volajiciho by
 * znamenalo poslat celou otazku do vozidla znovu, proto orezani, a ne
 * odmitnuti. Tiche to ale neni ani pred odebranim, ani po nem. */
uint16_t isotp_take(isotp_link_t *l, uint8_t *out, uint16_t cap);

static inline isotp_state_t isotp_state(const isotp_link_t *l) { return l->state; }
static inline isotp_err_t   isotp_error(const isotp_link_t *l) { return l->err; }
static inline bool isotp_busy(const isotp_link_t *l)
{
    return l->state == ISOTP_SENDING || l->state == ISOTP_WAITING ||
           l->state == ISOTP_RECEIVING;
}

/* Kratky nazev chyby pro log. Neni to text pro uzivatele - ten patri
 * do lang/ pres T(), az nad timhle vznikne UDS klient. */
const char *isotp_err_name(isotp_err_t e);

/* Prelozi kodovani STmin z flow control na milisekundy: 0x00-0x7F jsou
 * primo ms, 0xF1-0xF9 je 100-900 us (zaokrouhlime nahoru na 1 ms),
 * cokoli jineho je podle normy 127 ms. */
uint8_t isotp_st_min_ms(uint8_t raw);

#ifdef __cplusplus
}
#endif

#endif /* CAN_ISOTP_H */
