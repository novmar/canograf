/*
 * server.c - HTTP sluzba, ktera jen cte
 *
 * Rozbor rozhodnuti (dotaz misto SSE, kolik klientu, kde je zaznam,
 * proc priorita pod displejem) je v net/server.h. Tady je provedeni.
 *
 * Tenhle soubor NEZNA WiFi. O siti se dozvi jen to, co mu preda
 * cfg.net_info() v report_net_t, a v te strukture zadne heslo neni -
 * takze heslo k pristupovemu bodu se do zadne odpovedi nema jak
 * dostat. Je to stavba, ne opatrnost.
 */
#include "net/server.h"
#include "net/page.h"

#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pojistka, ne komentar. Rozbor je u teze pojistky v net/report.c; tady
 * jde o totez: sluzba, ktera jen cte, nesmi mit cim poslat ramec. */
#ifdef CAN_TWAI_H
#error "net/server: can/twai.h tu nema co delat - server do vozidla nezapisuje"
#endif

static const char *TAG = "canograf-web";

/* ---------------------------------------------------------------------
 * Stav
 *
 * Vsechny obsluhy bezi v JEDNE uloze esp_http_server, jedna po druhe,
 * takze spolecne odkladaci buffery nepotrebuji zamek. Zapisovatel do
 * zasobniku zaznamu je taky jediny (net_server_tick z hlavni smycky) a
 * cte ho jen obsluha stahovani; proc to k sobe nemuze dosahnout, je
 * rozebrane u weblog_t v net/report.h.
 * ------------------------------------------------------------------- */
#define BUF_META 4096
#define BUF_LIVE 2048
#define BUF_CSV  2048       /* davka radku, ktera odchazi najednou */
#define BUF_ROW   640       /* jeden radek CSV */

/* Po kolika davkach CSV se uloha vzda procesoru. Hlavni smycka bezi na
 * tyz priorite a je v ni can_poll(); bez tohohle by dlouhy prenos
 * nechal frontu radice pretect a rostl by rx_lost. */
#define CSV_YIELD_EVERY 1

static httpd_handle_t   s_httpd;
static net_server_cfg_t s_cfg;
static weblog_t         s_log;
static weblog_row_t    *s_rows;
static char            *s_buf;          /* BUF_META + BUF_LIVE + BUF_CSV + BUF_ROW */
static uint32_t         s_requests;

static char *buf_meta(void) { return s_buf; }
static char *buf_live(void) { return s_buf + BUF_META; }
static char *buf_csv(void)  { return s_buf + BUF_META + BUF_LIVE; }
static char *buf_row(void)  { return s_buf + BUF_META + BUF_LIVE + BUF_CSV; }

static void fill_net(report_net_t *n)
{
    memset(n, 0, sizeof(*n));
    n->clients = -1;
    if (s_cfg.net_info) s_cfg.net_info(n);
}

/* ---------------------------------------------------------------------
 * Obsluhy
 *
 * Vsechny jsou HTTP_GET a zadna z nich nema cim do vozidla sahnout -
 * jediny pristup k vozu je const diag_poller_t v s_cfg.
 * ------------------------------------------------------------------- */
