/*
 * Overeni regulace jasu pres plosku BL.
 *
 * Zapojeni: jediny dratek z GPIO6 (ploska na hrane modulu ESP32-S3-WROOM-1,
 * sesta od rohu: GND, 3V3, EN, IO4, IO5, IO6) na plosku BL vedle konektoru
 * LCD pasku. Zem se pajet nemusi, modul i budic sdileji zem desky.
 *
 * Jak to funguje: BL vede pres R11 10k na uzel, ktery ma C11 1uF na zem a
 * R7 68k na vstup FB budice AP3032. R11 a C11 tvori dolni propust s casovou
 * konstantou 10 ms, takze PWM nad asi 500 Hz se vyhladi na stejnosmerne
 * napeti. FB je regulovany uzel drzeny na 200 mV; proud vstrikly pres R7 se
 * musi vyrusit proudem pres R6 5,1k, a ten tece snimacim rezistorem R8 3,3R.
 * Proudove zesileni R6/R8 je 1545x, proto staci desitky mikroamper.
 *
 * Polarita je OBRACENA:
 *   strida 0 %   -> 0 V na plosce   -> plny jas
 *   strida 100 % -> 3,3 V na plosce -> zhasnuto
 *
 * Firmware nedela nic jineho: zadna LVGL, zadny panel. Na displeji bude jen
 * nahodny obsah nebo bila plocha - jde o to videt, jak plynule meni jas.
 */
#include <Arduino.h>
#include <Wire.h>

static const int PIN_SDA = 8;
static const int PIN_SCL = 9;
static const int PIN_BL = 6;

/* Filtr R11*C11 ma casovou konstantu 10 ms. 5 kHz je dve dekady nad nim,
 * takze zvlneni na uzlu je pod jedno promile a pritom je frekvence dost
 * nizka, aby na nic nenarazela. */
static const int PWM_HZ = 5000;
static const int PWM_BITS = 12;
static const int DUTY_MAX = (1 << PWM_BITS) - 1;

/* Jeden cely cyklus: rozsvitit a zase zhasnout. */
static const uint32_t CYCLE_MS = 3000;

/*
 * CH422G drzi signal DISP, ktery povoluje budic podsviceni i samotny panel.
 * Rezistor R2 4,7k tahne DISP na 5 V, takze po studenem startu je podsviceni
 * zapnute i bez nas. Jenze expander si pri pouhem resetu procesoru pamatuje
 * predchozi stav - kdyby ho hlavni firmware nechal stazeny dolu, zustala by
 * obrazovka tmava a test by vypadal jako nefunkcni. Proto ho nastavime.
 *
 * Pozor na EXIO0 a EXIO5: to jsou VSTUPY z izolovanych svorek DI0 a DI1.
 * Zvednout je nahoru by znamenalo tlacit proti tranzistoru optoclenu, proto
 * jdou dolu. Nahoru jdou jen EXIO1 (reset dotyku), EXIO2 (DISP),
 * EXIO3 (reset LCD) a EXIO4 (vyber SD karty, neaktivni ve vysoke urovni).
 */
static void expander_enable_display(void)
{
    const uint8_t CH422G_SET = 0x24; /* systemove nastaveni */
    const uint8_t CH422G_OUT = 0x38; /* vystupni registr IO0-7 */
    const uint8_t OUT_VALUE = 0x1E;  /* 0b00011110 */

    Wire.begin(PIN_SDA, PIN_SCL, 400000);

    Wire.beginTransmission(CH422G_SET);
    Wire.write(0x01); /* povolit vystupy */
    uint8_t e1 = Wire.endTransmission();

    Wire.beginTransmission(CH422G_OUT);
    Wire.write(OUT_VALUE);
    uint8_t e2 = Wire.endTransmission();

    Serial.printf("expander: nastaveni %s, vystupy %s (0x%02X)\n",
                  e1 == 0 ? "ok" : "CHYBA", e2 == 0 ? "ok" : "CHYBA",
                  OUT_VALUE);
}

void setup(void)
{
    Serial.begin(115200);
    delay(300);
    Serial.println("\nbl-fade - zkouska regulace jasu pres plosku BL");
    Serial.printf("GPIO%d, %d Hz, %d bitu, cyklus %lu ms\n", PIN_BL, PWM_HZ,
                  PWM_BITS, (unsigned long)CYCLE_MS);
    Serial.println("strida 0 % = plny jas, 100 % = zhasnuto");

    expander_enable_display();

    if (!ledcAttach(PIN_BL, PWM_HZ, PWM_BITS)) {
        Serial.println("CHYBA: LEDC se nepodarilo nastavit");
        return;
    }
    ledcWrite(PIN_BL, 0); /* zacneme na plnem jasu */
}

void loop(void)
{
    static uint32_t last_report = 0;

    uint32_t t = millis() % CYCLE_MS;
    uint32_t half = CYCLE_MS / 2;

    /* Trojuhelnik: prvni pulka cyklu tmavne, druha se rozjasnuje.
     * Protoze je polarita obracena, stoupajici strida znamena tmu. */
    uint32_t duty = (t < half) ? (DUTY_MAX * t / half)
                               : (DUTY_MAX * (CYCLE_MS - t) / half);

    ledcWrite(PIN_BL, duty);

    /* Vypis jednou za pul vteriny, at je v terminalu videt, ze to bezi,
     * a at se da porovnat cislo s tim, co je videt na displeji. */
    if (millis() - last_report >= 500) {
        last_report = millis();
        Serial.printf("strida %3lu %%  (%lu/%d)  napeti na plosce %.2f V\n",
                      (unsigned long)(duty * 100 / DUTY_MAX),
                      (unsigned long)duty, DUTY_MAX, duty * 3.3 / DUTY_MAX);
    }

    delay(10);
}
