/* Host unit tests for the input subsystem. Run: scripts/test.sh */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fs.h"
#include "input.h"
#include "input_config.h"

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); } } while (0)
#define B(x) (1u << RSL_BTN_##x)
#define A(x) (1u << RSL_ACT_##x)

static rsl_raw_input raw(uint32_t buttons)
{
    rsl_raw_input r;
    memset(&r, 0, sizeof r);
    r.buttons = buttons;
    return r;
}

static int count(const rsl_input *in, rsl_event_type t, rsl_action a)
{
    int n = 0;
    for (int i = 0; i < in->event_count; i++)
        n += in->events[i].type == t && in->events[i].action == a;
    return n;
}

static void test_presets(void)
{
    for (int p = 0; p < RSL_PRESET_COUNT; p++) {
        rsl_input_config c;
        rsl_config_preset(&c, (rsl_preset)p);
        CHECK(rsl_config_conflicts(&c) == 0);
        CHECK(rsl_config_complete(&c, NULL));
        CHECK(rsl_config_sanitize(&c));   /* presets are already in range */
        CHECK(c.nav_stick != c.cursor_stick);
    }
    rsl_input_config c;
    rsl_config_preset(&c, RSL_PRESET_DEFAULT);
    /* Nintendo labels: A (right face button) confirms, B (bottom) cancels. */
    CHECK(c.bindings[RSL_ACT_CONFIRM] == B(A));
    CHECK(c.bindings[RSL_ACT_BACK] == B(B));
    CHECK(c.bindings[RSL_ACT_SKIP] == B(ZR));
    CHECK(c.bindings[RSL_ACT_CLICK] == B(RSTICK));
}

static void test_normalize(void)
{
    /* libnx bit order: A B X Y StickL StickR L R ZL ZR Plus Minus Left Up Right Down */
    CHECK(rsl_normalize_npad(1ull << 0) == B(A));
    CHECK(rsl_normalize_npad(1ull << 1) == B(B));   /* not swapped */
    CHECK(rsl_normalize_npad(1ull << 2) == B(X));
    CHECK(rsl_normalize_npad(1ull << 3) == B(Y));
    CHECK(rsl_normalize_npad(1ull << 4) == B(LSTICK));
    CHECK(rsl_normalize_npad(1ull << 5) == B(RSTICK));
    CHECK(rsl_normalize_npad(1ull << 12) == B(DLEFT));
    CHECK(rsl_normalize_npad(1ull << 13) == B(DUP));
    CHECK(rsl_normalize_npad(1ull << 15) == B(DDOWN));
    CHECK(rsl_normalize_npad((1ull << 24) | (1ull << 27)) == (B(SL) | B(SR)));
    CHECK(rsl_normalize_npad(1ull << 26) == B(SL));
    CHECK(rsl_normalize_npad(0xFF0000ull) == 0);    /* digital stick directions ignored */
    CHECK(rsl_normalize_npad(1ull << 63) == 0);
    for (int b = 0; b < 16; b++)                    /* 16 face/shoulder/dpad bits, one each */
        CHECK(__builtin_popcount(rsl_normalize_npad(1ull << b)) == 1);
}

static void test_bind_conflicts(void)
{
    rsl_input_config c;
    rsl_config_preset(&c, RSL_PRESET_DEFAULT);
    uint32_t clash = rsl_config_bind(&c, RSL_ACT_CONFIRM, RSL_BTN_B);
    CHECK(clash == A(BACK));
    CHECK(rsl_config_conflicts(&c) == (A(CONFIRM) | A(BACK)));
    char err[256];
    CHECK(!rsl_config_save("/nonexistent/should-not-write.json", &c, err, sizeof err));
    CHECK(strstr(err, "shares a button") != NULL);
    /* finishing the swap resolves it */
    CHECK(rsl_config_bind(&c, RSL_ACT_BACK, RSL_BTN_A) == 0);
    CHECK(rsl_config_conflicts(&c) == 0);

    c.bindings[RSL_ACT_BACK] = 0;
    rsl_action unbound;
    CHECK(!rsl_config_complete(&c, &unbound) && unbound == RSL_ACT_BACK);
    c.bindings[RSL_ACT_AUTO] = 0;   /* non-essential may be unbound */
    c.bindings[RSL_ACT_BACK] = B(A);
    CHECK(rsl_config_complete(&c, NULL));
}

