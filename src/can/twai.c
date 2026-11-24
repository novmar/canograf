/*
 * twai.c - obal nad radicem TWAI v ESP32-S3, viz twai.h
 *
 * Jediny soubor v src/can/, ktery zna ESP-IDF. Logika ISO-TP je v
 * isotp.c a preklada se i na hostiteli.
 */
#include "twai.h"

#include <string.h>

#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "can";

/* Kolik ramcu nejvyse prevezmeme z ovladace v jednom can_poll(). Pri
 * 500 kbit/s a plne sbernici chodi radove 2000 ramcu za sekundu; kdyby
 * se vybiralo do prazdna, hlavni smycka by prestala kreslit. Co se
 * nevybere ted, vybere se za 5 ms znovu. */
#define CAN_POLL_BUDGET 32

static can_cfg_t   s_cfg;
static can_stats_t s_stats;
static bool        s_running;
static bool        s_recovering;

/* Kruhovy zasobnik poslednich ramcu. seq zacina na 1, nula znamena
 * "prazdne misto" - diky tomu nemusi mit zasobnik zvlast priznak
 * platnosti a ctenar pozna pretoceni jedinym porovnanim. */
static can_frame_t s_ring[CAN_RING_DEPTH];
static volatile uint32_t s_head = 1u;

uint32_t can_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void ring_push(uint32_t id, const uint8_t *data, uint8_t dlc, bool tx)
{
    uint32_t seq = s_head;
    can_frame_t *slot = &s_ring[seq % CAN_RING_DEPTH];

    /* Nejdriv misto zneplatnit, pak naplnit a az nakonec vystavit cislo.
     * Ctenar si po kopii cislo overi znovu, takze z pretoceneho mista
     * nikdy neodnese slepenec ze dvou ramcu. */
    slot->seq = 0u;
    slot->ts_ms = can_now_ms();
    slot->id = id;
    slot->dlc = (dlc > 8u) ? 8u : dlc;
    memset(slot->data, 0, sizeof(slot->data));
    if (data != NULL && slot->dlc > 0u) {
        memcpy(slot->data, data, slot->dlc);
    }
    slot->tx = tx;
    slot->seq = seq;
    s_head = seq + 1u;
}

void can_cfg_default(can_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->tx_pin = CANOGRAF_CAN_TX_PIN;
    c->rx_pin = CANOGRAF_CAN_RX_PIN;
    c->bitrate = CANOGRAF_CAN_BITRATE;
    /* Vychozi je listen-only. Firmware, ktery se poprve pripoji na cizi
     * vuz, nesmi na sbernici rict ani popel. */
    c->mode = CAN_MODE_LISTEN_ONLY;
    c->filter_id = 0u;
    c->filter_mask = 0u;
}

