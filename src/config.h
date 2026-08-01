// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_CONFIG_H
#define MUSIC_BOX_CONFIG_H

#include "loop_bank.h"
#include "transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MUSIC_BOX_NAME_LENGTH 64u

typedef struct {
    uint8_t loop_notes[MUSIC_BOX_LOOP_COUNT];
    uint32_t max_loop_seconds;

    uint32_t tempo_millibpm;
    uint32_t beats_per_bar;
    MusicBoxQuantizeMode quantize_mode;

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

#endif
