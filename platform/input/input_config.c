/* Versioned JSON persistence for rsl_input_config. */
#include "input_config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "fs.h"

#define FORMAT_ID "renpy-switch-loader/controller"
#define MAX_CONFIG_BYTES (64 * 1024)

static const char *stick_name(rsl_stick s) { return s == RSL_STICK_RIGHT ? "right" : "left"; }

static int index_of(const char *const *names, int count, const char *s)
{
    for (int i = 0; i < count; i++)
        if (strcmp(names[i], s) == 0)
            return i;
    return -1;
}

char *rsl_config_to_json(const rsl_input_config *cfg)
{
    cJSON *root = cJSON_CreateObject(), *binds = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "format", FORMAT_ID);
    cJSON_AddNumberToObject(root, "version", RSL_INPUT_VERSION);
    cJSON_AddItemToObject(root, "bindings", binds);
    for (int a = 0; a < RSL_ACT_COUNT; a++) {
        cJSON *arr = cJSON_AddArrayToObject(binds, rsl_action_names[a]);
        for (int b = 0; b < RSL_BTN_COUNT; b++)
            if (cfg->bindings[a] & (1u << b))
                cJSON_AddItemToArray(arr, cJSON_CreateString(rsl_button_names[b]));
    }
    /* round so 0.15f is written as 0.15, not 0.15000000596 */
    cJSON_AddNumberToObject(root, "deadzone", round(cfg->deadzone * 1000.0) / 1000.0);
    cJSON_AddNumberToObject(root, "cursor_speed", round(cfg->cursor_speed * 1000.0) / 1000.0);
    cJSON_AddNumberToObject(root, "cursor_accel", round(cfg->cursor_accel * 1000.0) / 1000.0);
    cJSON_AddNumberToObject(root, "repeat_delay_ms", cfg->repeat_delay_ms);
    cJSON_AddNumberToObject(root, "repeat_interval_ms", cfg->repeat_interval_ms);
    cJSON_AddStringToObject(root, "nav_stick", stick_name(cfg->nav_stick));
    cJSON_AddStringToObject(root, "cursor_stick", stick_name(cfg->cursor_stick));
    cJSON_AddBoolToObject(root, "cursor_visible", cfg->cursor_visible);
    char *out = cJSON_Print(root);
    cJSON_Delete(root);
    return out;
}

#define BAD(...) do { snprintf(err, errlen, __VA_ARGS__); goto fail; } while (0)

static bool get_num(const cJSON *root, const char *key, double *out, char *err, size_t errlen)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!v)
        return true;   /* optional: keep default */
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble)) {
        snprintf(err, errlen, "\"%s\" must be a number", key);
        return false;
    }
    *out = v->valuedouble;
    return true;
}

static bool get_stick(const cJSON *root, const char *key, rsl_stick *out, char *err, size_t errlen)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!v)
        return true;
    if (cJSON_IsString(v) && strcmp(v->valuestring, "left") == 0)
        *out = RSL_STICK_LEFT;
    else if (cJSON_IsString(v) && strcmp(v->valuestring, "right") == 0)
        *out = RSL_STICK_RIGHT;
    else {
        snprintf(err, errlen, "\"%s\" must be \"left\" or \"right\"", key);
        return false;
    }
    return true;
}

