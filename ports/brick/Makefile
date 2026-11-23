# Run make -C ports/brick from the project root. Override BUILD for cross builds.
ROOT := ../..
BUILD ?= $(ROOT)/target/brick/native
CORE := $(ROOT)/lib/core/src
SRC := $(ROOT)/src
SDL_CONFIG ?= sdl2-config
SDL_CFLAGS ?= $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS ?= $(shell $(SDL_CONFIG) --libs)
CPPFLAGS := $(CPPFLAGS) -include compat/bsd_string.h -DDTRK_BRICK -Iui_include -Icompat -I. -I$(SRC) -I$(CORE) $(SDL_CFLAGS)
# Use the same voice pool layout in every translation unit of the Brick port.
CPPFLAGS += -DMT_VOICES=64 -DMT_HEAVY_MAX=64
CXXFLAGS += -std=c++17 -O2 -pthread -MMD -MP
LDLIBS += $(SDL_LIBS) -pthread

# The Linux ARM64 build runs on the Brick's Cortex-A53. Tune the DSP there.
TARGET_TRIPLE := $(shell $(CXX) -dumpmachine)
ifneq ($(and $(filter aarch64%,$(TARGET_TRIPLE)),$(findstring linux,$(TARGET_TRIPLE))),)
CORE_CXXFLAGS := -O3 -mcpu=cortex-a53
endif

# Ignore editor-created copies such as "theme 2.cpp".
UI_NAMES := $(shell cd $(SRC)/ui && find . -maxdepth 1 -name '*.cpp' ! -name '* *' | sed 's|^./||' | sort)
STORAGE_NAMES := $(filter-out remote_fs.cpp space_query.cpp crashlog.cpp,$(notdir $(wildcard $(SRC)/storage/*.cpp)))
FW_SOURCES := $(addprefix ui/,$(filter-out wifi_dialog.cpp,$(UI_NAMES))) $(addprefix storage/,$(STORAGE_NAMES)) hw/sdcard.cpp
PORT_OBJECTS := $(patsubst %.cpp,$(BUILD)/brick/%.o,$(wildcard *.cpp))
FW_OBJECTS := $(patsubst %.cpp,$(BUILD)/fw/%.o,$(FW_SOURCES))
CORE_OBJECTS := $(patsubst $(CORE)/%.cpp,$(BUILD)/core/%.o,$(wildcard $(CORE)/*.cpp))
OBJECTS := $(PORT_OBJECTS) $(FW_OBJECTS) $(CORE_OBJECTS)

.PHONY: all
all: $(BUILD)/d-trk

$(BUILD)/d-trk: $(PORT_OBJECTS) $(FW_OBJECTS) $(BUILD)/libtracker.a
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/libtracker.a: $(CORE_OBJECTS)
	$(AR) rcs $@ $^

$(BUILD)/brick/%.o: %.cpp Makefile
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c "$<" -o "$@"

$(BUILD)/fw/%.o: $(SRC)/%.cpp Makefile
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c "$<" -o "$@"

$(BUILD)/core/%.o: $(CORE)/%.cpp Makefile
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CORE_CXXFLAGS) -c "$<" -o "$@"

-include $(OBJECTS:.o=.d)

CHECKS := check-render check-engine check-peripherals check-battery check-navigation
.PHONY: $(CHECKS)
$(CHECKS): %: $(BUILD)/%
	"$<"

$(addprefix $(BUILD)/,$(CHECKS)): $(BUILD)/check-%: $(BUILD)/brick/tests/check_%.o $(filter-out $(BUILD)/brick/ui_main.o,$(PORT_OBJECTS)) $(FW_OBJECTS) $(BUILD)/libtracker.a
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

-include $(wildcard $(BUILD)/brick/tests/*.d)
