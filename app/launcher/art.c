/* Change picture (the detail page): an Album screenshot becomes a game's
 * cover (the framed part) or backdrop (all of it). The system's Album
 * service decodes the screenshots: their JPEGs are 4:4:0, which TJpgDec
 * cannot read. The loader has no JPEG encoder, so the result is a 24-bit
 * BMP next to game/ (LVGL reads it), named in manifest.json as chosen, which
 * the importer keeps on --replace. docs/LOADER_UI.md "Covers". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "cJSON.h"
#include "fs.h"
#include "launcher.h"
#include "log.h"
#include "ui.h"

#define SHOT_W     1280      /* every Switch screenshot, handheld or docked */
#define SHOT_H     720
#define CROP_W     (SHOT_H * 2 / 3)
#define CROP_STEP  80
#define COVER_OUT_W 300      /* the importer's sizes (docs/LOADER_UI.md) */
#define COVER_OUT_H 450

static CapsAlbumEntry *shots;   /* the Album's screenshots, newest first */
static int loaded = -1;         /* index in g.shot, -1 for none */
static bool loaded_ok;
static uint32_t moved_at;

static uint64_t when(const CapsAlbumEntry *e)
{
    const CapsAlbumFileDateTime *d = &e->file_id.datetime;
    return (((((uint64_t)d->year * 13 + d->month) * 32 + d->day) * 24 + d->hour) * 60 + d->minute) * 60 * 256 +
           d->second * 256 + d->id;
}

static int newest_first(const void *a, const void *b)
{
    uint64_t x = when(a), y = when(b);
    return x < y ? 1 : x > y ? -1 : 0;
}

static void close_picture(void)
{
    free(shots);
    free(g.shot);   /* the page drops its image first (app/ui/ui.c, build) */
    shots = NULL;
    g.shot = NULL;
    g.shot_count = 0;
    capsaExit();
}

static void back_to_details(void)
{
    go(SCR_DETAIL);   /* picture_tick() closes the Album */
    g.sel = DETAIL_PICTURE;
}

void open_picture(int i)
{
    char path[256];
    struct stat st;
    snprintf(path, sizeof path, GAMES_DIR "/%s/manifest.json", g.games[i]);
    if (stat(path, &st) != 0) {
        status(ST_BAD, "%s has no manifest.json, which names its pictures: export it with the importer first.",
               title(i));
        return;
    }
    Result rc = capsaInitialize();
    if (R_FAILED(rc)) {
        status(ST_BAD, "The Album cannot be opened (0x%x).", (unsigned)rc);
        return;
    }
    int n = 0;
    for (int s = CapsAlbumStorage_Nand; s <= CapsAlbumStorage_Sd; s++) {
        u64 count = 0, got = 0;
        if (R_FAILED(capsaGetAlbumFileCount(s, &count)) || !count)
            continue;
        CapsAlbumEntry *more = realloc(shots, (n + count) * sizeof *shots);
        if (!more)
            break;
        shots = more;
        if (R_FAILED(capsaGetAlbumFileList(s, &got, shots + n, count)))
            got = 0;
        int start = n;
        for (u64 k = 0; k < got; k++)   /* screenshots, not videos */
            if (shots[start + k].file_id.content == CapsAlbumFileContents_ScreenShot)
                shots[n++] = shots[start + k];
    }
    g.shot = n ? calloc(SHOT_W * SHOT_H, 4) : NULL;   /* black until the first one is loaded */
    if (!g.shot) {
        close_picture();
        status(n ? ST_BAD : ST_INFO, n ? "Not enough memory for a screenshot." :
               "No screenshots in the Album yet. Press the Capture button on a game's title screen, then come back.");
        return;
    }
    qsort(shots, n, sizeof *shots, newest_first);
    rsl_log("change picture: %s, %d screenshot(s) in the Album", g.games[i], n);
    go(SCR_PICTURE);
    g.shot_w = SHOT_W;
    g.shot_h = SHOT_H;
    g.shot_count = n;
    g.crop_x = (SHOT_W - CROP_W) / 2;
    g.shot_date[0] = '\0';
    loaded = -1;
    moved_at = 0;
}

/* Every frame. The focused screenshot into g.shot, once the focus has stayed
 * on it 120 ms: holding a direction does not decode every picture it passes.
 * Off the page (also after the emergency combo), the Album is closed. */
