/*
 * server.h - mala vestavena HTTP sluzba: ziva cisla v telefonu
 *
 * Deska zustane ve voze, clovek stoji venku nebo sedi u stolu a kouka
 * na tataz cisla, ktera ma panel. Nic vic; tenhle soubor je jen
 * doprava.
 *
 * ---------------------------------------------------------------
 * Jen cteni, a je to videt ze stavby
 * ---------------------------------------------------------------
 * Server NEMA cim do vozidla zapsat:
 *
 *   - obsah odpovedi stavi net/report.c, kteremu chodi diag_poller_t
 *     jako CONST ukazatel. Nejde pres nej ani prestavit pohled, natoz
 *     poslat ramec.
 *   - ani server.c, ani report.c nezna can/twai.h, tedy can_send() ani
 *     can_isotp_send(). Oba soubory na to maji #error, takze to nehlida
 *     revize, ale prekladac.
 *   - vsechny obsluhy jsou HTTP_GET. Nehlida to jen tabulka: pokud by
 *     nekdo zapsal jinou metodu, net_server_start() se NEROZJEDE a
 *     rekne proc. Server, ktery umi POST, tedy nejde ani spustit.
 *
 * Pravidlo projektu pro vzdalene konce je v docs/safety.md: vzdaleny
 * klient podleha tomuz zamku jako panel a nikdy si ho nesmi otevrit
 * sam. Tady se to dodrzuje tim nejjednodussim zpusobem, ktery existuje
 * - zapis tu neni vubec.
 *
 * ---------------------------------------------------------------
 * Proc dotaz a ne server-sent events
 * ---------------------------------------------------------------
 * SSE drzi spojeni otevrene a usetri hlavicky. Jenze esp_http_server
 * vyrizuje pozadavky JEDNOU ulohou: obsluha, ktera se nevrati, drzi
 * celou sluzbu, takze prvni telefon s otevrenym SSE by zbyle klienty
 * odstavil, dokud by se neodpojil. Obejit se to da jen tak, ze si
 * obsluha vytahne sokety stranou a posila do nich z vlastni ulohy -
 * tedy vlastni fronta, vlastni zivotnost soketu a vlastni uklid po
 * telefonu, ktery si clovek strcil do kapsy a odesel.
 *
 * Dotaz to vsechno nema. Spojeni se zavira samo, zapomenuty telefon se
 * uspanim obrazovky prestane ptat sam od sebe a vypadek site se
 * spravi tim, ze se priste dotaz povede. Cena je hlavicka na dotaz:
 * snimek je asi 1 kB, pri 500 ms to dela 2 kB/s na klienta a to je
 * proti riziku zadarmo.
 *
 * KOLIK KLIENTU. NET_SOCKETS_MAX je strop soucasne otevrenych soketu.
 * Prohlizec si na jeden server obvykle drzi jedno az dve spojeni
 * (keep-alive), takze sest soketu jsou tri telefony pohodlne a ctvrty
 * na hrane. lru_purge_enable znamena, ze dalsi spojeni vytlaci to
 * nejdele necinne misto toho, aby bylo odmitnuto - clovek se tedy
 * vzdycky pripoji, jen nejstarsi karta v nejstarsim telefonu prestane
 * obnovovat. Cely zasobnik lwip ma na teto sestave 16 soketu
 * (CONFIG_LWIP_MAX_SOCKETS) a krome serveru z nich zije i stanice,
 * DHCP v rezimu AP a OTA, takze vic nez sest by si je bralo navzajem.
 * V rezimu AP je strop pripojenych zarizeni nastaveny na ctyri, tedy
 * na tutez mez z druhe strany.
 *
 * ---------------------------------------------------------------
 * Displej ma prednost
 * ---------------------------------------------------------------
 * LVGL bezi na priorite 2 (LVGL_PORT_TASK_PRIORITY), Arduino loop() na
 * 1. Uloha serveru proto dostava prioritu 1, tedy POD displejem:
 * vychozi nastaveni esp_http_server je tskIDLE_PRIORITY + 5 a s nim by
 * stahovani CSV predbihalo prekreslovani.
 *
 * Priorita 1 ale znamena, ze se server deli o cas s loop(), kde bezi
 * can_poll() a vycitani. Proto je stahovani CSV davkovane a mezi
 * davkami se uloha vzda procesoru - bez toho by dlouhy prenos nechal
 * frontu radice pretect a rostl by rx_lost v can_stats().
 *
 * ---------------------------------------------------------------
 * Kde je zaznam relace
 * ---------------------------------------------------------------
 * V PSRAM, ne na karte. microSD je na teto desce jednobitova a jeji
 * vyber (CS) visi na expanderu CH422G, takze KAZDA transakce stoji
 * kolecko po I2C - a po temze I2C chodi dotykovy radic. Zapis radku
 * dvakrat za vterinu by tedy platil dotykem, a to za zaznam nestoji.
 *
 * Kruhovy zasobnik ma NET_LOG_ROWS radku po NET_LOG_MS. Vychozi
 * hodnoty jsou 7200 a 500 ms, tedy HODINA jizdy za asi 1 MB z osmi,
 * ktere PSRAM ma. Kdyz se zaplni, prepisuje se nejstarsi radek a
 * kolik jich zmizelo, stoji v hlavicce stazeneho CSV - tise useknuty
 * zacatek jizdy by byl presne ten druh lzi, kterou tahle aplikace
 * delat nesmi.
 *
 * Proc 2 Hz a ne 10 Hz jako rychla skupina: hodina pri 10 Hz je 3,7 MB
 * a o PSRAM se deli s vyrovnavacimi pametmi panelu. Dve za vterinu je
 * zaroven o neco vic, nez dalo cteni pres ELM327 (3,3 vzorku za
 * vterinu na VSECHNY veliciny dohromady), takze zaznam neni horsi nez
 * to, z ceho se zatim delaly zavery.
 *
 * Zasobnik zije, dokud bezi server: zapnuti ho zalozi, vypnuti uvolni.
 * Drzet megabajt pro funkci, kterou nikdo nezapnul, by bylo pleytvani.
 */
