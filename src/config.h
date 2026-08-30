// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_CONFIG_H
#define MUSIC_BOX_CONFIG_H

#include "loop_bank.h"
#include "transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MUSIC_BOX_NAME_LENGTH 64u
/* MIDI notes are 0-127, so this cannot collide with a real mapping. */
#define MUSIC_BOX_NOTE_UNMAPPED 0xffu

typedef struct {
    uint8_t loop_notes[MUSIC_BOX_LOOP_COUNT];
    uint32_t max_loop_seconds;
    uint32_t max_sample_seconds;

    /* Pads held at least this long arm a sampler recording. */
    uint32_t pad_hold_milliseconds;
    bool mode_control_enabled;
    uint8_t mode_cc;
    /* Half-width of the dead band around the mode knob's midpoint, in CC
       counts. Smaller means less rotation is needed to change mode. */
    uint8_t mode_cc_hysteresis;

    uint32_t tempo_millibpm;
    uint32_t beats_per_bar;
    MusicBoxQuantizeMode quantize_mode;

    bool display_enabled;
    uint8_t display_i2c_address;
    char display_i2c_device[MUSIC_BOX_NAME_LENGTH];
    uint32_t display_volume_seconds;

    bool volume_control_enabled;
    bool volume_db_curve;
    uint8_t volume_cc;
    char volume_mixer_card[MUSIC_BOX_NAME_LENGTH];
    char volume_mixer_control[MUSIC_BOX_NAME_LENGTH];

    bool aec_enabled;
    size_t aec_taps;
    size_t aec_delay_frames;
    float aec_step_size;
    float aec_double_talk_ratio;

    float synth_gain;
    float loop_gain;
    float master_gain;

    char midi_source_prefix[MUSIC_BOX_NAME_LENGTH];
    char synth_midi_prefix[MUSIC_BOX_NAME_LENGTH];
    char synth_jack_prefix[MUSIC_BOX_NAME_LENGTH];
} MusicBoxConfig;

void music_box_config_defaults(MusicBoxConfig *config);
bool music_box_config_load(MusicBoxConfig *config, const char *path);
const char *music_box_quantize_mode_name(MusicBoxQuantizeMode mode);
const char *music_box_mode_name(MusicBoxMode mode);
/* Number of pads with a note assigned; the rest are inert. */
size_t music_box_config_mapped_pads(const MusicBoxConfig *config);

#endif
