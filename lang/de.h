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
    "REGEN.-STATUS",
    "AUS",
    "GESENDET",
    "LÄUFT",
    "LÄUFT %",

    /* Einstellungen */
    "TEMPO",
    "TEMPERATUR",
    "DUNKEL NACH",
    "HELLIGKEIT",
    "PROFIL",
    "SPRACHE",
    "WARTUNG",
    "nie",

    /* Profile der Einstellungen */
    "WERKSTATT",
    "TAG",
    "NACHT",
    "Profil hält Helligkeit und Abschaltung; Einheiten und Sprache "
    "gelten für alle",

    /* WLAN */
    "nicht verbunden",
    "NETZ WÄHLEN",
    "DISPLAY AUS",
    "WLAN-Netze",
    "suche...",
    "PASSWORT FÜR  %s",
    "Passwort",
    "grün = gespeichert, tippen verbindet, halten vergisst",
    "NETZ VERGESSEN",
    "%s\n\nDas gespeicherte Passwort wird gelöscht. Beim nächsten "
    "Verbinden fragt die Oberfläche erneut danach.",
    "kein Netz gefunden",
    "Suche fehlgeschlagen - erneut versuchen",
    "NOCHMAL",
    "+%d weitere Netze gefunden",

    /* Dialoge */
    "ZURÜCK",
    "VERSTANDEN",
    "ABBRECHEN",
    "%d s HALTEN",
    "FERTIG",
    "EIN",
    "AUS",

    /* Helligkeit der Hintergrundbeleuchtung */
    "Helligkeit",
    "Dimmen geht hier nicht - der Draht von GPIO6 zum BL-Pad fehlt.",
    "NACH FAHRZEUG",
    "erfordert eine Fahrzeugverbindung",

    /* Reset und Wartung */
    "Reset und Wartung",
    "WERKSEINSTELLUNGEN",
    "%d Profile und Einheiten",
    "Setzt %d Profile und die Einheiten zurück. Die Sprache bleibt - "
    "nach einem Reset muss lesbar bleiben, was auf dem Schirm steht.\n\n"
    "Zurzeit enthalten sie:\n\n%s",
    "ALLE NETZE VERGESSEN",
    "%d gespeichert",
    "Löscht alle %d gespeicherten Netze samt Passwörtern:\n\n%s\n\n"
    "Beim nächsten Verbinden fragt die Oberfläche erneut nach dem Passwort.",
    "CAN-ZÄHLER NULLEN",
    "rx %u, tx %u",
    "Nullt die Zähler des CAN-Busses. Zurzeit zeigen sie:\n\n%s",
    "PLATINE NEU STARTEN",
    "läuft %s",
    "Die Platine startet neu. Nichts wird gelöscht - Profile und "
    "gespeicherte Netze bleiben.\n\nSie läuft %s.",
    "auf dieser Platine nicht möglich",

    /* Navigation */
    "PLATZ FÜR WEITERE FUNKTIONEN",
    "DEBUG",

    /* Diagnose */
    "Diagnose",
    "FPS + SPEICHER",
    "ROHWERTE  (Soll / angezeigt, x100)",
    "CAN-BUS",
    "nicht verbunden - noch keine Daten",
    "SPEICHER UND LAUFZEIT",
};

#endif /* LANG_DE_H */