static void test_press_release(void)
{
    rsl_input_config c;
    rsl_config_preset(&c, RSL_PRESET_DEFAULT);
    rsl_input in;
    rsl_input_init(&in, &c);
    rsl_raw_input r = raw(B(A));
    rsl_input_update(&in, &r, 0, 0.016f);
    CHECK(in.event_count == 1 && count(&in, RSL_EV_ACTION_DOWN, RSL_ACT_CONFIRM) == 1);
    CHECK(in.pressed_buttons == B(A));
    rsl_input_update(&in, &r, 2000, 0.016f);   /* PRESS mode never repeats */
    CHECK(in.event_count == 0);
    CHECK(in.pressed_buttons == 0);
    r = raw(0);
    rsl_input_update(&in, &r, 2016, 0.016f);
    CHECK(in.event_count == 1 && count(&in, RSL_EV_ACTION_UP, RSL_ACT_CONFIRM) == 1);

    r = raw(B(ZR));
    rsl_input_update(&in, &r, 3000, 0.016f);
    CHECK(rsl_action_active(&in, RSL_ACT_SKIP));
    rsl_input_update(&in, &r, 9000, 0.016f);
    CHECK(rsl_action_active(&in, RSL_ACT_SKIP) && in.event_count == 0);
}

static void test_repeat(void)
{
    rsl_input_config c;
    rsl_config_preset(&c, RSL_PRESET_DEFAULT);   /* delay 400, interval 90 */
    rsl_input in;
    rsl_input_init(&in, &c);
    rsl_raw_input r = raw(B(DDOWN));
    rsl_input_update(&in, &r, 1000, 0.016f);
    CHECK(count(&in, RSL_EV_ACTION_DOWN, RSL_ACT_DOWN) == 1);
    rsl_input_update(&in, &r, 1399, 0.016f);
    CHECK(in.event_count == 0);
    rsl_input_update(&in, &r, 1400, 0.016f);
    CHECK(count(&in, RSL_EV_ACTION_REPEAT, RSL_ACT_DOWN) == 1);
    rsl_input_update(&in, &r, 1489, 0.016f);
    CHECK(in.event_count == 0);
    rsl_input_update(&in, &r, 1490, 0.016f);
    CHECK(count(&in, RSL_EV_ACTION_REPEAT, RSL_ACT_DOWN) == 1);
    rsl_input_update(&in, &r, 9000, 0.016f);    /* stall: one repeat, no burst */
    CHECK(in.event_count == 1);
    rsl_input_update(&in, &r, 9010, 0.016f);
    CHECK(in.event_count == 0);
}

static void test_nav_stick(void)
{
    rsl_input_config c;
    rsl_config_preset(&c, RSL_PRESET_DEFAULT);
    rsl_input in;
    rsl_input_init(&in, &c);
    rsl_raw_input r = raw(0);
    r.stick_y[RSL_STICK_LEFT] = 32767;   /* libnx: +y is up */
    rsl_input_update(&in, &r, 0, 0.016f);
    CHECK(count(&in, RSL_EV_ACTION_DOWN, RSL_ACT_UP) == 1);
    CHECK(in.buttons == B(NAV_UP));

    r = raw(0);
    r.stick_x[RSL_STICK_LEFT] = 3000;    /* inside 15% deadzone */
    rsl_input_update(&in, &r, 16, 0.016f);
    CHECK(count(&in, RSL_EV_ACTION_UP, RSL_ACT_UP) == 1);
    CHECK(in.buttons == 0);

    r.stick_x[RSL_STICK_LEFT] = -30000;
    r.stick_y[RSL_STICK_LEFT] = -12000;  /* x dominates */
    rsl_input_update(&in, &r, 32, 0.016f);
    CHECK(count(&in, RSL_EV_ACTION_DOWN, RSL_ACT_LEFT) == 1 && in.event_count == 1);

    /* right stick does not navigate in the default preset */
    r = raw(0);
    r.stick_y[RSL_STICK_RIGHT] = 32767;
    rsl_input_update(&in, &r, 48, 0.016f);
    CHECK(count(&in, RSL_EV_ACTION_DOWN, RSL_ACT_UP) == 0);
}

