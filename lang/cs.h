/*
 * Cestina. Poradi retezcu musi presne odpovidat vyctu v src/i18n.h,
 * jinak build spadne na _Static_assert v src/i18n.c.
 *
 * Soubor je v UTF-8 bez BOM a diakritika v nem je zamerne - fonty
 * src/font_ui_*.c ji umi vykreslit.
 */
#ifndef LANG_CS_H
#define LANG_CS_H

/* Kod jazyka podle ISO 639-1 a jmeno jazyka v nem samem - popisek
 * tlacitka volby jazyka. */
#define LANG_CS_CODE "cs"
#define LANG_CS_NAME "Čeština"

static const char *const LANG_CS[] = {
    /* nazvy stranek / navigacni tlacitka */
    "HLAVNÍ",
    "MOTOR",
    "TLAK",
    "CHYBY",
    "DPF",
    "NASTAV",

    /* horni lista */
    "CHLADICÍ KAPALINA",

    /* hlavni stranka */
    "ot/min",

    /* stranka motoru */
    "PLNICÍ TLAK",
    "ŽÁDÁNO",
    "simulovaná data",

    /* stranka s grafem tlaku */
    "SCHODEK mbar",
    "modrá = žádáno",
    "světlá = skutečnost",

    /* stranka chyb */
    "Bez připojení na CAN",
    "19 02 FF    seznam všech kódů",
    "19 02 01    jen to, co právě selhává",
    "19 04       freeze frame",
    "19 06       km a počet výskytů",

    /* stranka DPF */
    "TEPLOTA PŘED",
    "TEPLOTA ZA",
    "SPUSTIT REGENERACI",
    "zatím bez vozidla",
    "Regenerace DPF",
    "Vozidlo není připojeno.\n\nAž bude, tohle tlačítko odešle "
    "31 01 02 0D. Teplota výfuku přitom překročí 600 C. Jen na "
    "stojícím voze venku, se zahřátým motorem, a nesmí se přerušit.",
    "STAV REGENERACE",
    "VYPNUTO",
    "ODESLÁNO",
    "BĚŽÍ",
    "BĚŽÍ %",

    /* nastaveni */
    "RYCHLOST",
    "TEPLOTA",
    "ZHASNOUT PO",
    "JAS",
    "PROFIL",
    "JAZYK",
    "ÚDRŽBA",
    "nikdy",

    /* profily nastaveni */
    "DÍLNA",
    "DEN",
    "NOC",
    "profil drží jas a zhasínání; jednotky a jazyk platí pro všechny",

    /* WiFi */
    "nepřipojeno",
    "VYBRAT SÍŤ",
    "DISPLEJ VYP",
    "WiFi sítě",
    "hledám...",
    "HESLO PRO  %s",
    "heslo",
    "zelená = uložená, ťuknutí připojí, podržení zapomene",
    "ZAPOMENOUT SÍŤ",
    "%s\n\nUložené heslo se smaže. Při dalším připojení k této síti "
    "se UI zeptá znovu.",
    "žádná síť nenalezena",
    "hledání se nezdařilo - zkuste znovu",
    "ZNOVU",
    "+%d dalších nalezených sítí",

    /* dialogy */
    "ZPĚT",
    "ROZUMÍM",
    "ZRUŠIT",
    "DRŽ %d s",
    "HOTOVO",
    "ZAP",
    "VYP",

    /* jas podsviceni */
    "Jas",
    "Tato deska stmívat neumí - chybí drátek z GPIO6 na plošku BL.",
    "PODLE VOZU",
    "vyžaduje připojení k vozidlu",

    /* reset a udrzba */
    "Reset a údržba",
    "VÝCHOZÍ NASTAVENÍ",
    "%d profily a jednotky",
    "Vrátí %d profily a jednotky do výchozího stavu. Jazyk zůstane - "
    "po resetu musí jít přečíst, co na displeji stojí.\n\n"
    "Teď je v nich:\n\n%s",
    "ZAPOMENOUT VŠECHNY SÍTĚ",
    "uloženo %d",
    "Smaže všech %d uložených sítí i s hesly:\n\n%s\n\n"
    "Při dalším připojení se UI na heslo zeptá znovu.",
    "VYNULOVAT ČÍTAČE CAN",
    "rx %u, tx %u",
    "Vynuluje čítače sběrnice CAN. Teď ukazují:\n\n%s",
    "RESTART DESKY",
    "běží %s",
    "Deska se restartuje. Nic se nemaže - profily i uložené sítě "
    "zůstanou.\n\nBěží %s.",
    "na této desce to nejde",

    /* navigace */
    "MÍSTO PRO DALŠÍ FUNKCE",
    "DEBUG",

    /* diagnostika */
    "Diagnostika",
    "FPS + PAMĚŤ",
    "SYROVÉ HODNOTY  (cíl / zobrazeno, x100)",
    "SBĚRNICE CAN",
    "nepřipojeno - zatím bez dat",
    "PAMĚŤ A DOBA BĚHU",
};

#endif /* LANG_CS_H */
