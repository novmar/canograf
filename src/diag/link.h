/*
 * link.h - je deska opravdu v aute, nebo lezi na stole?
 *
 * Stejny vzor jako zbytek src/diag: zadne hodiny, zadny hardware, cas
 * chodi v parametru. Vrstva jen sbira udalosti, ktere ji nekdo preda,
 * a odpovida na jedinou otazku - jak daleko jsme se dostali.
 *
 * ---------------------------------------------------------------
 * Proc zrovna takhle
 * ---------------------------------------------------------------
 * Prvni pripojeni k cizimu vozu je VZDY v listen-only (viz can/twai.h):
 * radic nevysila ani potvrzovaci bit, takze se nedaji poslat ani
 * 3E 00, ani 22 na znamy DID. Detekce, ktera stoji na dotazu, by v tom
 * rezimu rekla "nepripojeno" i na rozjetem aute - a to je prave ta
 * situace, kdy clovek nejvic potrebuje vedet, jestli ma spravnou
 * rychlost a spravne piny.
 *
 * Proto je zakladem PROVOZ NA SBERNICI. Na X82 jezdi po HS CAN desitky
 * ramcu za zlomek vteriny, i kdyz se nikdo na nic neptal, a prijem
 * platneho ramce zaroven dokazuje, ze sedi rychlost i piny - pri
 * spatne rychlosti radic neposklada ani jeden ramec a neprijde nic.
 * Jedna uroven navic (jednotka odpovida) pak rika to, co bez vysilani
 * zjistit nejde.
 *
 * Tri urovne:
 *
 *   DIAG_LINK_DOWN  nic nejezdi. Deska na stole, vypnute zapalovani,
 *                   spatna rychlost nebo prohozene piny.
 *   DIAG_LINK_BUS   po sbernici chodi ramce - vuz zije a posloucham ho
 *                   spravne. Vic se v listen-only zjistit NEDA a je to
 *                   nejvyssi dosazitelna uroven, dokud se nevysila.
 *   DIAG_LINK_ECU   jednotka odpovedela na nas dotaz. Tohle uz je
 *                   obousmerne spojeni, tedy normalni rezim.
 *
 * Proc se na DIAG_LINK_BUS chce vic ramcu nez jeden
 * (DIAG_BUS_MIN_FRAMES): jeden ramec muze byt nahoda - zbytek fronty
 * po prepnuti rezimu, jeden pruchozi ramec pri skoro spravne
 * rychlosti, ozivani radice po bus-off. Ctyri ramce v jednom okne uz
 * nahoda neni, a na zivem X82 jich tolik prijde do sta milisekund,
 * takze to nic nezdrzi.
 *
 * Proc se uroven zase ztraci: zapalovani se da vypnout a konektor
 * vytahnout. DIAG_BUS_IDLE_MS (1 s) je na sbernici, kde nejpomalejsi
 * periodicke ramce chodi po stovkach ms, dlouha doba. Odpoved jednotky
 * drzi dele (DIAG_ECU_IDLE_MS), protoze mezi dvema dotazy muze byt
 * klidne pauza - session se mezitim udrzuje pres 3E 00 po 2 s, takze
 * deset vterin ticha uz znamena, ze jednotka opravdu prestala
 * odpovidat.
 *
 * ---------------------------------------------------------------
 * Jak to zapojit (pozdeji, v hlavni smycce)
 * ---------------------------------------------------------------
 *     can_poll();
 *     while (can_next(&cursor, &f)) {
 *         diag_link_on_frame(&dl, f.id, f.tx, f.ts_ms);
 *         uds_on_frame(&ecu, f.id, f.data, f.dlc, f.ts_ms);
 *     }
 *     uds_poll(&ecu, now);
 *     if (uds_rsp_count(&ecu) != seen) {     // jednotka se ozvala
 *         seen = uds_rsp_count(&ecu);
 *         diag_link_on_reply(&dl, 0x7E8, now);
 *     }
 *     if (uds_error(&ecu) == UDS_ERR_TIMEOUT) diag_link_on_silence(&dl, now);
 *     diag_link_poll(&dl, now);
 *
 * Zamerne tu neni zadny ukazatel na uds_client_t ani na can: vrstva
 * nema vedet, odkud udalosti chodi, a jde tak otestovat bez obojiho.
 *
 * Tenhle soubor smi zaviset jen na stdbool/stdint.
 */
