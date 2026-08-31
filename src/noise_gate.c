// SPDX-License-Identifier: MIT
#include "noise_gate.h"

#include <math.h>
#include <string.h>

bool music_box_noise_gate_init(MusicBoxNoiseGate *gate, bool enabled,
                               uint32_t sample_rate, float threshold,
                               float floor_gain, float attack_ms,
                               float release_ms)
{
    if (gate == NULL || sample_rate == 0u || threshold < 0.0f ||
        floor_gain < 0.0f || floor_gain > 1.0f || attack_ms <= 0.0f ||
        release_ms <= 0.0f) return false;
    memset(gate, 0, sizeof(*gate));
    gate->enabled = enabled;
    gate->threshold = threshold;
    gate->floor_gain = floor_gain;
    gate->attack_coefficient = expf(-1000.0f / (attack_ms * (float)sample_rate));
    gate->release_coefficient = expf(-1000.0f / (release_ms * (float)sample_rate));
    gate->gain = 1.0f;
    return true;
}

void music_box_noise_gate_process(MusicBoxNoiseGate *gate, float *left,
                                  float *right)
{
    if (!gate->enabled) return;
    const float level = fmaxf(fabsf(*left), fabsf(*right));
    const float target = level >= gate->threshold ? 1.0f : gate->floor_gain;
    const float coefficient = target > gate->gain
                                  ? gate->attack_coefficient
                                  : gate->release_coefficient;
    gate->gain = target + coefficient * (gate->gain - target);
    *left *= gate->gain;
    *right *= gate->gain;
}