static esp_err_t h_page(httpd_req_t *req)
{
    s_requests++;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    /* Stranka se nemeni, dokud se nezmeni firmware; prohlizec si ji
     * tedy smi nechat. Hodnoty pres to necham chodit vzdycky cerstve -
     * /live a /log.csv ulozit do mezipameti nesmi nikdo. */
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=600");
    return httpd_resp_send(req, NET_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_json(httpd_req_t *req, const char *body, int len)
{
    if (len < 0) {
        /* Oriznuty JSON se neposila. Prohlizec by ho bud nerozebral,
         * nebo by si nechal stara cisla a tvaril se, ze jsou nova. */
        ESP_LOGE(TAG, "odpoved se nevesla do bufferu");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, len);
}

static esp_err_t h_meta(httpd_req_t *req)
{
    s_requests++;
    return send_json(req, buf_meta(), report_meta_json(buf_meta(), BUF_META));
}

static esp_err_t h_live(httpd_req_t *req)
{
    report_net_t net;

    s_requests++;
    fill_net(&net);
    return send_json(req, buf_live(),
                     report_live_json(buf_live(), BUF_LIVE, s_cfg.dp, &net,
                                      &s_log));
}

/* Zaznam relace. Posila se po davkach a mezi nimi se uloha vzda
 * procesoru - viz CSV_YIELD_EVERY. */
static esp_err_t h_csv(httpd_req_t *req)
{
    char *chunk = buf_csv();
    char *row   = buf_row();
    int   at    = 0;
    int   batch = 0;

    s_requests++;
    httpd_resp_set_type(req, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"canograf.csv\"");

    int n = report_csv_head(chunk, BUF_CSV, &s_log);
    if (n < 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
        return ESP_FAIL;
    }
    at = n;

    uint32_t first = weblog_first(&s_log);
    uint32_t last  = s_log.head;
    for (uint32_t seq = first; seq < last; seq++) {
        n = report_csv_row(row, BUF_ROW, &s_log, seq);
        /* Radek, ktery zasobnik mezitim prepsal, se nepreskakuje tise:
         * zbytek uz je roztrhany, takze se prenos ukonci a hlavicka
         * souboru uz rika, kolik radku zmizelo. */
        if (n < 0) break;
        if (at + n >= BUF_CSV) {
            if (httpd_resp_send_chunk(req, chunk, at) != ESP_OK) return ESP_FAIL;
            at = 0;
            if (++batch >= CSV_YIELD_EVERY) {
                batch = 0;
                vTaskDelay(1);
            }
        }
        memcpy(chunk + at, row, (size_t)n);
        at += n;
    }
    if (at && httpd_resp_send_chunk(req, chunk, at) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* ---------------------------------------------------------------------
 * Tabulka obsluh
 *
 * Vsechno GET. Neni to jen zvyk: net_server_start() tabulku projde a
 * pri jine metode se VUBEC NEROZJEDE, takze server, ktery by umel
 * POST, nejde ani spustit.
 * ------------------------------------------------------------------- */
static const httpd_uri_t URI[] = {
    { .uri = "/",        .method = HTTP_GET, .handler = h_page, .user_ctx = NULL },
    { .uri = "/meta",    .method = HTTP_GET, .handler = h_meta, .user_ctx = NULL },
    { .uri = "/live",    .method = HTTP_GET, .handler = h_live, .user_ctx = NULL },
    { .uri = "/log.csv", .method = HTTP_GET, .handler = h_csv,  .user_ctx = NULL },
};

#define URI_N ((int)(sizeof(URI) / sizeof(URI[0])))

static void free_mem(void)
{
    if (s_rows) { heap_caps_free(s_rows); s_rows = NULL; }
    if (s_buf)  { heap_caps_free(s_buf);  s_buf  = NULL; }
    weblog_init(&s_log, NULL, 0, CANOGRAF_WEB_LOG_MS);
}

bool net_server_start(const net_server_cfg_t *cfg)
{
    if (s_httpd) return true;
    if (!cfg || !cfg->dp) {
        ESP_LOGE(TAG, "bez vrstvy diagnostiky nema co ukazovat");
        return false;
    }
    for (int i = 0; i < URI_N; i++) {
        if (URI[i].method != HTTP_GET) {
            ESP_LOGE(TAG, "%s neni GET - server jen cte, nerozjizdim se",
                     URI[i].uri);
            return false;
        }
    }

    s_cfg = *cfg;

    /* Odkladaci buffery. PSRAM: jsou to jednotky kilobajtu dvakrat za
     * vterinu, takze na rychlosti nezalezi, a vnitrni pamet patri
     * panelu a zasobniku ramcu. */
    s_buf = heap_caps_malloc(BUF_META + BUF_LIVE + BUF_CSV + BUF_ROW,
                             MALLOC_CAP_SPIRAM);
    if (!s_buf) s_buf = malloc(BUF_META + BUF_LIVE + BUF_CSV + BUF_ROW);
    if (!s_buf) {
        ESP_LOGE(TAG, "nedostatek pameti na odpovedi");
        return false;
    }

    /* Zaznam relace. Kdyz se nepovede, server bezi dal BEZ nej a
     * stranka to rekne - prazdny soubor ke stazeni by tvrdil, ze se
     * jizda nahrava, a ona by se nenahravala. */
    s_rows = heap_caps_malloc((size_t)CANOGRAF_WEB_LOG_ROWS *
                              sizeof(weblog_row_t), MALLOC_CAP_SPIRAM);
    weblog_init(&s_log, s_rows, CANOGRAF_WEB_LOG_ROWS, CANOGRAF_WEB_LOG_MS);
    if (!s_rows)
        ESP_LOGW(TAG, "PSRAM nestacila na %u radku zaznamu - zaznam vypnut",
                 (unsigned)CANOGRAF_WEB_LOG_ROWS);

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.server_port      = NET_PORT;
    hc.max_uri_handlers = URI_N;
    hc.max_open_sockets = NET_SOCKETS_MAX;
    hc.lru_purge_enable = true;
    /* POD displejem. LVGL ma prioritu 2, hlavni smycka 1; vychozi
     * nastaveni esp_http_server je tskIDLE_PRIORITY + 5 a s nim by
     * stahovani predbihalo prekreslovani. */
    hc.task_priority    = tskIDLE_PRIORITY + 1;
    hc.stack_size       = 5120;
    hc.recv_wait_timeout = 5;
    hc.send_wait_timeout = 5;

    esp_err_t err = httpd_start(&s_httpd, &hc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        s_httpd = NULL;
        free_mem();
        return false;
    }
    for (int i = 0; i < URI_N; i++) httpd_register_uri_handler(s_httpd, &URI[i]);

    s_requests = 0;
    ESP_LOGI(TAG, "port %d, %d soketu, zaznam %u radku po %u ms",
             NET_PORT, NET_SOCKETS_MAX,
             s_rows ? (unsigned)CANOGRAF_WEB_LOG_ROWS : 0u,
             (unsigned)CANOGRAF_WEB_LOG_MS);
    return true;
}

void net_server_stop(void)
{
    if (!s_httpd) return;
    /* httpd_stop() pocka, az uloha serveru skonci, takze po navratu uz
     * do bufferu ani do zasobniku zaznamu nikdo necte. Az potom se smi
     * uvolnit. */
    httpd_stop(s_httpd);
    s_httpd = NULL;
    free_mem();
    ESP_LOGI(TAG, "zastaveno");
}

bool net_server_running(void) { return s_httpd != NULL; }

void net_server_tick(uint32_t now_ms)
{
    if (!s_httpd) return;
    weblog_sample(&s_log, s_cfg.dp, now_ms);
}

uint32_t net_server_requests(void) { return s_requests; }
uint32_t net_server_log_rows(void) { return weblog_rows(&s_log); }
bool     net_server_log_ok(void)   { return s_rows != NULL; }