static void test_cursor(void)
{
    rsl_input_config c;
    rsl_config_preset(&c, RSL_PRESET_DEFAULT);
    c.cursor_accel = 0;
    rsl_input in;
    rsl_input_init(&in, &c);
    rsl_raw_input r = raw(0);
    r.stick_x[RSL_STICK_RIGHT] = 1000;   /* deadzone */
    rsl_input_update(&in, &r, 0, 0.5f);
    CHECK(!in.cursor_moved && in.cursor_x == 0.5f);
    r.stick_x[RSL_STICK_RIGHT] = 32767;
    rsl_input_update(&in, &r, 16, 0.5f);
    CHECK(in.cursor_moved && fabsf(in.cursor_x - (0.5f + 0.6f * 0.5f)) < 1e-4f);
    for (int i = 0; i < 10; i++)
        rsl_input_update(&in, &r, 32, 0.5f);
    CHECK(in.cursor_x == 1.0f);          /* clamped */
    r = raw(0);
    r.stick_y[RSL_STICK_RIGHT] = 32767;  /* up moves toward y = 0 */
    rsl_input_update(&in, &r, 48, 0.1f);
    CHECK(in.cursor_y < 0.5f);

    /* acceleration: holding longer moves farther per frame, capped */
    c.cursor_accel = 1.5f;
    rsl_input_init(&in, &c);
    r = raw(0);
    r.stick_x[RSL_STICK_RIGHT] = 32767;
    rsl_input_update(&in, &r, 0, 0.01f);
    float first = in.cursor_x - 0.5f;
    for (int i = 0; i < 200; i++)
        rsl_input_update(&in, &r, 0, 0.01f);
    in.cursor_x = 0.5f;
    rsl_input_update(&in, &r, 0, 0.01f);
    float later = in.cursor_x - 0.5f;
    CHECK(later > first * 3.0f && later <= first * 4.0f + 1e-5f);
}

static void test_emergency(void)
{
    rsl_input_config c;
    rsl_config_preset(&c, RSL_PRESET_DEFAULT);
    rsl_config_bind(&c, RSL_ACT_CONFIRM, RSL_BTN_SL);   /* "broken" mapping */
    rsl_input in;
    rsl_input_init(&in, &c);
    rsl_raw_input r = raw(B(PLUS) | B(MINUS));
    rsl_input_update(&in, &r, 100, 0.016f);
    CHECK(count(&in, RSL_EV_ACTION_DOWN, RSL_ACT_GAME_MENU) == 0);
    CHECK(count(&in, RSL_EV_ACTION_DOWN, RSL_ACT_QUICK_MENU) == 0);
    rsl_input_update(&in, &r, 100 + RSL_EMERGENCY_HOLD_MS - 1, 0.016f);
    CHECK(in.event_count == 0);
    rsl_input_update(&in, &r, 100 + RSL_EMERGENCY_HOLD_MS, 0.016f);
    CHECK(count(&in, RSL_EV_EMERGENCY, RSL_ACT_OVERLAY) == 1);
    CHECK(in.cfg.bindings[RSL_ACT_CONFIRM] == B(A));
    rsl_input_update(&in, &r, 100 + 2 * RSL_EMERGENCY_HOLD_MS, 0.016f);
    CHECK(in.event_count == 0);          /* fires once per hold */
    r = raw(0);
    rsl_input_update(&in, &r, 10000, 0.016f);
    r = raw(B(PLUS) | B(MINUS));
    rsl_input_update(&in, &r, 10016, 0.016f);
    rsl_input_update(&in, &r, 10016 + RSL_EMERGENCY_HOLD_MS, 0.016f);
    CHECK(count(&in, RSL_EV_EMERGENCY, RSL_ACT_OVERLAY) == 1);   /* re-armed */

    /* a remap of + and - cannot disable it */
    rsl_input_config c2;
    rsl_config_preset(&c2, RSL_PRESET_DEFAULT);
    for (int a = 0; a < RSL_ACT_COUNT; a++)
        c2.bindings[a] = 0;
    rsl_input_init(&in, &c2);
    rsl_input_update(&in, &r, 0, 0.016f);
    rsl_input_update(&in, &r, RSL_EMERGENCY_HOLD_MS, 0.016f);
    CHECK(count(&in, RSL_EV_EMERGENCY, RSL_ACT_OVERLAY) == 1);
}

