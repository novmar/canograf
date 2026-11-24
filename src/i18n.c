/*
 * i18n.c - tabulka jazyku
 *
 * Cisty C, zadne LVGL a zadne Arduino - prelozi se stejne na desce
 * i na hostiteli.
 *
 * PRIDANI JAZYKA
 *   1. zkopirovat lang/en.h na lang/<kod>.h, prelozit retezce
 *   2. #include ho tady
 *   3. pridat radek do LANGS[] a jeden _Static_assert
 *   Nic jineho. Podrobne v lang/README.md.
 */
#include "i18n.h"

#include <stddef.h>
#include <string.h>

#include "../lang/en.h"
#include "../lang/cs.h"
#include "../lang/de.h"

/* Pojistka proti tichemu chybejicimu prekladu: kazdy jazyk musi mit presne
 * tolik retezcu, kolik ma vycet polozek. Chybejici radek v lang/<kod>.h
 * neprojde prekladem, misto aby se projevil az na obrazovce. */
#define LANG_CHECK(arr) \
    _Static_assert(sizeof(arr) / sizeof((arr)[0]) == STR__COUNT, \
                   #arr ": pocet retezcu neodpovida vyctu v src/i18n.h")

LANG_CHECK(LANG_EN);
LANG_CHECK(LANG_CS);
LANG_CHECK(LANG_DE);

typedef struct {
    const char        *code;    /* ISO 639-1 */
    const char        *name;    /* jmeno jazyka v nem samem */
    const char *const *str;
} lang_t;

/* Poradi urcuje i cislo ulozene v NVS, takze se novy jazyk prida na konec.
 * Index 0 je vychozi jazyk. */
static const lang_t LANGS[] = {
    { LANG_EN_CODE, LANG_EN_NAME, LANG_EN },
    { LANG_CS_CODE, LANG_CS_NAME, LANG_CS },
    { LANG_DE_CODE, LANG_DE_NAME, LANG_DE },
};

#define LANG_N ((int)(sizeof(LANGS) / sizeof(LANGS[0])))

static int cur = 0;

const char *T(int id)
{
    if (id < 0 || id >= STR__COUNT) return "?";
    return LANGS[cur].str[id];
}

void i18n_set(int lang)
{
    if (lang < 0 || lang >= LANG_N) return;
    cur = lang;
}

int i18n_get(void) { return cur; }

int i18n_count(void) { return LANG_N; }

const char *i18n_name(int lang)
{
    if (lang < 0 || lang >= LANG_N) return "?";
    return LANGS[lang].name;
}

const char *i18n_code(int lang)
{
    if (lang < 0 || lang >= LANG_N) return "?";
    return LANGS[lang].code;
}

int i18n_find(const char *code)
{
    if (!code) return -1;
    for (int i = 0; i < LANG_N; i++)
        if (strcmp(code, LANGS[i].code) == 0) return i;
    return -1;
}