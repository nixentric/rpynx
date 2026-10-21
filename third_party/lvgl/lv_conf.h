/* LVGL 9.6.0 configuration for the loader UI (app/ui). Only what differs from
 * LVGL's defaults; lv_conf_internal.h fills in the rest. */
#ifndef LV_CONF_H
#define LV_CONF_H

/* B,G,R,X in memory: the Switch framebuffer's BGRA layout and a 32-bit BMP. */
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_XRGB8888

/* newlib / libc instead of LVGL's own heap: no fixed pool to size. */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 16

/* Software renderer with only the formats the loader draws: the display
 * (XRGB8888), layers (ARGB8888), JPEG covers (RGB888) and glyphs (A8). */
#define LV_DRAW_SW_SUPPORT_RGB565                0
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED        0
#define LV_DRAW_SW_SUPPORT_RGB565A8              0
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8                    0
#define LV_DRAW_SW_SUPPORT_AL88                  0
#define LV_DRAW_SW_SUPPORT_I1                    0
#ifdef __SWITCH__
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NEON
#endif

#define LV_USE_LOG    1
#define LV_LOG_LEVEL  LV_LOG_LEVEL_WARN

/* Covers are decoded once into their own buffers (app/ui/ui.c); this cache
 * only holds image headers. */
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 8

/* Text: the console's own font through tiny_ttf (stb_truetype). Montserrat 14
 * stays as the default font and the fallback for LV_SYMBOL_* glyphs. */
#define LV_USE_TINY_TTF 1
#define LV_USE_FONT_PLACEHOLDER 1

#define LV_USE_TJPGD    1
#define LV_USE_FS_STDIO 1
#define LV_FS_STDIO_LETTER 'S'

/* Plain objects styled by app/ui; no LVGL theme. */
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE  0
#define LV_USE_THEME_MONO    0
#define LV_USE_OBSERVER      0
#define LV_USE_GRID          0

/* Widgets: label, image, bar and canvas (cover pre-scaling) only. */
#define LV_USE_ANIMIMG      0
#define LV_USE_ARC          0
#define LV_USE_ARCLABEL     0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR     0
#define LV_USE_CHART        0
#define LV_USE_CHECKBOX     0
#define LV_USE_DROPDOWN     0
#define LV_USE_IMAGEBUTTON  0
#define LV_USE_KEYBOARD     0
#define LV_USE_LED          0
#define LV_USE_LINE         0
#define LV_USE_LIST         0
#define LV_USE_MENU         0
#define LV_USE_MSGBOX       0
#define LV_USE_ROLLER       0
#define LV_USE_SCALE        0
#define LV_USE_SLIDER       0
#define LV_USE_SPAN         0
#define LV_USE_SPINBOX      0
#define LV_USE_SPINNER      0
#define LV_USE_SWITCH       0
#define LV_USE_TEXTAREA     0
#define LV_USE_TABLE        0
#define LV_USE_TABVIEW      0
#define LV_USE_TILEVIEW     0
#define LV_USE_WIN          0

#endif
