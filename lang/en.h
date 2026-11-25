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
    "NO BUS",
    "LISTEN",
    "NO ECU",
    "ECU",
    "none",
    "AP",

    /* main page */
    "rpm",

    /* engine page */
    "BOOST PRESSURE",
    "DEMAND",
    "INTAKE AIR C",

    /* boost chart page */
    "DEFICIT mbar",
    "blue = demand",
    "bone = actual",

    /* faults page */
    "No CAN connection",
    "fault memory not read",
    "no stored faults",
    "%u faults",
    "first %u shown",
    "tap a fault for its records",
    "CONFIRMED",
    "THIS CYCLE",
    "PENDING",
    "LAMP",
    "EXTENDED RECORDS",
    "FREEZE FRAME",
    "reading...",
    "unit holds nothing for this one",
    "ODOMETER km",
    "odometer at occurrence",
    "aging counter",
    "occurrences",
    "odometer when cleared",
    "driven with the fault",
    "odometer at first",
    "not set",

    /* DPF page */
    "TEMP BEFORE C",
    "TEMP AFTER C",
    "TARGET C",
    "SOOT g",
    "LOAD g",
    "SINCE LAST REGEN",
    "BACKPRESSURE mbar",
    "FILTER DIFF mbar",
    "SENSOR V",
    "FROZEN",
    "Starting a regeneration writes to the ECU (31 01 02 0D). This "
    "build only reads, so the button is not here - see docs/safety.md.",
    "REGEN",
    "off",
    "running",

    /* settings */
    "SPEED",
    "TEMPERATURE",
    "DIM AFTER",
    "BRIGHTNESS",
    "PROFILE",
    "LANGUAGE",
    "SERVICE",
    "never",

    /* dimming after inactivity */
    "Dim after inactivity",
    "AFTER",
    "TO",
    "This board cannot dim - the wire from GPIO6 to the BL pad is missing, so after this time the display switches off instead.",
    "off",

    /* the dimming wire can still be added; stated, not recommended */
    "CAN BE\nADDED",
    "Dimming can be added",
    "One wire from GPIO6 to the BL pad next to the LCD ribbon connector. "
    "Nothing is cut and nothing is unsoldered, so it can be undone.\n\n"
    "Ground does not have to be soldered - the module and the backlight "
    "driver already share the board ground.\n\n"
    "It gives the full brightness range; measured on a real board.\n\n"
    "The price: GPIO6 is also the interrupt output of the PCF85063 clock, "
    "so while the wire is there the RTC interrupt cannot be used. This "
    "firmware does not use it.\n\n"
    "Described in detail, with the measurements:\n"
    "docs/hardware/backlight.md",

    /* bus mode */
    "LISTEN ONLY",
    "NORMAL",
    "no CAN on this board",
    "Transmit on the bus",
    "The board is listening only. Nothing can be read that way - every "
    "request is a transmission.\n\nIn normal mode this board STARTS "
    "TRANSMITTING on the vehicle bus: diagnostic requests and the "
    "acknowledge bit of every frame it receives.\n\nOnly do this once "
    "the bus speed and the wiring are confirmed - the top bar has to "
    "read LISTEN, not NO BUS.",
    "the controller did not come up - still listening only",

    /* setting profiles */
    "BENCH",
    "DAY",
    "NIGHT",
    "a profile holds brightness and blanking; units and language are shared",

    /* WiFi */
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
    "DONE",
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
    "POLLING  (measured)",
    "MEMORY AND UPTIME",
    "SAVE TO %s",
    "DEFAULTS",

    /* a value that is still only copied out of the ECU file */
    "?  =  from the ECU description file only - this van has never "
    "answered it",

    /* battery and charging, energy module 74D */
    "BATTERY",
    "Battery and charging",
    "BATTERY CURRENT A",
    "CHARGE %",
    "ALTERNATOR LOAD %",
    "CRANKING MIN V",
    "AT LAST SLEEP V",
    "The van measures this at its own battery sensor, so the sign is "
    "the whole point: one way is charging, the other way is what is "
    "being taken out. With the engine off and everything asleep, what "
    "is left is the standing draw - this board included.\n\n"
    "Which sign means which way is not established on this van yet. "
    "Start the engine once and the charging direction names itself.",

    /* injector balance - one-off measurement */
    "INJECTORS",
    "Injector balance",
    "MEASURE",
    "reading...",
    "SPREAD mg",
    "CYL %d",
    "not measured yet",
    "The ECU does not answer FD38-FD3B (7F %02X). That is the Bosch "
    "supplier range and a refusal is a normal answer here, not a fault.",
    "Warm idle, no load. Under load the balance regulator does not run "
    "and the four numbers are stale rather than wrong.",
    "%s rpm   %s C  at the measurement",

    /* WiFi mode: join a network, or be one */
    "MODE",
    "NETWORK",
    "ACCESS POINT",
    "NETWORK NAME",
    "PASSWORD",
    "ADDRESS",
    "%d connected",
    "The board is its own network, so this works with nothing else "
    "around. Join it from the phone with the name and password above, "
    "then open the address.\n\n"
    "There is no network list in this mode: the radio can either scan "
    "or be an access point, never both.",
    "NEW PASSWORD",
    "A new password is generated and shown here. Every device "
    "currently connected drops off and has to join again with the new "
    "one.",

    /* the live-value server; everything from WEB_TITLE down is what the
       page itself says */
    "SERVER",
    "Live values in a browser on a phone. It only reads: the page has "
    "no way to write anything to the vehicle.\n\n"
    "While it is on, anyone who can reach this address sees the "
    "readings, the odometer included. In access point mode that is "
    "whoever knows the password; on someone else's network it is "
    "everyone on it.",
    "no server in this firmware",
    "canograf - live values",
    "Read only - this page cannot write anything to the vehicle.",
    "The board reads only what its own screen is showing, so the other "
    "groups go old until that page is opened on it.",
    "No vehicle connected - the board is not reading anything, so "
    "there are no values to show.",
    "No answer from the board - the values below are old.",
    "DOWNLOAD CSV",
    "rows recorded",
    "oldest dropped",
    "not recording - no memory for the log",
    "not read",
    "live",
    "old",
    "refused",
};

#endif /* LANG_EN_H */
