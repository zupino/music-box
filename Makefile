# SPDX-License-Identifier: MIT
CC ?= cc
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
CFLAGS ?= -O2 -g
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Isrc
WARNINGS = -Wall -Wextra -Wpedantic -Wconversion -Wshadow
LDLIBS = -ljack -lasound -lpthread -lm

SOURCES = \
	src/main.c \
	src/config.c \
	src/rt_queue.c \
	src/transport.c \
	src/loop_bank.c \
	src/echo_cancel.c \
	src/noise_gate.c \
	src/audio_engine.c \
	src/midi.c \
	src/display.c \
	src/volume.c
OBJECTS = $(SOURCES:.c=.o)

TEST_BINS = \
	build/test_transport \
	build/test_queue \
	build/test_loop_bank \
	build/test_config \
	build/test_echo_cancel \
	build/test_noise_gate \
	build/test_volume

.PHONY: all clean install test

all: music-box

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

build/test_config: tests/test_config.c src/config.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -o $@ $^

build/test_echo_cancel: tests/test_echo_cancel.c src/echo_cancel.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -o $@ $^ -lm

build/test_noise_gate: tests/test_noise_gate.c src/noise_gate.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -o $@ $^ -lm

build/test_volume: tests/test_volume.c src/volume.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -o $@ $^ -lasound -lm

test: $(TEST_BINS)
	@for test_binary in $(TEST_BINS); do \
		echo "Running $$test_binary"; \
		./$$test_binary || exit 1; \
	done

install: music-box
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 music-box "$(DESTDIR)$(BINDIR)/music-box"

clean:
	rm -f music-box $(OBJECTS)
	rm -rf build
