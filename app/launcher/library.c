/* The game library: scanning games/, reading each manifest.json, the
 * loader's own settings (config/loader.json) and launching a game. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "cJSON.h"
#include "fs.h"
#include "launcher.h"
#include "log.h"

const char *title(int i)
{
    return g.info[i].title[0] ? g.info[i].title : g.games[i];
}

/* --------------------------------------------------------------- settings */

/* The loader's own settings; the controller files stay separate (their
 * parser rejects unknown keys). */
void load_settings(void)
{
    char err[128];
    char *text = rsl_read_file(LOADER_CFG, 4096, NULL, err, sizeof err);
    cJSON *j = text ? cJSON_Parse(text) : NULL;
    free(text);
    const char *layout = cJSON_GetStringValue(cJSON_GetObjectItem(j, "library_layout"));
    const char *accent = cJSON_GetStringValue(cJSON_GetObjectItem(j, "accent"));
    cJSON *hue = cJSON_GetObjectItem(j, "accent_hue");
    g.layout = layout && !strcmp(layout, "grid") ? LAYOUT_GRID : LAYOUT_ROW;
    g.accent = accent && accent[0] == '#' ? (uint32_t)strtoul(accent + 1, NULL, 16) & 0xffffff :
               accent && !strcmp(accent, "blue") ? 0x25d0f2 : DEFAULT_ACCENT;
    g.accent_hue = cJSON_IsNumber(hue) ? ((int)hue->valuedouble % 360 + 360) % 360 : DEFAULT_HUE;
    m.help_seen = cJSON_IsTrue(cJSON_GetObjectItem(j, "help_seen"));
    cJSON_Delete(j);
}

void save_settings(void)
{
    static const char *const layouts[] = { "row", "grid" };
    char text[240], err[128];
    int n = snprintf(text, sizeof text, "{\n  \"library_layout\": \"%s\",\n"
                     "  \"accent\": \"#%06x\",\n  \"accent_hue\": %d,\n  \"help_seen\": %s\n}\n",
                     layouts[g.layout], (unsigned)g.accent, g.accent_hue,
                     m.help_seen ? "true" : "false");
    if (!rsl_write_atomic(LOADER_CFG, text, (size_t)n, err, sizeof err))
        status(ST_BAD, "Settings not saved: %s", err);
}

/* ------------------------------------------------------------------ library */

/* A file the manifest names, inside the game's folder; "" if it is not
 * there or the name tries to leave the folder. */
static void game_file(char *out, size_t size, const char *game, const char *name)
{
    struct stat st;
    out[0] = '\0';
    if (!name || !name[0] || name[0] == '/' || strstr(name, "..") || strchr(name, ':') || strlen(name) > 100)
        return;
    snprintf(out, size, GAMES_DIR "/%.63s/%.100s", game, name);
    if (stat(out, &st) != 0 || !S_ISREG(st.st_mode))
        out[0] = '\0';
}

/* What a game needs, from the manifest.json the importer writes
 * (docs/IMPORTER.md). A game copied by hand has none and gets the default
 * runtime. Fills gi->problem when the game must not start. */
