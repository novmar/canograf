/*
 * Deutsch. Die Reihenfolge der Zeichenketten muss exakt der Aufzaehlung
 * in src/i18n.h entsprechen, sonst schlaegt der Build fehl
 * (_Static_assert in src/i18n.c).
 *
 * UTF-8 ohne BOM.
 */
#ifndef LANG_DE_H
#define LANG_DE_H

/* ISO-639-1-Code und der Name der Sprache in der Sprache selbst -
 * Beschriftung der Sprachtaste. */
#define LANG_DE_CODE "de"
#define LANG_DE_NAME "Deutsch"

static const char *const LANG_DE[] = {
    /* Seitennamen / Navigationstasten */
    "HAUPT",
    "MOTOR",
    "DRUCK",
    "FEHLER",
    "DPF",
    "EINST.",

    /* Kopfzeile */
    "KÜHLMITTEL",

    /* Hauptseite */
    "U/min",

    /* Motorseite */
    "LADEDRUCK",
    "SOLLWERT",
    "simulierte Daten",

    /* Druckverlauf */
    "DEFIZIT mbar",
    "blau = Sollwert",
    "hell = Istwert",

    /* Fehlerseite */
    "Keine CAN-Verbindung",
    "19 02 FF    alle gespeicherten Codes",
    "19 02 01    nur aktuell anliegende",
    "19 04       Umgebungsbedingungen",
    "19 06       km und Häufigkeit",

    /* DPF-Seite */
    "TEMP. VOR",
    "TEMP. NACH",
    "REGEN. STARTEN",
    "noch ohne Fahrzeug",
    "DPF-Regeneration",
    "Kein Fahrzeug verbunden.\n\nSobald eines da ist, sendet diese "
    "Taste 31 01 02 0D. Die Abgastemperatur übersteigt dabei 600 C. "
    "Nur am stehenden Fahrzeug im Freien, bei warmem Motor, und sie "
    "darf nicht unterbrochen werden.",

    /* Einstellungen */
    "TEMPO",
    "TEMPERATUR",
    "DUNKEL NACH",
    "HELLIGKEIT",
    "SPRACHE",
    "nie",
    "PWM-Test",

    /* WLAN */
    "nicht verbunden",
    "NETZ WÄHLEN",
    "WLAN-Netze",
    "suche...",
    "PASSWORT FÜR  %s",
    "Passwort",

    /* Dialoge */
    "ZURÜCK",
    "FERTIG",
    "VERSTANDEN",

    /* Versuch mit PWM-Hintergrundbeleuchtung */
    "Helligkeit - Versuch mit Software-PWM",
    "Ein Umschalten über I2C dauert %u us. Bei %d ms Periode ist der "
    "kürzeste Puls %d ms - wenn das Display bei wenig Helligkeit ganz "
    "ausgeht, kommt der AP3032 Aufwärtswandler nicht hoch.",
    "FREQUENZ",
    "VOLLE HELLIGKEIT",

    /* Navigation */
    "PLATZ FÜR WEITERE FUNKTIONEN",
};

#endif /* LANG_DE_H */
