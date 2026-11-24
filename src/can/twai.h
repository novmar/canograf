/*
 * twai.h - obal nad radicem TWAI (CAN) v ESP32-S3
 *
 * Jedna sbernice, 11bitove identifikatory, 500 kbit/s. Na desce
 * Waveshare 4.3B je transceiver TJA1051T/3 pevne na GPIO15 (TX) a
 * GPIO16 (RX); piny i rychlost ale chodi z konfigurace, aby slo postavit
 * firmware pro jinou desku nebo jine vozidlo bez sahani do kodu:
 *
 *   CANOGRAF_CAN_TX_PIN, CANOGRAF_CAN_RX_PIN   config/board/<deska>.env
 *   CANOGRAF_CAN_BITRATE                       config/vehicle/<vuz>.env
 *
 * Dva rezimy:
 *
 *   CAN_MODE_LISTEN_ONLY  radic nevysila VUBEC NIC, ani potvrzovaci bit.
 *                         Tohle je vychozi stav a jedine, co se smi
 *                         pustit na cizi vuz na slepo: pri spatne
 *                         rychlosti nebo prohozenych pinech nema sanci
 *                         rozhodit provozni sbernici.
 *   CAN_MODE_NORMAL       bezny provoz, muzeme se ptat jednotek.
 *
 * Ticho dela VYHRADNE radic, ne transceiver. Na teto desce ma TJA1051T/3
 * pin S (silent) pripojeny na zem pres R96 = 0R, takze je natvrdo v
 * normalnim rezimu a hardwarove se umlcet neda - wiki Waveshare, podle
 * ktere se CAN povoluje pres expander CH422G, pro revizi 1.1 neplati a
 * zadne prepinani na expanderu se tu proto nedela. Jedina pojistka je
 * TWAI_MODE_LISTEN_ONLY v konfiguraci radice.
 *
 * Prijate i odeslane ramce se ukladaji do jednoho kruhoveho zasobniku.
 * Odber je kurzorovy a neblokujici: kazdy ctenar si drzi vlastni kurzor,
 * takze odposlech v UI nebere ramce transportni vrstve a naopak.
 *
 * Vlakna: zapisuje jen ten, kdo vola can_poll() a can_send(), a to musi
 * byt jedno vlakno. Ctenaru muze byt vic a smi byt na jinem jadru -
 * kazdy slot je seqlock (cislo ramce se vystavuje az po naplneni a
 * ctenar ho overi pred kopii i po ni), takze ctenar bud dostane cely
 * jeden ramec, nebo nedostane nic. Co se tim NESLIBUJE: ze se ke
 * ctenari dostane kazdy ramec - kdyz zasobnik predbehne pomaleho
 * ctenare, preskoci se na nejstarsi drzeny a ztrata se spocita do
 * ring_lost. Transportni vrstve to nevadi, ta bezi ve stejnem vlakne
 * jako pumpa.
 *
 * Tenhle soubor je jedina cast src/can/, ktera zna ESP-IDF. Logika
 * ISO-TP v isotp.h ne - ta se preklada i na hostiteli.
 */
#ifndef CAN_TWAI_H
#define CAN_TWAI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Vychozi hodnoty, kdyz build_config.py nebezel (treba pri rucnim
 * prekladu jednoho souboru). Piny zamerne -1, stejne jako
 * config/default.env: jedina pravda o pinech je profil desky a
 * can_start() se spatnym pinem radsi neudela nic, nez aby sahlo na
 * nahodny pin cizi desky. */
#ifndef CANOGRAF_CAN_TX_PIN
#define CANOGRAF_CAN_TX_PIN (-1)
#endif
#ifndef CANOGRAF_CAN_RX_PIN
#define CANOGRAF_CAN_RX_PIN (-1)
#endif
#ifndef CANOGRAF_CAN_BITRATE
#define CANOGRAF_CAN_BITRATE 500000
#endif

/* Kolik poslednich ramcu drzime pro odposlech. 64 x 20 B = 1,3 kB. */
#ifndef CAN_RING_DEPTH
#define CAN_RING_DEPTH 64
#endif

typedef enum {
    CAN_MODE_LISTEN_ONLY = 0,  /* nevysila nic, bezpecne pro prvni pokus */
    CAN_MODE_NORMAL = 1
} can_mode_t;

