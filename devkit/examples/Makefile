# Builds the riscos-mesa devkit examples into ready-to-run RISC OS
# applications, in build/ (build/!GLWindow, build/!Teapot...).
#
#   make                  all six
#   make 2-window         just one (use the folder name)
#   make zip              build/examples.zip, with RISC OS filetypes set,
#                         to copy to the RISC OS machine
#   make clean
#
# Needs GCCSDK's GCC 10 (arm-riscos-gnueabihf-gcc) on your PATH, or
# GCCSDK_ENV=<its env directory>. The devkit is the folder above this one;
# set DEVKIT=... if you copy the examples somewhere else.
#
# Every line below is explained in ../README.md ("The build flags, and
# why"). Use these flags in your own programs too.

DEVKIT ?= ..
ifdef GCCSDK_ENV
CC := $(GCCSDK_ENV)/bin/arm-riscos-gnueabihf-gcc
else
CC := arm-riscos-gnueabihf-gcc
endif

# How to compile:
#   -O2                        optimise
#   -mfpu=vfpv3 -mfloat-abi=hard   use the hardware floating point unit, in
#                              the form every ARMv7 RISC OS machine has
#   -fstack-clash-protection   ESSENTIAL: without it, programs crash at random
#                              (see the README)
CFLAGS := -O2 -mfpu=vfpv3 -mfloat-abi=hard -fstack-clash-protection \
          -I$(DEVKIT)/include -Wall

# How to link: -static puts everything in the one program file. The order
# of the -l libraries matters: each one before the libraries it uses.
GL_LIBS    := -lEGL -lOSMesa -lstdc++ -lz -lm
SDL_LIBS   := -lSDL2 -lOSMesa -lstdc++ -lz -lm
GLUT_LIBS  := -lglut -lGLU -lEGL -lOSMesa -lstdc++ -lz -lm
SOUND_LIBS := -lopenal -lSDL2 -lOSMesa -lstdc++ -lz -lm
# -s strips the debugging names out, making the program much smaller.
LDFLAGS    := -static -s -L$(DEVKIT)/lib

EXAMPLES := 1-fullscreen 2-window 3-shaders 4-sdl2 5-glut 6-sound

all: $(EXAMPLES)

# name of the application (from each folder's APP file), e.g. GLWindow
app = $(shell cat $(1)/APP)

# $(call build,folder,source,libraries,extra cflags)
define build
	@mkdir -p "build/!$(call app,$(1))"
	$(CC) $(CFLAGS) $(4) $(1)/$(2) -o "build/!$(call app,$(1))/!RunImage,e1f" $(LDFLAGS) $(3)
	cp "$(1)/!Run,feb" "$(1)/!Help,fff" "build/!$(call app,$(1))/"
	@echo "built build/!$(call app,$(1))"
endef

1-fullscreen: ; $(call build,$@,fullscreen.c,$(GL_LIBS))
2-window:     ; $(call build,$@,window.c,$(GL_LIBS))
3-shaders:    ; $(call build,$@,shaders.c,$(GL_LIBS))
4-sdl2:       ; $(call build,$@,sdl2.c,$(SDL_LIBS),-I$(DEVKIT)/include/SDL2)
5-glut:       ; $(call build,$@,glut.c,$(GLUT_LIBS))
6-sound:      ; $(call build,$@,sound.c,$(SOUND_LIBS),-I$(DEVKIT)/include/SDL2)
	cp "$(DEVKIT)/riscos/PThrTicker,ffa" "$(DEVKIT)/riscos/PThreadTicker-Licence,fff" \
	   "build/!$(call app,$@)/"          # it has threads: see ../README.md

# A zip that keeps the RISC OS filetypes (,e1f = ELF program, ,feb = Obey
# file, ,fff = Text): unzip it on RISC OS with SparkFS or unzip.
zip: all
	cd build && rm -f examples.zip && python3 $(abspath $(DEVKIT))/bin/mkrozip.py examples.zip !*
	@echo "made build/examples.zip"

clean:
	rm -rf build

.PHONY: all zip clean $(EXAMPLES)
