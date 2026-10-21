# Nintendo 3DS build (.3dsx for the Homebrew Launcher / emulators such as Citra/Lime3DS).
#
#   1. install devkitPro + the 3ds-dev group:  dkp-pacman -S 3ds-dev
#   2. export DEVKITPRO=/opt/devkitpro ; export DEVKITARM=$DEVKITPRO/devkitARM
#   3. cd 3ds && make
#
# The engine core and demo are the *same* sources as the desktop build; only
# platform/n3ds.c replaces platform/sdl2.c.

ifeq ($(strip $(DEVKITPRO)),)
$(error Set DEVKITPRO (e.g. export DEVKITPRO=/opt/devkitpro))
endif
DEVKITARM ?= $(DEVKITPRO)/devkitARM

TARGET  := gloamwood
ROOT    := ..
CC      := $(DEVKITARM)/bin/arm-none-eabi-gcc
3DSXTOOL:= $(DEVKITPRO)/tools/bin/3dsxtool
SMDHTOOL:= $(DEVKITPRO)/tools/bin/smdhtool
APP_TITLE  := Fran's demo
APP_DESC   := A platformer with fighting-game duels
APP_AUTHOR := ge engine
CTRULIB := $(DEVKITPRO)/libctru

SRC := $(wildcard $(ROOT)/engine/src/*.c) $(ROOT)/platform/n3ds.c $(wildcard $(ROOT)/demo/*.c)
OBJ := $(patsubst $(ROOT)/%.c,build/%.o,$(SRC))

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -std=gnu99 -g -Wall -O3 -mword-relocations -ffunction-sections -fomit-frame-pointer $(ARCH) \
           -D__3DS__ -I$(ROOT)/engine/include -I$(ROOT)/demo -I$(CTRULIB)/include
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,build/$(TARGET).map
LIBS    := -L$(CTRULIB)/lib -lctru -lm

all: $(TARGET).3dsx

# the .smdh carries the title and icon the Homebrew Launcher shows
build/$(TARGET).smdh: icon.png
	@mkdir -p build
	$(SMDHTOOL) --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" $< $@

$(TARGET).3dsx: build/$(TARGET).elf build/$(TARGET).smdh
	$(3DSXTOOL) $< $@ --smdh=build/$(TARGET).smdh

build/$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

build/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf build $(TARGET).3dsx

-include $(OBJ:.o=.d)
.PHONY: all clean
