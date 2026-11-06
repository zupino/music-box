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

/* The absolute mode knob is split into six approximately equal zones. */
#define MUSIC_BOX_MODE_FIRST_SWITCH 21
#define MUSIC_BOX_MODE_SECOND_SWITCH 42
#define MUSIC_BOX_MODE_THIRD_SWITCH 64
#define MUSIC_BOX_MODE_FOURTH_SWITCH 85
#define MUSIC_BOX_MODE_FIFTH_SWITCH 106

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
    /* Zero-based General MIDI program sent to every FluidSynth channel. */
    uint8_t soundfont_program;
    uint8_t loop_select_cc;
    uint8_t sample_select_cc;
    uint8_t trim_start_cc;
    uint8_t trim_end_cc;
    bool monitor_modifier_enabled;
    uint8_t monitor_modifier_cc;
    bool loop_panic_enabled;
    uint8_t loop_panic_cc;
    bool session_persistence_enabled;
    char session_directory[MUSIC_BOX_NAME_LENGTH];

    uint32_t tempo_millibpm;
    uint32_t beats_per_bar;
    MusicBoxQuantizeMode quantize_mode;

    bool display_enabled;
    uint8_t display_i2c_address;
    char display_i2c_device[MUSIC_BOX_NAME_LENGTH];
    bool display_rotate_180;
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
    bool noise_gate_enabled;
    float noise_gate_threshold;
    float noise_gate_floor_gain;
    float noise_gate_attack_ms;
    float noise_gate_release_ms;

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
/* Resolves a mode-knob position. Positions within hysteresis counts of a switch
   point return current, which is what keeps a resting knob from flapping. */
MusicBoxMode music_box_mode_for_knob(MusicBoxMode current, uint8_t value,
                                     uint8_t hysteresis);

#endif
