/*
 * i18n.h - prekladove retezce UI
 *
 * Vsechny viditelne texty UI jsou tady jako vycet. Samotne preklady jsou
 * v lang/<kod>.h, jeden soubor na jazyk, pole retezcu ve stejnem poradi
 * jako tenhle vycet. Pridani jazyka = novy soubor v lang/ a jeden radek
 * v tabulce v i18n.c; nic jineho se nemeni. Viz lang/README.md.
 *
 * Primarni jazyk je anglictina (lang/en.h). Ostatni jazyky z nej vychazeji.
 *
 * Pozor na retezce s formatovacimi znackami (%s, %u, %d) - preklad je musi
 * mit ve stejnem poctu a poradi, jinak to spadne az za behu. U kazdeho
 * takoveho retezce je to napsane v komentari.
 *
 * Pocet retezcu v kazdem jazyce hlida _Static_assert v i18n.c, takze
 * chybejici preklad neprojde prekladem.
 */
#ifndef I18N_H
#define I18N_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    /* nazvy stranek - zaroven popisky navigacnich tlacitek,
     * vejit se musi do 129 px pri fontu 28 */
    STR_PAGE_MAIN = 0,
    STR_PAGE_ENGINE,
    STR_PAGE_BOOST,
    STR_PAGE_FAULTS,
    STR_PAGE_DPF,
    STR_PAGE_SETUP,

    /* stavovy radek
     *
     * Ctyri urovne pripojeni k vozidlu. Rozdil mezi "nikdo neodpovida"
     * a "neptame se" je pro uzivatele zasadni: v prvnim pripade je
     * neco spatne, v druhem si to sam vypnul. */
    STR_TOP_TEMP,
    STR_LINK_NO_BUS,        /* na sbernici nic nejezdi */
    STR_LINK_LISTEN,        /* sbernice zije, ale jsme jen posluchac */
    STR_LINK_NO_ECU,        /* ptame se a nikdo neodpovida */
    STR_LINK_ECU,           /* jednotka odpovida */
    STR_NONE,               /* zadna sit; kratke, vejde se i na tlacitko */
    STR_WIFI_AP,            /* deska sama je pristupovy bod */

    /* hlavni stranka */
    STR_UNIT_RPM,

    /* stranka motoru */
    STR_BOOST_PRESSURE,
    STR_DEMAND,
    STR_INTAKE_AIR,

    /* stranka s grafem tlaku */
    STR_GAP_MBAR,
    STR_LEGEND_DEMAND,
    STR_LEGEND_ACTUAL,

    /* stranka chyb */
    STR_NO_CAN,
    STR_DTC_UNREAD,         /* pamet jeste nikdo necetl */
    STR_DTC_NONE,           /* precteno a prazdne - to uz je vysledek */
    STR_DTC_SOME,           /* jedno %u = pocet chyb */
    STR_DTC_SHOWN,          /* jedno %u = kolik se jich veslo na stranku */
    STR_DTC_TAP,
    STR_DTC_CONF,           /* stavove bity, zkratky do radku seznamu */
    STR_DTC_CYCLE,
    STR_DTC_PEND,
    STR_DTC_LAMP,
    STR_DTC_EXT_T,
    STR_DTC_FREEZE_T,
    STR_DTC_WAIT,           /* vybrano, odpoved jeste nedosla */
    STR_DTC_EMPTY,          /* jednotka k te chybe nic nema */
    STR_ODOMETER,
    STR_REC_ODO_AT,         /* nazvy rozsirenych zaznamu 19 06 */
    STR_REC_AGING,
    STR_REC_COUNT,
    STR_REC_ODO_HEALED,
    STR_REC_DIST_WITH,
    STR_REC_ODO_FIRST,
    STR_REC_UNSET,          /* zaznam je samych 0xFF, jednotka ho nema */

    /* stranka DPF */
    STR_DPF_TEMP_IN,
    STR_DPF_TEMP_OUT,
    STR_DPF_TEMP_TARGET,
    STR_DPF_SOOT,
    STR_DPF_LOAD,
    STR_DPF_SINCE,
    /* Protitlak a tlakovy rozdil na filtru. Fyzikalni signal, ze
     * ktereho jednotka odhad sazi teprve pocita. */
    STR_DPF_EXH_P,
    STR_DPF_DP,
    STR_DPF_DP_U,
    STR_DPF_FROZEN,         /* misto cisla, kdyz jednotka hlasi zamrzly signal */
    STR_DPF_REGEN_NA,       /* proc tu neni tlacitko na spusteni */
    STR_REGEN_STATE,
    STR_REGEN_OFF,
    STR_REGEN_RUN,

    /* nastaveni */
    STR_SET_SPEED,
    STR_SET_TEMP,
    STR_SET_DIM,            /* popisek tlacitka, ktere otevira ztlumeni */
    STR_SET_BRIGHT,
    STR_SET_PROFILE,        /* popisek tlacitka volby profilu */
    STR_SET_LANG,
    STR_SET_MAINT,          /* popisek tlacitka, ktere otevira resety */
    STR_BLANK_NEVER,

    /* ztlumeni po necinnosti
     *
     * Nahradilo drivejsi zhasinani. Zhasly displej za jizdy nerika
     * nic; ztlumeny je porad citelny. Uplne zhasnuti zustalo jen jako
     * rucni akce. */
    STR_DIM_TITLE,
    STR_DIM_AFTER,
    STR_DIM_TO,
    STR_DIM_NA,             /* deska stmivat neumi - misto urovni */
    STR_DIM_OFF_ONLY,       /* a co tedy udela misto ztlumeni */

    /* Stmivani jde na tuto desku dodelat jednim dratkem. Nabizi se jen
     * tam, kde chybi - kde uz je, nema co radit. Je to konstatovani
     * moznosti, ne doporuceni, a nenahrazuje navod; ten je v
     * docs/hardware/backlight.md a text na nej odkazuje. */
    STR_HW_DIM_BTN,         /* maly popisek tlacitka vedle vysvetleni */
    STR_HW_DIM_TITLE,
    STR_HW_DIM_BODY,

    /* rezim sbernice
     *
     * Prepnuti do normalniho rezimu je jedina akce v UI, po ktere deska
     * zacne na cizi sbernici vysilat, takze text musi rict presne to. */
    STR_CAN_LISTEN,
    STR_CAN_NORMAL,
    STR_CAN_NA,             /* deska CAN nema nebo rezim prepnout neumi */
    STR_CAN_TITLE,
    STR_CAN_BODY,
    STR_CAN_FAILED,         /* radic se v novem rezimu nerozjel */

    /* profily nastaveni - jmena jsou pevna, viz komentar v ui.h */
    STR_PROF_SHOP,
    STR_PROF_DAY,
    STR_PROF_NIGHT,
    STR_PROF_HINT,          /* co profil nese a co plati pro vsechny */

    /* WiFi */
    STR_SET_DISPLAY_OFF,
    STR_WIFI_TITLE,
    STR_WIFI_SEARCHING,
    STR_WIFI_PASS_FOR,      /* jedno %s = nazev site */
    STR_WIFI_PASS_PH,
    STR_WIFI_SAVED,         /* legenda u seznamu siti */
    STR_WIFI_FORGET_TITLE,
    STR_WIFI_FORGET_BODY,   /* jedno %s = nazev site */
    STR_WIFI_NONE,          /* sken dobehl a nic nenasel */
    STR_WIFI_FAILED,        /* sken se nerozjel nebo se nedockal */
    STR_WIFI_RETRY,
    STR_WIFI_MORE,          /* jedno %d = kolik siti se do seznamu neveslo */

    /* dialogy */
    STR_BACK,
    STR_OK,
    STR_CANCEL,
    STR_HOLD_FMT,       /* "HOLD %d s" - potvrzeni drzenim */
    /* Akce probehla - ukaze se na chvili misto odpoctu. Je to
     * KONSTATOVANI, ne pokyn: prst uz je nahore a nic se nepotvrzuje.
     * "OK" tedy ne, to se cte jako tlacitko k odklepnuti. */
    STR_HOLD_DONE,
    STR_ON,
    STR_OFF,

    /* jas podsviceni */
    STR_BRIGHT_TITLE,
    STR_BRIGHT_NA,          /* deska stmivat neumi - misto tlacitek */
    STR_BRIGHT_VEHICLE,     /* ctvrta moznost: podle osvetleni vozidla */
    STR_BRIGHT_VEHICLE_NA,  /* a duvod, proc zatim nejde zvolit */

    /* reset a udrzba
     *
     * Kazda z techto akci neco nevratne smaze, takze v textu musi byt
     * videt CO PRESNE - od toho jsou formatovaci znacky. Preklad je
     * musi mit ve stejnem poctu i poradi. */
    STR_RESET_TITLE,
    STR_RESET_CFG,
    STR_RESET_CFG_SUB,      /* jedno %d = pocet profilu */
    STR_RESET_CFG_BODY,     /* %d pocet profilu, %s jejich dnesni obsah */
    STR_RESET_WIFI,
    STR_RESET_WIFI_SUB,     /* jedno %d = kolik siti je ulozeno */
    STR_RESET_WIFI_BODY,    /* %d pocet siti, %s jejich seznam */
    STR_RESET_CAN,
    STR_RESET_CAN_SUB,      /* %u prijatych, %u odeslanych ramcu */
    STR_RESET_CAN_BODY,     /* jedno %s = vsechny citace i s hodnotami */
    STR_RESET_REBOOT,
    STR_RESET_REBOOT_SUB,   /* jedno %s = doba behu */
    STR_RESET_REBOOT_BODY,  /* jedno %s = doba behu */
    STR_RESET_NA,           /* deska tuhle akci neumi */

    /* navigace */
    STR_NAV_PLACEHOLDER,
    STR_NAV_DEBUG,          /* popisek dlazdice diagnostiky */

    /* diagnostika */
    STR_DBG_TITLE,
    STR_DBG_PERF,           /* prepinac prekryvu s FPS a pameti */
    STR_DBG_RAW,            /* syrove hodnoty, ktere UI prave ukazuje */
    STR_DBG_BUS,
    STR_DBG_POLL,           /* merena rychlost vycitani */
    STR_DBG_MEM,
    STR_DBG_SAVE,           /* jedno %s = jmeno profilu, do ktereho se uklada */
    STR_DBG_DEFAULT,

    /* Udaj, ktery je zatim jen opsany z popisu jednotky
     *
     * Neni to paty stav hodnoty - stavy odpovidaji na "jak dopadlo
     * tohle cteni", kdezto tohle je vlastnost samotne veliciny. Proto
     * je oznaceni na POPISKU, ne na cisle, a tahle veta rika, co
     * otaznik u popisku znamena. */
    STR_PROBE_MARK,

    /* stav baterie - energetika EMM, blok 21 34 */
    STR_NAV_BATT,           /* popisek dlazdice */
    STR_BATT_TITLE,
    STR_BATT_I,
    STR_BATT_SOC,
    STR_BATT_ALT,
    STR_BATT_CRANK,
    STR_BATT_SLEEP,
    STR_BATT_HINT,          /* co znamena znamenko a k cemu to je */

    /* vyrovnani vstrikovacu - jednorazove mereni */
    STR_NAV_INJ,            /* popisek dlazdice */
    STR_INJ_TITLE,
    STR_INJ_MEASURE,        /* tlacitko, ktere mereni spusti */
    STR_INJ_BUSY,
    STR_INJ_SPREAD,
    STR_INJ_CYL,            /* jedno %d = cislo valce */
    STR_INJ_NONE,           /* jeste se nemerilo */
    STR_INJ_REFUSED,        /* jedno %02X = duvod od jednotky */
    STR_INJ_COND,           /* za jakych podminek to ma smysl */
    STR_INJ_STATE,          /* dve %s = otacky a teplota pri mereni */

    STR__COUNT
};

/* Preklad jednoho retezce v prave nastavenem jazyce. Mimo rozsah vraci "?",
 * takze se chyba pozna na obrazovce a nikdy to neni NULL. */
const char *T(int id);

/* Prepnuti jazyka. Mimo rozsah se ignoruje. UI se tim samo neprekresli -
 * o to se stara ui.c. */
void i18n_set(int lang);
int  i18n_get(void);

/* Pocet zkompilovanych jazyku, jmeno jazyka v nem samem ("Cestina")
 * a jeho kod podle ISO 639-1 ("cs"). Mimo rozsah vraci "?". */
int         i18n_count(void);
const char *i18n_name(int lang);
const char *i18n_code(int lang);

/* Index jazyka podle kodu, nebo -1 kdyz takovy jazyk neni zkompilovany. */
int i18n_find(const char *code);

#ifdef __cplusplus
}
#endif

#endif /* I18N_H */