static void test_reset(void)
{
    rsl_input_config c;
    rsl_config_preset(&c, RSL_PRESET_DEFAULT);
    rsl_input in;
    rsl_input_init(&in, &c);
    rsl_raw_input r = raw(B(A) | B(ZR));
    rsl_input_update(&in, &r, 0, 0.016f);
    CHECK(rsl_action_active(&in, RSL_ACT_SKIP));
    rsl_input_reset(&in, B(A) | B(ZR));      /* e.g. resume from sleep */
    CHECK(!rsl_action_active(&in, RSL_ACT_SKIP));
    rsl_input_update(&in, &r, 16, 0.016f);
    CHECK(in.event_count == 0);              /* still-held buttons ignored */
    r = raw(B(ZR));                          /* A released, ZR still stuck */
    rsl_input_update(&in, &r, 32, 0.016f);
    CHECK(in.event_count == 0);
    r = raw(B(A) | B(ZR));
    rsl_input_update(&in, &r, 48, 0.016f);
    CHECK(count(&in, RSL_EV_ACTION_DOWN, RSL_ACT_CONFIRM) == 1 && in.event_count == 1);
}

static void test_math(void)
{
    float x, y;
    rsl_stick_normalize(32767, 32767, 0.15f, &x, &y);
    CHECK(fabsf(sqrtf(x * x + y * y) - 1.0f) < 1e-4f);
    rsl_stick_normalize(0, 0, 0.0f, &x, &y);
    CHECK(x == 0.0f && y == 0.0f);
    rsl_stick_normalize(0, -32767, 0.2f, &x, &y);
    CHECK(x == 0.0f && fabsf(y + 1.0f) < 1e-4f);
    rsl_stick_normalize(0, (int32_t)(32767 * 0.6f), 0.2f, &x, &y);
    CHECK(fabsf(y - 0.5f) < 1e-3f);          /* (0.6-0.2)/(1-0.2) */

    rsl_rect v = rsl_letterbox(1280, 720, 800, 600);
    CHECK(v.w == 960 && v.h == 720 && v.x == 160 && v.y == 0);
    float gx, gy;
    CHECK(rsl_screen_to_game(v, 800, 600, 160, 0, &gx, &gy) && gx == 0 && gy == 0);
    CHECK(rsl_screen_to_game(v, 800, 600, 640, 360, &gx, &gy) && gx == 400 && gy == 300);
    CHECK(!rsl_screen_to_game(v, 800, 600, 100, 10, &gx, &gy));
    v = rsl_letterbox(1920, 1080, 1920, 1080);
    CHECK(v.x == 0 && v.y == 0 && v.w == 1920);
    v = rsl_letterbox(1280, 720, 1280, 1024);   /* tall game: pillarbox */
    CHECK(v.h == 720 && v.y == 0 && v.x > 0);
}

