#
# build_config.py - vrstvena konfigurace buildu pro canograf
#
# Zapojeno v platformio.ini jako:
#     extra_scripts = pre:scripts/build_config.py
#
# Co to dela: nacte nekolik .env souboru, slouci je (pozdejsi prepisuje
# drivejsi), vysledek preda prekladaci jako -D makra a na zacatku buildu
# vypise tabulku, ze ktere vrstvy kazda hodnota pochazi.
#
# Smysl: prepnuti ochrany zapisu, vozidla, desky nebo modulu se dela
# upravou .env souboru, nikdy ne editaci platformio.ini. Verze v
# platformio.ini jsou zamcene a nemaji se cim dal sahat (viz CLAUDE.md).
#
# Poradi vrstev, od nejnizsi priority po nejvyssi:
#
#   1. config/default.env             povinna, v repu, soucasne seznam
#                                     vsech znamych klicu
#   2. config/board/<board>.env       profil desky
#   3. config/vehicle/<vehicle>.env   profil vozidla
#   4. local.env                      neni v repu (.gitignore), nejvyssi
#                                     priorita, pro jednu konkretni desku
#                                     na stole
#
# Ktery profil desky a vozidla se nacte, rika klic CANOGRAF_BOARD a
# CANOGRAF_VEHICLE. Protoze oba jdou prepsat v local.env, cte se nadvakrat:
# nejdriv default.env + local.env kvuli prepinacum, pak cely retez.
#
# Zavislosti: pouze standardni knihovna Python 3, kterou PlatformIO uz ma.
#
# Podrobny popis vcetne tabulky klicu je v config/README.md.
#
import difflib
import os
import re
import sys

Import("env")  # noqa: F821  (dodava PlatformIO)

PROJECT_DIR = env["PROJECT_DIR"]
CONFIG_DIR = os.path.join(PROJECT_DIR, "config")
DEFAULT_ENV = os.path.join(CONFIG_DIR, "default.env")
LOCAL_ENV = os.path.join(PROJECT_DIR, "local.env")

SELECT_BOARD = "CANOGRAF_BOARD"
SELECT_VEHICLE = "CANOGRAF_VEHICLE"

TRUE_WORDS = ("1", "true", "on", "yes", "y")
FALSE_WORDS = ("0", "false", "off", "no", "n")

# retezec v uvozovkach, za nim uz jen bily znak nebo komentar
RE_QUOTED = re.compile(r"""^(["'])(.*)\1\s*(?:#.*)?$""")
RE_INT = re.compile(r"^[+-]?[0-9]+$")
RE_HEX = re.compile(r"^0[xX][0-9a-fA-F]+$")
RE_FLOAT = re.compile(r"^[+-]?[0-9]+\.[0-9]+$")
RE_KEY = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def rel(path):
    """Cesta vuci korenu projektu, at se tabulka vejde na sirku."""
    try:
        return os.path.relpath(path, PROJECT_DIR)
    except ValueError:
        return path


def fail(lines):
    """Zastavi build s vysvetlenim. Tise projit nesmi nic."""
    sys.stderr.write("\n")
    sys.stderr.write("=" * 78 + "\n")
    sys.stderr.write("konfigurace buildu - chyba\n")
    sys.stderr.write("=" * 78 + "\n")
    for line in lines:
        sys.stderr.write(line + "\n")
    sys.stderr.write("=" * 78 + "\n\n")
    env.Exit(1)  # noqa: F821


