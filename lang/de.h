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
    "KEIN BUS",
    "HÖRE ZU",
    "KEINE ANTWORT",
    "ECU",
    "keine",
    "AP",

    /* Hauptseite */
    "U/min",

    /* Motorseite */
    "LADEDRUCK",
    "SOLLWERT",
    "ANSAUGLUFT C",

    /* Druckverlauf */
    "DEFIZIT mbar",
    "blau = Sollwert",
    "hell = Istwert",

    /* Fehlerseite */
    "Keine CAN-Verbindung",
    "Fehlerspeicher nicht gelesen",
    "kein Fehler gespeichert",
    "%u Fehler",
    "erste %u gezeigt",
    "Fehler antippen zeigt die Einträge",
    "BESTÄTIGT",
    "DIESER ZYKLUS",
    "WARTEND",
    "LAMPE",
    "ERWEITERTE EINTRÄGE",
    "UMGEBUNGSDATEN",
    "lese...",
    "das Steuergerät hat dazu nichts",
    "KILOMETERSTAND km",
    "Stand beim Auftreten",
    "Alterungszähler",
    "Häufigkeit",
    "Stand beim Verschwinden",
    "mit dem Fehler gefahren",
    "Stand beim ersten Mal",
    "nicht gesetzt",

    /* DPF-Seite */
    "TEMP. VOR C",
    "TEMP. NACH C",
    "ZIEL C",
    "RUSS g",
    "BELADUNG g",
    "SEIT LETZTER REGEN.",
    "Eine Regeneration zu starten schreibt ins Steuergerät "
    "(31 01 02 0D). Dieser Stand liest nur, deshalb gibt es die Taste "
    "hier nicht - siehe docs/safety.md.",
    "REGEN.",
    "aus",
    "läuft",

    /* Einstellungen */
    "TEMPO",
    "TEMPERATUR",
    "DIMMEN NACH",
    "HELLIGKEIT",
    "PROFIL",
    "SPRACHE",
    "WARTUNG",
    "nie",

    /* Dimmen nach Untaetigkeit */
    "Dimmen nach Untätigkeit",
    "NACH",
    "AUF",
    "Diese Platine kann nicht dimmen - der Draht von GPIO6 zum BL-Pad fehlt, deshalb schaltet das Display nach dieser Zeit stattdessen ab.",
    "aus",

    /* Der Dimm-Draht laesst sich nachruesten; Feststellung, kein Rat */
    "NACH-\nRÜSTBAR",
    "Dimmen lässt sich nachrüsten",
    "Ein Draht von GPIO6 zum BL-Pad neben dem LCD-Flachbandstecker. "
    "Nichts wird durchtrennt und nichts abgelötet, es ist also "
    "umkehrbar.\n\n"
    "Masse muss nicht gelötet werden - Modul und Backlight-Treiber "
    "teilen sich bereits die Masse der Platine.\n\n"
    "Das ergibt den vollen Helligkeitsbereich; an einer echten Platine "
    "gemessen.\n\n"
    "Der Preis: GPIO6 ist zugleich der Interrupt-Ausgang der Uhr "
    "PCF85063, solange der Draht dran ist, lässt sich der RTC-Interrupt "
    "also nicht nutzen. Diese Firmware nutzt ihn nicht.\n\n"
    "Ausführlich und mit den Messwerten:\n"
    "docs/hardware/backlight.md",

    /* Busmodus */
    "NUR HÖREN",
    "NORMAL",
    "kein CAN auf dieser Platine",
    "Auf dem Bus senden",
    "Die Platine hört nur zu. So lässt sich nichts lesen - jede "
    "Anfrage ist ein Senden.\n\nIm Normalmodus SENDET diese Platine "
    "auf dem Fahrzeugbus: Diagnoseanfragen und das Quittungsbit zu "
    "jedem empfangenen Rahmen.\n\nErst tun, wenn Busgeschwindigkeit "
    "und Verdrahtung sicher sind - in der Kopfzeile muss HÖRE ZU "
    "stehen, nicht KEIN BUS.",
    "der Controller kam nicht hoch - weiterhin nur Hören",

    /* Profile der Einstellungen */
    "WERKSTATT",
    "TAG",
    "NACHT",
    "Profil hält Helligkeit und Abschaltung; Einheiten und Sprache "
    "gelten für alle",

    /* WLAN */
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
    "ABFRAGE  (gemessen)",
    "SPEICHER UND LAUFZEIT",
    "IN %s SICHERN",
    "STANDARD",
};

#endif /* LANG_DE_H */