static void test_sanitize(void)
{
    rsl_input_config c;
    rsl_config_preset(&c, RSL_PRESET_DEFAULT);
    c.deadzone = NAN;
    c.cursor_speed = 100;
    c.repeat_interval_ms = 0;
    c.cursor_stick = c.nav_stick;
    c.bindings[RSL_ACT_CONFIRM] |= 1u << 31;
    CHECK(!rsl_config_sanitize(&c));
    CHECK(c.deadzone == 0.0f && c.cursor_speed == 5.0f && c.repeat_interval_ms == 16);
    CHECK(c.cursor_stick != c.nav_stick);
    CHECK(c.bindings[RSL_ACT_CONFIRM] == B(A));
}

static bool same(const rsl_input_config *a, const rsl_input_config *b)
{
    return memcmp(a->bindings, b->bindings, sizeof a->bindings) == 0 &&
           fabsf(a->deadzone - b->deadzone) < 1e-4f &&
           fabsf(a->cursor_speed - b->cursor_speed) < 1e-4f &&
           fabsf(a->cursor_accel - b->cursor_accel) < 1e-4f &&
           a->repeat_delay_ms == b->repeat_delay_ms &&
           a->repeat_interval_ms == b->repeat_interval_ms &&
           a->nav_stick == b->nav_stick && a->cursor_stick == b->cursor_stick &&
           a->cursor_visible == b->cursor_visible;
}

static bool parses(const char *json, char *err)
{
    rsl_input_config c;
    return rsl_config_from_json(json, &c, err, 256);
}

static void test_json(void)
{
    char err[256];
    for (int p = 0; p < RSL_PRESET_COUNT; p++) {
        rsl_input_config c, back;
        rsl_config_preset(&c, (rsl_preset)p);
        char *j = rsl_config_to_json(&c);
        CHECK(j && rsl_config_from_json(j, &back, err, sizeof err));
        CHECK(same(&c, &back));
        free(j);
    }
    const char *hdr = "{\"format\":\"renpy-switch-loader/controller\",\"version\":1";
    char buf[512];
    rsl_input_config c, def;
    rsl_config_preset(&def, RSL_PRESET_DEFAULT);

    snprintf(buf, sizeof buf, "%s}", hdr);   /* minimal file = defaults */
    CHECK(rsl_config_from_json(buf, &c, err, sizeof err) && same(&c, &def));

    snprintf(buf, sizeof buf, "%s,\"bindings\":{\"history\":[\"R\",\"NavUp\"]},\"deadzone\":0.3}", hdr);
    CHECK(rsl_config_from_json(buf, &c, err, sizeof err));
    CHECK(c.bindings[RSL_ACT_HISTORY] == (B(R) | B(NAV_UP)));
    CHECK(c.bindings[RSL_ACT_CONFIRM] == B(A));   /* untouched action keeps default */
    CHECK(fabsf(c.deadzone - 0.3f) < 1e-6f);

    snprintf(buf, sizeof buf, "%s,\"bindings\":{\"jump\":[\"A\"]}}", hdr);
    CHECK(!parses(buf, err) && strstr(err, "jump"));
    snprintf(buf, sizeof buf, "%s,\"bindings\":{\"confirm\":[\"Start\"]}}", hdr);
    CHECK(!parses(buf, err) && strstr(err, "unknown button"));
    snprintf(buf, sizeof buf, "%s,\"bindings\":{\"confirm\":[]}}", hdr);
    CHECK(!parses(buf, err) && strstr(err, "confirm"));
    snprintf(buf, sizeof buf, "%s,\"deadzone\":\"big\"}", hdr);
    CHECK(!parses(buf, err) && strstr(err, "deadzone"));
    snprintf(buf, sizeof buf, "%s,\"nav_stick\":\"up\"}", hdr);
    CHECK(!parses(buf, err));
    CHECK(!parses("{\"format\":\"renpy-switch-loader/controller\",\"version\":2}", err) && strstr(err, "newer"));
    CHECK(!parses("{\"format\":\"other\",\"version\":1}", err));
    CHECK(!parses("{\"version\":1}", err));
    CHECK(!parses("[1,2]", err));
    CHECK(!parses("{\"format\":\"renpy-switch-loader/controller\",\"version\":1,", err));
    CHECK(!parses("", err));
}

