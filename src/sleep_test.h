/*
 * sleep_test.h - mereni klidoveho odberu misto normalniho startu
 *
 * Zapina se prekladem: CANOGRAF_SLEEP_TEST=on v local.env (nebo v
 * config/default.env, kdyz to ma platit pro cely repozitar). Vychozi je
 * vypnuto, takze verejny build je vzdy normalni firmware.
 *
 * Co se meri, jak se to cte a cemu merena cisla NEodpovidaji, je
 * sepsane v src/sleep_test.cpp. Kdo bude cisla zapisovat, musi si to
 * precist - jinak si 5V vetev splete s odberem z baterie.
 */
#ifndef SLEEP_TEST_H
#define SLEEP_TEST_H

/* Klic dodava scripts/build_config.py z config/*.env. Tahle pojistka je
 * pro preklady, ktere tim skriptem nejdou (hostitelsky build v sim/). */
#ifndef CANOGRAF_SLEEP_TEST
#define CANOGRAF_SLEEP_TEST 0
#endif

#if CANOGRAF_SLEEP_TEST

#ifdef __cplusplus
extern "C" {
#endif

/* NEVRACI SE - posledni krok mereni je hluboky spanek a po probuzeni
 * deska nabehne znovu od zacatku. Vola se jako uplne prvni vec v
 * setup(), aby se nerozjelo nic jineho. */
void sleep_test_run(void);

#ifdef __cplusplus
}
#endif

#endif /* CANOGRAF_SLEEP_TEST */

#endif /* SLEEP_TEST_H */