static bool timing_for(uint32_t bitrate, twai_timing_config_t *t)
{
    switch (bitrate) {
    case 1000000u: *t = (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();   return true;
    case 800000u:  *t = (twai_timing_config_t)TWAI_TIMING_CONFIG_800KBITS(); return true;
    case 500000u:  *t = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS(); return true;
    case 250000u:  *t = (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS(); return true;
    case 125000u:  *t = (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS(); return true;
    case 100000u:  *t = (twai_timing_config_t)TWAI_TIMING_CONFIG_100KBITS(); return true;
    case 50000u:   *t = (twai_timing_config_t)TWAI_TIMING_CONFIG_50KBITS();  return true;
    default: return false;
    }
}

/* Hardwarovy filtr v rezimu jednoho filtru: u 11bitovych ramcu sedi
 * identifikator na bitech 31..21, bit 20 je RTR a nize jsou prvni dva
 * datove bajty. Jednicka v masce radice znamena "nezajima me", proto se
 * nase maska obraci a vse pod identifikatorem se propusti. */
static void filter_for(const can_cfg_t *c, twai_filter_config_t *f)
{
    if (c->filter_mask == 0u) {
        *f = (twai_filter_config_t)TWAI_FILTER_CONFIG_ACCEPT_ALL();
        return;
    }
    f->acceptance_code = (c->filter_id & 0x7FFu) << 21;
    f->acceptance_mask = ((~c->filter_mask & 0x7FFu) << 21) | 0x1FFFFFu;
    f->single_filter = true;
}

bool can_start(const can_cfg_t *c)
{
    twai_general_config_t g;
    twai_timing_config_t t;
    twai_filter_config_t f;
    esp_err_t err;

    if (s_running) {
        ESP_LOGE(TAG, "uz bezi");
        return false;
    }
    if (c == NULL) {
        return false;
    }
    if (c->tx_pin < 0 || c->rx_pin < 0) {
        /* -1 znamena "neni vybrany profil desky" - viz config/default.env.
         * Sahnout na nahodny pin cizi desky je horsi nez nejet. */
        ESP_LOGE(TAG, "neplatne piny TX=%d RX=%d, zkontroluj profil desky",
                 c->tx_pin, c->rx_pin);
        return false;
    }
    if (!timing_for(c->bitrate, &t)) {
        ESP_LOGE(TAG, "nepodporovana rychlost %u bit/s", (unsigned)c->bitrate);
        return false;
    }

    /* Rezim radice je jedina vec, ktera na teto desce drzi ticho:
     * TJA1051T/3 ma pin S na zemi pres R96 = 0R, takze transceiver je
     * natvrdo v normalnim rezimu a umlcet se neda (to, co o povolovani
     * CAN pres expander CH422G pise wiki Waveshare, pro revizi 1.1
     * neplati). TWAI_MODE_LISTEN_ONLY podle ESP-IDF "will not influence
     * the bus (No transmissions or acknowledgments)" - a prave ten
     * chybejici ACK je dulezity: na zive sbernici v aute by nas ACK na
     * ramec, ktery jsme spatne precetli (jina rychlost, prohozene piny),
     * delal chyby ostatnim jednotkam. Cokoli jineho nez CAN_MODE_NORMAL
     * konci v listen-only zamerne - omyl ma vest k tichu. */
    g = (twai_general_config_t)TWAI_GENERAL_CONFIG_DEFAULT(
            (gpio_num_t)c->tx_pin, (gpio_num_t)c->rx_pin,
            (c->mode == CAN_MODE_NORMAL) ? TWAI_MODE_NORMAL
                                         : TWAI_MODE_LISTEN_ONLY);
    /* Vychozi fronty maji po peti ramcich. Pri 500 kbit/s je to par
     * milisekund provozu a hlavni smycka kresli - pretekalo by to. */
    g.rx_queue_len = 64;
    g.tx_queue_len = 16;
    filter_for(c, &f);

    err = twai_driver_install(&g, &t, &f);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_driver_install: %s", esp_err_to_name(err));
        return false;
    }
    err = twai_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_start: %s", esp_err_to_name(err));
        twai_driver_uninstall();
        return false;
    }

    s_cfg = *c;
    s_running = true;
    s_recovering = false;
    ESP_LOGI(TAG, "TX=%d RX=%d %u bit/s %s", c->tx_pin, c->rx_pin,
             (unsigned)c->bitrate,
             (c->mode == CAN_MODE_NORMAL) ? "normal" : "listen-only");
    return true;
}

void can_stop(void)
{
    if (!s_running) {
        return;
    }
    twai_stop();
    twai_driver_uninstall();
    s_running = false;
    s_recovering = false;
}

bool can_running(void) { return s_running; }

can_mode_t can_mode(void) { return s_cfg.mode; }

bool can_set_mode(can_mode_t m)
{
    can_cfg_t next = s_cfg;

    if (!s_running) {
        /* Rezim se nastavuje pri instalaci ovladace, takze zastavenemu
         * CAN nema co prepinat. Volajici ma pouzit can_start(). */
        return false;
    }
    if (s_cfg.mode == m) {
        return true;
    }
    /* Co lezi ve frontach, se restartem zahodi - po prepnuti rezimu
     * stejne zacina jina etapa prace. */
    next.mode = m;
    can_stop();
    return can_start(&next);
}

bool can_send(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    twai_message_t msg;
    esp_err_t err;

    if (!s_running || dlc > 8u) {
        s_stats.tx_fail++;
        return false;
    }
    if (s_cfg.mode != CAN_MODE_NORMAL) {
        /* Listen-only je ochrana, ne doporuceni: nic neodejde a volajici
         * se to dozvi. */
        s_stats.tx_fail++;
        return false;
    }

    memset(&msg, 0, sizeof(msg));
    msg.identifier = id & 0x7FFu;
    msg.data_length_code = dlc;
    if (data != NULL && dlc > 0u) {
        memcpy(msg.data, data, dlc);
    }

    /* Kratke cekani staci: pri 500 kbit/s odejde ramec za 0,2 ms a delsi
     * blokovani hlavni smycky by bylo videt na obrazovce. */
    err = twai_transmit(&msg, pdMS_TO_TICKS(10));
    if (err != ESP_OK) {
        s_stats.tx_fail++;
        ESP_LOGE(TAG, "twai_transmit %03X: %s", (unsigned)id,
                 esp_err_to_name(err));
        return false;
    }
    s_stats.tx++;
    ring_push(msg.identifier, msg.data, dlc, true);
    return true;
}

bool can_isotp_send(void *user, uint32_t id, const uint8_t *data, uint8_t dlc)
{
    (void)user;
    return can_send(id, data, dlc);
}

static void pump_status(void)
{
    twai_status_info_t st;

    if (twai_get_status_info(&st) != ESP_OK) {
        return;
    }
    /* Citace radice jsou kumulativni od instalace ovladace, takze se
     * prebiraji, ne pricitaji. */
    s_stats.rx_lost = st.rx_missed_count + st.rx_overrun_count;
    s_stats.bus_err = st.bus_error_count;
    s_stats.arb_lost = st.arb_lost_count;

    if (st.state == TWAI_STATE_BUS_OFF && !s_recovering) {
        /* Prerusene vedeni nebo spatna rychlost - radic se odpoji sam.
         * Bez ozivovani by ticho na sbernici vypadalo jako "vuz nic
         * neposila", coz je ta nejhorsi mozna diagnostika. */
        ESP_LOGE(TAG, "bus-off, ozivuji");
        if (twai_initiate_recovery() == ESP_OK) {
            s_recovering = true;
            s_stats.bus_off++;
        }
    } else if (st.state == TWAI_STATE_STOPPED && s_recovering) {
        if (twai_start() == ESP_OK) {
            s_recovering = false;
            ESP_LOGI(TAG, "sbernice ozivena");
        }
    }
}

unsigned can_poll(void)
{
    twai_message_t msg;
    unsigned n = 0u;

    if (!s_running) {
        return 0u;
    }
    while (n < (unsigned)CAN_POLL_BUDGET) {
        if (twai_receive(&msg, 0) != ESP_OK) {
            break;
        }
        n++;
        if (msg.extd || msg.rtr) {
            /* Tahle platforma jede cela na 11 bitech a diagnostika
             * remote ramce nepouziva. */
            continue;
        }
        s_stats.rx++;
        ring_push(msg.identifier, msg.data, msg.data_length_code, false);
    }
    pump_status();
    return n;
}

bool can_at(uint32_t seq, can_frame_t *out)
{
    const can_frame_t *slot;
    const volatile uint32_t *stamp;

    if (seq == 0u || seq >= s_head || out == NULL) {
        return false;
    }
    slot = &s_ring[seq % CAN_RING_DEPTH];
    /* Cislo se cte pres volatile, aby ho prekladac nemohl schovat do
     * registru - jinak by kontrola po kopii nic neoverila. */
    stamp = &slot->seq;
    if (*stamp != seq) {
        return false;
    }
    *out = *slot;
    /* Mohlo se to pod rukama pretocit - tim by kopie byla slepenec. */
    return *stamp == seq;
}

bool can_next(uint32_t *cursor, can_frame_t *out)
{
    uint32_t head = s_head;
    uint32_t oldest = (head > (uint32_t)CAN_RING_DEPTH)
                        ? head - (uint32_t)CAN_RING_DEPTH : 1u;

    if (cursor == NULL || out == NULL) {
        return false;
    }
    if (*cursor < oldest) {
        /* Novy ctenar (kurzor 0) nic neztratil, jen zacina. Ztrata je,
         * kdyz uz nekde byl a zasobnik se mu mezitim pretocil. */
        if (*cursor != 0u) {
            s_stats.ring_lost += oldest - *cursor;
        }
        *cursor = oldest;
    }
    if (*cursor >= head) {
        return false;
    }
    if (!can_at(*cursor, out)) {
        (*cursor)++;
        s_stats.ring_lost++;
        return false;
    }
    (*cursor)++;
    return true;
}

uint32_t can_seq(void) { return s_head; }

unsigned can_depth(void)
{
    uint32_t used = s_head - 1u;
    return (used > (uint32_t)CAN_RING_DEPTH) ? (unsigned)CAN_RING_DEPTH
                                             : (unsigned)used;
}

const can_stats_t *can_stats(void) { return &s_stats; }

void can_stats_reset(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}
