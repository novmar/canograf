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
    "PROTITLAK mbar",
    "ROZDÍL NA FILTRU mbar",
    "SNÍMAČ V",
    "ZAMRZLÝ",
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

    /* údaj, který je zatím jen opsaný z popisu jednotky */
    "?  =  jen z popisu jednotky - tenhle vůz na to ještě neodpověděl",

    /* stav baterie, energetika 74D */
    "BATERIE",
    "Baterie a dobíjení",
    "PROUD BATERIE A",
    "NABITÍ %",
    "ALTERNÁTOR %",
    "MIN PŘI STARTU V",
    "PŘI USPÁNÍ V",
    "Měří to vůz svým vlastním snímačem na baterii, takže znaménko je "
    "to hlavní: jedna strana je dobíjení, druhá to, co se z baterie "
    "bere. Se zhasnutým motorem a vším uspaným zbyde klidový odběr - "
    "tahle deska v něm je taky.\n\n"
    "Které znaménko znamená který směr, na tomhle voze ověřené není. "
    "Stačí jednou nastartovat a směr dobíjení se pojmenuje sám.",

    /* vyrovnání vstřikovačů - jednorázové měření */
    "VSTŘIKOVAČE",
    "Vyrovnání vstřikovačů",
    "ZMĚŘIT",
    "čtu...",
    "ROZPTYL mg",
    "VÁLEC %d",
    "ještě neměřeno",
    "Jednotka na FD38-FD3B neodpovídá (7F %02X). Je to dodavatelský "
    "rozsah Bosch a odmítnutí je tu normální odpověď, ne porucha.",
    "Teplý volnoběh, bez zátěže. Pod zátěží regulátor vyrovnání "
    "neběží a ta čtyři čísla jsou spíš stará než špatná.",
    "%s 1/min   %s C  při měření",

    /* režim WiFi: připojit se k síti, nebo síť dělat */
    "REŽIM",
    "SÍŤ",
    "PŘÍSTUPOVÝ BOD",
    "JMÉNO SÍTĚ",
    "HESLO",
    "ADRESA",
    "připojeno %d",
    "Deska je sama sítí, takže to funguje i v garáži, kde není nic "
    "dalšího. Připoj se k ní telefonem podle jména a hesla nahoře a "
    "pak otevři tu adresu.\n\n"
    "Seznam sítí v tomhle režimu není: rádio umí buď hledat, nebo být "
    "přístupovým bodem, nikdy obojí.",
    "NOVÉ HESLO",
    "Vyrobí se nové heslo a ukáže se tady. Všechna zařízení, která "
    "jsou právě připojená, se odpojí a musí se přihlásit znovu tím "
    "novým.",

    /* server se živými hodnotami; od WEB_TITLE dolů je to text,
       který říká sama ta stránka */
    "SERVER",
    "Živé hodnoty v prohlížeči na telefonu. Jenom čte: stránka nemá "
    "čím do vozidla zapsat.\n\n"
    "Dokud běží, vidí hodnoty včetně tachometru každý, kdo se na tu "
    "adresu dostane. V režimu přístupového bodu je to ten, kdo zná "
    "heslo; v cizí síti všichni, kdo v ní jsou.",
    "server v tomto firmwaru není",
    "canograf - živé hodnoty",
    "Jen čtení - tahle stránka do vozidla nic nezapíše.",
    "Deska čte jen to, co má sama na obrazovce, takže ostatní skupiny "
    "stárnou, dokud se na ní ta stránka neotevře.",
    "Vozidlo připojené není - deska nic nečte, takže není co ukázat.",
    "Deska neodpovídá - hodnoty níž jsou staré.",
    "STÁHNOUT CSV",
    "zaznamenaných řádků",
    "nejstarších přepsáno",
    "nenahrává se - nebyla na záznam paměť",
    "nepřečteno",
    "živá",
    "stará",
    "odmítnuta",

    /* emulace ELM327; prepinac zije v dialogu WiFi */
    "ELM327",
    "Diagnostická aplikace v telefonu se k desce připojí po síti a "
    "použije ji jako rozhraní k vozu - a dostane i víceramcové "
    "odpovědi složené, což skutečný ELM327 neumí.\n\n"
    "Jen čte: zápis, mazání paměti závad ani rutiny se na sběrnici "
    "nedostanou, ať si aplikace řekne o cokoliv. V režimu poslechu "
    "neodejde vůbec nic. Vždy jen jeden nástroj.",
    "běží, ale deska zatím nemá adresu",
};

#endif /* LANG_CS_H */