static void test_sample_profiles(void)
{
    /* The JSON files in config/profiles ship with the loader; they must match the presets. */
    char path[256], err[256];
    for (int p = 0; p < RSL_PRESET_COUNT; p++) {
        rsl_input_config want, got;
        rsl_config_preset(&want, (rsl_preset)p);
        snprintf(path, sizeof path, "config/profiles/%s.json", rsl_preset_names[p]);
        CHECK(rsl_config_load(path, &got, err, sizeof err) == RSL_LOAD_OK);
        CHECK(same(&want, &got));
    }
}

static void test_files(void)
{
    char dir[] = "/tmp/rsl-test-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char sub[256], path[300], bak[310], err[256];
    snprintf(sub, sizeof sub, "%s/config/controller", dir);
    CHECK(rsl_mkdirs(sub));
    CHECK(rsl_mkdirs(sub));                  /* idempotent */
    snprintf(path, sizeof path, "%s/global.json", sub);
    snprintf(bak, sizeof bak, "%s.bak", path);

    rsl_input_config c, got;
    CHECK(rsl_config_load(path, &got, err, sizeof err) == RSL_LOAD_DEFAULTS);
    CHECK(err[0] != '\0');

    rsl_config_preset(&c, RSL_PRESET_LEFT_HANDED);
    CHECK(rsl_config_save(path, &c, err, sizeof err));
    CHECK(rsl_config_load(path, &got, err, sizeof err) == RSL_LOAD_OK && same(&c, &got));
    CHECK(access(bak, F_OK) != 0);           /* first save: nothing to back up */

    rsl_config_preset(&c, RSL_PRESET_DEFAULT);
    c.deadzone = 0.25f;
    CHECK(rsl_config_save(path, &c, err, sizeof err));
    CHECK(access(bak, F_OK) == 0);

    /* simulate a torn write of the main file: loader falls back to .bak */
    FILE *f = fopen(path, "wb");
    fputs("{\"format\":\"renpy-swi", f);
    fclose(f);
    rsl_input_config lh;
    rsl_config_preset(&lh, RSL_PRESET_LEFT_HANDED);
    CHECK(rsl_config_load(path, &got, err, sizeof err) == RSL_LOAD_BACKUP && same(&got, &lh));
    CHECK(strstr(err, "global.json") != NULL);

    remove(path);                            /* main missing (crash mid-rename) */
    CHECK(rsl_config_load(path, &got, err, sizeof err) == RSL_LOAD_BACKUP);
    remove(bak);

    char missing[320];
    snprintf(missing, sizeof missing, "%s/nope/global.json", dir);
    CHECK(!rsl_write_atomic(missing, "x", 1, err, sizeof err) && err[0]);

    size_t n;
    CHECK(rsl_write_atomic(path, "hello", 5, err, sizeof err));
    char *s = rsl_read_file(path, 1024, &n, err, sizeof err);
    CHECK(s && n == 5 && strcmp(s, "hello") == 0);
    free(s);
    CHECK(rsl_read_file(path, 2, &n, err, sizeof err) == NULL);   /* size cap */

    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    CHECK(system(cmd) == 0);
}

int main(void)
{
    test_presets();
    test_normalize();
    test_bind_conflicts();
    test_press_release();
    test_repeat();
    test_nav_stick();
    test_cursor();
    test_emergency();
    test_reset();
    test_math();
    test_sanitize();
    test_json();
    test_sample_profiles();
    test_files();
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures != 0;
}
