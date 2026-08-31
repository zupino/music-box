// SPDX-License-Identifier: MIT
#include "noise_gate.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    MusicBoxNoiseGate gate;
    assert(music_box_noise_gate_init(&gate, true, 1000u, 0.1f, 0.0f,
                                     1.0f, 10.0f));
    float left = 0.01f;
    float right = -0.01f;
    for (unsigned int frame = 0u; frame < 100u; ++frame) {
        left = 0.01f;
        right = -0.01f;
        music_box_noise_gate_process(&gate, &left, &right);
    }
    assert(fabsf(left) < 0.01f);

    left = 0.5f;
    right = -0.5f;
    for (unsigned int frame = 0u; frame < 10u; ++frame) {
        left = 0.5f;
        right = -0.5f;
        music_box_noise_gate_process(&gate, &left, &right);
    }
    assert(fabsf(left) > 0.1f);
    puts("noise gate tests passed");
    return 0;
}