void read_manifest(const char *game, struct game_info *gi)
{
    char path[256], err[128];
    struct stat st;
    memset(gi, 0, sizeof *gi);
    strcpy(gi->runtime, "renpy-8.5.3");
    snprintf(path, sizeof path, GAMES_DIR "/%s/manifest.json", game);
    char *text = rsl_read_file(path, 1 << 20, NULL, err, sizeof err);
    if (text) {
        cJSON *j = cJSON_Parse(text);
        free(text);
        const char *format = cJSON_GetStringValue(cJSON_GetObjectItem(j, "format"));
        const char *state = cJSON_GetStringValue(cJSON_GetObjectItem(j, "status"));
        const char *runtime = cJSON_GetStringValue(cJSON_GetObjectItem(j, "runtime"));
        const char *version = cJSON_GetStringValue(cJSON_GetObjectItem(j, "renpy_version"));
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(j, "title"));
        const char *reason = cJSON_GetStringValue(cJSON_GetArrayItem(cJSON_GetObjectItem(j, "reasons"), 0));
        snprintf(gi->title, sizeof gi->title, "%s", name ? name : "");
        snprintf(gi->renpy, sizeof gi->renpy, "%s", version ? version : "?");
        snprintf(gi->state, sizeof gi->state, "%s", state ? state : "?");
        game_file(gi->cover, sizeof gi->cover, game, cJSON_GetStringValue(cJSON_GetObjectItem(j, "cover")));
        gi->recompile = cJSON_IsTrue(cJSON_GetObjectItem(j, "recompile"));
        cJSON *issues = cJSON_GetObjectItem(cJSON_GetObjectItem(j, "performance"), "issues");
        gi->issues = cJSON_GetArraySize(issues);
        for (int i = 0; i < 3 && i < gi->issues; i++) {
            const char *text = cJSON_GetStringValue(cJSON_GetArrayItem(issues, i));
            snprintf(gi->issue[i], sizeof gi->issue[i], "%s", text ? text : "?");
        }
        gi->runtime[0] = '\0';
        if (!format || strcmp(format, "renpy-switch-loader/manifest") != 0)
            snprintf(gi->problem, sizeof gi->problem, "manifest.json is unreadable; export the game again.");
        else if (!state || strcmp(state, "unsupported") == 0 || !runtime)
            snprintf(gi->problem, sizeof gi->problem, "cannot run here: %.130s", reason ? reason : "unsupported");
        else if (!runtime[0] || strlen(runtime) >= sizeof gi->runtime ||
                 strspn(runtime, "abcdefghijklmnopqrstuvwxyz0123456789.-") != strlen(runtime))
            snprintf(gi->problem, sizeof gi->problem, "bad runtime name in manifest.json.");
        else
            strcpy(gi->runtime, runtime);
        cJSON_Delete(j);
    }
    if (!gi->runtime[0])
        return;
    char runtime[sizeof gi->runtime], nro[sizeof gi->nro];
    strcpy(runtime, gi->runtime);
    snprintf(nro, sizeof nro, ROOT "/runtimes/%s.nro", runtime);
    /* renpy-8.5.3 is the runtime installed as renpy-runtime.nro. */
    if (stat(nro, &st) != 0 && strcmp(runtime, "renpy-8.5.3") == 0)
        snprintf(nro, sizeof nro, "%s", RUNTIME_NRO);
    strcpy(gi->nro, nro);
    gi->runtime_ok = stat(nro, &st) == 0;
    if (!gi->runtime_ok)
        snprintf(gi->problem, sizeof gi->problem, "needs runtime %s; copy it to %.90s", runtime, nro);
}

void scan_games(void)
{
    g.game_count = 0;
    DIR *d = opendir(GAMES_DIR);
    if (!d) {
        status(ST_BAD, "Cannot open %s", GAMES_DIR);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) && g.game_count < MAX_GAMES) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof g.games[0])
            continue;
        char p[512];
        struct stat st;
        snprintf(p, sizeof p, "%s/%s", GAMES_DIR, e->d_name);
        if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
        strcpy(g.games[g.game_count], e->d_name);
        snprintf(p, sizeof p, "%s/%s/game", GAMES_DIR, e->d_name);
        g.game_ok[g.game_count] = stat(p, &st) == 0 && S_ISDIR(st.st_mode);
        read_manifest(e->d_name, &g.info[g.game_count]);
        g.game_count++;
    }
    closedir(d);
    rsl_log("library scan: %d folder(s)", g.game_count);
}

/* ------------------------------------------------------------------ actions */

/* Exit to `nro` (via the Homebrew loader) and have it bring us back after. */
void chain_load(const char *nro, const char *args)
{
    struct stat st;
    char argv[1024];
    if (!envHasNextLoad()) {
        status(ST_BAD, "Cannot start other programs: launch the loader from the Homebrew Menu.");
        return;
    }
    if (stat(nro, &st) != 0) {
        status(ST_BAD, "Runtime not installed: %s", nro);
        return;
    }
    /* libnx splits on spaces and strips "..." around an argument. */
    snprintf(argv, sizeof argv, "\"%s\" --return \"%s\" %s", nro, m.self_path, args);
    Result rc = envSetNextLoad(nro, argv);
    if (R_FAILED(rc)) {
        status(ST_BAD, "envSetNextLoad failed: 0x%x", (unsigned)rc);
        return;
    }
    rsl_log("chain-loading: %s", argv);
    m.quit = true;
}

void launch(int i)
{
    char args[512];
    if (!g.game_ok[i]) {
        status(ST_BAD, "%s has no game/ folder; copy the whole game folder.", g.games[i]);
    } else if (strchr(g.games[i], '"')) {
        status(ST_BAD, "Rename %s: folder names cannot contain quotes.", g.games[i]);
    } else if (!g.full_memory) {
        /* Applet mode (the loader opened from the Album): a Ren'Py game took the
         * whole console down with a fatal error, twice (hardware runs 8 and 26),
         * also with a 64 MB image cache. Games do not start here, no override;
         * the notice says how to get full memory. */
        rsl_log("%s: not started, applet mode (%llu MiB)", g.games[i], (unsigned long long)(g.mem_total >> 20));
        g.dialog_open = true;
    } else if (g.info[i].problem[0]) {
        status(ST_BAD, "%s: %s", title(i), g.info[i].problem);
    } else {
        rsl_log("%s: runtime %s (%s)", g.games[i], g.info[i].runtime, g.info[i].nro);
        snprintf(args, sizeof args, "--game \"" GAMES_DIR "/%s\"", g.games[i]);
        chain_load(g.info[i].nro, args);
    }
}
