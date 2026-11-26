.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

ifeq ($(OS),Windows_NT)
PLATFORM ?= windows
else
PLATFORM ?= linux
endif

# Keep release and debug objects apart when Make is invoked directly.
CONFIG ?= release
BUILD ?= build/$(PLATFORM)$(if $(filter debug,$(CONFIG)),-debug)
ifneq ($(CONFIG),debug)
ifneq ($(CONFIG),release)
$(error CONFIG must be debug or release)
endif
endif

# Only ROM_TESTS need firmware files. `make check` runs CHECK_TESTS.
CHECK_TESTS := adpcm bus_data cellular content cpu cpu_cache cpu_diff dsp_mailbox \
               firmware gpio irom jar key_text keypad lp_timer ma3 ma_detune ma_water \
               machine machine_run nvram peripheral peripheral_schedule phone_io rtc \
               sim userfs wap
ROM_TESTS := c100_boot c100_charger c100_compat c100_identity c100_keypad c100_sim
TOOLS := fwinspect jarinspect phoneprobe

ifeq ($(PLATFORM),windows)
ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
.SHELLFLAGS := /d /c
endif
CC := cl
OBJ := obj
EXE := .exe
PORT_SRC := src/port_win32.c
BACKENDS := src/java_net.c src/phone_net.c src/wap_http.c src/ma5_hw.c
CHECK_TESTS += gui_profile java_net phone_net wap_http wap_paging
TOOLS += cpubench ma3pcmprobe ma5hwprobe ma5probe ma5seqprobe perfbench phonedrive

CPPFLAGS += /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /Isrc
CFLAGS += /nologo /std:c17 /W4 /WX
ifeq ($(CONFIG),debug)
CFLAGS += /Od /Z7 /RTC1 /MD
LDFLAGS += /DEBUG
else
CFLAGS += /O2 /GL /MT /DNDEBUG
LDFLAGS += /LTCG
endif
LDLIBS += bcrypt.lib version.lib winmm.lib windowscodecs.lib ole32.lib uuid.lib ws2_32.lib winhttp.lib
GUI_LIBS := user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib
GUI_MANIFEST := /MANIFEST:EMBED /MANIFESTINPUT:src/gui.manifest

native_path = $(subst /,\,$(1))
make_directory = if not exist "$(call native_path,$@)" mkdir "$(call native_path,$@)"
compile = $(CC) $(CPPFLAGS) $(CFLAGS) /c "$<" /Fo"$@"
link = $(CC) /nologo /Fe"$@" $(filter %.$(OBJ),$^) /link /MACHINE:X86 \
       /SUBSYSTEM:$(or $(SUBSYSTEM),CONSOLE),6.01 $(LDFLAGS) $(EXTRA_LDFLAGS) $(LDLIBS) $(EXTRA_LIBS)

else ifeq ($(PLATFORM),linux)
OBJ := o
PORT_SRC := src/port_posix.c
BACKENDS := src/net_stub.c src/ma5_hw_stub.c

CPPFLAGS += -Isrc
CFLAGS += -std=c17 -Wall -Wextra
CXXFLAGS += -std=c++17 -Wall -Wextra
ifeq ($(CONFIG),debug)
CFLAGS += -O0 -g
CXXFLAGS += -O0 -g
else
CFLAGS += -O2
CXXFLAGS += -O2
endif
LDLIBS += -lm

