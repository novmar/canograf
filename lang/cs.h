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
    "BEZ SBĚRNICE",
    "POSLOUCHÁM",
    "BEZ ODPOVĚDI",
    "JEDNOTKA",
    "žádná",
    "AP",

    /* hlavni stranka */
    "ot/min",

    /* stranka motoru */
    "PLNICÍ TLAK",
    "ŽÁDÁNO",
    "NASÁVANÝ VZDUCH C",

    /* stranka s grafem tlaku */
    "SCHODEK mbar",
    "modrá = žádáno",
    "světlá = skutečnost",

    /* stranka chyb */
    "Bez připojení na CAN",
    "paměť chyb nepřečtena",
    "žádná uložená chyba",
    "chyb: %u",
    "zobrazeno prvních %u",
    "ťuknutím na chybu se načtou záznamy",
    "POTVRZENÁ",
    "V TOMTO CYKLU",
    "ČEKÁ",
    "KONTROLKA",
    "ROZŠÍŘENÉ ZÁZNAMY",
    "FREEZE FRAME",
    "čtu...",
    "jednotka k ní nic nemá",
    "TACHOMETR km",
    "tachometr při výskytu",
    "čítač stárnutí",
    "počet výskytů",
    "tachometr při vymizení",
    "ujeto se závadou",
    "tachometr při prvním",
    "nenastaveno",

    /* stranka DPF */
    "TEPLOTA PŘED C",
    "TEPLOTA ZA C",
    "CÍL C",
    "SAZE g",
    "ZATÍŽENÍ g",
    "OD POSLEDNÍ REGENERACE",
    "Spuštění regenerace je zápis do jednotky (31 01 02 0D). Tenhle "
    "obraz umí jen číst, takže tu tlačítko není - viz docs/safety.md.",
    "REGENERACE",
    "neběží",
    "běží",

    /* nastaveni */
    "RYCHLOST",
    "TEPLOTA",
    "ZTLUMIT PO",
    "JAS",
    "PROFIL",
    "JAZYK",
    "ÚDRŽBA",
    "nikdy",

    /* ztlumeni po necinnosti */
    "Ztlumení po nečinnosti",
    "PO",
    "NA",
    "Tato deska stmívat neumí - chybí drátek z GPIO6 na plošku BL, takže po této době místo ztlumení zhasne.",
    "zhasne",

    /* drátek na stmívání jde dodělat; konstatování, ne doporučení */
    "JDE\nDODĚLAT",
    "Stmívání jde dodělat",
    "Jeden drátek z GPIO6 na plošku BL vedle konektoru LCD pásku. "
    "Nic se neřeže a nic se neodpájí, takže je to vratné.\n\n"
    "Zem se pájet nemusí - modul i budič podsvícení sdílejí zem desky.\n\n"
    "Dá to plný rozsah jasu; ověřeno na skutečné desce.\n\n"
    "Cena: GPIO6 je zároveň budicí výstup hodin PCF85063, takže dokud "
    "tam ten drátek je, nejde použít přerušení z RTC. Tento firmware ho "
    "nepoužívá.\n\n"
    "Podrobně i s měřením:\ndocs/hardware/backlight.md",

    /* rezim sbernice */
    "JEN POSLECH",
    "NORMÁLNÍ",
    "tato deska CAN nemá",
    "Vysílání na sběrnici",
    "Deska jen poslouchá. Tak se nedá nic přečíst - každý dotaz je "
    "odeslání.\n\nV normálním režimu deska ZAČNE VYSÍLAT na sběrnici "
    "vozu: diagnostické dotazy a potvrzovací bit ke každému přijatému "
    "rámci.\n\nDělejte to až tehdy, když je jistá rychlost sběrnice i "
    "zapojení - v horní liště musí stát POSLOUCHÁM, ne BEZ SBĚRNICE.",
    "řadič se nerozjel - pořád jen poslouchám",

    /* profily nastaveni */
    "DÍLNA",
    "DEN",
    "NOC",
    "profil drží jas a zhasínání; jednotky a jazyk platí pro všechny",

    /* WiFi */
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
    "VYČÍTÁNÍ  (měřeno)",
    "PAMĚŤ A DOBA BĚHU",
    "ULOŽIT DO %s",
    "VÝCHOZÍ",
};

#endif /* LANG_CS_H */
