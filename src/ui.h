/*
 * ui.h - rozhrani uzivatelskeho rozhrani Talento HMI
 *
 * Tenhle kod zna jen LVGL. Nesmi zaviset na Arduino.h, WiFi.h ani na
 * ESP32_Display_Panel, aby sel prelozit i na hostiteli (viz sim/).
 * Vse, co umi jen deska, se predava pres ui_host_t. Simulator necha
 * ukazatele NULL a ui.c to musi snest.
 *
 * Vyjimka jsou can/twai.h a diag/poller.h: oba jsou cisty C bez ESP-IDF
 * (to je az v twai.c), takze se prelozi i na hostiteli. Z prvniho se
 * bere typ can_stats_t, z druheho cely pristup k namerenym hodnotam -
 * UI se pta vrstvy diagnostiky, ne vozidla, a nezna jediny DID.
 *
 * Pozn.: fonty UI maji ceskou i nemeckou diakritiku, viz src/font_ui.h.
 */
#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>

#include "can/twai.h"
#include "diag/poller.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------
 * Nastaveni, ktere prezije restart. Deska ho nacte z NVS a preda sem
 * pred ui_build(); kdykoliv ho uzivatel zmeni, posle ho UI zpatky pres
 * ui_host_t.save_cfg.
 *
 * PROFILY. Nastaveni se deli na dve casti:
 *
 *   - co patri CLOVEKU, plati porad a zustava stejne: jednotky a jazyk.
 *     Kdo cte v km/h, cte v km/h na stole v dilne i v noci za volantem,
 *     a prepnuti profilu nesmi nikdy zpusobit, ze UI prestane byt
 *     citelna.
 *   - co patri SITUACI, ve ktere deska zrovna je: jas a zhasinani.
 *     Presne tyhle dve veci chce clovek jinak na stole, jinak za jizdy
 *     ve dne a jinak v noci, a presne kvuli nim profily existuji.
 *
 * Profily jsou tri a maji PEVNA JMENA (DILNA / DEN / NOC, prelozena
 * pres i18n). Cislovani 1/2/3 by nesvazovalo, ale za jizdy v noci se
 * cislo nepozna - clovek by musel vedet, co si pod ktery ulozil.
 * Jmeno je jen popisek toho, KDY se profil hodi; co v nem je, urcuje
 * uzivatel, takze svazuje jen slovem, ne obsahem.
 *
 * Profily zaroven nahradily drivejsi dvojici jasu den/noc i prepinac
 * den/noc. Mit obe veci vedle sebe by znamenalo dve cesty k temuz -
 * sest ulozenych urovni jasu a dva prepinace, ktere se umi rozejit.
 * ------------------------------------------------------------------- */
#define UI_PROFILES 3

enum { UI_PROF_SHOP = 0, UI_PROF_DAY, UI_PROF_NIGHT };

/* ---------------------------------------------------------------------
 * Co profil drzi.
 *
 * ZTLUMENI, NE ZHASNUTI. Drive se displej po case cely zhasl. Zhasly
 * displej za jizdy nic nerika - clovek nevi, jestli deska bezi, a
 * musi na ni sahnout, aby se to dozvedel. Ztlumeny displej je porad
 * citelny, v noci dokonce prijemnejsi, a pritom neoslnuje. Zustava
 * jen RUCNI vypnuti (tlacitko ZHASNOUT DISPLEJ), protoze to je jedina
 * cesta k uplne tme a pouziva se pri mereni na ploskach.
 *
 * Deska BEZ dratku z GPIO6 na plosku BL stmivat neumi (ui_host_t
 * .bl_set_percent == NULL). Tam zustava drivejsi chovani, tedy
 * zhasnuti, a UI u toho rekne proc - nabidnout uroven ztlumeni na
 * desce, ktera zna jen zap/vyp, by byl slib naprazdno.
 * ------------------------------------------------------------------- */
typedef struct {
    int      bright;        /* jas 0..100 % */
    uint32_t dim_s;         /* ztlumit po tolika vterinach; 0 = nikdy */
    int      dim_bright;    /* na jakou uroven ztlumit, 0..100 % */
    bool     perf;          /* prekryv s FPS a pameti, viz dlazdice DEBUG */
} ui_profile_t;

typedef struct {
    /* spolecne pro vsechny profily */
    bool mph;               /* false = km/h */
    bool fahrenheit;        /* false = C */
    int  lang;              /* index jazyka z i18n.c, 0 = English */

    /* prave zvoleny profil a obsah vsech tri */
    int          profile;   /* 0..UI_PROFILES-1 */
    ui_profile_t prof[UI_PROFILES];
} ui_cfg_t;

