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

    /* horni lista */
    STR_TOP_TEMP,

    /* hlavni stranka */
    STR_UNIT_RPM,

    /* stranka motoru */
    STR_BOOST_PRESSURE,
    STR_DEMAND,
    STR_SIM_DATA,

    /* stranka s grafem tlaku */
    STR_GAP_MBAR,
    STR_LEGEND_DEMAND,
    STR_LEGEND_ACTUAL,

    /* stranka chyb */
    STR_NO_CAN,
    STR_DTC_ALL,
    STR_DTC_CURRENT,
    STR_DTC_FREEZE,
    STR_DTC_STATS,

    /* stranka DPF */
    STR_DPF_TEMP_IN,
    STR_DPF_TEMP_OUT,
    STR_DPF_REGEN_BTN,
    STR_DPF_NO_VEHICLE,
    STR_REGEN_TITLE,
    STR_REGEN_BODY,
    STR_REGEN_STATE,        /* popisek rucniho prepinace stavu regenerace */
    STR_REGEN_OFF,
    STR_REGEN_REQ,
    STR_REGEN_RUN,
    STR_REGEN_RUN_PCT,

    /* nastaveni */
    STR_SET_SPEED,
    STR_SET_TEMP,
    STR_SET_BLANK,
    STR_SET_BRIGHT,
    STR_SET_DAYNIGHT,       /* popisek prepinace denni/nocni uroven */
    STR_SET_LANG,
    STR_BLANK_NEVER,

    /* WiFi */
    STR_WIFI_NOT_CONN,
    STR_WIFI_PICK,
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
    STR_HOLD_DONE,      /* potvrzeno - ukaze se na chvili misto odpoctu */
    STR_ON,
    STR_OFF,

    /* jas podsviceni */
    STR_BRIGHT_TITLE,
    STR_BRIGHT_NA,          /* deska stmivat neumi - misto tlacitek */
    STR_BRIGHT_DAY,
    STR_BRIGHT_NIGHT,
    STR_BRIGHT_VEHICLE,     /* treti rezim: podle osvetleni vozidla */
    STR_BRIGHT_VEHICLE_NA,  /* a duvod, proc zatim nejde zvolit */
    STR_BRIGHT_TOPBAR,      /* rychly prepinac den/noc v horni liste */

    /* navigace */
    STR_NAV_PLACEHOLDER,
    STR_NAV_DEBUG,          /* popisek dlazdice diagnostiky */

    /* diagnostika */
    STR_DBG_TITLE,
    STR_DBG_PERF,           /* prepinac prekryvu s FPS a pameti */
    STR_DBG_RAW,            /* syrove hodnoty, ktere UI prave ukazuje */
    STR_DBG_BUS,
    STR_DBG_BUS_NA,         /* CAN jeste neni na UI napojeny */
    STR_DBG_MEM,

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
