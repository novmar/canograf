/*
 * English - the primary language. Every other translation follows this one.
 *
 * The order of the strings must match the enum in src/i18n.h exactly.
 * A missing or extra entry fails the build (_Static_assert in src/i18n.c).
 *
 * UTF-8, no BOM. Characters outside ASCII need to be in the generated font,
 * see lang/README.md.
 */
#ifndef LANG_EN_H
#define LANG_EN_H

/* ISO 639-1 code and the name of this language, written in this language.
 * The name is what the language button shows. */
#define LANG_EN_CODE "en"
#define LANG_EN_NAME "English"

static const char *const LANG_EN[] = {
    /* page names / navigation buttons */
    "MAIN",
    "ENGINE",
    "BOOST",
    "FAULTS",
    "DPF",
    "SETUP",

    /* top bar */
    "COOLANT",

    /* main page */
    "rpm",

    /* engine page */
    "BOOST PRESSURE",
    "DEMAND",
    "simulated data",

    /* boost chart page */
    "DEFICIT mbar",
    "blue = demand",
    "bone = actual",

    /* faults page */
    "No CAN connection",
    "19 02 FF    every stored code",
    "19 02 01    only what is failing now",
    "19 04       freeze frame",
    "19 06       distance and occurrences",

    /* DPF page */
    "TEMP BEFORE",
    "TEMP AFTER",
    "FORCE REGEN",
    "no vehicle yet",
    "DPF regeneration",
    "No vehicle is connected.\n\nOnce one is, this button sends "
    "31 01 02 0D. Exhaust temperature goes past 600 C. Stationary "
    "vehicle outdoors only, engine warm, and it must not be interrupted.",
    "REGEN STATE",
    "OFF",
    "REQUESTED",
    "RUNNING",
    "RUNNING %",

    /* settings */
    "SPEED",
    "TEMPERATURE",
    "BLANK AFTER",
    "BRIGHTNESS",
    "PROFILE",
    "LANGUAGE",
    "SERVICE",
    "never",

    /* setting profiles */
    "BENCH",
    "DAY",
    "NIGHT",
    "a profile holds brightness and blanking; units and language are shared",

    /* WiFi */
    "not connected",
    "PICK NETWORK",
    "DISPLAY OFF",
    "WiFi networks",
    "scanning...",
    "PASSWORD FOR  %s",
    "password",
    "green = saved, tap connects, hold forgets",
    "FORGET NETWORK",
    "%s\n\nThe saved password will be deleted. Connecting to this "
    "network again will ask for it.",
    "no network found",
    "scan failed - try again",
    "RETRY",
    "+%d more networks found",

    /* dialogs */
    "BACK",
    "GOT IT",
    "CANCEL",
    "HOLD %d s",
    "OK",
    "ON",
    "OFF",

    /* backlight brightness */
    "Brightness",
    "Not on this board - the wire from GPIO6 to the BL pad is missing.",
    "BY VEHICLE",
    "requires a vehicle connection",

    /* reset and maintenance */
    "Reset and maintenance",
    "FACTORY SETTINGS",
    "%d profiles and units",
    "Returns %d profiles and the units to their defaults. The language "
    "stays - a reset must never make the screen unreadable.\n\n"
    "Right now they hold:\n\n%s",
    "FORGET ALL NETWORKS",
    "%d saved",
    "Deletes all %d saved networks together with their passwords:\n\n%s\n\n"
    "Connecting to any of them again will ask for the password.",
    "ZERO BUS COUNTERS",
    "rx %u, tx %u",
    "Zeroes the CAN bus counters. Right now they read:\n\n%s",
    "RESTART BOARD",
    "up %s",
    "The board restarts. Nothing is erased - profiles and saved networks "
    "stay.\n\nIt has been up %s.",
    "not available on this board",

    /* navigation */
    "ROOM FOR MORE FUNCTIONS",
    "DEBUG",

    /* diagnostics */
    "Diagnostics",
    "FPS + MEMORY",
    "RAW VALUES  (target / shown, x100)",
    "CAN BUS",
    "not connected - no data yet",
    "MEMORY AND UPTIME",
};

#endif /* LANG_EN_H */
