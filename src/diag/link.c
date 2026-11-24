/*
 * link.c - detekce pripojeni k vozidlu, viz link.h
 *
 * Zadne hodiny, zadny hardware, zadna zavislost na CAN ani na UDS -
 * jen tri druhy udalosti a cas v parametru.
 */
#include "link.h"

static bool fresh(uint32_t now, uint32_t t, uint32_t limit)
{
    return (uint32_t)(now - t) < limit;
}

void diag_link_init(diag_link_t *l)
{
    l->state = (uint8_t)DIAG_LINK_DOWN;
    l->misses = 0u;
    l->burst = 0u;
    l->seen_frame = false;
    l->seen_reply = false;
    l->t_frame = 0u;
    l->t_reply = 0u;
    l->last_rx_id = 0u;
    l->n_frames = 0u;
    l->n_replies = 0u;
    l->n_silence = 0u;
}

/* Prepocet urovne z toho, co je videt ted. Jedine misto, kde se state
 * meni - udalosti jen sbiraji fakta. */
static void settle(diag_link_t *l, uint32_t now)
{
    bool bus = l->seen_frame && fresh(now, l->t_frame, DIAG_BUS_IDLE_MS);
    bool ecu = l->seen_reply && fresh(now, l->t_reply, DIAG_ECU_IDLE_MS) &&
               l->misses < (uint8_t)DIAG_ECU_MISS_MAX;

    if (!bus) {
        /* Po tichu se ramce pocitaji od zacatku. Jeden ramec po minute
         * ticha neni ziva sbernice, je to nahoda. */
        l->burst = 0u;
    }
    if (ecu) {
        l->state = (uint8_t)DIAG_LINK_ECU;
    } else if (bus && l->burst >= (uint16_t)DIAG_BUS_MIN_FRAMES) {
        l->state = (uint8_t)DIAG_LINK_BUS;
    } else {
        l->state = (uint8_t)DIAG_LINK_DOWN;
    }
}

void diag_link_on_frame(diag_link_t *l, uint32_t id, bool from_us,
                        uint32_t now_ms)
{
    (void)id;

    if (from_us) {
        /* Vlastni vysilani nedokazuje nic o tom, co je na druhem konci
         * kabelu. V listen-only navic zadne neni. */
        return;
    }
    if (l->seen_frame && !fresh(now_ms, l->t_frame, DIAG_BUS_IDLE_MS)) {
        l->burst = 0u;
    }
    if (l->burst < (uint16_t)0xFFFFu) {
        l->burst++;
    }
    l->t_frame = now_ms;
    l->seen_frame = true;
    l->n_frames++;
    settle(l, now_ms);
}

void diag_link_on_reply(diag_link_t *l, uint32_t rx_id, uint32_t now_ms)
{
    l->last_rx_id = rx_id;
    l->t_reply = now_ms;
    l->seen_reply = true;
    l->misses = 0u;
    l->n_replies++;
    /* Odpoved sama o sobe dokazuje i zivou sbernici, i kdyz nam ramce
     * nikdo neposila (treba kvuli filtru radice). */
    l->state = (uint8_t)DIAG_LINK_ECU;
}

void diag_link_on_silence(diag_link_t *l, uint32_t now_ms)
{
    if (l->misses < 0xFFu) {
        l->misses++;
    }
    l->n_silence++;
    settle(l, now_ms);
}

diag_link_state_t diag_link_poll(diag_link_t *l, uint32_t now_ms)
{
    settle(l, now_ms);
    return (diag_link_state_t)l->state;
}

uint32_t diag_link_quiet_ms(const diag_link_t *l, uint32_t now_ms)
{
    if (!l->seen_frame) {
        return 0xFFFFFFFFu;
    }
    return (uint32_t)(now_ms - l->t_frame);
}

const char *diag_link_state_name(diag_link_state_t s)
{
    switch (s) {
    case DIAG_LINK_DOWN: return "down";
    case DIAG_LINK_BUS:  return "bus-alive";
    case DIAG_LINK_ECU:  return "ecu-answering";
    default:             return "?";
    }
}
