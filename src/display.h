// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_DISPLAY_H
#define MUSIC_BOX_DISPLAY_H

#include "config.h"
#include "rt_queue.h"

#include <stdbool.h>
#include <stdint.h>

#define MUSIC_BOX_DISPLAY_WIDTH 128u
#define MUSIC_BOX_DISPLAY_HEIGHT 32u
#define MUSIC_BOX_DISPLAY_BUFFER_SIZE \
    (MUSIC_BOX_DISPLAY_WIDTH * MUSIC_BOX_DISPLAY_HEIGHT / 8u)

typedef struct {
    int fd;
    bool enabled;
    uint32_t tempo_millibpm;
    uint8_t beat;
    uint8_t recording_slots;
    uint8_t armed_slots;
    uint8_t volume_percent;
    bool volume_visible;
    uint32_t volume_hold_milliseconds;
    uint64_t volume_hide_at_milliseconds;
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

#endif
