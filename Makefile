# SPDX-License-Identifier: MIT
CC ?= cc
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
CFLAGS ?= -O2 -g
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Isrc
WARNINGS = -Wall -Wextra -Wpedantic -Wconversion -Wshadow
LDLIBS = -ljack -lasound -lpthread -lm

# Teensy builds use PJRC's board package through arduino-cli rather than
# duplicating its arm-none-eabi compiler/core/linker recipes. Make remains the
# project's build entry point on Linux and macOS.
ARDUINO_CLI ?= arduino-cli
TEENSY_CORE_URL ?= https://www.pjrc.com/teensy/package_teensy_index.json
TEENSY_CORE ?= teensy:avr
TEENSY40_FQBN ?= teensy:avr:teensy40
TEENSY41_FQBN ?= teensy:avr:teensy41
TEENSY_PORT_PROTOCOL ?= teensy
TEENSY_SKETCH := firmware/teensy
TEENSY_BUILD_DIR := build/teensy

SOURCES = \
	src/main.c \
	src/config.c \
	src/rt_queue.c \
	src/transport.c \
	src/loop_bank.c \
	src/melody.c \
	src/echo_cancel.c \
	src/noise_gate.c \
	src/session_store.c \
	src/audio_engine.c \
	src/midi.c \
	src/display.c \
	src/volume.c
OBJECTS = $(SOURCES:.c=.o)

TEST_BINS = \
	build/test_transport \
	build/test_queue \
	build/test_loop_bank \
	build/test_melody \
	build/test_config \
	build/test_echo_cancel \
	build/test_noise_gate \
	build/test_session_store \
	build/test_volume

.PHONY: all rpi clean install test teensy-check teensy-setup teensy40 teensy41 teensy40-upload teensy41-upload

all: rpi
rpi: music-box

music-box: $(OBJECTS)
	$(CC) $(CFLAGS) $(WARNINGS) -o $@ $(OBJECTS) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -c -o $@ $<

build:
	mkdir -p build

build/test_transport: tests/test_transport.c src/transport.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -o $@ $^

build/test_queue: tests/test_queue.c src/rt_queue.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -o $@ $^

build/test_loop_bank: tests/test_loop_bank.c src/loop_bank.c src/transport.c src/rt_queue.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -o $@ $^

build/test_melody: tests/test_melody.c src/melody.c src/loop_bank.c src/transport.c src/rt_queue.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -o $@ $^

build/test_config: tests/test_config.c src/config.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -o $@ $^

build/test_echo_cancel: tests/test_echo_cancel.c src/echo_cancel.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -o $@ $^ -lm

build/test_noise_gate: tests/test_noise_gate.c src/noise_gate.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -o $@ $^ -lm

build/test_session_store: tests/test_session_store.c src/session_store.c src/config.c src/loop_bank.c src/transport.c src/rt_queue.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -o $@ $^

build/test_volume: tests/test_volume.c src/volume.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -o $@ $^ -lasound -lm

test: $(TEST_BINS)
	@for test_binary in $(TEST_BINS); do \
		echo "Running $$test_binary"; \
		./$$test_binary || exit 1; \
	done

# On macOS install the only host prerequisite with:
#   brew install arduino-cli
# Then run `make teensy-setup`. PJRC's core package downloads the ARM GCC
# compiler, linker, Teensy core, upload tools and bundled Audio/USBHost/SD libs.
teensy-check:
	@command -v $(ARDUINO_CLI) >/dev/null || { \
		echo "arduino-cli not found."; \
		echo "macOS: brew install arduino-cli"; \
		exit 2; \
	}
	@$(ARDUINO_CLI) version
	@echo "Installed Teensy cores:"
	@$(ARDUINO_CLI) core list | grep teensy || true
	@echo "Detected boards:"
	@$(ARDUINO_CLI) board list || true

teensy-setup: teensy-check
	$(ARDUINO_CLI) core update-index --additional-urls $(TEENSY_CORE_URL)
	$(ARDUINO_CLI) core install $(TEENSY_CORE) --additional-urls $(TEENSY_CORE_URL)
	$(ARDUINO_CLI) lib install "Adafruit GFX Library" "Adafruit SSD1306"
	@echo "Teensy environment ready. Try: make teensy40"

teensy40:
	@command -v $(ARDUINO_CLI) >/dev/null || { echo "Run: make teensy-setup"; exit 2; }
	mkdir -p $(TEENSY_BUILD_DIR)/40
	$(ARDUINO_CLI) compile --fqbn $(TEENSY40_FQBN) \
		--build-property compiler.cpp.extra_flags="-DMUSIC_BOX_SD_BUILTIN=0" \
		--build-path $(TEENSY_BUILD_DIR)/40 $(TEENSY_SKETCH)

teensy41:
	@command -v $(ARDUINO_CLI) >/dev/null || { echo "Run: make teensy-setup"; exit 2; }
	mkdir -p $(TEENSY_BUILD_DIR)/41
	$(ARDUINO_CLI) compile --fqbn $(TEENSY41_FQBN) \
		--build-property compiler.cpp.extra_flags="-DMUSIC_BOX_SD_BUILTIN=1" \
		--build-path $(TEENSY_BUILD_DIR)/41 $(TEENSY_SKETCH)

# Teensy discovery normally reports a custom port such as `usb:210000` with
# protocol `teensy`; it does not have to be a /dev/cu.* serial pathname.
# Upload from the exact --build-path produced by the compile target. If
# automatic reboot cannot reach the bootloader, press Program once and retry.
teensy40-upload: teensy40
	@test -n "$(PORT)" || (echo "Set PORT=<Port column from: arduino-cli board list>"; exit 2)
	@test -f "$(TEENSY_BUILD_DIR)/40/teensy.ino.hex" || (echo "Missing $(TEENSY_BUILD_DIR)/40/teensy.ino.hex; run make teensy40"; exit 2)
	$(ARDUINO_CLI) upload -p "$(PORT)" -l $(TEENSY_PORT_PROTOCOL) \
		--fqbn $(TEENSY40_FQBN) --build-path $(TEENSY_BUILD_DIR)/40 $(TEENSY_SKETCH)

teensy41-upload: teensy41
	@test -n "$(PORT)" || (echo "Set PORT=<Port column from: arduino-cli board list>"; exit 2)
	@test -f "$(TEENSY_BUILD_DIR)/41/teensy.ino.hex" || (echo "Missing $(TEENSY_BUILD_DIR)/41/teensy.ino.hex; run make teensy41"; exit 2)
	$(ARDUINO_CLI) upload -p "$(PORT)" -l $(TEENSY_PORT_PROTOCOL) \
		--fqbn $(TEENSY41_FQBN) --build-path $(TEENSY_BUILD_DIR)/41 $(TEENSY_SKETCH)

install: music-box
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 music-box "$(DESTDIR)$(BINDIR)/music-box"

clean:
	rm -f music-box $(OBJECTS)
	rm -rf build
