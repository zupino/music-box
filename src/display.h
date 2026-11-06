// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_DISPLAY_H
#define MUSIC_BOX_DISPLAY_H

#include "config.h"
#include "wave.h"
#include "rt_queue.h"

#include <stdbool.h>
#include <stdint.h>

#define MUSIC_BOX_DISPLAY_WIDTH 128u
#define MUSIC_BOX_DISPLAY_HEIGHT 32u
#define MUSIC_BOX_DISPLAY_BUFFER_SIZE \
    (MUSIC_BOX_DISPLAY_WIDTH * MUSIC_BOX_DISPLAY_HEIGHT / 8u)

/*
 * Slot bitmasks are kept per bank so that switching mode shows the state that
 * bank was left in, rather than whatever the other bank was doing. One bit per
 * pad, so sixteen pads need sixteen bits.
 */
typedef struct {
    int fd;
    bool enabled;
    uint32_t tempo_millibpm;
    uint8_t beat;
    MusicBoxMode mode;
    MusicBoxRecordingSource recording_source;
    uint8_t selected_loop;
    uint8_t selected_sample;
    uint8_t pad_program;
    uint8_t wave_parameters[4];
    /* Pads with a note assigned; the rest are left blank on the grid. */
    uint16_t mapped_slots;
    uint16_t recording_slots[MUSIC_BOX_MODE_COUNT];
    uint16_t armed_slots[MUSIC_BOX_MODE_COUNT];
    /* Slots holding audio, and the subset of them currently sounding. */
    uint16_t loaded_slots[MUSIC_BOX_MODE_COUNT];
    uint16_t playing_slots[MUSIC_BOX_MODE_COUNT];
    /* Sounding out the current cycle, then stopping at the next boundary. */
    uint16_t pending_stop_slots[MUSIC_BOX_MODE_COUNT];
    bool blink_on;
    uint64_t blink_at_milliseconds;
    /* Consecutive failed frames; the screen is only given up after several. */
    unsigned int write_failures;
    bool dirty;
    uint8_t volume_percent;
    bool volume_visible;
    uint32_t volume_hold_milliseconds;
    uint64_t volume_hide_at_milliseconds;
    bool trim_visible;
    uint8_t trim_slot;
    uint8_t trim_start;
    uint8_t trim_end;
    uint64_t trim_hide_at_milliseconds;
    uint8_t pixels[MUSIC_BOX_DISPLAY_BUFFER_SIZE];
} MusicBoxDisplay;

bool music_box_display_init(MusicBoxDisplay *display,
                            const MusicBoxConfig *config);
void music_box_display_handle_event(MusicBoxDisplay *display,
                                    const MusicBoxEvent *event);
/* Temporarily replaces the beat screen with the current volume. */
void music_box_display_show_volume(MusicBoxDisplay *display, uint8_t percent);
/* Called periodically so a volume indication can time out. */
void music_box_display_tick(MusicBoxDisplay *display);
void music_box_display_destroy(MusicBoxDisplay *display);
/* True once the panel has been initialised and has not since been given up. */
bool music_box_display_active(const MusicBoxDisplay *display);

#endif
