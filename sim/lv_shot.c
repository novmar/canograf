/*
 * lv_shot - vyfoti kazdou obrazovku UI do PNG, bez displeje a bez SDL.
 *
 * LVGL kresli do pametoveho bufferu, vlastni flush callback si ho jen
 * prekopiruje a pak se z nej udela PNG. Cas je virtualni (lv_tick_inc),
 * takze beh je deterministicky a nezavisi na rychlosti stroje.
 *
 * Pouziti:  lv_shot <vystupni adresar> [kod jazyka]
 *
 * Bez kodu jazyka se kresli vychozi anglictina. "lv_shot /tmp/shots cs"
 * vyfoti ceskou verzi - tim se da zkontrolovat, ze se diakritika kresli
 * a ze se delsi preklady vejdou do rozvrzeni.
 */
#include <lvgl.h>

#include "ui.h"
#include "i18n.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define SCR_W 800
#define SCR_H 480

/* krok virtualniho casu; shodny s periodou lv_timeru na desce */
#define STEP_MS   20
/* kolik kroku nechat probehnout, nez se obrazovka vyfoti - pri 20 ms
 * to je 1,2 s, dost na dojeti sinusovek i na naplneni grafu */
#define WARM_STEPS 60

static lv_color_t draw_buf[SCR_W * SCR_H];
static lv_color_t frame[SCR_W * SCR_H];

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    for (lv_coord_t y = area->y1; y <= area->y2; y++) {
        if (y < 0 || y >= SCR_H) continue;
        for (lv_coord_t x = area->x1; x <= area->x2; x++) {
            if (x < 0 || x >= SCR_W) continue;
            frame[y * SCR_W + x] = color_p[(y - area->y1) * (area->x2 - area->x1 + 1)
                                           + (x - area->x1)];
        }
    }
    lv_disp_flush_ready(drv);
}

/* LVGL 8 umi ruzne hloubky barev; lv_color_to32 prevede kteroukoliv
 * na ARGB8888, takze zapis PNG na LV_COLOR_DEPTH nezavisi. */
static void frame_to_rgb(unsigned char *rgb)
{
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        uint32_t c = lv_color_to32(frame[i]);
        rgb[i * 3 + 0] = (unsigned char)((c >> 16) & 0xFF);
        rgb[i * 3 + 1] = (unsigned char)((c >> 8) & 0xFF);
        rgb[i * 3 + 2] = (unsigned char)(c & 0xFF);
    }
}

/* Nazvy stranek jsou prelozene, takze muzou obsahovat diakritiku i tecku.
 * Do nazvu souboru pustime jen holy ASCII. */
static void safe_name(char *dst, size_t n, const char *src)
{
    size_t k = 0;
    for (; *src && k + 1 < n; src++)
        if ((*src >= 'A' && *src <= 'Z') || (*src >= 'a' && *src <= 'z') ||
            (*src >= '0' && *src <= '9'))
            dst[k++] = *src;
    dst[k] = '\0';
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "pouziti: %s <vystupni adresar> [kod jazyka]\n", argv[0]);
        return 2;
    }
    const char *outdir = argv[1];

    int lang = 0;
    if (argc == 3) {
        lang = i18n_find(argv[2]);
        if (lang < 0) {
            fprintf(stderr, "neznamy jazyk %s; mam:", argv[2]);
            for (int i = 0; i < i18n_count(); i++)
                fprintf(stderr, " %s", i18n_code(i));
            fprintf(stderr, "\n");
            return 2;
        }
    }

    if (mkdir(outdir, 0777) != 0) {
        struct stat st;
        if (stat(outdir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            fprintf(stderr, "nelze vytvorit adresar %s\n", outdir);
            return 1;
        }
    }

    lv_init();

    static lv_disp_draw_buf_t dbuf;
    lv_disp_draw_buf_init(&dbuf, draw_buf, NULL, SCR_W * SCR_H);

    static lv_disp_drv_t ddrv;
    lv_disp_drv_init(&ddrv);
    ddrv.draw_buf = &dbuf;
    ddrv.hor_res = SCR_W;
    ddrv.ver_res = SCR_H;
    ddrv.flush_cb = flush_cb;
    ddrv.full_refresh = 1;      /* at je v bufferu vzdy cely obraz */
    lv_disp_drv_register(&ddrv);

    /* host zustava nenaplneny - ui.c musi NULL snest */
    ui_set_cfg(false, false, 0, lang);
    ui_build();

    unsigned char *rgb = malloc((size_t)SCR_W * SCR_H * 3);
    if (!rgb) { fprintf(stderr, "malloc selhal\n"); return 1; }

    int n = ui_page_count();
    for (int i = 0; i < n; i++) {
        ui_show_page(i);
        for (int s = 0; s < WARM_STEPS; s++) {
            lv_tick_inc(STEP_MS);
            ui_tick();
            lv_timer_handler();
        }
        /* vynutit prekresleni cele obrazovky, at ve frame neni nic stareho */
        lv_obj_invalidate(lv_scr_act());
        lv_refr_now(NULL);

        frame_to_rgb(rgb);

        char path[512], name[64];
        safe_name(name, sizeof(name), ui_page_name(i));
        snprintf(path, sizeof(path), "%s/%02d-%s.png", outdir, i, name);
        if (!stbi_write_png(path, SCR_W, SCR_H, 3, rgb, SCR_W * 3)) {
            fprintf(stderr, "zapis %s selhal\n", path);
            free(rgb);
            return 1;
        }
        printf("%s\n", path);
    }

    free(rgb);
    return 0;
}