void picture_tick(void)
{
    int x;
    if (ui_frame_drag(&x) && g.screen == SCR_PICTURE && !g.menu_open)   /* a finger on the screenshot */
        g.crop_x = x < 0 ? 0 : x > SHOT_W - CROP_W ? SHOT_W - CROP_W : x;
    if (g.screen != SCR_PICTURE && g.shot)
        close_picture();
    if (!g.shot || g.sel == loaded || g.now - moved_at < 120)
        return;
    const CapsAlbumEntry *e = &shots[g.sel];
    const CapsAlbumFileDateTime *d = &e->file_id.datetime;
    u64 w = 0, h = 0;
    void *work = malloc(e->size ? e->size : 1);   /* at least the JPEG's size */
    Result rc = work ? capsaLoadAlbumScreenShotImage(&w, &h, &e->file_id, g.shot, SHOT_W * SHOT_H * 4, work,
                                                     e->size)
                     : MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    free(work);
    loaded = g.sel;
    loaded_ok = R_SUCCEEDED(rc) && w == SHOT_W && h == SHOT_H;
    if (loaded_ok) {
        uint32_t *p = g.shot;   /* R,G,B,A in memory -> B,G,R,X (XRGB8888) */
        for (int k = 0; k < SHOT_W * SHOT_H; k++)
            p[k] = (p[k] & 0xff00ff00u) | (p[k] & 0xff) << 16 | (p[k] >> 16 & 0xff);
    } else {
        memset(g.shot, 0, SHOT_W * SHOT_H * 4);
        status(ST_BAD, "This screenshot cannot be read (0x%x, %llux%llu).", (unsigned)rc,
               (unsigned long long)w, (unsigned long long)h);
    }
    snprintf(g.shot_date, sizeof g.shot_date, "%04u-%02u-%02u %02u:%02u", d->year, d->month, d->day, d->hour,
             d->minute);
    g.shot_gen++;
}

/* The part x, 0, w x h of the screenshot, scaled to ow x oh:
 * games/<game>/<key>.bmp. */
static bool save_bmp(int i, const char *key, int x, int w, int h, int ow, int oh, char *err, size_t errlen)
{
    char path[256];
    uint32_t *px = malloc((size_t)ow * oh * 4);
    if (!px) {
        snprintf(err, errlen, "not enough memory");
        return false;
    }
    ui_shot_part(x, 0, w, h, px, ow, oh);
    snprintf(path, sizeof path, GAMES_DIR "/%s/%s.bmp", g.games[i], key);
    bool ok = rsl_write_bmp(path, px, ow, oh, err, errlen);
    free(px);
    return ok;
}

/* "cover": "cover.bmp", "cover_custom": true in the game's
 * manifest.json; the rest stays as the importer wrote it. */
static bool name_in_manifest(int i, char *err, size_t errlen)
{
    char path[256], name[24];
    snprintf(path, sizeof path, GAMES_DIR "/%s/manifest.json", g.games[i]);
    char *text = rsl_read_file(path, 1 << 20, NULL, err, errlen);
    cJSON *j = text ? cJSON_Parse(text) : NULL;
    bool ok = false;
    if (text && !cJSON_IsObject(j))
        snprintf(err, errlen, "manifest.json is unreadable");
    free(text);
    if (cJSON_IsObject(j)) {
        snprintf(name, sizeof name, "cover.bmp");
        cJSON_DeleteItemFromObject(j, "cover");
        cJSON_AddStringToObject(j, "cover", name);
        cJSON_DeleteItemFromObject(j, "cover_custom");
        cJSON_AddTrueToObject(j, "cover_custom");
        char *out = cJSON_Print(j);
        ok = out && rsl_write_atomic(path, out, strlen(out), err, errlen);
        if (!out)
            snprintf(err, errlen, "not enough memory");
        free(out);
    }
    cJSON_Delete(j);
    return ok;
}

/* The picture first, then the manifest: if that fails, the game keeps the
 * cover it had. */
static void use_picture(void)
{
    int i = g.game;
    char err[128] = "";
    if (!save_bmp(i, "cover", g.crop_x, CROP_W, SHOT_H, COVER_OUT_W, COVER_OUT_H, err, sizeof err) ||
        !name_in_manifest(i, err, sizeof err)) {
        status(ST_BAD, "Picture not saved: %s", err);
        return;
    }
    char path[sizeof g.info[i].cover];
    snprintf(path, sizeof path, GAMES_DIR "/%s/cover.bmp", g.games[i]);
    strcpy(g.info[i].cover, path);
    ui_art_changed(i);
    back_to_details();
    status(ST_OK, "Cover saved for %s.", title(i));
}

/* Left/right: newer/older screenshot. L/R: move the cover's frame.
 * Confirm: the framed part becomes the game's cover. */
void picture_action(rsl_action a)
{
    switch (a) {
    case RSL_ACT_LEFT:
        if (g.sel > 0)
            g.sel--, moved_at = g.now;
        break;
    case RSL_ACT_RIGHT:
        if (g.sel < g.shot_count - 1)
            g.sel++, moved_at = g.now;
        break;
    case RSL_ACT_ROLLBACK:
        g.crop_x = g.crop_x > CROP_STEP ? g.crop_x - CROP_STEP : 0;
        break;
    case RSL_ACT_ROLLFORWARD:
        g.crop_x = g.crop_x + CROP_STEP < SHOT_W - CROP_W ? g.crop_x + CROP_STEP : SHOT_W - CROP_W;
        break;
    case RSL_ACT_CONFIRM:
        if (loaded == g.sel && loaded_ok)
            use_picture();
        break;
    case RSL_ACT_BACK:
        back_to_details();
        break;
    default:
        break;
    }
}
