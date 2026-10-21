# LVGL as a static library, for the loader and for the host mockups:
#   make -f third_party/lvgl/Makefile OUT=<dir> CC=<cc> AR=<ar> CFLAGS=<flags>
# Run third_party/lvgl/fetch.sh first. Separate from the devkitPro template
# Makefile because LVGL has duplicate file names in different folders.
LVGL := build/lvgl/lvgl
OBJS := $(patsubst $(LVGL)/%.c,$(OUT)/%.o,$(shell find $(LVGL)/src -name '*.c'))

$(OUT)/liblvgl.a: $(OBJS)
	@rm -f $@
	@$(AR) rcs $@ $^

$(OUT)/%.o: $(LVGL)/%.c third_party/lvgl/lv_conf.h
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) -Ithird_party/lvgl -I$(LVGL) -c $< -o $@
