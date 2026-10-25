#---------------------------------------------------------------------------------
# rpynx - devkitA64/libnx build. Based on switchbrew/switch-examples
# templates/application/Makefile. Run through scripts/build.sh for the pinned toolchain.
#---------------------------------------------------------------------------------
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# ROMFS is the directory containing data to be added to RomFS, relative to the Makefile (Optional)
#
# NO_ICON: if set to anything, do not use icon.
# NO_NACP: if set to anything, no .nacp file is generated.
# APP_TITLE is the name of the app stored in the .nacp file (Optional)
# APP_AUTHOR is the author of the app stored in the .nacp file (Optional)
# APP_VERSION is the version of the app stored in the .nacp file (Optional)
# APP_TITLEID is the titleID of the app stored in the .nacp file (Optional)
# ICON is the filename of the icon (.jpg), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.jpg
#     - icon.jpg
#     - <libnx folder>/default_icon.jpg
#
# CONFIG_JSON is the filename of the NPDM config file (.json), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.json
#     - config.json
#   If a JSON file is provided or autodetected, an ExeFS PFS0 (.nsp) is built instead
#   of a homebrew executable (.nro). This is intended to be used for sysmodules.
#   NACP building is skipped as well.
#---------------------------------------------------------------------------------
# Two programs share this Makefile:
#   PROGRAM=loader  (default) dist/renpy-loader.nro   the library / settings UI
#   PROGRAM=runtime           dist/renpy-runtime.nro  embedded CPython (+ Ren'Py later)
# `make runtime` needs `make python` (static CPython in build/python/dist) first.
PROGRAM		?=	loader
APP_AUTHOR	:=	rpynx contributors
APP_VERSION	:=	0.1.0
DATA		:=
PYDIST		:=	$(CURDIR)/build/python/dist
RENPYDIST	:=	$(CURDIR)/build/renpy/dist

ifeq ($(PROGRAM),runtime)
TARGET		:=	renpy-runtime
BUILD		:=	build/runtime
SOURCES		:=	runtime runtime/python app/diagnostics platform/input platform/filesystem third_party/cjson
INCLUDES	:=	app/diagnostics platform/input platform/filesystem third_party/cjson build/python/dist/include/python3.12
APP_TITLE	:=	rpynx runtime
ROMFS		:=	build/runtime-romfs
# Static archives with circular dependencies, so one group. GLESv2 is reached
# through SDL_GL_GetProcAddress (Ren'Py's uguu loader) but must be linked.
# The wraps let runtime/main.c track every pthread and close leftovers at exit.
PROGRAM_LIBS	:=	-Wl,--wrap=__syscall_thread_create -Wl,--wrap=__syscall_thread_join \
			-Wl,--start-group -lrenpy -lpython3.12 -lSDL2_image -lSDL2 -lEGL -lGLESv2 -lglapi \
			-ldrm_nouveau -lavformat -lavcodec -lswresample -lswscale -lavutil -ldav1d \
			-lharfbuzz -lfreetype -lfribidi -lpng16 -ljpeg -lwebp -lbz2 -llzma -lz \
			-lstdc++ -Wl,--end-group
PROGRAM_LIBDIRS	:=	$(PYDIST) $(RENPYDIST)
else
TARGET		:=	renpy-loader
BUILD		:=	build/switch
SOURCES		:=	app/launcher app/ui app/diagnostics platform/input platform/filesystem platform/transfer \
			third_party/cjson
INCLUDES	:=	app/launcher app/ui app/diagnostics platform/input platform/filesystem platform/transfer \
			third_party/cjson \
			third_party/lvgl build/lvgl/lvgl
APP_TITLE	:=	rpynx
# Homebrew Menu icon, made by scripts/icons.sh.
ICON		:=	app/launcher/icon.jpg
# LVGL (docs/LOADER_UI.md), built as a library by the `lvgl` target below.
PROGRAM_LIBS	:=	-llvgl
PROGRAM_LIBDIRS	:=	$(CURDIR)/build/lvgl/switch
endif

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

DEFINES	:=	-DRSL_VERSION=\"$(APP_VERSION)\"

CFLAGS	:=	-g -Wall -Wextra -Wno-unused-parameter -O2 -ffunction-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= $(PROGRAM_LIBS) -lnx -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PROGRAM_LIBDIRS) $(PORTLIBS) $(LIBNX)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
# BUILD is nested (build/switch), so compare last components only.
ifneq ($(notdir $(BUILD)),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/dist/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(CONFIG_JSON)),)
	jsons := $(wildcard *.json)
	ifneq (,$(findstring $(TARGET).json,$(jsons)))
		export APP_JSON := $(TOPDIR)/$(TARGET).json
	else
		ifneq (,$(findstring config.json,$(jsons)))
			export APP_JSON := $(TOPDIR)/config.json
		endif
	endif
else
	export APP_JSON := $(TOPDIR)/$(CONFIG_JSON)
endif

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(OUTPUT).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all python renpy runtime romfs test-python lvgl

#---------------------------------------------------------------------------------
all: $(BUILD)

python:
	runtime/python/build.sh

runtime:
	@$(MAKE) --no-print-directory PROGRAM=runtime

renpy:
	runtime/renpy/build.sh

# Python-side tests, run with the host CPython 3.12.8 from `make python`.
test-python:
	build/python/host/bin/python3.12 tests/unit/test_horizon_paths.py build/python/src/Python-3.12.8/Lib

# Runtime RomFS: compiled stdlib, the Ren'Py engine (renpy-base) and the
# Python self-test.
romfs:
	@rm -rf $(ROMFS) && mkdir -p $(ROMFS)/lib
	@cp $(PYDIST)/lib/python312.zip $(ROMFS)/lib/
	@cp -R $(RENPYDIST)/renpy-base $(ROMFS)/
	@cp runtime/python/selftest.py $(ROMFS)/
	@rm -f dist/$(TARGET).nro   # RomFS is not a make dependency: always repack

# LVGL for the loader: fetched (pinned, checksummed) and built once into
# build/lvgl/switch/lib/liblvgl.a, with LVGL's own flags (its code is not
# held to this project's warning set).
lvgl:
	@third_party/lvgl/fetch.sh
	@$(MAKE) --no-print-directory -f third_party/lvgl/Makefile OUT=build/lvgl/switch/lib \
		CC="$(CC)" AR="$(AR)" CFLAGS="-g -O2 -ffunction-sections $(ARCH) -D__SWITCH__"

$(BUILD): $(if $(ROMFS),romfs) $(if $(filter loader,$(PROGRAM)),lvgl)
	@[ -d $@ ] || mkdir -p $@ dist
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
ifeq ($(strip $(APP_JSON)),)
	@rm -fr $(BUILD) $(ROMFS) dist/$(TARGET).nro dist/$(TARGET).nacp dist/$(TARGET).elf
else
	@rm -fr $(BUILD) $(TARGET).nsp $(TARGET).nso $(TARGET).npdm $(TARGET).elf
endif


#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
ifeq ($(strip $(APP_JSON)),)

all	:	$(OUTPUT).nro

ifeq ($(strip $(NO_NACP)),)
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
else
$(OUTPUT).nro	:	$(OUTPUT).elf
endif

else

all	:	$(OUTPUT).nsp

$(OUTPUT).nsp	:	$(OUTPUT).nso $(OUTPUT).npdm

$(OUTPUT).nso	:	$(OUTPUT).elf

endif

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