typedef struct {
    /* Podsviceni: tvrde zap/vyp pres EXIO2. Vypina i samotny panel,
     * takze je to hlavni vypinac a jedina cesta k uplne tme. */
    void (*backlight)(bool on);

    /* Plynule stmivani, 0..100 % jasu. NULL znamena, ze deska stmivat
     * neumi - UI pak stmivani vubec nenabidne a netvari se, ze jde.
     * Panel tim nezhasina, od toho je backlight(). */
    void (*bl_set_percent)(int percent);

    /* trvale ulozeni nastaveni (NVS) */
    void (*save_cfg)(const ui_cfg_t *c);

    /* WiFi */
    void        (*wifi_scan_start)(void);
    int         (*wifi_scan_result)(void);  /* <0 jeste bezi, jinak pocet siti */
    const char *(*wifi_scan_ssid)(int i);
    void        (*wifi_connect)(const char *ssid, const char *pass);
    bool        (*wifi_connected)(void);
    const char *(*wifi_ssid)(void);         /* "" kdyz nepripojeno */
    const char *(*wifi_ip)(void);           /* "" kdyz nepripojeno */

    /* Sila signalu v dBm, jak ji hlasi radic. Kresli se z ni carky ve
     * stavovem radku. NULL nebo hodnota 0 znamena "nevim" - carky pak
     * nesviti vubec, misto aby predstiraly plny signal.
     *
     * wifi_ap() rika, jestli deska bezi jako pristupovy bod. To neni
     * pripojeni k cizi siti, takze sila signalu v tom rezimu nic
     * neznamena a stavovy radek misto carek napise, ze je to AP. */
    int  (*wifi_rssi)(void);
    bool (*wifi_ap)(void);

    /* Zapamatovane site. Hesla drzi deska, UI je nikdy nevidi - jen se
     * pta, jestli uz sit zna, a rekne, kterou pouzit nebo zapomenout.
     * wifi_connect() ulozeni obstara sam, takze dalsi funkce na to neni. */
    bool (*wifi_known)(const char *ssid);          /* uz ulozena? */
    bool (*wifi_connect_known)(const char *ssid);  /* pripojit ulozenym heslem */
    void (*wifi_forget)(const char *ssid);         /* smazat ze seznamu */

    /* Cely seznam ulozenych siti. UI ho potrebuje proto, aby pred
     * hromadnym zapomenutim ukazalo, KTERE site to smaze a kolik jich
     * je - "smaze ulozene site" bez cisla a jmen je slib naslepo. */
    int         (*wifi_saved_count)(void);
    const char *(*wifi_saved_ssid)(int i);         /* NULL mimo rozsah */
    void        (*wifi_forget_all)(void);

    /* ---- pristupovy bod ----
     *
     * Deska bud hleda cizi sit, nebo sit SAMA DELA. V garazi, kde zadna
     * sit neni, je to jediny zpusob, jak se k ni telefonem dostat.
     *
     * Obe veci se VYLUCUJI a neni to volba vzhledu: radic umi bud
     * skenovat, nebo byt pristupovym bodem, a pokus o oboji konci
     * donekonecna v ESP_ERR_WIFI_STATE (docs/findings.md). UI proto v
     * rezimu AP seznam siti vubec nepostavi - ne ze by tlacitko jen
     * zesedlo, ono tam neni - a deska si to jeste hlida sama.
     *
     * Ktery rezim prave plati, rika wifi_ap() vyse. Je to JEDINY zdroj
     * pravdy, protoze se pta radice; druha promenna vedle nej by se s
     * nim umela rozejit.
     *
     * JMENO A HESLO MUSI BYT VIDET NA DISPLEJI, jinak se k siti nikdo
     * nepripoji - deska nema kudy je jinak rict. Heslo je proto to
     * jedine, co UI o heslech vidi; ulozena hesla cizich siti zustavaji
     * jen v NVS a UI se na ne nikdy nedostane.
     *
     * NULL kterekoliv z techto funkci znamena, ze deska pristupovy bod
     * neumi - UI prepinani rezimu vubec nenabidne. */
    void        (*wifi_set_ap)(bool on);
    const char *(*wifi_ap_ssid)(void);
    const char *(*wifi_ap_pass)(void);
    int         (*wifi_ap_clients)(void);   /* -1 = nevim */
    void        (*wifi_ap_newpass)(void);   /* vyrobi a ulozi nove heslo */

    /* ---- server se zivymi hodnotami ----
     *
     * Port zustava zavreny, dokud ho nekdo nezapne tady; tak to chce
     * pravidlo pro vzdalene konce v docs/safety.md. Server jen cte -
     * cestu k zapisu do vozidla nema a mit nemuze, viz net/server.h.
     *
     * web_url() vraci adresu, na ktere stranka je, nebo "" kdyz
     * neni kde (server stoji, nebo deska nema adresu). UI ji ukaze,
     * protoze "server bezi" bez adresy je polovina informace.
     *
     * NULL znamena, ze firmware server nema - UI ho pak nenabidne. */
    bool        (*web_enabled)(void);
    void        (*web_set)(bool on);
    const char *(*web_url)(void);

    /* ---- emulace ELM327 pro cizi diagnosticke nastroje ----
     *
     * Totez pravidlo jako u serveru: port zustava zavreny, dokud ho
     * nekdo nezapne tady, a zapnuti je za potvrzenim drzenim, protoze
     * otevira port do site (docs/safety.md).
     *
     * Emulace JEN CTE. Mezi pripojenym nastrojem a sbernici stoji bily
     * seznam sluzeb a v listen-only neodejde ramec vubec - rozbor je v
     * net/elm.h. UI o tom nerozhoduje a rozhodovat nema; jen to rekne.
     *
     * elm_url() vraci adresu I PORT ("192.168.4.1:35000"), protoze do
     * nastroje se opisuje oboji a port nikdo neuhadne. "" znamena, ze
     * neni kde - emulace stoji, nebo deska nema adresu.
     *
     * elm_client() rika, jestli na portu nekdo opravdu visi. Otevreny
     * port a pripojeny nastroj jsou dve ruzne veci a clovek, kteremu to
     * v aplikaci nejede, ma videt, ve ktere z nich je.
     *
     * NULL znamena, ze firmware emulaci nema - UI ji pak nenabidne. */
    bool        (*elm_enabled)(void);
    void        (*elm_set)(bool on);
    const char *(*elm_url)(void);
    bool        (*elm_client)(void);

    /* Citace sbernice. Ukazatel plati porad a struktura se sama
     * obnovuje, takze si ho UI nemusi obnovovat; NULL znamena, ze deska
     * CAN nema a UI nulovani citacu vubec nenabidne. */
    const can_stats_t *(*can_stats)(void);
    void               (*can_stats_reset)(void);

    /* Rezim sbernice. Po startu je radic v listen-only a dotaz je
     * odeslani, takze bez prepnuti do normalniho rezimu se nic neprecte.
     * Prepnuti je VEDOME rozhodnuti uzivatele, nikdy ne vychozi stav:
     * v normalnim rezimu deska na cizi sbernici vysila.
     *
     * NULL u kterekoliv z dvojice znamena, ze deska CAN nema nebo rezim
     * prepnout neumi - UI pak prepinani vubec nenabidne a netvari se,
     * ze jde. set_normal() vraci false, kdyz se rezim nepodarilo zmenit;
     * radic v tom pripade zustava tam, kde byl. */
    bool (*can_is_normal)(void);
    bool (*can_set_normal)(bool normal);

    /* Restart desky. NULL = deska restartovat neumi, tlacitko nevznikne. */
    void (*reboot)(void);

    /* diagnostika */
    unsigned (*heap_kb)(void);
    unsigned (*psram_kb)(void);
} ui_host_t;

