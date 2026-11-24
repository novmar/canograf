# sim - nahledy obrazovek bez desky

Prelozi `src/ui.c` proti LVGL 8.4.0 na hostiteli a vyrobi binarku
`lv_shot`, ktera vyfoti kazdou obrazovku do PNG 800x480.

Zadny displej, zadne SDL, zadny X server. LVGL kresli do pametoveho
bufferu, vlastni flush callback si ho prekopiruje a PNG zapise
`stb_image_write.h` (vendorovany v `third_party/`).

## Preklad a spusteni

```sh
cmake -S sim -B sim/build
cmake --build sim/build
./sim/build/lv_shot /tmp/shots
```

Vznikne `00-MAIN.png` az `05-SETUP.png`.

Druhy, nepovinny parametr je kod jazyka - `en`, `cs`, `de`, viz
`lang/README.md`. Bez nej se kresli vychozi anglictina:

```sh
./sim/build/lv_shot /tmp/shots/cs cs
```

Nazvy souboru vychazeji z prelozenych nazvu stranek, takze kazdy jazyk
patri do vlastniho adresare. Do nazvu souboru jde jen ASCII.

Potreba je jen cmake >= 3.16, C kompilator a git (LVGL se stahuje
pres FetchContent). Kdyz uz LVGL 8.4.0 nekde mate, muzete stahovani
preskocit:

```sh
cmake -S sim -B sim/build -DLVGL_DIR=.pio/libdeps/waveshare43b/lvgl
```

## Jak to funguje

`src/ui.c` nezna Arduino.h, WiFi.h ani ESP32_Display_Panel. Vse, co umi
jen deska (podsviceni, NVS, WiFi, volna pamet, pokus se softwarovym PWM),
dostava pres `ui_host_t` z `src/ui.h`. Firmware ji v `main.cpp` naplni,
simulator necha ukazatele NULL a `ui.c` si s tim poradi.

Cas je virtualni: `lv_shot` sam posouva `lv_tick_inc(20)`, takze beh
nezavisi na rychlosti stroje a obrazky vychazeji porad stejne. Pred
kazdym snimkem probehne 60 kroku (1,2 s), aby sinusovky simulovanych
dat dojely na hodnoty a graf se naplnil.

Stejne to dela i GitHub Actions - job `screenshots` v
`.github/workflows/ci.yml` nahraje PNG jako artefakt.
