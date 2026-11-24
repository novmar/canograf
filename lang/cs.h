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

    /* nastaveni */
    "RYCHLOST",
    "TEPLOTA",
    "ZHASNOUT PO",
    "JAS",
    "JAZYK",
    "nikdy",
    "PWM test",

    /* WiFi */
    "nepřipojeno",
    "VYBRAT SÍŤ",
    "WiFi sítě",
    "hledám...",
    "HESLO PRO  %s",
    "heslo",

    /* dialogy */
    "ZPĚT",
    "ZAVŘÍT",
    "ROZUMÍM",

    /* pokus s PWM podsvicenim */
    "Jas - pokus se softwarovým PWM",
    "Jedno přepnutí přes I2C trvá %u us. Při periodě %d ms je "
    "nejkratší puls %d ms - jestli při nízkém jasu zhasne úplně, "
    "nestíhá naběhnout boost měnič AP3032.",
    "FREKVENCE",
    "ZPĚT NA PLNÝ JAS",

    /* navigace */
    "MÍSTO PRO DALŠÍ FUNKCE",
};

#endif /* LANG_CS_H */