/* Ukazatel si UI jen pamatuje, struktura musi prezit cely beh. */
void ui_set_host(const ui_host_t *h);

/* Nastaveni nactene z NVS; volat pred ui_build(). Neplatny lang spadne
 * na vychozi jazyk (0 = English), jas mimo rozsah se oreze a profil
 * mimo rozsah spadne na DEN - z NVS muze prijit cokoliv a UI se podle
 * toho nesmi rozsypat. */
void ui_set_cfg(const ui_cfg_t *c);

/* Vychozi obsah profilu. Deska ho potrebuje pri prvnim startu a UI pri
 * resetu nastaveni, takze je jen na jednom miste. Index mimo rozsah
 * vraci DEN. */
ui_profile_t ui_profile_default(int i);

/* ---------------------------------------------------------------------
 * Prevod jasu v procentech na stridu PWM pro plosku BL.
 *
 * Dve veci, ktere se tu daji tise rozbit, a proto maji vlastni funkci
 * a kontrolu v sim/lv_shot.c:
 *
 *   1. POLARITA JE OBRACENA. Napeti na plosce BL ubira proud LEDkam:
 *      0 V = plny jas, 3,3 V = tma. Vetsi jas tedy znamena MENSI stridu.
 *   2. Prepocet neni linearni, viz komentar u implementace v ui.c.
 *
 * percent se oreze na 0..100, vraci 0..duty_max.
 * ------------------------------------------------------------------- */
uint32_t ui_bl_duty(int percent, uint32_t duty_max);

void        ui_build(void);       /* postavi celou UI do lv_scr_act() */
void        ui_tick(void);        /* jeden krok obnovy, volat ~50 Hz */
int         ui_page_count(void);
void        ui_show_page(int i);
const char *ui_page_name(int i);

/* ---------------------------------------------------------------------
 * Vrstva diagnostiky.
 *
 * Odsud bere UI vsechny namerene hodnoty i stav pripojeni. Ukazatel
 * musi prezit cely beh; NULL znamena, ze deska diagnostiku nema - UI
 * pak nikde neukaze zadne cislo, protoze zadne nema.
 *
 * Neni const: UI vrstve hlasi, ktera stranka je prave videt
 * (diag_poller_set_view), a na strance chyb vybira zaznam, ke kteremu
 * se maji docist 19 04 a 19 06.
 *
 * Stav regenerace DPF si UI odvozuje samo z kanalu DIAG_CH_RGN_DUR -
 * drive to byl rucni prepinac, protoze nebylo odkud cist. Spusteni
 * regenerace je zapis (31 01 02 0D) a ten do tohoto obrazu nepatri,
 * viz docs/safety.md.
 * ------------------------------------------------------------------- */
void ui_set_diag(diag_poller_t *p);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
