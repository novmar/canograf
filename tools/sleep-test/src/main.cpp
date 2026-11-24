/*
 * Mereni klidoveho odberu po krocich.
 *
 * Smysl: nezajima nas jen kolik deska bere, ale KDE ten proud je. Firmware
 * proto projde stavy jeden po druhem, kazdy drzi 20 vterin a predem rekne
 * do seriove linky, co prave vypnul. Rozdil mezi dvema kroky je cena te
 * jedne veci.
 *
 * Jak merit:
 *   1. Nahrat, odpojit terminal (jen napajeni), USB meraka dat do cesty.
 *   2. Opsat proud na konci kazdeho kroku - hodnota se musi ustalit.
 *   3. Kroky jdou samy za sebou, posledni je hluboky spanek.
 *
 * DULEZITE OMEZENI, at se vysledek necte spatne:
 *   - Merime 5V vetev z USB. Vozidlo napaji 7-36 V pres menic SY8293FCC,
 *     takze proud z baterie je zhruba  I_5V * 5 / 12 / ucinnost.
 *     Pri ucinnosti 0,85 to vychazi asi na 0,49 nasobek.
 *   - Pri napajeni z USB zustava zivy prevodnik USB-serial v ESP32-S3,
 *     takze hluboky spanek NEBUDE tak hluboky jako pri napajeni ze svorek.
 *     Namerena hodnota je tedy HORNI MEZ, ne skutecne dno.
 *   - Budic CAN TJA1051 ma ridici vyvod natvrdo na zemi, takze je porad
 *     v normalnim rezimu a vypnout se neda. To je dno, pod ktere se bez
 *     zasahu do hardwaru nedostaneme - a zaroven je to ta soucastka, ktera
 *     umi desku probudit provozem na sbernici.
 */
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

static const int PIN_SDA = 8;
static const int PIN_SCL = 9;

/* CH422G: registr je zakodovany v adrese, posila se jeden datovy bajt. */
static const uint8_t CH422G_SET = 0x24;
static const uint8_t CH422G_OUT = 0x38;

/* GT911 dotykovy radic. Adresa zavisi na stavu pri resetu, zkusime obe. */
static const uint8_t GT911_A = 0x5D;
static const uint8_t GT911_B = 0x14;

static const uint32_t STEP_MS = 20000;

static void expander_write(uint8_t value)
{
    Wire.beginTransmission(CH422G_SET);
    Wire.write(0x01); /* povolit vystupy */
    Wire.endTransmission();
    Wire.beginTransmission(CH422G_OUT);
    Wire.write(value);
    Wire.endTransmission();
}

/* EXIO0 a EXIO5 jsou VSTUPY DI0/DI1 - nechavame je dole, zvedat je proti
 * optoclenum by bylo spatne. EXIO1 reset dotyku, EXIO2 DISP, EXIO3 reset
 * LCD, EXIO4 vyber SD (neaktivni nahore). */
static const uint8_t EXP_DISPLAY_ON = 0x1E; /* 0b00011110 */
static const uint8_t EXP_DISPLAY_OFF = 0x1A; /* totez bez EXIO2 */

static bool gt911_sleep(void)
{
    const uint8_t addrs[2] = { GT911_A, GT911_B };
    for (int i = 0; i < 2; i++) {
        Wire.beginTransmission(addrs[i]);
        Wire.write(0x80); /* registr 0x8040 = prikaz */
        Wire.write(0x40);
        Wire.write(0x05); /* 5 = uspat */
        if (Wire.endTransmission() == 0) {
            Serial.printf("  dotyk uspan na adrese 0x%02X\n", addrs[i]);
            return true;
        }
    }
    Serial.println("  dotyk NEODPOVEDEL - uspani se nepovedlo");
    return false;
}

static void step(const char *what)
{
    Serial.printf("\n=== %s ===\n", what);
    Serial.printf("    drzim %lu s, opis proud az se ustali\n",
                  (unsigned long)(STEP_MS / 1000));
    uint32_t t0 = millis();
    while (millis() - t0 < STEP_MS) {
        delay(200);
    }
}

void setup(void)
{
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n\nsleep-test - kde je klidovy odber");
    Serial.println("merime 5V vetev z USB; proud z baterie je asi 0,49x");
    Serial.println("pozor: pri napajeni z USB je spanek melci nez ze svorek");

    Wire.begin(PIN_SDA, PIN_SCL, 400000);

    /* Krok 0: vychozi stav, displej zapnuty. Panel sice nic nekresli,
     * ale podsviceni sviti a radic bezi - to je ta nejvetsi polozka. */
    expander_write(EXP_DISPLAY_ON);
    step("0: displej ZAPNUTY, WiFi jeste nebezi");

    /* Krok 1: zhasnuty panel i podsviceni. DISP vede na obe veci zaroven,
     * takze jeden bit vypne oboji. */
    expander_write(EXP_DISPLAY_OFF);
    step("1: displej VYPNUTY (EXIO2 dole)");

    /* Krok 2: dotykovy radic do spanku. Sam o sobe je to male cislo, ale
     * merime ho zvlast, at vime, jestli stoji za to ho resit. */
    gt911_sleep();
    step("2: + dotykovy radic uspan");

    /* Krok 3: radio uplne pryc. WiFi.mode(WIFI_OFF) nestaci, chceme i
     * zastavit ovladac, jinak zustane cast napajeni zapnuta. */
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    esp_wifi_deinit();
    step("3: + WiFi ovladac zastaven");

    /* Krok 4: lehky spanek. Procesor stoji, pamet si pamatuje, periferie
     * zustavaji napajene. Probuzeni casovacem, aby slo pokracovat. */
    Serial.println("\n=== 4: lehky spanek, 20 s ===");
    Serial.println("    opis proud; procesor stoji, periferie bezi dal");
    Serial.flush();
    esp_sleep_enable_timer_wakeup((uint64_t)STEP_MS * 1000ULL);
    esp_light_sleep_start();
    Serial.println("    lehky spanek skoncil");

    /* Krok 5: hluboky spanek. Zustava jen RTC domena. Probuzeni casovacem
     * za 30 s; po nem deska NABEHNE ZNOVU od zacatku, takze tenhle krok je
     * posledni a mereni se opakuje dokola.
     *
     * Kdyz chces misto casovace zkusit probuzeni provozem na sbernici,
     * odkomentuj radek s GPIO16 (CAN RX): sbernice v klidu drzi vysokou
     * uroven, dominantni bit ji strhne dolu, a to desku probudi. */
    Serial.println("\n=== 5: HLUBOKY SPANEK, 30 s ===");
    Serial.println("    tohle je dno, ktereho umime dosahnout softwarove");
    Serial.println("    po probuzeni deska nabehne znovu a mereni se opakuje");
    Serial.flush();
    delay(100);

    esp_sleep_enable_timer_wakeup(30ULL * 1000ULL * 1000ULL);
    /* esp_sleep_enable_ext0_wakeup(GPIO_NUM_16, 0); */
    esp_deep_sleep_start();
}

void loop(void)
{
    /* sem se to nikdy nedostane, setup() konci hlubokym spankem */
}
