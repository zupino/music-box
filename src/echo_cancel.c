// SPDX-License-Identifier: MIT
#include "echo_cancel.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MUSIC_BOX_AEC_MAX_TAPS 512u
#define MUSIC_BOX_AEC_MAX_DELAY_FRAMES 8192u

static float clamp_sample(float sample)
{
    if (sample > 1.0f) {
        return 1.0f;
    }
    if (sample < -1.0f) {
        return -1.0f;
    }
    return sample;
}

static void free_buffers(MusicBoxEchoCanceller *canceller)
{
    free(canceller->history_left);
    free(canceller->history_right);
    free(canceller->speaker_l_to_mic_l);
    free(canceller->speaker_r_to_mic_l);
    free(canceller->speaker_l_to_mic_r);
    free(canceller->speaker_r_to_mic_r);

    canceller->history_left = NULL;
    canceller->history_right = NULL;
    canceller->speaker_l_to_mic_l = NULL;
    canceller->speaker_r_to_mic_l = NULL;
    canceller->speaker_l_to_mic_r = NULL;
    canceller->speaker_r_to_mic_r = NULL;
}

bool music_box_echo_cancel_init(MusicBoxEchoCanceller *canceller,
                                bool enabled,
                                size_t taps,
                                size_t delay_frames,
                                float step_size,
                                float double_talk_ratio)
{
    if (canceller == NULL || taps == 0u || taps > MUSIC_BOX_AEC_MAX_TAPS ||
        delay_frames > MUSIC_BOX_AEC_MAX_DELAY_FRAMES ||
        step_size <= 0.0f || step_size > 1.0f ||
        double_talk_ratio <= 0.0f) {
        return false;
    }

    memset(canceller, 0, sizeof(*canceller));
    canceller->enabled = enabled;
    canceller->taps = taps;
    canceller->delay_frames = delay_frames;
    canceller->history_length = taps + delay_frames + 1u;
    canceller->step_size = step_size;
    canceller->double_talk_ratio = double_talk_ratio;
    canceller->minimum_reference_energy = 1.0e-8f;

    canceller->history_left =
        calloc(canceller->history_length, sizeof(float));
    canceller->history_right =
        calloc(canceller->history_length, sizeof(float));
    canceller->speaker_l_to_mic_l = calloc(taps, sizeof(float));
    canceller->speaker_r_to_mic_l = calloc(taps, sizeof(float));
    canceller->speaker_l_to_mic_r = calloc(taps, sizeof(float));
    canceller->speaker_r_to_mic_r = calloc(taps, sizeof(float));

    if (canceller->history_left == NULL ||
        canceller->history_right == NULL ||
        canceller->speaker_l_to_mic_l == NULL ||
        canceller->speaker_r_to_mic_l == NULL ||
        canceller->speaker_l_to_mic_r == NULL ||
        canceller->speaker_r_to_mic_r == NULL) {
        free_buffers(canceller);
        return false;
    }

    return true;
}

void music_box_echo_cancel_destroy(MusicBoxEchoCanceller *canceller)
{
    if (canceller == NULL) {
        return;
    }
    free_buffers(canceller);
}

void music_box_echo_cancel_reset(MusicBoxEchoCanceller *canceller)
{
    if (canceller == NULL || canceller->history_left == NULL) {
        return;
    }

    memset(canceller->history_left,
           0,
           canceller->history_length * sizeof(float));
    memset(canceller->history_right,
           0,
           canceller->history_length * sizeof(float));
    memset(canceller->speaker_l_to_mic_l,
           0,
           canceller->taps * sizeof(float));
    memset(canceller->speaker_r_to_mic_l,
           0,
           canceller->taps * sizeof(float));
    memset(canceller->speaker_l_to_mic_r,
           0,
           canceller->taps * sizeof(float));
    memset(canceller->speaker_r_to_mic_r,
           0,
           canceller->taps * sizeof(float));
    canceller->write_index = 0u;
}

void music_box_echo_cancel_process(MusicBoxEchoCanceller *canceller,
                                   float microphone_left,
                                   float microphone_right,
                                   float reference_left,
                                   float reference_right,
                                   float *clean_left,
                                   float *clean_right)
{
    if (!canceller->enabled) {
        *clean_left = microphone_left;
        *clean_right = microphone_right;
        return;
    }

    const size_t write = canceller->write_index;
    canceller->history_left[write] = reference_left;
    canceller->history_right[write] = reference_right;

    size_t index =
        (write + canceller->history_length - canceller->delay_frames) %
        canceller->history_length;

    float estimated_left = 0.0f;
    float estimated_right = 0.0f;
    float reference_energy = 0.0f;

    for (size_t tap = 0u; tap < canceller->taps; ++tap) {
        const float history_left = canceller->history_left[index];
        const float history_right = canceller->history_right[index];

        estimated_left +=
            canceller->speaker_l_to_mic_l[tap] * history_left +
            canceller->speaker_r_to_mic_l[tap] * history_right;
        estimated_right +=
            canceller->speaker_l_to_mic_r[tap] * history_left +
            canceller->speaker_r_to_mic_r[tap] * history_right;
        reference_energy +=
            history_left * history_left + history_right * history_right;

        index = (index == 0u) ?
            canceller->history_length - 1u : index - 1u;
    }

    const float error_left = microphone_left - estimated_left;
    const float error_right = microphone_right - estimated_right;
    const float microphone_energy =
        microphone_left * microphone_left +
        microphone_right * microphone_right;
    const float mean_reference_energy =
        reference_energy / (float)canceller->taps;

    /*
     * Freeze adaptation when microphone energy is disproportionately larger
     * than the average loudspeaker-reference energy. This inexpensive
     * double-talk guard reduces the chance of teaching the filter to cancel
     * the performer.
     */
    const bool may_adapt =
        reference_energy > canceller->minimum_reference_energy &&
        microphone_energy <=
            mean_reference_energy * canceller->double_talk_ratio +
            canceller->minimum_reference_energy;

    if (may_adapt) {
        const float scale_left =
            canceller->step_size * error_left /
            (reference_energy + canceller->minimum_reference_energy);
        const float scale_right =
            canceller->step_size * error_right /
            (reference_energy + canceller->minimum_reference_energy);

        index =
            (write + canceller->history_length - canceller->delay_frames) %
            canceller->history_length;

        for (size_t tap = 0u; tap < canceller->taps; ++tap) {
            const float history_left = canceller->history_left[index];
            const float history_right = canceller->history_right[index];

            canceller->speaker_l_to_mic_l[tap] +=
                scale_left * history_left;
            canceller->speaker_r_to_mic_l[tap] +=
                scale_left * history_right;
            canceller->speaker_l_to_mic_r[tap] +=
                scale_right * history_left;
            canceller->speaker_r_to_mic_r[tap] +=
                scale_right * history_right;

            index = (index == 0u) ?
                canceller->history_length - 1u : index - 1u;
        }
    }

    *clean_left = clamp_sample(error_left);
    *clean_right = clamp_sample(error_right);
    canceller->write_index = (write + 1u) % canceller->history_length;
}