native_path = $(1)
make_directory = mkdir -p "$@"
compile = $(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"
link = $(CC) $(LDFLAGS) $(filter %.$(OBJ),$^) -o "$@" $(LDLIBS)
else
$(error PLATFORM must be linux or windows)
endif

# The machine uses one host implementation and one set of platform backends.
CPU_SRC := src/arm7.c src/bus.c
JAR_SRC := src/jar.c src/jar_inflate.c src/file.c
NVRAM_SRC := src/nvram.c src/firmware.c src/sha256.c $(PORT_SRC)
MACHINE_SRC := $(addprefix src/,machine.c lp_timer.c cellular.c wap_gateway.c phone_io.c \
               rtc.c battery.c dsp_mailbox.c gpio.c keypad.c sim.c sim_card.c sds_uart.c \
               sds_dma.c ma3.c irom.c flash.c lcd.c intc.c pmic.c file.c) \
               $(CPU_SRC) $(PORT_SRC) $(BACKENDS)
APP_SRC := $(MACHINE_SRC) $(NVRAM_SRC) $(JAR_SRC) src/content.c src/host.c src/userfs.c

# Some tests supply their own implementations (for example ma3_test stubs out MA-5).
TEST_SRC_bus_data            := src/bus.c
TEST_SRC_cellular            := src/cellular.c src/sim_card.c $(CPU_SRC) $(PORT_SRC)
TEST_SRC_content             := src/content.c $(JAR_SRC) $(CPU_SRC) $(PORT_SRC)
TEST_SRC_cpu                 := $(CPU_SRC)
TEST_SRC_cpu_cache           := src/flash.c src/sds_dma.c src/sds_uart.c $(CPU_SRC)
TEST_SRC_cpu_diff            := tests/arm7_reference.c $(CPU_SRC)
TEST_SRC_dsp_mailbox         := src/dsp_mailbox.c src/bus.c
TEST_SRC_firmware            := src/firmware.c src/sha256.c $(PORT_SRC)
TEST_SRC_gpio                := src/gpio.c src/battery.c src/dsp_mailbox.c src/intc.c $(CPU_SRC)
TEST_SRC_irom                := $(MACHINE_SRC)
TEST_SRC_jar                 := $(JAR_SRC)
TEST_SRC_keypad              := $(MACHINE_SRC)
TEST_SRC_lp_timer            := src/lp_timer.c src/intc.c src/bus.c
TEST_SRC_ma3                 := src/ma3.c src/bus.c
TEST_SRC_machine             := $(MACHINE_SRC)
TEST_SRC_machine_run         := $(MACHINE_SRC)
TEST_SRC_nvram               := $(MACHINE_SRC) $(NVRAM_SRC)
TEST_SRC_peripheral          := src/lcd.c src/flash.c src/intc.c src/pmic.c $(CPU_SRC) src/file.c
TEST_SRC_peripheral_schedule := src/sim.c src/sim_card.c src/sds_dma.c src/sds_uart.c src/intc.c src/bus.c $(PORT_SRC)
TEST_SRC_phone_io            := src/gpio.c src/phone_io.c $(CPU_SRC)
TEST_SRC_rtc                 := src/rtc.c src/bus.c
TEST_SRC_sim                 := src/sim.c src/sim_card.c src/intc.c src/bus.c $(PORT_SRC)
TEST_SRC_userfs              := src/userfs.c src/bus.c src/file.c
TEST_SRC_wap                 := src/wap_gateway.c
TEST_SRC_c100_boot           := $(MACHINE_SRC)
TEST_SRC_c100_charger        := $(MACHINE_SRC) $(NVRAM_SRC) src/userfs.c
TEST_SRC_c100_compat         := $(MACHINE_SRC) $(NVRAM_SRC) src/userfs.c
TEST_SRC_c100_identity       := $(MACHINE_SRC)
TEST_SRC_c100_keypad         := $(MACHINE_SRC) $(NVRAM_SRC)
TEST_SRC_c100_sim            := $(MACHINE_SRC) $(NVRAM_SRC)
TEST_SRC_gui_profile         := $(APP_SRC)
TEST_SRC_java_net            := src/java_net.c $(TEST_SRC_cellular)
TEST_SRC_phone_net           := src/phone_net.c src/wap_gateway.c src/wap_http.c $(TEST_SRC_java_net)
TEST_SRC_wap_http            := src/wap_http.c
TEST_SRC_wap_paging          := src/wap_http.c

TOOL_SRC_cpubench    := tests/arm7_reference.c $(CPU_SRC)
TOOL_SRC_fwinspect   := src/file.c
TOOL_SRC_jarinspect  := $(JAR_SRC)
TOOL_SRC_ma3pcmprobe := src/ma3.c src/ma5_hw.c src/bus.c src/file.c
TOOL_SRC_ma5hwprobe  := src/ma5_hw.c src/file.c
TOOL_SRC_ma5probe    := src/file.c
TOOL_SRC_ma5seqprobe := src/ma5_hw.c src/file.c
TOOL_SRC_perfbench   := $(APP_SRC)
TOOL_SRC_phonedrive  := $(APP_SRC)
TOOL_SRC_phoneprobe  := $(APP_SRC)

test_binaries = $(addprefix $(BUILD)/,$(addsuffix _test$(EXE),$(1)))
CHECK_BIN := $(call test_binaries,$(CHECK_TESTS))
TEST_BIN := $(CHECK_BIN) $(call test_binaries,$(ROM_TESTS))
TOOL_BIN := $(addprefix $(BUILD)/,$(addsuffix $(EXE),$(TOOLS)))
CLI_BIN := $(BUILD)/sghemu$(EXE)
objects = $(patsubst %.c,$(BUILD)/%.$(OBJ),$(1))

# Create a link rule from an executable path ($(1)) and its C sources ($(2)).
# Defer the link command so $@ and $^ refer to the executable and its objects.
# Make lists duplicate prerequisites only once in $^ (e.g. shared file.c).
define program
$(1): $(call objects,$(2))
	$$(link)
endef

all: $(CLI_BIN)
$(eval $(call program,$(CLI_BIN),src/main.c $(APP_SRC)))
$(foreach test,$(CHECK_TESTS) $(ROM_TESTS), \
    $(eval $(call program,$(call test_binaries,$(test)),tests/$(test)_test.c $(TEST_SRC_$(test)))))
$(foreach tool,$(TOOLS), \
    $(eval $(call program,$(BUILD)/$(tool)$(EXE),tools/$(tool).c $(TOOL_SRC_$(tool)))))

ifeq ($(PLATFORM),windows)
# tools/build-pgo.cmd uses a separate GUI and profile database for training.
GUI_BIN := $(if $(SGHEMU_GUI_OUT),$(subst \,/,$(SGHEMU_GUI_OUT)),$(BUILD)/sghemu-gui.exe)
all: gui
gui: $(GUI_BIN)
$(eval $(call program,$(GUI_BIN),src/gui.c $(APP_SRC)))
$(GUI_BIN) $(BUILD)/gui_profile_test.exe: src/gui.manifest
$(GUI_BIN): SUBSYSTEM := WINDOWS
$(GUI_BIN) $(BUILD)/gui_profile_test.exe: EXTRA_LIBS := $(GUI_LIBS)
$(GUI_BIN) $(BUILD)/gui_profile_test.exe: EXTRA_LDFLAGS := $(GUI_MANIFEST)
ifeq ($(SGHEMU_PGO_PHASE),instrument)
$(GUI_BIN): EXTRA_LDFLAGS += /GENPROFILE /PGD:"$(SGHEMU_PGO_DATABASE)"
else ifeq ($(SGHEMU_PGO_PHASE),optimize)
$(GUI_BIN): EXTRA_LDFLAGS += /USEPROFILE /PGD:"$(SGHEMU_PGO_DATABASE)"
endif

# Linux objects track their own header dependencies in .d files. For MSVC,
# rebuild on any project header change, including the reference CPU in tests/.
HEADERS := $(wildcard src/*.h tests/*.h third_party/puff/*.h)
else
# Only `make gui` needs Qt 5. Compile the frontend as C++, reuse the C core.
PKG_CONFIG ?= pkg-config
GUI_BIN := $(BUILD)/sghemu-gui
gui: $(GUI_BIN)

$(BUILD)/src/gui_qt.o: src/gui_qt.cpp Makefile | $(BUILD)/src
	@$(PKG_CONFIG) --exists Qt5Widgets || { echo "Install the Qt 5 Widgets development files (Qt5Widgets.pc)." >&2; exit 1; }
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fPIC $$($(PKG_CONFIG) --cflags Qt5Widgets) -MMD -MP -c "$<" -o "$@"

$(GUI_BIN): $(BUILD)/src/gui_qt.o $(call objects,$(MACHINE_SRC) $(NVRAM_SRC))
	$(CXX) $(LDFLAGS) $^ -o "$@" $$($(PKG_CONFIG) --libs Qt5Widgets) $(LDLIBS)
endif

# Runtime logs and test fixtures use build/ even with a custom BUILD path.
$(sort $(BUILD) build):
	@$(make_directory)

ifneq ($(BUILD),build)
$(BUILD): | build
endif

$(BUILD)/src $(BUILD)/tests $(BUILD)/tools: | $(BUILD)
	@$(make_directory)

$(BUILD)/src/%.$(OBJ): src/%.c Makefile $(HEADERS) | $(BUILD)/src
	$(compile)

$(BUILD)/tests/%.$(OBJ): tests/%.c Makefile $(HEADERS) | $(BUILD)/tests
	$(compile)

$(BUILD)/tools/%.$(OBJ): tools/%.c Makefile $(HEADERS) | $(BUILD)/tools
	$(compile)

# These C files are included directly, so don't link a second copy.
$(BUILD)/src/jar_inflate.$(OBJ): third_party/puff/puff.c
$(BUILD)/tests/gui_profile_test.$(OBJ): src/gui.c
$(BUILD)/tests/wap_paging_test.$(OBJ): src/wap_gateway.c

-include $(wildcard $(BUILD)/*/*.d)

tests: $(TEST_BIN)
tools: $(TOOL_BIN)

# The blank lines give each test its own recipe line. Run tests in sequence
# because several use the same fixture paths under build/.
define run_test
"$(call native_path,$(1))"


endef

check: $(CHECK_BIN) | build
	$(foreach test,$(CHECK_BIN),$(call run_test,$(test)))

clean:
ifeq ($(PLATFORM),windows)
	@if exist "$(call native_path,$(BUILD))" rmdir /s /q "$(call native_path,$(BUILD))"
else
	rm -rf -- "$(BUILD)"
endif

.PHONY: all check clean gui tests tools