#ifndef NET_SERVER_H
#define NET_SERVER_H

#include <stdbool.h>
#include <stdint.h>

#include "net/report.h"

/* Pojistka proti zapisu je v net/server.c za includy, ne tady - tenhle
 * soubor cte i src/main.cpp, ktery can/twai.h znat SMI. Rozbor je u
 * teze pojistky v net/report.h. */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NET_PORT
#define NET_PORT 80
#endif

#ifndef NET_SOCKETS_MAX
#define NET_SOCKETS_MAX 6
#endif

/* Kolik zarizeni se smi pripojit k pristupovemu bodu. Tataz mez jako
 * NET_SOCKETS_MAX, jen z druhe strany. */
#ifndef NET_AP_CLIENTS_MAX
#define NET_AP_CLIENTS_MAX 4
#endif

/* Zaznam relace. Prestavitelne pres config/, viz config/README.md. */
#ifndef CANOGRAF_WEB_LOG_ROWS
#define CANOGRAF_WEB_LOG_ROWS 7200
#endif
#ifndef CANOGRAF_WEB_LOG_MS
#define CANOGRAF_WEB_LOG_MS 500
#endif

typedef struct {
    /* Odkud se berou hodnoty. CONST schvalne: server se smi ptat a nic
     * vic - ani prestavit pohled, natoz poslat ramec. */
    const diag_poller_t *dp;

    /* Kdo vi o siti. Server se site sam nepta a ani nemuze: zna jen
     * tenhle callback a report_net_t, ve kterem zadne heslo neni. */
    void (*net_info)(report_net_t *out);
} net_server_cfg_t;

/* Rozjede sluzbu. false = port se nepodarilo otevrit, pamet nestacila,
 * nebo nekdo do tabulky obsluh zapsal jinou metodu nez GET; duvod jde
 * do logu. */
bool net_server_start(const net_server_cfg_t *cfg);

void net_server_stop(void);
bool net_server_running(void);

/* Jeden krok zaznamu relace. Vola se z TEHOZ vlakna, ktere krmi
 * diag_poller_poll(), tedy z hlavni smycky - zapisovatel do zasobniku
 * je jen jeden. */
void net_server_tick(uint32_t now_ms);

/* Mereni pro diagnostiku. */
uint32_t net_server_requests(void);
uint32_t net_server_log_rows(void);

/* Vede se zaznam? false znamena, ze se pamet nepodarilo ziskat -
 * stranka to pak rekne misto toho, aby nabizela prazdny soubor. */
bool net_server_log_ok(void);

#ifdef __cplusplus
}
#endif

#endif /* NET_SERVER_H */
