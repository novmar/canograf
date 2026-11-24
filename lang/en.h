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
    "LANGUAGE",
    "never",
    "PWM test",

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

    /* dialogs */
    "BACK",
    "CLOSE",
    "GOT IT",
    "CANCEL",
    "HOLD %d s",
    "OK",

    /* backlight PWM experiment */
    "Brightness - software PWM experiment",
    "One toggle over I2C takes %u us. At a %d ms period the shortest "
    "pulse is %d ms - if the panel goes dark at low brightness, the "
    "AP3032 boost converter cannot start up in time.",
    "FREQUENCY",
    "FULL BRIGHTNESS",

    /* navigation */
    "ROOM FOR MORE FUNCTIONS",
};

#endif /* LANG_EN_H */
