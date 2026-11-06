# kk4s: Nintendo Switch build of rlvm -> rlvm.nro (docs/engines.md).
#
#   tools/build-rlvm-deps.sh          # Boost + libsndfile into out-engines/rlvm-deps/prefix
#   make -C engines/rlvm RLVM_DEPS=<that prefix>
#
# or simply tools/build-engines.sh rlvm. Needs devkitPro (devkitA64, libnx) with
# switch-sdl2 switch-sdl2_image switch-sdl2_mixer switch-sdl2_ttf switch-mesa
# switch-libvorbis switch-freetype switch-zlib.
#
# Object files keep their directory (build/src/..., build/vendor/...) because rlvm
# has several files with the same name (file.cc, ...), which the usual libnx
# template would put in one folder.

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

include $(DEVKITPRO)/libnx/switch_rules

TARGET      := rlvm
BUILD       := build
RLVM_DEPS   ?= $(CURDIR)/../../out-engines/rlvm-deps/prefix

APP_TITLE   := rlvm (kk4s)
APP_AUTHOR  := Elliot Glaysher et al.; kk4s
APP_VERSION := 0.14
APP_ICON    := $(CURDIR)/icon.jpg

#---------------------------------------------------------------------------------
# sources
#---------------------------------------------------------------------------------
RLVM_SRC := $(shell find src -name '*.cc' -not -path 'src/platforms/*') \
            $(wildcard src/platforms/gcn/*.cc) \
            src/platforms/switch/rlvm_switch.cc

VENDOR_SRC := $(wildcard vendor/xclannad/*.cc vendor/xclannad/*.cpp) \
              vendor/pygame/alphablit.cc \
              $(wildcard vendor/zita-resampler/src/*.cc)

GUICHAN_SRC := $(wildcard vendor/guichan/src/*.cpp) \
               $(wildcard vendor/guichan/src/widgets/*.cpp) \
               $(wildcard vendor/guichan/src/sdl/*.cpp) \
               $(wildcard vendor/guichan/src/opengl/*.cpp)

SRCS := $(RLVM_SRC) $(VENDOR_SRC) $(GUICHAN_SRC)
OBJS := $(addprefix $(BUILD)/,$(addsuffix .o,$(basename $(SRCS))))
DEPS := $(OBJS:.o=.d)

#---------------------------------------------------------------------------------
# flags
#---------------------------------------------------------------------------------
ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

INCLUDES := -Iswitch/include -I. -Isrc -Ivendor -Ivendor/guichan/include \
            -Ivendor/zita-resampler/include -Ivendor/xclannad \
            -I$(RLVM_DEPS)/include \
            -I$(PORTLIBS)/include -I$(PORTLIBS)/include/SDL2 -I$(LIBNX)/include

DEFINES  := -D__SWITCH__ -DHAVE_CONFIG_H -DNO_SDL_GLEXT -DCASE_SENSITIVE_FILESYSTEM \
            -D_THREAD_SAFE -DNDEBUG -DBOOST_DISABLE_ASSERTS \
            -DBOOST_HAS_GETTIMEOFDAY

CFLAGS   := -g -O2 -ffunction-sections -fdata-sections -funsigned-char \
            -Wno-sign-compare -Wno-narrowing -Wno-write-strings \
            $(ARCH) $(DEFINES) $(INCLUDES)
CXXFLAGS := $(CFLAGS) -std=gnu++17 -fexceptions -frtti
ASFLAGS  := -g $(ARCH)
LDFLAGS  := -specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,--gc-sections \
            -Wl,-Map,$(BUILD)/$(TARGET).map

LIBDIRS  := -L$(RLVM_DEPS)/lib -L$(PORTLIBS)/lib -L$(LIBNX)/lib
LIBS     := -lboost_program_options -lboost_serialization -lboost_iostreams \
            -lboost_filesystem -lboost_date_time -lboost_system \
            -lsndfile \
            -lSDL2_ttf -lSDL2_image -lSDL2_mixer \
            -lfreetype -lharfbuzz -lfreetype -lbz2 -lpng16 -lwebp -ljpeg \
            -lmodplug -lmpg123 -lopusfile -lopus -lFLAC -lvorbisidec \
            -lvorbisfile -lvorbis -logg \
            -lSDL2 -lEGL -lGLESv2 -lglapi -ldrm_nouveau \
            -lz -lnx -lm

#---------------------------------------------------------------------------------
# rules
#---------------------------------------------------------------------------------
.PHONY: all clean
all: $(TARGET).nro

$(BUILD)/%.o: %.cc
	@mkdir -p $(dir $@)
	@echo $<
	@$(CXX) -MMD -MP $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo $<
	@$(CXX) -MMD -MP $(CXXFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	@echo linking $(notdir $@)
	@$(CXX) $(LDFLAGS) $(OBJS) $(LIBDIRS) $(LIBS) -o $@

$(BUILD)/$(TARGET).nacp: Makefile
	@nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(TARGET).nro: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).nacp
	@elf2nro $< $@ --icon=$(APP_ICON) --nacp=$(BUILD)/$(TARGET).nacp
	@echo built ... $@

clean:
	@rm -rf $(BUILD) $(TARGET).nro

-include $(DEPS)