#ifndef DIAG_LINK_H
#define DIAG_LINK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Kolik ramcu za sebou (bez dire delsi nez DIAG_BUS_IDLE_MS) uzna
 * sbernici za zivou. */
#ifndef DIAG_BUS_MIN_FRAMES
#define DIAG_BUS_MIN_FRAMES 4u
#endif

/* Po jak dlouhem tichu na sbernici padame zpatky na DIAG_LINK_DOWN. */
#ifndef DIAG_BUS_IDLE_MS
#define DIAG_BUS_IDLE_MS 1000u
#endif

/* Po jak dlouhem tichu od jednotky padame z DIAG_LINK_ECU o uroven niz. */
#ifndef DIAG_ECU_IDLE_MS
#define DIAG_ECU_IDLE_MS 10000u
#endif

/* Kolik dotazu bez odpovedi za sebou staci, abychom DIAG_LINK_ECU
 * opustili drive nez po DIAG_ECU_IDLE_MS. Tri timeouty po P2 = 1 s jsou
 * asi tri vteriny - tak dlouho trva, nez se po vypnuti zapalovani
 * prestane tvarit, ze jednotka odpovida. */
#ifndef DIAG_ECU_MISS_MAX
#define DIAG_ECU_MISS_MAX 3u
#endif

typedef enum {
    DIAG_LINK_DOWN = 0,   /* nepripojeno */
    DIAG_LINK_BUS,        /* sbernice zije */
    DIAG_LINK_ECU         /* jednotka odpovida */
} diag_link_state_t;

typedef struct {
    uint8_t  state;        /* diag_link_state_t */
    uint8_t  misses;       /* dotazy bez odpovedi za sebou */
    uint16_t burst;        /* ramcu od posledniho ticha, strop na meznim poctu */
    bool     seen_frame;
    bool     seen_reply;
    uint32_t t_frame;      /* cas posledniho CIZIHO ramce */
    uint32_t t_reply;      /* cas posledni odpovedi jednotky */
    uint32_t last_rx_id;   /* ktera jednotka se ozvala naposled */
    uint32_t n_frames;     /* citace pro diagnostickou obrazovku */
    uint32_t n_replies;
    uint32_t n_silence;
} diag_link_t;

void diag_link_init(diag_link_t *l);

/* Jeden ramec ze sbernice. from_us = ramec jsme poslali my (can_frame_t.tx);
 * takovy se nepocita, vlastni vysilani o pritomnosti vozu nedokazuje nic. */
void diag_link_on_frame(diag_link_t *l, uint32_t id, bool from_us,
                        uint32_t now_ms);

/* Jednotka odpovedela. Zaporna odpoved se pocita stejne jako kladna -
 * 7F je taky dukaz, ze tam nekdo je. */
void diag_link_on_reply(diag_link_t *l, uint32_t rx_id, uint32_t now_ms);

/* Nas dotaz zustal bez odpovedi. */
void diag_link_on_silence(diag_link_t *l, uint32_t now_ms);

/* Prepocte uroven podle casu. Volat z hlavni smycky. */
diag_link_state_t diag_link_poll(diag_link_t *l, uint32_t now_ms);

static inline diag_link_state_t diag_link_state(const diag_link_t *l)
{
    return (diag_link_state_t)l->state;
}

/* Jak dlouho uz je na sbernici ticho. UINT32_MAX = jeste nikdy nic
 * neprislo; nula by v te situaci lhala. */
uint32_t diag_link_quiet_ms(const diag_link_t *l, uint32_t now_ms);

/* Kratky nazev urovne pro log. Text pro uzivatele patri do lang/. */
const char *diag_link_state_name(diag_link_state_t s);

#ifdef __cplusplus
}
#endif

#endif /* DIAG_LINK_H */
