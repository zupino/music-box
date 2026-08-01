// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_ECHO_CANCEL_H
#define MUSIC_BOX_ECHO_CANCEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Lightweight stereo acoustic echo canceller.
 *
 * This is a two-reference/two-microphone normalized LMS adaptive filter. It
 * uses the exact digital signal sent to the speakers as its reference and
 * estimates the acoustic path from both speakers to both microphones.
 *
 * It intentionally has no third-party DSP dependency. It is useful for the
 * short speaker-to-microphone path of the WM8960 HAT setup, but it is not a
 * replacement for a full telephony-grade AEC with automatic delay estimation,
 * nonlinear processing and sophisticated double-talk detection.
 */
typedef struct {
    bool enabled;
    size_t taps;
    size_t delay_frames;
    size_t history_length;
    size_t write_index;
    float step_size;
    float double_talk_ratio;
    float minimum_reference_energy;

    float *history_left;
    float *history_right;

    float *speaker_l_to_mic_l;
    float *speaker_r_to_mic_l;
    float *speaker_l_to_mic_r;
    float *speaker_r_to_mic_r;
} MusicBoxEchoCanceller;

bool music_box_echo_cancel_init(MusicBoxEchoCanceller *canceller,
                                bool enabled,
                                size_t taps,
                                size_t delay_frames,
                                float step_size,
                                float double_talk_ratio);
void music_box_echo_cancel_destroy(MusicBoxEchoCanceller *canceller);
void music_box_echo_cancel_reset(MusicBoxEchoCanceller *canceller);
void music_box_echo_cancel_process(MusicBoxEchoCanceller *canceller,
                                   float microphone_left,
                                   float microphone_right,
                                   float reference_left,
                                   float reference_right,
                                   float *clean_left,
                                   float *clean_right);

#endif