typedef struct {
    uint32_t seq;      /* poradi od startu, roste o 1 na ramec */
    uint32_t ts_ms;    /* uptime pri zarazeni do zasobniku */
    uint32_t id;       /* 11bitovy identifikator */
    uint8_t  data[8];
    uint8_t  dlc;
    bool     tx;       /* true = poslali jsme my */
} can_frame_t;

typedef struct {
    uint32_t rx;         /* prijatych ramcu */
    uint32_t tx;         /* odeslanych ramcu */
    uint32_t tx_fail;    /* odeslani se nepovedlo (plna fronta, listen-only) */
    uint32_t rx_lost;    /* fronta ovladace pretekla */
    uint32_t ring_lost;  /* nas zasobnik pretekl, nekdo necte dost rychle */
    uint32_t bus_err;    /* chyby sbernice z radice */
    uint32_t arb_lost;   /* prohrane arbitraze */
    uint32_t bus_off;    /* kolikrat radic odpadl a byl ozivovan */
} can_stats_t;

typedef struct {
    int        tx_pin;
    int        rx_pin;
    uint32_t   bitrate;
    can_mode_t mode;
    /* Hardwarovy filtr. mask = 0 znamena "ber vsechno" (to chce
     * odposlech). Pro provoz staci propustit odpovedi jednotek:
     * id = 0x700, mask = 0x700. Bit v masce = ten bit musi sedet. */
    uint32_t   filter_id;
    uint32_t   filter_mask;
} can_cfg_t;

/* Nastaveni z maker vyse: piny desky, rychlost vozidla, listen-only,
 * bez filtru. */
void can_cfg_default(can_cfg_t *c);

/* Nahodi ovladac. false = nespravny pin, nepodporovana rychlost nebo
 * ovladac uz bezi; duvod jde do logu. */
bool can_start(const can_cfg_t *c);
void can_stop(void);
bool can_running(void);

can_mode_t can_mode(void);
/* Prepnuti rezimu za behu. Radic se musi zastavit a znovu rozjet, takze
 * se tim zahodi, co prave lezi ve fronte. Na zastavenem CAN vrati false
 * - rezim se voli uz v can_start(). Kdyz se novy rezim nerozjede, vraci
 * se puvodni nastaveni a do logu jde proc; CAN tedy nezustane mrtvy. */
bool can_set_mode(can_mode_t m);

/* Odeslani jednoho ramce. V listen-only vrati false a zvysi tx_fail -
 * rezim je ochrana, ne doporuceni. */
bool can_send(uint32_t id, const uint8_t *data, uint8_t dlc);

/* Totez v podobe, kterou chce isotp_cfg_t.send. Parametr user se
 * nepouziva (jedna sbernice). */
bool can_isotp_send(void *user, uint32_t id, const uint8_t *data, uint8_t dlc);

/* Prebere ramce z fronty ovladace do kruhoveho zasobniku, obnovi citace
 * a pripadne ozivi radic po bus-off. Vraci pocet prevzatych ramcu.
 * Vola se z hlavni smycky. */
unsigned can_poll(void);

/* Monotonni cas od startu v milisekundach. Je tu proto, aby cely CAN
 * cetl cas z jednoho mista - razitka ramcu i casovace v isotp_poll(). */
uint32_t can_now_ms(void);

/* Neblokujici odber. Kurzor si drzi volajici, vynuluje ho na 0 nebo na
 * can_seq(). Vraci false, kdyz uz nic noveho neni. Kdyz kurzor zaostal
 * za zasobnikem, preskoci na nejstarsi drzeny ramec a zvysi ring_lost. */
bool can_next(uint32_t *cursor, can_frame_t *out);

/* Nahled na ramec podle poradi, bez posouvani kurzoru - pro odposlech v
 * UI. false = tenhle uz v zasobniku neni. */
bool can_at(uint32_t seq, can_frame_t *out);

/* Kolik ramcu uz proslo celkem (seq dalsiho) a kolik jich jde jeste
 * precist zpetne. */
uint32_t can_seq(void);
unsigned can_depth(void);

/* Citace od posledniho can_stats_reset(). Resetuji se i ty, ktere
 * pochazi z radice a vynulovat nejdou - drzi se k nim zaklad, ktery se
 * odecita, takze po resetu nikdy nenaskoci stara epocha. */
const can_stats_t *can_stats(void);
void can_stats_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* CAN_TWAI_H */
