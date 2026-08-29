// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_VOLUME_H
#define MUSIC_BOX_VOLUME_H

#include "config.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct _snd_mixer snd_mixer_t;
typedef struct _snd_mixer_elem snd_mixer_elem_t;

/* Speaker and headphone outputs are usually enough; the limit only bounds the
   comma-separated volume_mixer_control list. */
#define MUSIC_BOX_VOLUME_CONTROL_LIMIT 4u

typedef struct {
    snd_mixer_elem_t *element;
    long minimum;
    long maximum;
    char name[MUSIC_BOX_NAME_LENGTH];
} MusicBoxVolumeControl;

typedef struct {
    bool available;
    uint8_t cc;
    snd_mixer_t *mixer;
    MusicBoxVolumeControl controls[MUSIC_BOX_VOLUME_CONTROL_LIMIT];
    size_t control_count;
    char control_summary[MUSIC_BOX_NAME_LENGTH * 2];
    atomic_uint percent;
    atomic_uint generation;
} MusicBoxVolume;

bool music_box_volume_init(MusicBoxVolume *volume, const MusicBoxConfig *config);
void music_box_volume_destroy(MusicBoxVolume *volume);

/* Called from the MIDI thread; returns true when the event was consumed. */
bool music_box_volume_handle_cc(MusicBoxVolume *volume, uint8_t controller,
                                uint8_t value);
bool music_box_volume_set_percent(MusicBoxVolume *volume, uint8_t percent);

bool music_box_volume_available(const MusicBoxVolume *volume);
/* Joined names of every driven control, for the startup banner. */
const char *music_box_volume_control_summary(const MusicBoxVolume *volume);
uint8_t music_box_volume_percent(const MusicBoxVolume *volume);
/* Bumped once per applied change so the main loop can notice it without a queue. */
uint32_t music_box_volume_generation(const MusicBoxVolume *volume);

/* Pure helpers, unit tested without any sound hardware. */
uint8_t music_box_volume_percent_for_cc(uint8_t value);
long music_box_volume_raw_for_percent(long minimum, long maximum,
                                      uint8_t percent);
uint8_t music_box_volume_percent_for_raw(long minimum, long maximum, long raw);
/* Splits a comma-separated control list; returns the number of names stored. */
size_t music_box_volume_split_controls(const char *text,
                                       char names[][MUSIC_BOX_NAME_LENGTH],
                                       size_t limit);

#endif