def parse_env_file(path):
    """Vrati seznam (klic, syrova hodnota, cislo radku) v poradi souboru.

    Format je zamerne hloupy: KLIC=hodnota, prazdne radky a radky
    zacinajici # se ignoruji. Hodnota v uvozovkach je vzdy retezec,
    bez uvozovek se rozhoduje podle tvaru (viz classify)."""
    out = []
    errors = []
    # ascii zamerne: v repu nesmi byt diakritika ani v konfiguraci,
    # a znak, ktery by se dostal do -D makra, by se projevil az jako
    # nesrozumitelna chyba prekladace
    try:
        with open(path, "r", encoding="ascii") as fh:
            lines = fh.readlines()
    except UnicodeDecodeError as exc:
        fail(["%s: soubor neni ascii (bajt %d)" % (rel(path), exc.start),
              "Konfigurace se pise bez diakritiky, stejne jako zbytek repa."])
        return []
    for lineno, raw in enumerate(lines, 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            errors.append("%s:%d: radek neni KLIC=hodnota: %s" % (rel(path), lineno, line))
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if not RE_KEY.match(key):
            errors.append("%s:%d: neplatny nazev klice: %s" % (rel(path), lineno, key))
            continue
        out.append((key, value, lineno))
    if errors:
        fail(errors)
    return out


def classify(raw):
    """Prevede syrovou hodnotu na ("num"|"str", text pro prekladac).

    - hodnota v uvozovkach je vzdy retezec, i kdyz vypada jako cislo
    - true/false/on/off/yes/no -> 1/0
    - cele cislo, hex 0x.. a desetinne cislo jdou do maker tak jak jsou
    - vse ostatni je retezec
    """
    m = RE_QUOTED.match(raw)
    if m:
        return "str", m.group(2)

    # komentar za nezauvozkovanou hodnotou: "0   # proc" -> "0"
    value = re.split(r"\s+#", raw, 1)[0].strip()

    if value == "":
        return "str", ""
    low = value.lower()
    if low in TRUE_WORDS:
        return "num", "1"
    if low in FALSE_WORDS:
        return "num", "0"
    if RE_INT.match(value) or RE_HEX.match(value) or RE_FLOAT.match(value):
        return "num", value
    return "str", value


def load_layer(path, merged, allowed, unknown):
    """Prelozi jednu vrstvu pres merged. Neznamy klic jde do unknown.

    merged: klic -> (kind, hodnota, zdroj)
    """
    for key, raw, lineno in parse_env_file(path):
        if allowed is not None and key not in allowed:
            unknown.append((key, rel(path), lineno))
            continue
        kind, value = classify(raw)
        merged[key] = (kind, value, "%s:%d" % (rel(path), lineno))


def resolve_selector(key, pairs):
    """Posledni vyskyt klice v seznamu dvojic, uz prelozeny na text."""
    found = None
    for k, raw, _lineno in pairs:
        if k == key:
            found = classify(raw)[1]
    return found


def main():
    if not os.path.isfile(DEFAULT_ENV):
        fail([
            "chybi %s" % rel(DEFAULT_ENV),
            "Je to povinna vrstva - soucasne seznam vsech znamych klicu.",
        ])

    base_pairs = parse_env_file(DEFAULT_ENV)
    allowed = set(k for k, _v, _l in base_pairs)

    # --- prvni pruchod: jen zjistit, ktery profil desky a vozidla chceme.
    # Prepinace smi prepsat local.env, proto se cte uz tady.
    local_pairs = parse_env_file(LOCAL_ENV) if os.path.isfile(LOCAL_ENV) else []
    board = resolve_selector(SELECT_BOARD, base_pairs + local_pairs) or ""
    vehicle = resolve_selector(SELECT_VEHICLE, base_pairs + local_pairs) or ""

    # --- sestaveni retezu vrstev
    layers = [(DEFAULT_ENV, True)]
    if board:
        layers.append((os.path.join(CONFIG_DIR, "board", board + ".env"), False))
    if vehicle:
        layers.append((os.path.join(CONFIG_DIR, "vehicle", vehicle + ".env"), False))
    layers.append((LOCAL_ENV, False))

    merged = {}
    unknown = []
    report = []
    for path, required in layers:
        if not os.path.isfile(path):
            if required:
                fail(["chybi povinna vrstva %s" % rel(path)])
            # Profil, ktery si nekdo vyslovne vyzadal, ale neexistuje, je
            # preklep - tise ho preskocit by znamenalo postavit firmware pro
            # jinou desku nebo jine auto, nez clovek chtel.
            if path != LOCAL_ENV:
                which = "desky" if os.sep + "board" + os.sep in path else "vozidla"
                avail = available_profiles(os.path.dirname(path))
                fail([
                    "profil %s neexistuje: %s" % (which, rel(path)),
                    "k dispozici: %s" % (", ".join(avail) if avail else "zadny"),
                ])
            report.append((rel(path), "neni - preskoceno"))
            continue
        load_layer(path, merged, allowed, unknown)
        report.append((rel(path), "nacteno"))

    if unknown:
        lines = ["Neznamy klic v konfiguraci. Build zastaven.", ""]
        # Spolecny prefix CANOGRAF_ by podobnost nafoukl u kazde dvojice,
        # takze se porovnava az zbytek nazvu - jinak skript radi nesmysly.
        stems = dict((k.replace("CANOGRAF_", "", 1), k) for k in allowed)
        for key, where, lineno in unknown:
            stem = key.replace("CANOGRAF_", "", 1)
            hint = difflib.get_close_matches(stem, sorted(stems), n=1, cutoff=0.8)
            lines.append("  %s:%d  %s" % (where, lineno, key))
            if hint:
                lines.append("      mysleno %s ?" % stems[hint[0]])
        lines += [
            "",
            "Znamy klic je ten, ktery je v config/default.env. Tam patri i nove",
            "klice - vcetne komentare a vychozi hodnoty. Preklep v local.env by",
            "jinak tise neudelal nic a firmware by se postavil s jinym nastavenim,",
            "nez clovek cetl - u ochrany zapisu je to nebezpecne.",
        ]
        fail(lines)

    # klice, ktere default.env zna, ale zadna vrstva nenastavila, nemohou
    # nastat: default.env je soucasne seznam klicu i jejich vychozich hodnot

    print_table(report, merged, board, vehicle)
    apply_to_env(merged)


def available_profiles(directory):
    if not os.path.isdir(directory):
        return []
    return sorted(
        f[:-4] for f in os.listdir(directory) if f.endswith(".env")
    )


def print_table(report, merged, board, vehicle):
    """Tabulka na zacatku buildu. Clovek musi hned videt, s cim stavi -
    tedy i to, ze hodnota prisla z local.env a ne z repa."""
    rows = []
    for key in sorted(merged):
        kind, value, source = merged[key]
        rows.append((key, '"%s"' % value if kind == "str" else value, source))

    k_w = max([len("KLIC")] + [len(r[0]) for r in rows])
    v_w = max([len("HODNOTA")] + [len(r[1]) for r in rows])
    s_w = max([len("ZDROJ")] + [len(r[2]) for r in rows])
    width = 1 + k_w + 2 + v_w + 2 + s_w

    print("")
    print("=" * width)
    print(" canograf - konfigurace buildu")
    print(" deska: %s   vozidlo: %s"
          % (board or "(zadna)", vehicle or "(zadne)"))
    print("-" * width)
    l_w = max(len(p) for p, _s in report)
    for i, (path, state) in enumerate(report, 1):
        print(" %d. %-*s   %s" % (i, l_w, path, state))
    print(" (pozdejsi vrstva prepisuje drivejsi)")
    print("-" * width)
    print(" %-*s  %-*s  %s" % (k_w, "KLIC", v_w, "HODNOTA", "ZDROJ"))
    print("-" * width)
    for key, shown, source in rows:
        print(" %-*s  %-*s  %s" % (k_w, key, v_w, shown, source))
    print("=" * width)
    print("")


def apply_to_env(merged):
    defines = []
    for key in sorted(merged):
        kind, value, _source = merged[key]
        if kind == "str":
            defines.append((key, env.StringifyMacro(value)))
        else:
            defines.append((key, value))
    env.Append(CPPDEFINES=defines)

    # Stupen 1 ochrany zapisu bere safety.h doslova: pri vypnutem zapisu se
    # zapisovy kod nema do obrazu vubec prelozit. src/write/ zatim neexistuje,
    # filtr na neexistujici adresar nicemu nevadi a az adresar vznikne, nemusi
    # se sahat do platformio.ini.
    if merged.get("CANOGRAF_ALLOW_WRITE", (None, "0", None))[1] == "0":
        # SRC_FILTER musi zustat retezec - PlatformIO ho pousti pres
        # env.subst(), ktery seznam prevede na jeho repr a pak neodpovida
        # zadnemu souboru (projevi se to az chybou linkeru)
        src_filter = env.get("SRC_FILTER") or "+<*>"
        if isinstance(src_filter, (list, tuple)):
            src_filter = " ".join(src_filter)
        env.Replace(SRC_FILTER="%s -<write%s>" % (src_filter, os.sep))


main()
