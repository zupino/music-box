// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_NOISE_GATE_H
#define MUSIC_BOX_NOISE_GATE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool enabled;
    float threshold;
    float floor_gain;
    float attack_coefficient;
    float release_coefficient;
    float gain;
} MusicBoxNoiseGate;

bool music_box_noise_gate_init(MusicBoxNoiseGate *gate, bool enabled,
                               uint32_t sample_rate, float threshold,
                               float floor_gain, float attack_ms,
                               float release_ms);
void music_box_noise_gate_process(MusicBoxNoiseGate *gate, float *left,
                                  float *right);

#endif
