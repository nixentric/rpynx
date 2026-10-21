/* Loader screens drawn with LVGL from the state in loader.h (docs/LOADER_UI.md). */
#ifndef RSL_UI_H
#define RSL_UI_H

#include <stddef.h>

/* Call after lv_init() and creating the display. ttf is the text font
 * (TrueType data, kept alive by the caller); NULL falls back to Montserrat 14. */
void ui_init(const void *ttf, size_t ttf_len);

/* Bring the screen in line with g. Call every frame: it only touches what
 * changed, and decodes at most one cover per call. */
void ui_update(void);

#endif