bool rsl_config_from_json(const char *text, rsl_input_config *out, char *err, size_t errlen)
{
    rsl_input_config cfg;
    rsl_config_preset(&cfg, RSL_PRESET_DEFAULT);
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithOpts(text, &end, 0);
    if (!root)
        BAD("invalid JSON at byte %d", end ? (int)(end - text) : 0);
    if (!cJSON_IsObject(root))
        BAD("top level must be a JSON object");

    const cJSON *fmt = cJSON_GetObjectItemCaseSensitive(root, "format");
    if (!cJSON_IsString(fmt) || strcmp(fmt->valuestring, FORMAT_ID) != 0)
        BAD("\"format\" must be \"%s\"", FORMAT_ID);
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(ver) || ver->valueint < 1)
        BAD("missing or invalid \"version\"");
    if (ver->valueint > RSL_INPUT_VERSION)
        BAD("version %d was written by a newer loader (this one reads up to %d)",
            ver->valueint, RSL_INPUT_VERSION);
    /* Future: migrate older versions here. Actions missing from an older file
     * keep their default binding, so adding actions needs no migration. */

    const cJSON *binds = cJSON_GetObjectItemCaseSensitive(root, "bindings");
    if (binds && !cJSON_IsObject(binds))
        BAD("\"bindings\" must be an object");
    const cJSON *item;
    cJSON_ArrayForEach(item, binds) {
        int a = index_of(rsl_action_names, RSL_ACT_COUNT, item->string);
        if (a < 0)
            BAD("unknown action \"%s\"", item->string);
        if (!cJSON_IsArray(item))
            BAD("binding for \"%s\" must be a list of buttons", item->string);
        uint32_t mask = 0;
        const cJSON *btn;
        cJSON_ArrayForEach(btn, item) {
            int b = cJSON_IsString(btn) ? index_of(rsl_button_names, RSL_BTN_COUNT, btn->valuestring) : -1;
            if (b < 0)
                BAD("unknown button in \"%s\"", item->string);
            mask |= 1u << b;
        }
        cfg.bindings[a] = mask;
    }

    double dz = cfg.deadzone, sp = cfg.cursor_speed, ac = cfg.cursor_accel;
    double rd = cfg.repeat_delay_ms, ri = cfg.repeat_interval_ms;
    if (!get_num(root, "deadzone", &dz, err, errlen) ||
        !get_num(root, "cursor_speed", &sp, err, errlen) ||
        !get_num(root, "cursor_accel", &ac, err, errlen) ||
        !get_num(root, "repeat_delay_ms", &rd, err, errlen) ||
        !get_num(root, "repeat_interval_ms", &ri, err, errlen) ||
        !get_stick(root, "nav_stick", &cfg.nav_stick, err, errlen) ||
        !get_stick(root, "cursor_stick", &cfg.cursor_stick, err, errlen))
        goto fail;
    cfg.deadzone = (float)dz;
    cfg.cursor_speed = (float)sp;
    cfg.cursor_accel = (float)ac;
    cfg.repeat_delay_ms = rd < 0 ? 0 : rd > 1e6 ? 1000000u : (uint32_t)rd;
    cfg.repeat_interval_ms = ri < 0 ? 0 : ri > 1e6 ? 1000000u : (uint32_t)ri;
    const cJSON *vis = cJSON_GetObjectItemCaseSensitive(root, "cursor_visible");
    if (vis && !cJSON_IsBool(vis))
        BAD("\"cursor_visible\" must be true or false");
    if (vis)
        cfg.cursor_visible = cJSON_IsTrue(vis);

    rsl_config_sanitize(&cfg);
    rsl_action unbound;
    if (!rsl_config_complete(&cfg, &unbound))
        BAD("essential action \"%s\" has no button", rsl_action_names[unbound]);

    cJSON_Delete(root);
    *out = cfg;
    return true;
fail:
    cJSON_Delete(root);
    return false;
}

static bool load_one(const char *path, rsl_input_config *out, char *err, size_t errlen)
{
    char *text = rsl_read_file(path, MAX_CONFIG_BYTES, NULL, err, errlen);
    if (!text)
        return false;
    char why[256];
    bool ok = rsl_config_from_json(text, out, why, sizeof why);
    free(text);
    if (!ok)
        snprintf(err, errlen, "%s: %s", path, why);
    return ok;
}

rsl_load_result rsl_config_load(const char *path, rsl_input_config *out, char *err, size_t errlen)
{
    char bak[1024];
    err[0] = '\0';
    if (load_one(path, out, err, errlen))
        return RSL_LOAD_OK;
    snprintf(bak, sizeof bak, "%s.bak", path);
    char err2[256];
    if (load_one(bak, out, err2, sizeof err2))
        return RSL_LOAD_BACKUP;
    rsl_config_preset(out, RSL_PRESET_DEFAULT);
    return RSL_LOAD_DEFAULTS;
}

bool rsl_config_save(const char *path, const rsl_input_config *cfg, char *err, size_t errlen)
{
    rsl_action unbound;
    if (!rsl_config_complete(cfg, &unbound)) {
        snprintf(err, errlen, "\"%s\" needs a button before saving", rsl_action_names[unbound]);
        return false;
    }
    uint32_t clash = rsl_config_conflicts(cfg);
    if (clash) {
        int a = 0;
        while (!(clash & (1u << a)))
            a++;
        snprintf(err, errlen, "\"%s\" shares a button with another action", rsl_action_names[a]);
        return false;
    }
    char *json = rsl_config_to_json(cfg);
    if (!json) {
        snprintf(err, errlen, "out of memory");
        return false;
    }
    bool ok = rsl_write_atomic(path, json, strlen(json), err, errlen);
    free(json);
    return ok;
}
